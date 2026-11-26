#pragma once

#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "version.h"
#include "model.h"
#include "d2d_ctx.h"

namespace pp {

inline std::wstring NormalizeCrlfForClipboard(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out += L"\r\n";
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
        } else if (s[i] == L'\n') {
            out += L"\r\n";
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

inline bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    // Prepare memory BEFORE opening/emptying clipboard so allocation errors never wipe user clipboard
    std::wstring normalized = NormalizeCrlfForClipboard(text);
    size_t bytes = (normalized.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) return false;
    void* ptr = GlobalLock(hMem);
    if (!ptr) {
        DWORD err = GetLastError();
        GlobalFree(hMem);
        SetLastError(err);
        return false;
    }
    memcpy(ptr, normalized.c_str(), bytes);
    GlobalUnlock(hMem);

    DWORD lastErr = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(owner)) {
            if (!EmptyClipboard()) {
                lastErr = GetLastError();
                CloseClipboard();
                GlobalFree(hMem);
                SetLastError(lastErr);
                return false;
            }
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                lastErr = GetLastError();
                CloseClipboard();
                GlobalFree(hMem);
                SetLastError(lastErr);
                return false;
            }
            // Ownership transferred to OS on SetClipboardData success
            CloseClipboard();
            return true;
        }
        lastErr = GetLastError();
        Sleep(10);
    }
    GlobalFree(hMem);
    SetLastError(lastErr);
    return false;
}

// Native zero-extra-DLL auto-paste helper: safely validates foreground target (UIPI, shell, own-process, modifiers)
// and synthesizes a layout-aware Ctrl+V via SendInput after clipboard copy succeeds.
namespace AutoPaste {
    enum class TargetCheck : int {
        Ready = 0,
        NoForeground = 1,
        OwnProcess = 2,
        DesktopOrShell = 3,
        ModifierHeld = 4,
        ElevatedBlocked = 5
    };

    inline bool IsDesktopOrShellWindow(HWND h) {
        if (!h) return true;
        wchar_t cls[64]{};
        if (GetClassNameW(h, cls, _countof(cls)) <= 0) return false;
        return wcscmp(cls, L"Progman") == 0 ||
               wcscmp(cls, L"WorkerW") == 0 ||
               wcscmp(cls, L"Shell_TrayWnd") == 0 ||
               wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
               wcscmp(cls, L"NotifyIconOverflowWindow") == 0 ||
               wcscmp(cls, L"TopLevelWindowForOverflowXamlIsland") == 0 ||
               wcscmp(cls, L"XamlExplorerHostIslandWindow") == 0 ||
               wcscmp(cls, L"Shell_InputSwitchTopLevelWindow") == 0;
    }

    inline bool IsCurrentProcessElevated() {
        static int cached = -1;
        if (cached >= 0) return cached != 0;
        cached = 0;
        HANDLE hTok = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok) && hTok) {
            TOKEN_ELEVATION elev{};
            DWORD cb = 0;
            if (GetTokenInformation(hTok, TokenElevation, &elev, sizeof(elev), &cb) && elev.TokenIsElevated != 0) {
                cached = 1;
            }
            CloseHandle(hTok);
        }
        return cached != 0;
    }

    [[gnu::noinline]] inline bool IsTargetBlockedByUipi(HWND fg) {
        if (!fg || IsCurrentProcessElevated()) return false;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == 0 || fgPid == GetCurrentProcessId()) return false;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fgPid);
        if (!hProc) {
            return GetLastError() == ERROR_ACCESS_DENIED;
        }
        bool blocked = false;
        HANDLE hTok = nullptr;
        if (OpenProcessToken(hProc, TOKEN_QUERY, &hTok) && hTok) {
            TOKEN_ELEVATION elev{};
            DWORD cb = 0;
            if (GetTokenInformation(hTok, TokenElevation, &elev, sizeof(elev), &cb)) {
                blocked = (elev.TokenIsElevated != 0);
            }
            CloseHandle(hTok);
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            blocked = true;
        }
        CloseHandle(hProc);
        return blocked;
    }

    inline bool AreBlockingModifiersHeld() {
        // Skip auto-paste if Shift / Alt / Win is held (avoids Ctrl+Shift+V / Ctrl+Alt+V / menu-bar activation,
        // and naturally gives users a Shift+Click "copy only" escape hatch!)
        return ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) ||
               ((GetAsyncKeyState(VK_MENU)  & 0x8000) != 0) ||
               ((GetAsyncKeyState(VK_LWIN)  & 0x8000) != 0) ||
               ((GetAsyncKeyState(VK_RWIN)  & 0x8000) != 0);
    }

    [[gnu::noinline]] inline TargetCheck CheckForegroundTarget(HWND fg) {
        if (!fg || !IsWindow(fg)) return TargetCheck::NoForeground;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid != 0 && fgPid == GetCurrentProcessId()) return TargetCheck::OwnProcess;
        if (IsDesktopOrShellWindow(fg)) return TargetCheck::DesktopOrShell;
        if (AreBlockingModifiersHeld()) return TargetCheck::ModifierHeld;
        if (IsTargetBlockedByUipi(fg)) return TargetCheck::ElevatedBlocked;
        return TargetCheck::Ready;
    }

    [[gnu::noinline]] inline bool SendCtrlV() {
        bool ctrlAlreadyDown = ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0) ||
                               ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0);
        WORD ctrlScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
        WORD vScan    = (WORD)MapVirtualKeyW('V', MAPVK_VK_TO_VSC);

        INPUT inputs[4]{};
        UINT count = 0;
        if (!ctrlAlreadyDown) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = VK_CONTROL;
            inputs[count].ki.wScan = ctrlScan;
            ++count;
        }
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 'V';
        inputs[count].ki.wScan = vScan;
        ++count;

        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = 'V';
        inputs[count].ki.wScan = vScan;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;

        if (!ctrlAlreadyDown) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = VK_CONTROL;
            inputs[count].ki.wScan = ctrlScan;
            inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
            ++count;
        }
        return SendInput(count, inputs, sizeof(INPUT)) == count;
    }
}

// Manage Windows current-user startup registry entry (no admin UAC required)
namespace AutoStart {
    static constexpr const wchar_t* kRunSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    inline std::wstring CurrentExePath() {
        std::wstring buf(MAX_PATH * 4, L'\0');
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (len == 0 || len >= buf.size()) return {};
        buf.resize(len);
        return buf;
    }

    inline std::wstring CurrentExeQuotedCommand() {
        std::wstring exe = CurrentExePath();
        if (exe.empty()) return {};
        return L"\"" + exe + L"\" --autostart";
    }

    inline std::wstring NormalizeLowerPath(std::wstring s) {
        for (auto& c : s) {
            if (c == L'/') c = L'\\';
            else if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        }
        return s;
    }

    inline std::wstring ExpandEnvPath(const std::wstring& path) {
        if (path.find(L'%') == std::wstring::npos) return path;
        DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
        if (needed == 0 || needed > 32768) return path;
        std::wstring out((size_t)needed, L'\0');
        DWORD wrote = ExpandEnvironmentStringsW(path.c_str(), out.data(), needed);
        if (wrote == 0 || wrote > needed) return path;
        out.resize((size_t)(wrote - 1));
        return out;
    }

    inline bool IsRegisteredTargetDefinitivelyMissing(const std::wstring& exePath) {
        if (exePath.empty()) return false;
        std::wstring expanded = ExpandEnvPath(exePath);
        DWORD attr = GetFileAttributesW(expanded.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) return false;
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return false;
        // Parent directory must be online & accessible so offline drives / permission errors never trigger auto-rebind
        size_t sep = expanded.find_last_of(L"\\/");
        if (sep == std::wstring::npos || sep == 0) return false;
        std::wstring parent = expanded.substr(0, sep);
        if (parent.size() == 2 && parent[1] == L':') parent.push_back(L'\\');
        DWORD pAttr = GetFileAttributesW(parent.c_str());
        return (pAttr != INVALID_FILE_ATTRIBUTES) && ((pAttr & FILE_ATTRIBUTE_DIRECTORY) != 0);
    }

    enum class Status : int {
        Disabled = 0,
        EnabledCurrent = 1,
        StaleOtherPath = 2
    };

    inline Status GetStatus(const wchar_t* valueName = APP_RUN_REG_VALUE_W, std::wstring* outRegExe = nullptr) {
        if (outRegExe) outRegExe->clear();
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return Status::Disabled;
        }
        wchar_t buf[MAX_PATH * 4]{};
        DWORD cb = sizeof(buf) - sizeof(wchar_t);
        DWORD type = 0;
        LSTATUS st = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
        RegCloseKey(hKey);
        if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || buf[0] == L'\0') {
            return Status::Disabled;
        }
        std::wstring regVal(buf);
        if (type == REG_EXPAND_SZ) regVal = ExpandEnvPath(regVal);
        std::wstring regExe;
        if (!regVal.empty() && regVal[0] == L'"') {
            size_t endQ = regVal.find(L'"', 1);
            regExe = (endQ != std::wstring::npos) ? regVal.substr(1, endQ - 1) : regVal.substr(1);
        } else {
            size_t sp = regVal.find(L' ');
            regExe = (sp != std::wstring::npos) ? regVal.substr(0, sp) : regVal;
        }
        regExe = ExpandEnvPath(regExe);
        if (outRegExe) *outRegExe = regExe;
        if (regExe.empty()) return Status::Disabled;
        std::wstring curExe = CurrentExePath();
        if (curExe.empty()) return Status::EnabledCurrent;
        return (NormalizeLowerPath(regExe) == NormalizeLowerPath(curExe))
            ? Status::EnabledCurrent
            : Status::StaleOtherPath;
    }

    inline bool IsEnabled(const wchar_t* valueName = APP_RUN_REG_VALUE_W) {
        return GetStatus(valueName) == Status::EnabledCurrent;
    }

    inline bool SetEnabled(bool enable, const wchar_t* valueName = APP_RUN_REG_VALUE_W) {
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        LSTATUS st = ERROR_SUCCESS;
        if (enable) {
            std::wstring cmd = CurrentExeQuotedCommand();
            if (cmd.empty()) {
                RegCloseKey(hKey);
                return false;
            }
            DWORD cb = (DWORD)((cmd.size() + 1) * sizeof(wchar_t));
            st = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), cb);
        } else {
            st = RegDeleteValueW(hKey, valueName);
            if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
        }
        RegCloseKey(hKey);
        return st == ERROR_SUCCESS;
    }
}

struct DarkMenuItem {
    int id = 0;
    std::wstring label;
    std::wstring rightHint;
    bool isSeparator = false;
    bool isSectionHeader = false;
    bool checked = false;
    bool danger = false;
    bool disabled = false;

    [[gnu::noinline]] ~DarkMenuItem() = default;

    [[gnu::noinline]] static DarkMenuItem Action(int id, const std::wstring& label, const std::wstring& hint = L"",
                               bool checked = false, bool danger = false, bool disabled = false) {
        DarkMenuItem it{};
        it.id = id;
        it.label = UiText(label);
        it.rightHint = UiText(hint);
        it.checked = checked;
        it.danger = danger;
        it.disabled = disabled;
        return it;
    }

    [[gnu::noinline]] static DarkMenuItem Separator() {
        DarkMenuItem it{};
        it.isSeparator = true;
        return it;
    }

    [[gnu::noinline]] static DarkMenuItem Header(const std::wstring& title) {
        DarkMenuItem it{};
        it.isSectionHeader = true;
        it.label = UiText(title);
        it.disabled = true;
        return it;
    }
};

enum class MenuOwner : int {
    None  = 0,
    Dock  = 1,
    Tray  = 2,
    Vault = 3
};

// Non-focus-stealing Raycast-style dark layered popup menu
class DarkPopupMenu {
public:
    [[gnu::noinline]] DarkPopupMenu() = default;
    [[gnu::noinline]] ~DarkPopupMenu() = default;

    HWND hwnd = nullptr;
    LayeredCanvas canvas;
    std::vector<DarkMenuItem> items;
    std::vector<D2D1_RECT_F> itemRects;
    std::function<void(int)> onSelect;

    int hoverIndex = -1;
    int pressedIndex = -1;
    POINT pressDownClientPt{ -99999, -99999 };
    bool pressDuringEmerge = false;
    bool visible = false;
    bool kbNavActive = false;
    MenuOwner owner = MenuOwner::None;
    ULONGLONG lastCloseTick = 0;
    MenuOwner lastCloseOwner = MenuOwner::None;
    bool closedByOutsideClick = false;
    POINT lastOutsideClickPt{ -99999, -99999 };
    HWND openForegroundHwnd = nullptr;
    ULONGLONG openTick = 0;
    bool openFgWasTrayShell = false;
    bool prevLDown = false;
    bool prevRDown = false;
    bool prevUpDown = false;
    bool prevDownDown = false;
    bool prevEnterDown = false;
    bool prevEscDown = false;
    POINT lastPollCursor{ -99999, -99999 };
    int winX = 0;
    int winY = 0;
    float menuW = 228.0f;
    float menuH = 100.0f;
    float dpiScale = 1.0f;

    // Unified Apple-grade popup motion state (anchor-corner emerge + sliding highlight pill)
    Spring1D openSpring = Spring1D::Snappy(1.0f);
    Spring1D hoverYSpring = Spring1D::Snappy(0.0f);
    Spring1D hoverHSpring = Spring1D::Snappy(30.0f);
    float hoverAlpha = 0.0f;
    float hoverDangerT = 0.0f;
    float pressAnim = 0.0f;
    bool hoverPillInit = false;
    bool openFlipLeft = false;
    bool openFlipUp = false;
    float lastRenderOpenT = 1.0f;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;

    std::function<void(MenuOwner)> onClosed;
    std::function<HWND()> foregroundProvider;
    std::function<BOOL(POINT*)> cursorPosProvider;

    ComPtr<IDWriteTextFormat> fmtItem;
    ComPtr<IDWriteTextFormat> fmtHint;
    ComPtr<IDWriteTextFormat> fmtHeader;

    static constexpr UINT_PTR kTimerPoll = 9001;
    static constexpr UINT_PTR kTimerAnim = 9002;
    static constexpr float kPad = 12.0f;

    friend int RunHeadlessSelfTest(const std::wstring& testDir);

    bool Init(HINSTANCE hInst) {
        QueryPerformanceFrequency(&qpcFreq);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptPod_DarkMenuWnd";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName,
            L"",
            WS_POPUP,
            0, 0, 10, 10,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd) return false;

        auto& eng = D2DEngine::Instance();
        fmtItem = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtHint = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtHeader = eng.CreateFormat(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        return true;
    }

    bool IsOpen() const { return visible; }
    bool IsOpenFor(MenuOwner targetOwner) const { return visible && owner == targetOwner; }

    bool HandleForwardedKey(WPARAM vk) {
        if (!visible) return false;
        if (vk == VK_DOWN) { MoveSelection(1); return true; }
        if (vk == VK_UP) { MoveSelection(-1); return true; }
        if (vk == VK_RETURN) {
            if (kbNavActive) ConfirmSelection();
            return true;
        }
        if (vk == VK_ESCAPE) { Close(); return true; }
        return false;
    }

    bool WasJustClosedByOutsideClick(MenuOwner targetOwner, ULONGLONG windowMs = 240) const {
        if (!closedByOutsideClick || lastCloseOwner != targetOwner || lastCloseTick == 0) return false;
        ULONGLONG now = GetTickCount64();
        return now >= lastCloseTick && (now - lastCloseTick) <= windowMs;
    }

    bool WasJustClosedByOutsideClickAt(MenuOwner targetOwner, POINT screenPt, int maxDistPx = 42, ULONGLONG windowMs = 240) const {
        if (!WasJustClosedByOutsideClick(targetOwner, windowMs)) return false;
        if (lastOutsideClickPt.x <= -90000) return true; // No spatial info available (synthetic close)
        return std::abs(screenPt.x - lastOutsideClickPt.x) <= maxDistPx &&
               std::abs(screenPt.y - lastOutsideClickPt.y) <= maxDistPx;
    }

    void Close(bool byOutsideClick = false, const POINT* outsidePt = nullptr) {
        if (!visible) return;
        MenuOwner closedOwner = owner;
        visible = false;
        kbNavActive = false;
        owner = MenuOwner::None;
        lastCloseOwner = closedOwner;
        lastCloseTick = GetTickCount64();
        closedByOutsideClick = byOutsideClick;
        if (byOutsideClick && outsidePt) {
            lastOutsideClickPt = *outsidePt;
        } else {
            lastOutsideClickPt = POINT{ -99999, -99999 };
        }
        openForegroundHwnd = nullptr;
        openFgWasTrayShell = false;
        hoverIndex = -1;
        pressedIndex = -1;
        pressDownClientPt = POINT{ -99999, -99999 };
        pressDuringEmerge = false;
        hoverPillInit = false;
        hoverAlpha = 0.0f;
        hoverDangerT = 0.0f;
        pressAnim = 0.0f;
        openSpring.Snap(1.0f);
        lastRenderOpenT = 1.0f;
        StopAnimTimer();
        KillTimer(hwnd, kTimerPoll);
        if (hwnd && GetCapture() == hwnd) ReleaseCapture();
        ShowWindow(hwnd, SW_HIDE);
        if (onClosed) onClosed(closedOwner);
    }

    void ShowAt(POINT screenPt, const std::vector<DarkMenuItem>& menuItems, std::function<void(int)> callback,
                float customWidthDip = 236.0f, MenuOwner menuOwner = MenuOwner::Dock) {
        if (!hwnd || menuItems.empty()) return;
        Close();

        items = menuItems;
        onSelect = std::move(callback);
        dpiScale = GetDpiScaleForPoint(screenPt, hwnd);

        // Compute layout in DIPs
        menuW = customWidthDip;
        itemRects.assign(items.size(), D2D1::RectF(0, 0, 0, 0));
        float y = kPad + 6.0f;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            float h = 30.0f;
            if (it.isSeparator) h = 9.0f;
            else if (it.isSectionHeader) h = 22.0f;
            itemRects[i] = D2D1::RectF(kPad + 5.0f, y, kPad + menuW - 5.0f, y + h);
            y += h;
        }
        menuH = y - kPad + 6.0f;

        float totalWDip = menuW + kPad * 2.0f;
        float totalHDip = menuH + kPad * 2.0f;
        int wPx = (int)std::ceil(totalWDip * dpiScale);
        int hPx = (int)std::ceil(totalHDip * dpiScale);

        RECT work = GetMonitorWorkAreaForPoint(screenPt);
        int x = screenPt.x - (int)(kPad * dpiScale);
        int yPos = screenPt.y - (int)(kPad * dpiScale);
        openFlipLeft = false;
        openFlipUp = false;
        if (x + wPx > work.right) {
            x = screenPt.x - wPx + (int)(kPad * dpiScale);
            openFlipLeft = true;
        }
        if (yPos + hPx > work.bottom) {
            yPos = work.bottom - hPx - 4;
            openFlipUp = true;
        }
        if (x < work.left) x = work.left + 4;
        if (yPos < work.top) yPos = work.top + 4;

        winX = x;
        winY = yPos;
        hoverIndex = -1;
        pressedIndex = -1;
        pressDownClientPt = POINT{ -99999, -99999 };
        pressDuringEmerge = false;
        hoverPillInit = false;
        hoverAlpha = 0.0f;
        hoverDangerT = 0.0f;
        pressAnim = 0.0f;
        if (Motion::IsReducedMotion()) {
            openSpring.Snap(1.0f);
        } else {
            openSpring.Snap(0.16f);
            openSpring.target = 1.0f;
        }
        kbNavActive = false;
        closedByOutsideClick = false;
        prevLDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        prevRDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        prevUpDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        prevDownDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        prevEnterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        prevEscDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        POINT initPt{};
        if (cursorPosProvider ? cursorPosProvider(&initPt) : GetCursorPos(&initPt)) {
            lastPollCursor = initPt;
        } else {
            lastPollCursor = { -99999, -99999 };
        }
        openForegroundHwnd = foregroundProvider ? foregroundProvider() : GetForegroundWindow();
        openTick = GetTickCount64();
        openFgWasTrayShell = (menuOwner == MenuOwner::Tray) ||
                             (!foregroundProvider && IsShellTrayOrOverflowWindow(openForegroundHwnd));

        if (!Render()) {
            DiagLogger::Instance().LogFmt("ERROR", "MENU",
                L"DarkPopupMenu Render failed: step=%hs hr=0x%08lX err=%lu",
                canvas.lastFailStep ? canvas.lastFailStep : "unknown",
                (unsigned long)canvas.lastHr, (unsigned long)canvas.lastWin32Err);
            return;
        }

        if (SetTimer(hwnd, kTimerPoll, 20, nullptr) == 0) {
            DiagLogger::Instance().LogFmt("ERROR", "MENU",
                L"DarkPopupMenu SetTimer(kTimerPoll) failed err=%lu; aborting popup to prevent stuck menu",
                (unsigned long)GetLastError());
            openForegroundHwnd = nullptr;
            return;
        }

        owner = menuOwner;
        visible = true;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_TOPMOST, winX, winY, wPx, hPx, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (!openSpring.IsSettled()) EnsureAnimTimer();
    }

private:
    [[gnu::noinline]] void SnapAllAnims() {
        openSpring.Snap(openSpring.target);
        hoverYSpring.Snap(hoverYSpring.target);
        hoverHSpring.Snap(hoverHSpring.target);
        hoverAlpha = (hoverIndex >= 0) ? 1.0f : 0.0f;
        hoverDangerT = (hoverIndex >= 0 && hoverIndex < (int)items.size() && items[(size_t)hoverIndex].danger) ? 1.0f : 0.0f;
        pressAnim = (pressedIndex >= 0 && pressedIndex == hoverIndex) ? 1.0f : 0.0f;
        StopAnimTimer();
        Render();
    }

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            SnapAllAnims();
        }
    }

    void StopAnimTimer() {
        StopWindowAnimTimer(hwnd, kTimerAnim, animActive);
    }

    void SetHoverIndex(int newHover) {
        if (newHover == hoverIndex) return;
        hoverIndex = newHover;
        if (hoverIndex >= 0 && hoverIndex < (int)itemRects.size()) {
            const auto& r = itemRects[(size_t)hoverIndex];
            if (!hoverPillInit || hoverAlpha < 0.08f || Motion::IsReducedMotion()) {
                hoverYSpring.Snap(r.top);
                hoverHSpring.Snap(r.bottom - r.top);
                hoverDangerT = items[(size_t)hoverIndex].danger ? 1.0f : 0.0f;
                hoverPillInit = true;
            } else {
                hoverYSpring.target = r.top;
                hoverHSpring.target = r.bottom - r.top;
            }
        }
        if (Motion::IsReducedMotion()) {
            hoverAlpha = (hoverIndex >= 0) ? 1.0f : 0.0f;
            hoverDangerT = (hoverIndex >= 0 && hoverIndex < (int)items.size() && items[(size_t)hoverIndex].danger) ? 1.0f : 0.0f;
            Render();
        } else {
            EnsureAnimTimer();
            Render();
        }
    }

    void OnAnimTick() {
        if (!visible) {
            StopAnimTimer();
            return;
        }
        float dt = StepDeltaQpc(qpcFreq, lastAnimQpc);

        bool mOpen = openSpring.Step(dt);
        bool mHY   = hoverYSpring.Step(dt, 0.15f, 1.0f);
        bool mHH   = hoverHSpring.Step(dt, 0.15f, 1.0f);
        bool mHA   = Motion::ExpApproach(hoverAlpha, (hoverIndex >= 0) ? 1.0f : 0.0f, 24.0f, dt);
        float wantDanger = (hoverIndex >= 0 && hoverIndex < (int)items.size() && items[(size_t)hoverIndex].danger) ? 1.0f : 0.0f;
        bool mHD   = (hoverIndex >= 0) ? Motion::ExpApproach(hoverDangerT, wantDanger, 20.0f, dt) : false;
        float wantPress = (pressedIndex >= 0 && pressedIndex == hoverIndex) ? 1.0f : 0.0f;
        bool mPr   = Motion::ExpApproach(pressAnim, wantPress, 28.0f, dt);

        Render();
        if (!mOpen && !mHY && !mHH && !mHA && !mHD && !mPr) {
            StopAnimTimer();
        }
    }

    void UnmapDip(float& dx, float& dy) const {
        if (!Motion::IsReducedMotion() && lastRenderOpenT < 0.998f) {
            float a = Motion::EaseOutCubic(lastRenderOpenT);
            float sc = 0.955f + 0.045f * a;
            float ax = openFlipLeft ? (kPad + menuW) : kPad;
            float ay = openFlipUp ? (kPad + menuH) : kPad;
            float dyShift = (openFlipUp ? 4.0f : -4.0f) * (1.0f - a);
            dx = (dx - ax) / sc + ax;
            dy = ((dy - dyShift) - ay) / sc + ay;
        }
    }

    int HitTestDip(float dx, float dy) const {
        UnmapDip(dx, dy);
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            if (it.isSeparator || it.isSectionHeader || it.disabled) continue;
            const auto& r = itemRects[i];
            if (dx >= r.left && dx <= r.right && dy >= r.top && dy <= r.bottom) {
                return (int)i;
            }
        }
        return -1;
    }

    bool Render() {
        float totalWDip = menuW + kPad * 2.0f;
        float totalHDip = menuH + kPad * 2.0f;
        int wPx = (int)std::ceil(totalWDip * dpiScale);
        int hPx = (int)std::ceil(totalHDip * dpiScale);

        if (!canvas.Begin(wPx, hPx, dpiScale)) return false;
        auto* rt = canvas.rt.Get();

        float openT = std::clamp(openSpring.value, 0.0f, 1.0f);
        lastRenderOpenT = openT;
        float a = Motion::EaseOutCubic(openT);
        D2D1_RECT_F box = D2D1::RectF(kPad, kPad, kPad + menuW, kPad + menuH);

        // Anchor-corner spring emerge (HitTestDip / UnmapDip inverse-maps exact transform!)
        if (!Motion::IsReducedMotion() && openT < 0.998f) {
            float sc = 0.955f + 0.045f * a;
            float ax = openFlipLeft ? box.right : box.left;
            float ay = openFlipUp ? box.bottom : box.top;
            float dyShift = (openFlipUp ? 4.0f : -4.0f) * (1.0f - a);
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(ax, ay)) *
                             D2D1::Matrix3x2F::Translation(0.0f, dyShift) *
                             D2D1::Matrix3x2F::Scale(dpiScale, dpiScale));
        }

        DrawAmbientShadow(rt, canvas.Brush(D2D1::ColorF(0, 0, 0, 1)), box, 10.0f, a);

        // Rich obsidian surface (shared gradient helper)
        FillRoundedGradient(rt, box, 10.0f, Rgba(0x181C26, 0.98f * a), Rgba(0x10131A, 0.98f * a));
        DrawTopLitBorder(rt, box, 10.0f, Rgba(0xFFFFFF, 0.17f * a), Rgba(0xFFFFFF, 0.06f * a), 1.0f);

        // Continuous sliding highlight pill (glides smoothly between hovered/keyboard-selected rows!)
        if (hoverPillInit && hoverAlpha > 0.01f) {
            float inset = 0.65f * pressAnim;
            D2D1_RECT_F hr = D2D1::RectF(
                kPad + 5.0f + inset,
                hoverYSpring.value + inset,
                kPad + menuW - 5.0f - inset,
                hoverYSpring.value + hoverHSpring.value - inset
            );
            D2D1_COLOR_F accentCol = LerpColor(Rgba(Theme::AccentBlue, 1.0f), Rgba(Theme::DangerRed, 1.0f), hoverDangerT);
            D2D1_COLOR_F fillCol = accentCol;
            fillCol.a = (0.18f + 0.10f * pressAnim) * hoverAlpha * a;
            D2D1_COLOR_F rimCol = accentCol;
            rimCol.a = (0.35f + 0.14f * pressAnim) * hoverAlpha * a;
            FillRMRim(rt, canvas.solidBrush.Get(), hr, 6.0f, fillCol, rimCol, Rgba(0xFFFFFF, 0.05f * hoverAlpha * a), 1.0f);
        }

        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            const auto& r = itemRects[i];
            if (it.isSeparator) {
                float my = (r.top + r.bottom) * 0.5f;
                rt->DrawLine(D2D1::Point2F(r.left + 6.0f, my), D2D1::Point2F(r.right - 6.0f, my),
                             canvas.Brush(Rgba(0xFFFFFF, 0.07f * a)), 1.0f);
                continue;
            }
            auto* br = canvas.solidBrush.Get();
            if (it.isSectionHeader) {
                DrawTextHex(rt, br, fmtHeader.Get(), D2D1::RectF(r.left + 8.0f, r.top + 2.0f, r.right - 8.0f, r.bottom),
                            it.label.c_str(), (UINT32)it.label.size(), Theme::AccentCyan, 0.78f * a);
                continue;
            }

            bool hov = ((int)i == hoverIndex) && !it.disabled;

            float leftTextX = r.left + 24.0f;
            if (it.checked) {
                DrawAnimatedCheckmark(rt, br, r.left + 12.6f, (r.top + r.bottom) * 0.5f,
                                      9.2f, 1.0f, Rgba(Theme::CopyGreen, 0.95f * a), 1.7f);
            }

            uint32_t txtHex = it.disabled ? Theme::TextMuted : (it.danger ? Theme::DangerRed : (hov ? 0xFFFFFF : Theme::TextPrimary));
            float txtA = (it.disabled ? 0.65f : (it.danger ? (hov ? 1.0f : 0.90f) : (hov ? 1.0f : 0.94f))) * a;

            float rightReserve = it.rightHint.empty() ? 8.0f : 62.0f;
            DrawTextHex(rt, br, fmtItem.Get(), D2D1::RectF(leftTextX, r.top, r.right - rightReserve, r.bottom),
                        it.label.c_str(), (UINT32)it.label.size(), txtHex, txtA);
            if (!it.rightHint.empty()) {
                DrawTextHex(rt, br, fmtHint.Get(), D2D1::RectF(r.right - 64.0f, r.top, r.right - 8.0f, r.bottom),
                            it.rightHint.c_str(), (UINT32)it.rightHint.size(), Theme::TextSecondary, (hov ? 0.90f : 0.62f) * a);
            }
        }

        return canvas.EndAndPresent(hwnd, winX, winY);
    }

    bool IsSelectableItem(size_t idx) const {
        if (idx >= items.size()) return false;
        const auto& it = items[idx];
        return !it.isSeparator && !it.isSectionHeader && !it.disabled;
    }

    void MoveSelection(int dir) {
        if (items.empty() || dir == 0) return;
        int n = (int)items.size();
        int start = hoverIndex;
        for (int step = 1; step <= n; ++step) {
            int cand = 0;
            if (start < 0) {
                cand = (dir > 0) ? (step - 1) : (n - step);
            } else {
                cand = (start + dir * step % n + n) % n;
            }
            if (IsSelectableItem((size_t)cand)) {
                kbNavActive = true;
                SetHoverIndex(cand);
                return;
            }
        }
    }

    static bool IsShellTrayOrOverflowWindow(HWND h) {
        if (!h) return false;
        wchar_t cls[64]{};
        if (GetClassNameW(h, cls, _countof(cls)) <= 0) return false;
        return wcscmp(cls, L"NotifyIconOverflowWindow") == 0 ||
               wcscmp(cls, L"TopLevelWindowForOverflowXamlIsland") == 0 ||
               wcscmp(cls, L"Shell_TrayWnd") == 0 ||
               wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
               wcscmp(cls, L"XamlExplorerHostIslandWindow") == 0;
    }

    bool IsKeyboardPollAllowed(HWND curFg) const {
        // VaultWindow synchronously forwards Up/Down/Enter/Esc directly to popupMenu so SearchEdit & menu NEVER double-fire!
        if (owner == MenuOwner::Vault) return false;
        if (!curFg) return false;
        DWORD fgPid = 0;
        GetWindowThreadProcessId(curFg, &fgPid);
        if (fgPid == GetCurrentProcessId()) return true;
        return IsShellTrayOrOverflowWindow(curFg);
    }

    void ConfirmSelection() {
        if (hoverIndex >= 0 && IsSelectableItem((size_t)hoverIndex)) {
            int chosenId = items[(size_t)hoverIndex].id;
            auto cb = onSelect;
            Close();
            if (cb) cb(chosenId);
        }
    }

    void OnPollTimer() {
        if (!visible) return;
        if (owner != MenuOwner::Vault) {
            bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (escDown && !prevEscDown) {
                Close();
                return;
            }
            prevEscDown = escDown;
        }

        // Detect foreground window switches (Alt+Tab, Win key, or elevated UIPI windows where GetAsyncKeyState returns 0)
        HWND curFg = foregroundProvider ? foregroundProvider() : GetForegroundWindow();
        if (curFg != openForegroundHwnd) {
            // Adopt once if Windows 10/11 tray overflow flyout (TopLevelWindowForOverflowXamlIsland / NotifyIconOverflowWindow)
            // or Shell_TrayWnd auto-dismissed / settled right after spawning our Tray menu
            if (openFgWasTrayShell && (GetTickCount64() - openTick) < 450) {
                openForegroundHwnd = curFg;
                openFgWasTrayShell = false;
            } else {
                Close();
                return;
            }
        }

        // Keyboard navigation (Up / Down / Enter) ONLY when foreground belongs to us/taskbar and Enter ONLY after explicit kbNav!
        bool upDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        bool downDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        bool enterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        if (IsKeyboardPollAllowed(curFg)) {
            if (downDown && !prevDownDown) {
                prevDownDown = true;
                MoveSelection(1);
                return;
            }
            if (upDown && !prevUpDown) {
                prevUpDown = true;
                MoveSelection(-1);
                return;
            }
            if (enterDown && !prevEnterDown && kbNavActive) {
                prevEnterDown = true;
                ConfirmSelection();
                return;
            }
        }
        prevUpDown = upDown;
        prevDownDown = downDown;
        prevEnterDown = enterDown;

        POINT pt{};
        BOOL gotPt = cursorPosProvider ? cursorPosProvider(&pt) : GetCursorPos(&pt);
        if (!gotPt) {
            Close();
            return;
        }
        float dx = (float)(pt.x - winX) / dpiScale;
        float dy = (float)(pt.y - winY) / dpiScale;
        float lx = dx, ly = dy;
        UnmapDip(lx, ly);
        bool insideBox = (lx >= kPad && lx <= kPad + menuW && ly >= kPad && ly <= kPad + menuH);

        if (pt.x != lastPollCursor.x || pt.y != lastPollCursor.y) {
            lastPollCursor = pt;
            int newHover = insideBox ? HitTestDip(dx, dy) : -1;
            if (newHover != hoverIndex) {
                kbNavActive = false;
                SetHoverIndex(newHover);
            }
        }

        bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool rDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        if ((lDown && !prevLDown) || (rDown && !prevRDown)) {
            if (!insideBox) {
                Close(true, &pt);
                return;
            }
        }
        prevLDown = lDown;
        prevRDown = rDown;
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        DarkPopupMenu* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<DarkPopupMenu*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DarkPopupMenu*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_SETTINGCHANGE:
                Motion::RefreshSystemReducedMotion();
                if (Motion::IsReducedMotion() && self->visible) self->SnapAllAnims();
                return 0;
            case WM_TIMER:
                if (wp == kTimerPoll) self->OnPollTimer();
                else if (wp == kTimerAnim) self->OnAnimTick();
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverIndex >= 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_MOUSEMOVE: {
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int nh = self->HitTestDip(dx, dy);
                if (nh != self->hoverIndex) {
                    self->kbNavActive = false;
                    self->SetHoverIndex(nh);
                }
                return 0;
            }
            case WM_CANCELMODE:
                self->Close();
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_DOWN) self->MoveSelection(1);
                else if (wp == VK_UP) self->MoveSelection(-1);
                else if (wp == VK_RETURN) self->ConfirmSelection();
                else if (wp == VK_ESCAPE) self->Close();
                return 0;
            case WM_LBUTTONDOWN: {
                int cx = (int)(short)LOWORD(lp);
                int cy = (int)(short)HIWORD(lp);
                float dx = (float)cx / self->dpiScale;
                float dy = (float)cy / self->dpiScale;
                self->pressedIndex = self->HitTestDip(dx, dy);
                if (self->pressedIndex >= 0) {
                    self->pressDownClientPt = POINT{ cx, cy };
                    self->pressDuringEmerge = (!Motion::IsReducedMotion() && self->lastRenderOpenT < 0.998f);
                    SetCapture(h);
                    if (Motion::IsReducedMotion()) { self->pressAnim = 1.0f; self->Render(); }
                    else { self->EnsureAnimTimer(); self->Render(); }
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                int cx = (int)(short)LOWORD(lp);
                int cy = (int)(short)HIWORD(lp);
                float dx = (float)cx / self->dpiScale;
                float dy = (float)cy / self->dpiScale;
                int hit = self->HitTestDip(dx, dy);
                int downIdx = self->pressedIndex;
                POINT downPt = self->pressDownClientPt;
                bool duringEmerge = self->pressDuringEmerge;
                self->pressedIndex = -1;
                self->pressDownClientPt = POINT{ -99999, -99999 };
                self->pressDuringEmerge = false;
                if (GetCapture() == h) ReleaseCapture();
                int tolPx = (int)std::round(6.0f * self->dpiScale);
                bool emergeSteadyClick = (duringEmerge && downIdx >= 0 &&
                                          std::abs(cx - downPt.x) <= tolPx &&
                                          std::abs(cy - downPt.y) <= tolPx);
                if (downIdx >= 0 && downIdx < (int)self->items.size() && (hit == downIdx || emergeSteadyClick)) {
                    int chosenId = self->items[(size_t)downIdx].id;
                    auto cb = self->onSelect;
                    self->Close();
                    if (cb) cb(chosenId);
                } else if (downIdx >= 0) {
                    if (Motion::IsReducedMotion()) { self->pressAnim = 0.0f; self->Render(); }
                    else { self->EnsureAnimTimer(); self->Render(); }
                }
                return 0;
            }
            case WM_CAPTURECHANGED: {
                if (self->pressedIndex >= 0) {
                    self->pressedIndex = -1;
                    self->pressDownClientPt = POINT{ -99999, -99999 };
                    self->pressDuringEmerge = false;
                    if (self->visible) {
                        if (Motion::IsReducedMotion()) { self->pressAnim = 0.0f; self->Render(); }
                        else { self->EnsureAnimTimer(); self->Render(); }
                    }
                }
                return 0;
            }
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

} // namespace pp
