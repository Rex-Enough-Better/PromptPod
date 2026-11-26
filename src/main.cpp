#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdint>
#include <string>
#include <vector>

#include "version.h"
#include "model.h"
#include "theme.h"
#include "d2d_ctx.h"
#include "dark_menu.h"
#include "updater.h"
#include "vault_wnd.h"
#include "dock_wnd.h"

namespace pp {

static constexpr UINT WM_TRAYICON             = WM_APP + 10;
static constexpr UINT WM_REVEAL_POD           = WM_APP + 11;
static constexpr UINT kTrayIconId             = 3;
static constexpr UINT_PTR kTimerTrayRetry     = 8101;
static constexpr UINT_PTR kTimerSilentUpdate  = 8102;

#ifndef NIF_SHOWTIP
#define NIF_SHOWTIP 0x00000080
#endif
#ifndef NOTIFYICON_VERSION_4
#define NOTIFYICON_VERSION_4 4
#endif
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif

inline uint64_t HashPathFnv1a64(const std::wstring& s) {
    uint64_t h = 14695981039346656037ULL;
    for (wchar_t c : s) {
        h ^= (uint64_t)(uint16_t)c;
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint32_t PackAppVersion(uint32_t major, uint32_t minor, uint32_t patch) {
    return ((major & 0x3FFu) << 20) | ((minor & 0x3FFu) << 10) | (patch & 0x3FFu);
}

inline bool ShouldTakeOverExistingInstance(uint32_t runningVer, uint32_t currentVer, bool forceReplace) {
    return forceReplace || (runningVer < currentVer);
}

class AppController {
public:
    HINSTANCE hInst = nullptr;
    HWND hwndTrayHost = nullptr;
    UINT msgTaskbarCreated = 0;
    NOTIFYICONDATAW nid{};
    uint64_t dirPathHash = 0;
    int trayRetryCount = 0;
    bool trayV4Active = false;
    ULONGLONG lastTrayPrimaryTick = 0;

    StorageManager storage;
    AppState state;
    DarkPopupMenu popupMenu;
    UpdateManager updater;
    VaultWindow vaultWnd;
    DockWindow dockWnd;

    bool Init(HINSTANCE inst, const std::wstring& customDataDir, bool silentAutostart, uint64_t pathHash) {
        hInst = inst;
        dirPathHash = pathHash;
        if (!D2DEngine::Instance().Init()) return false;

        storage.Init(customDataDir);
        bool loadedCleanly = storage.LoadOrSeed(state);

        DiagLogger::Instance().LogFmt("INFO", "STARTUP",
            L"=== %ls v%ls starting (autostart=%d, loadedCleanly=%d, readOnlyRecovery=%d, edge=%d, ratioMilli=%d, dataDir=%ls) ===",
            APP_DISPLAY_TITLE_W, APP_VERSION_W,
            silentAutostart ? 1 : 0, loadedCleanly ? 1 : 0, storage.readOnlyRecoveryMode ? 1 : 0,
            (int)state.dockEdge, (int)(state.dockRatioY * 10000.0 + 0.5),
            storage.SafeDirLabelForLog().c_str());

        if (!CreateTrayHostWindow()) return false;
        if (!popupMenu.Init(hInst)) return false;

        updater.Init(hwndTrayHost, customDataDir);

        vaultWnd.Init(hInst, &state, &storage, &popupMenu, [this]() {
            dockWnd.Refresh();
            UpdateTrayTooltip();
        });
        vaultWnd.updater = &updater;
        vaultWnd.onResetDockPos = [this]() {
            dockWnd.ResetToEdgeDefault();
        };
        vaultWnd.onSetDockEdge = [this](DockEdge edge) {
            dockWnd.SetDockEdgeAndPersist(edge, true);
        };
        vaultWnd.getDockLocationSummary = [this]() -> std::wstring {
            return dockWnd.DescribeDockLocation();
        };
        vaultWnd.onReplayOnboarding = [this]() {
            if (vaultWnd.hwnd && IsWindowVisible(vaultWnd.hwnd)) {
                ShowWindow(vaultWnd.hwnd, SW_HIDE);
            }
            dockWnd.StartOnboarding(true);
        };

        if (!dockWnd.Init(hInst, &state, &storage, &popupMenu, &vaultWnd, [this](POINT pt) {
            ShowAppTrayMenu(pt, MenuOwner::Dock);
        })) {
            return false;
        }

        if (!silentAutostart && customDataDir.empty()) {
            // Silently auto-repair autostart path ONLY when parent dir is online and target exe is definitively missing
            std::wstring oldAutoExe;
            if (AutoStart::GetStatus(APP_RUN_REG_VALUE_W, &oldAutoExe) == AutoStart::Status::StaleOtherPath &&
                AutoStart::IsRegisteredTargetDefinitivelyMissing(oldAutoExe)) {
                if (AutoStart::SetEnabled(true)) {
                    DiagLogger::Instance().Log("INFO", "AUTOSTART", L"Auto-repaired stale autostart entry after exe relocation");
                }
            }
        }

        InstallTrayIcon();

        if (!silentAutostart) {
            if (!state.onboardingCompleted && !storage.readOnlyRecoveryMode) {
                // First-launch interactive 3-step edge onboarding
                dockWnd.StartOnboarding(false);
            } else if (state.dockEdge != DockEdge::None) {
                // Smoothly reveal on manual launch so user sees where 词舱 PromptPod is docked
                dockWnd.ExpandAndReveal(2200);
            }
        }

        // One-shot delayed silent update check (24h cooldown, zero background polling timers)
        if (state.autoCheckUpdates) {
            int64_t now = NowUnixSeconds();
            if (state.lastUpdateCheckAt <= 0 || (now - state.lastUpdateCheckAt) >= UpdateManager::kSilentCheckIntervalSec) {
                SetTimer(hwndTrayHost, kTimerSilentUpdate, 8000, nullptr);
            }
        }
        return true;
    }

    void Shutdown() {
        DiagLogger::Instance().Log("INFO", "SHUTDOWN", L"PromptPod shutting down cleanly");
        updater.Shutdown();
        RemoveTrayIcon();
        if (!storage.readOnlyRecoveryMode) {
            storage.Save(state);
        }
    }

private:
    bool CreateTrayHostWindow() {
        msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TrayHostWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"PromptPod_TrayHost";
        RegisterClassExW(&wc);

        hwndTrayHost = CreateWindowExW(
            0, wc.lpszClassName, APP_DISPLAY_TITLE_W, 0,
            0, 0, 0, 0,
            nullptr, nullptr, hInst, this
        );
        if (!hwndTrayHost) return false;
        SetPropW(hwndTrayHost, L"PP_DirHashLo", reinterpret_cast<HANDLE>((ULONG_PTR)(dirPathHash & 0xFFFFFFFFULL)));
        SetPropW(hwndTrayHost, L"PP_DirHashHi", reinterpret_cast<HANDLE>((ULONG_PTR)((dirPathHash >> 32) & 0xFFFFFFFFULL)));
        SetPropW(hwndTrayHost, L"PP_AppVerPacked",
                 reinterpret_cast<HANDLE>((ULONG_PTR)PackAppVersion(APP_VER_MAJOR, APP_VER_MINOR, APP_VER_PATCH)));
        return true;
    }

    HICON hTrayIcon = nullptr;

    std::wstring BuildTrayTooltip() const {
        if (storage.readOnlyRecoveryMode) {
            return UiText(L"\x8BCD\x8231 PromptPod v" APP_VERSION_W L"\xFF08\x53EA\x8BFB\x4FDD\x62A4\x6A21\x5F0F -- \x8BF7\x6253\x5F00\x8BCD\x8231\x6062\x590D\xFF09");
        }
        auto upSnap = updater.GetSnapshot();
        if (upSnap.phase == UpdatePhase::Available && !upSnap.latestVersion.empty()) {
            return UiText(L"\x8BCD\x8231 PromptPod v" APP_VERSION_W L"\xFF08\x53D1\x73B0\x65B0\x7248\x672C v" + upSnap.latestVersion + L"\xFF09");
        }
        return UiText(L"\x8BCD\x8231 PromptPod v" APP_VERSION_W);
    }

    void InstallTrayIcon() {
        if (!hTrayIcon) {
            hTrayIcon = static_cast<HICON>(LoadImageW(
                hInst,
                MAKEINTRESOURCEW(101),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR | LR_SHARED
            ));
            if (!hTrayIcon) hTrayIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
            if (!hTrayIcon) hTrayIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }

        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwndTrayHost;
        nid.uID = kTrayIconId;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = hTrayIcon;

        std::wstring tip = BuildTrayTooltip();
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

        BOOL added = Shell_NotifyIconW(NIM_ADD, &nid);
        BOOL modified = added ? Shell_NotifyIconW(NIM_MODIFY, &nid) : FALSE;
        if (!added && !modified) {
            ++trayRetryCount;
            DiagLogger::Instance().LogFmt("WARN", "TRAY",
                L"Shell_NotifyIconW(NIM_ADD) failed (attempt %d, err=%lu)",
                trayRetryCount, (unsigned long)GetLastError());
            if (trayRetryCount <= 5 && hwndTrayHost) {
                SetTimer(hwndTrayHost, kTimerTrayRetry, 1500, nullptr);
            }
        } else {
            nid.uVersion = NOTIFYICON_VERSION_4;
            trayV4Active = (Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE);
            trayRetryCount = 0;
            if (hwndTrayHost) KillTimer(hwndTrayHost, kTimerTrayRetry);
        }
    }

    void UpdateTrayTooltip() {
        if (!nid.hWnd) return;
        std::wstring tip = BuildTrayTooltip();
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
        nid.uFlags = NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    }

    void RemoveTrayIcon() {
        if (nid.hWnd) {
            Shell_NotifyIconW(NIM_DELETE, &nid);
            nid.hWnd = nullptr;
        }
    }

    POINT ResolveTrayMenuAnchorPoint(WPARAM wp) const {
        NOTIFYICONIDENTIFIER niid{};
        niid.cbSize = sizeof(niid);
        niid.hWnd = hwndTrayHost;
        niid.uID = kTrayIconId;
        RECT iconRc{};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&niid, &iconRc)) &&
            iconRc.right > iconRc.left && iconRc.bottom > iconRc.top) {
            return POINT{ (iconRc.left + iconRc.right) / 2, iconRc.top };
        }
        if (trayV4Active) {
            int px = (int)(short)LOWORD(wp);
            int py = (int)(short)HIWORD(wp);
            if (px != 0 || py != 0) return POINT{ px, py };
        }
        POINT pt{};
        GetCursorPos(&pt);
        return pt;
    }

    void ExecuteTrayPrimaryAction() {
        ULONGLONG now = GetTickCount64();
        ULONGLONG dblGuardMs = std::min<ULONGLONG>(GetDoubleClickTime(), 450ULL);
        if (lastTrayPrimaryTick != 0 && (now - lastTrayPrimaryTick) < dblGuardMs) {
            // Swallow second click of a fast double-click so it never double-fires or flips a toggle back
            if (state.trayClickOpensVault) {
                vaultWnd.ShowOrFocus(0);
            }
            return;
        }
        lastTrayPrimaryTick = now;

        if (state.trayClickOpensVault) {
            DiagLogger::Instance().Log("INFO", "TRAY", L"Tray primary action -> ShowOrFocus VaultWindow");
            vaultWnd.ShowOrFocus(0);
        } else {
            if (state.dockEdge != DockEdge::None && dockWnd.IsExpandedOrExpanding()) {
                DiagLogger::Instance().Log("INFO", "TRAY", L"Tray primary action -> CollapseNow");
                dockWnd.CollapseNow();
            } else {
                DiagLogger::Instance().Log("INFO", "TRAY", L"Tray primary action -> ExpandAndReveal");
                dockWnd.ExpandAndReveal(2600);
            }
        }
    }

    void ShowAppTrayMenu(POINT pt, MenuOwner menuOwner = MenuOwner::Tray) {
        bool bubbleOn = state.showHoverBubble;
        bool isFloating = (state.dockEdge == DockEdge::None);
        bool dockExpanded = (!isFloating && dockWnd.IsExpandedOrExpanding());
        auto upSnap = updater.GetSnapshot();
        std::wstring updateLabel = L"检查更新...";
        std::wstring updateHint;
        if (upSnap.phase == UpdatePhase::Available && !upSnap.latestVersion.empty()) {
            updateLabel = L"发现新版本 v" + upSnap.latestVersion + L"...";
            updateHint = L"新";
        } else if (upSnap.phase == UpdatePhase::Checking) {
            updateLabel = L"正在检查更新...";
        } else if (upSnap.phase == UpdatePhase::Downloading) {
            updateLabel = L"正在下载新版本...";
        } else if (upSnap.phase == UpdatePhase::Installing) {
            updateLabel = L"正在安装更新...";
        }

        std::vector<DarkMenuItem> items;
        items.push_back(DarkMenuItem::Header(L"\x8BCD\x8231 PromptPod v" APP_VERSION_W));
        if (!isFloating) {
            items.push_back(DarkMenuItem::Action(1, dockExpanded ? L"\x6536\x8D77\x8D34\x8FB9\x9762\x677F" : L"\x5C55\x5F00\x8D34\x8FB9\x9762\x677F"));
        } else if (menuOwner == MenuOwner::Tray) {
            items.push_back(DarkMenuItem::Action(1, L"\x7F6E\x9876\x663E\x793A\x9762\x677F"));
        }
        items.push_back(DarkMenuItem::Action(2, L"\x6253\x5F00\x8BCD\x8231\x7BA1\x7406..."));
        items.push_back(DarkMenuItem::Action(3, L"\x4F7F\x7528\x6392\x884C..."));
        items.push_back(DarkMenuItem::Action(6, L"\x6253\x5F00\x8BBE\x7F6E..."));
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(7, L"\x60AC\x505C\x9884\x89C8\x6B63\x6587", L"", bubbleOn));
        items.push_back(DarkMenuItem::Action(8, L"\x70B9\x51FB\x540E\x81EA\x52A8\x7C98\x8D34", L"", state.autoPasteOnClick));
        items.push_back(DarkMenuItem::Action(5, L"\x9762\x677F\x5F52\x4F4D"));
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(10, updateLabel, updateHint));
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(9, L"\x9000\x51FA\x8BCD\x8231 PromptPod", L"", false, true));

        popupMenu.ShowAt(pt, items, [this, isFloating, dockExpanded, menuOwner](int cmd) {
            switch (cmd) {
                case 1:
                    if (isFloating) dockWnd.ExpandAndReveal(0);
                    else if (dockExpanded) dockWnd.CollapseNow();
                    else dockWnd.ExpandAndReveal(2600);
                    break;
                case 2:
                    vaultWnd.Open(0, -1);
                    break;
                case 3:
                    vaultWnd.Open(1, -1);
                    break;
                case 6:
                    vaultWnd.Open(2, -1);
                    break;
                case 7:
                    state.showHoverBubble = !state.showHoverBubble;
                    if (!storage.Save(state)) {
                        vaultWnd.ShowExternalToast(L"保存设置失败：当前处于只读保护或配置文件不可写", true);
                    }
                    dockWnd.Refresh();
                    vaultWnd.RefreshIfOpen();
                    break;
                case 8: {
                    AppState candidate = state;
                    candidate.autoPasteOnClick = !candidate.autoPasteOnClick;
                    if (!storage.Save(candidate)) {
                        if (menuOwner == MenuOwner::Dock) {
                            dockWnd.ShowDockToast(L"保存失败：处于只读保护，请右键打开设置", true, 3200);
                        } else {
                            vaultWnd.ShowExternalToast(L"保存设置失败：当前处于只读保护或配置文件不可写", true);
                        }
                    } else {
                        state = std::move(candidate);
                    }
                    dockWnd.Refresh();
                    vaultWnd.RefreshIfOpen();
                    break;
                }
                case 5:
                    dockWnd.ResetToEdgeDefault();
                    vaultWnd.RefreshIfOpen();
                    break;
                case 10: {
                    vaultWnd.OpenSettingsAtUpdate();
                    auto s = updater.GetSnapshot();
                    if (s.phase != UpdatePhase::Available && !updater.IsBusy()) {
                        updater.StartCheck(false);
                    }
                    vaultWnd.RefreshIfOpen();
                    break;
                }
                case 9:
                    PostQuitMessage(0);
                    break;
            }
        }, 238.0f, menuOwner);
    }

    static LRESULT CALLBACK TrayHostWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        AppController* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<AppController*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<AppController*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        if (self->msgTaskbarCreated != 0 && msg == self->msgTaskbarCreated) {
            DiagLogger::Instance().Log("WARN", "TRAY", L"TaskbarCreated received (Explorer restarted), reinstalling tray icon & syncing dock");
            self->trayRetryCount = 0;
            self->InstallTrayIcon();
            self->dockWnd.ScheduleDisplaySync(L"TaskbarCreated");
            return 0;
        }

        switch (msg) {
            case WM_TIMER:
                if (wp == kTimerTrayRetry) {
                    KillTimer(h, kTimerTrayRetry);
                    self->InstallTrayIcon();
                } else if (wp == kTimerSilentUpdate) {
                    KillTimer(h, kTimerSilentUpdate);
                    if (self->state.autoCheckUpdates) {
                        self->updater.StartCheck(true);
                    }
                }
                return 0;

            case WM_UPDATE_STATE_CHANGED: {
                auto snap = self->updater.GetSnapshot();
                if (snap.phase == UpdatePhase::UpToDate || snap.phase == UpdatePhase::Available) {
                    self->state.lastUpdateCheckAt = NowUnixSeconds();
                    if (!self->storage.readOnlyRecoveryMode) {
                        self->storage.Save(self->state);
                    }
                }
                self->UpdateTrayTooltip();
                self->vaultWnd.RefreshIfOpen();
                return 0;
            }

            case WM_UPDATE_RESTART_READY:
                DiagLogger::Instance().Log("INFO", "UPDATER", L"WM_UPDATE_RESTART_READY received -> exiting old instance for seamless handoff");
                PostQuitMessage(0);
                return 0;

            case WM_REVEAL_POD:
                DiagLogger::Instance().Log("INFO", "TRAY", L"WM_REVEAL_POD received from second instance launch");
                self->dockWnd.ExpandAndReveal(2600);
                return 0;

            case WM_TRAYICON: {
                UINT ev = self->trayV4Active ? (UINT)LOWORD(lp) : (UINT)lp;
                if (ev == NIN_SELECT || ev == NIN_KEYSELECT ||
                    (!self->trayV4Active && (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK))) {
                    self->ExecuteTrayPrimaryAction();
                } else if (ev == WM_CONTEXTMENU || (!self->trayV4Active && ev == WM_RBUTTONUP)) {
                    POINT pt = self->ResolveTrayMenuAnchorPoint(wp);
                    self->ShowAppTrayMenu(pt, MenuOwner::Tray);
                }
                return 0;
            }

            case WM_CLOSE:
            case WM_DESTROY:
                RemovePropW(h, L"PP_DirHashLo");
                RemovePropW(h, L"PP_DirHashHi");
                RemovePropW(h, L"PP_AppVerPacked");
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

inline HWND FindExistingInstanceTrayHost(uint64_t expectedHash) {
    struct SearchCtx {
        uint64_t wantHash = 0;
        HWND found = nullptr;
    } ctx{ expectedHash, nullptr };

    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<SearchCtx*>(lp);
        wchar_t cls[64]{};
        if (GetClassNameW(h, cls, _countof(cls)) > 0 && wcscmp(cls, L"PromptPod_TrayHost") == 0) {
            HANDLE lo = GetPropW(h, L"PP_DirHashLo");
            HANDLE hi = GetPropW(h, L"PP_DirHashHi");
            if (!lo && !hi) {
                // Backward compatibility with pre-0.5.0 default instance
                if (!c->found) c->found = h;
                return TRUE;
            }
            uint64_t got = ((uint64_t)(uint32_t)(ULONG_PTR)hi << 32) | (uint64_t)(uint32_t)(ULONG_PTR)lo;
            if (got == c->wantHash) {
                c->found = h;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// Headless deterministic self-test for build verification (zero pollution of user data or autostart)
inline int RunHeadlessSelfTest(const std::wstring& testRootDir) {
    if (testRootDir.empty()) return 2;
    std::wstring canonRoot = StorageManager::CanonicalizeDirPath(testRootDir);
    std::wstring canonDef  = StorageManager::CanonicalizeDirPath(StorageManager::ResolveDefaultDataDir());
    if (canonRoot.empty() || canonRoot == canonDef) return 3;

    // Create dedicated isolated subfolder for this run so existing files in testRootDir are never touched
    wchar_t runSub[64]{};
    wsprintfW(runSub, L"\\run_%lu_%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    std::wstring testDir = testRootDir + runSub;

    static auto stubClipOk   = [](const std::wstring&) { return true; };
    static auto stubClipFail = [](const std::wstring&) { return false; };
    auto makeFixedMon = [](RECT mon, RECT work) {
        return [mon, work](POINT, RECT* outMon) -> RECT {
            if (outMon) *outMon = mon;
            return work;
        };
    };

    // 1. Verify deterministic Chinese typography normalizer (including new brand name)
    if (UiText(L"\u8BCD\u8231PromptPod v0.6.0") != L"\u8BCD\u8231 PromptPod v0.6.0") return 10;
    if (UiText(L"\u5DF2\u590D\u523618\u6B21") != L"\u5DF2\u590D\u5236 18 \u6B21") return 11;
    if (UiText(L"\u6807\u9898 \uFF0C \u6B63\u6587\u2014\u2014\u6D4B\u8BD5") != L"\u6807\u9898\uFF0C\u6B63\u6587--\u6D4B\u8BD5") return 12;

    // 2. Verify storage seed, slot replacement preserving vault & useCount, and strict JSON round-trip
    StorageManager sm;
    sm.Init(testDir);

    AppState s1{};
    if (!sm.LoadOrSeed(s1)) return 20;
    if (s1.vault.size() != 12 || s1.ActiveSlots()[0] != L"seed_01" || s1.ActiveSlots()[1] != L"seed_02" ||
        s1.EnabledSceneCount() != 2 || !s1.scenes[0].enabled || !s1.scenes[1].enabled ||
        s1.scenes[2].enabled || s1.scenes[3].enabled ||
        s1.scenes[1].name != L"产品" || s1.scenes[1].slots[0] != L"seed_11" || s1.scenes[1].slots[1] != L"seed_12") return 21;
    for (size_t i = 2; i < 10; ++i) {
        if (!s1.ActiveSlots()[i].empty()) return 21;
    }
    if (s1.onboardingCompleted != false || s1.trayClickOpensVault != true || s1.hasDockAnchor != false) return 17;

    PromptItem* p1 = s1.FindById(s1.ActiveSlots()[0]);
    if (!p1) return 22;
    p1->useCount = 3;
    p1->content += L"\nLine2 \"quoted\" \\backslash \xD83D\xDE80";
    std::wstring expectedP1Content = p1->content;

    PromptItem extra{ L"vault_extra_11", L"\x5907\x7528\x6D4B\x8BD5\x6761\x76EE", L"Extra prompt body", 0, NowUnixSeconds(), 0 };
    s1.vault.push_back(extra);
    s1.AssignToSlot(0, L"vault_extra_11");
    if (s1.ActiveSlots()[0] != L"vault_extra_11") return 23;
    const PromptItem* p1After = s1.FindById(L"seed_01");
    if (!p1After || p1After->useCount != 3 || s1.FindSlotOfId(L"seed_01") != -1) return 24;

    if (!s1.showHoverBubble) return 19;
    if (!s1.autoCheckUpdates) return 18;
    s1.showHoverBubble = false;
    s1.autoCheckUpdates = false;
    s1.lastUpdateCheckAt = 1795000123LL;
    if (!sm.Save(s1)) return 25;

    AppState s2{};
    if (!sm.LoadOrSeed(s2)) return 26;
    if (s2.vault.size() != 13 || s2.ActiveSlots()[0] != L"vault_extra_11" || s2.TotalCopies() != 3) return 27;
    if (s2.showHoverBubble != false || s2.autoCheckUpdates != false || s2.lastUpdateCheckAt != 1795000123LL) return 29;
    const PromptItem* p1Reloaded = s2.FindById(L"seed_01");
    if (!p1Reloaded || p1Reloaded->content != expectedP1Content) return 28;

    // 2a-compat. Legacy v0.5.0/v0.6.x JSON without v0.6/v0.7 keys must still parse cleanly with safe defaults
    {
        AppState legacy{};
        std::string legacyJson = "{\"schemaVersion\":2,\"dockEdge\":2,\"dockRatioMilli\":3200,\"showHoverBubble\":true,\"slots\":[\"seed_01\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"],\"vault\":[{\"id\":\"seed_01\",\"title\":\"T\",\"content\":\"C\",\"useCount\":1,\"createdAt\":1,\"lastUsedAt\":2}]}";
        if (!ParseStateFromJson(legacyJson, legacy) || !legacy.autoCheckUpdates || legacy.lastUpdateCheckAt != 0 ||
            !legacy.onboardingCompleted || !legacy.trayClickOpensVault || legacy.hasDockAnchor) return 75;
    }

    // 2b. P0 Regression: existing corrupt data.json must NEVER be overwritten by LoadOrSeed() or Save()!
    {
        StorageManager corruptSm;
        corruptSm.Init(testDir + L"\\corrupt_case");
        std::string brokenBytes = "{ \"schemaVersion\": 2, \"vault\": [ CORRUPT_USER_DATA ";
        if (!StorageManager::WriteAllBytesAtomic(corruptSm.dataFilePath, brokenBytes)) return 60;

        AppState recovered{};
        if (corruptSm.LoadOrSeed(recovered)) return 61; // Must report failure, not silent seed
        if (!corruptSm.readOnlyRecoveryMode) return 62;
        if (corruptSm.Save(recovered)) return 63; // Silent Save() must be blocked
        std::string onDisk;
        if (!StorageManager::ReadAllBytes(corruptSm.dataFilePath, onDisk) || onDisk != brokenBytes) return 64;
        DiagLogger::Instance().Init(sm.dataDir);
    }

    // 2c. P1 Regression: strict RFC 8259 JSON parser rejects empty {}, trailing garbage, deep nesting, lone surrogate, NUL, duplicate IDs
    {
        AppState dummy{};
        if (ParseStateFromJson("{}", dummy)) return 65;
        if (ParseStateFromJson("{\"slots\":[],\"vault\":[]} garbage", dummy)) return 66;
        std::string deepNest = "{\"slots\":[],\"vault\":[],\"extra\":";
        for (int i = 0; i < 64; ++i) deepNest += "[";
        for (int i = 0; i < 64; ++i) deepNest += "]";
        deepNest += "}";
        if (ParseStateFromJson(deepNest, dummy)) return 67;
        if (ParseStateFromJson("{\"slots\":[],\"vault\":[{\"id\":\"a\",\"title\":\"t\",\"content\":\"x\\u0000y\"}]}", dummy)) return 68;
        if (ParseStateFromJson("{\"slots\":[],\"vault\":[{\"id\":\"a\",\"title\":\"t\",\"content\":\"\\uD800\"}]}", dummy)) return 69;
        if (ParseStateFromJson("{\"slots\":[],\"vault\":[{\"id\":\"dup\",\"title\":\"A\",\"content\":\"1\"},{\"id\":\"dup\",\"title\":\"B\",\"content\":\"2\"}]}", dummy)) return 70;
    }

    // 3. Verify unique-named isolated autostart registry toggle & cleanup
    wchar_t tempRegVal[96]{};
    wsprintfW(tempRegVal, L"PromptPod_SelfTest_%lu_%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (!AutoStart::SetEnabled(true, tempRegVal)) return 30;
    if (!AutoStart::IsEnabled(tempRegVal)) {
        AutoStart::SetEnabled(false, tempRegVal);
        return 31;
    }
    if (!AutoStart::SetEnabled(false, tempRegVal)) return 32;
    if (AutoStart::IsEnabled(tempRegVal)) return 33;

    // 4. Verify DockWindow interactions, geometry/size self-heal, hover bubble, and first-principles capsule reachability self-heal
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int uiErr = 0;
    if (!D2DEngine::Instance().Init()) {
        uiErr = 40;
    } else {
        DockWindow dock;
        std::wstring capturedClip;
        bool clipShouldSucceed = true;
        dock.clipboardWriter = [&](const std::wstring& txt) -> bool {
            if (!clipShouldSucceed) return false;
            capturedClip = txt;
            return true;
        };
        if (!dock.Init(GetModuleHandleW(nullptr), &s2, &sm, nullptr, nullptr, nullptr, false)) {
            uiErr = 41;
        } else {
            dock.expandSpring.Snap(1.0f);
            dock.Refresh();

            auto makeSlotLp = [&](int slotIdx) -> LPARAM {
                float cxDip = Theme::ShadowPad + Theme::PanelWidth * 0.5f;
                float cyDip = Theme::ShadowPad + Theme::HeaderHeight +
                              (float)slotIdx * (Theme::SlotHeight + Theme::SlotGap) + Theme::SlotHeight * 0.5f;
                int px = (int)std::round(cxDip * dock.dpiScale);
                int py = (int)std::round(cyDip * dock.dpiScale);
                return MAKELPARAM((short)px, (short)py);
            };

            // 4a. Left-click on Slot 01 must copy content & increment useCount
            LPARAM lp0 = makeSlotLp(0);
            SendMessageW(dock.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp0);
            SendMessageW(dock.hwnd, WM_LBUTTONUP, 0, lp0);
            const PromptItem* slot0Item = s2.FindById(L"vault_extra_11");
            if (!slot0Item || slot0Item->useCount != 1 || capturedClip != L"Extra prompt body" || dock.copiedSlot != 0) {
                uiErr = 42;
            }

            // 4a-fail. Clipboard failure must NOT increment useCount, must arm inline red flash, and must NOT auto-retract!
            if (uiErr == 0) {
                clipShouldSucceed = false;
                capturedClip.clear();
                dock.pendingAutoRetract = false;
                dock.copyFailedSlot = -1;
                SendMessageW(dock.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp0);
                SendMessageW(dock.hwnd, WM_LBUTTONUP, 0, lp0);
                if (slot0Item->useCount != 1 || !capturedClip.empty() ||
                    dock.copyFailedSlot != 0 || dock.copyFailedTick == 0 ||
                    dock.pendingAutoRetract || dock.expandSpring.target < 0.5f) {
                    uiErr = 71;
                }
                clipShouldSucceed = true;
            }

            // 4b. Press on Slot 01 then release far outside panel must cancel without copying
            if (uiErr == 0) {
                capturedClip.clear();
                SendMessageW(dock.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp0);
                SendMessageW(dock.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(-120, (short)HIWORD(lp0)));
                if (slot0Item->useCount != 1 || !capturedClip.empty()) {
                    uiErr = 43;
                }
            }

            // 4c. Drag Slot 01 down onto Slot 02 must swap slots without triggering copy
            if (uiErr == 0) {
                std::wstring origSlot0 = s2.ActiveSlots()[0];
                std::wstring origSlot1 = s2.ActiveSlots()[1];
                LPARAM lp1 = makeSlotLp(1);
                SendMessageW(dock.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp0);
                SendMessageW(dock.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lp1);
                SendMessageW(dock.hwnd, WM_LBUTTONUP, 0, lp1);
                if (s2.ActiveSlots()[0] != origSlot1 || s2.ActiveSlots()[1] != origSlot0 || slot0Item->useCount != 1 || !capturedClip.empty()) {
                    uiErr = 44;
                }
            }

            // 4d. Verify DIBSection capacity reuse on collapse + D2D target recreate resilience + non-zero solid center alpha
            if (uiErr == 0) {
                HBITMAP expandedDib = dock.canvas.dib;
                dock.expandSpring.Snap(0.0f);
                dock.canvas.rt.Reset();
                if (!dock.RenderAndPresent("selftest-collapse") || !dock.lastPresentOk ||
                    dock.canvas.dib != expandedDib || dock.lastCollapsedCenterAlpha < 180) {
                    uiErr = 45;
                }
            }

            // 4e. Verify display switch (1920x1080 -> 2560x1440), external move & size-only drift self-heal
            if (uiErr == 0) {
                RECT simMon{ 0, 0, 1920, 1080 };
                RECT simWork{ 0, 0, 1920, 1040 };
                dock.monitorProvider = [&](POINT, RECT* outMon) -> RECT {
                    if (outMon) *outMon = simMon;
                    return simWork;
                };
                SendMessageW(dock.hwnd, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
                int expectedX1920 = 1920 - dock.curWinW;
                if (dock.curWinX != expectedX1920) {
                    uiErr = 46;
                }

                simMon = { 0, 0, 2560, 1440 };
                simWork = { 0, 0, 2560, 1400 };
                SendMessageW(dock.hwnd, WM_DISPLAYCHANGE, 32, MAKELPARAM(2560, 1440));
                int expectedX2560 = 2560 - dock.curWinW;
                if (dock.curWinX != expectedX2560) {
                    uiErr = 47;
                }

                // External move to X=1280 + click jitter must NOT detach
                {
                    DockWindow::ScopedSelfPresent guard(dock.selfPresentDepth);
                    SetWindowPos(dock.hwnd, nullptr, 1280, dock.curWinY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                SendMessageW(dock.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 20));
                SendMessageW(dock.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(6, 21));
                SendMessageW(dock.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(6, 21));
                if (s2.dockEdge != DockEdge::Right) {
                    uiErr = 48;
                }

                // Watchdog heals external move AND external size-only drift (same X/Y, shrunken width!)
                dock.expandSpring.Snap(0.0f);
                dock.RenderAndPresent("selftest-reset-collapsed");
                int expCollapsedW = dock.curWinW;
                {
                    DockWindow::ScopedSelfPresent guard(dock.selfPresentDepth);
                    SetWindowPos(dock.hwnd, nullptr, dock.curWinX, dock.curWinY, 2, 2, SWP_NOZORDER | SWP_NOACTIVATE);
                }
                dock.animRunning = false;
                dock.OnWatchdogTimer();
                RECT healedRc{};
                GetWindowRect(dock.hwnd, &healedRc);
                if (healedRc.left != expectedX2560 || (healedRc.right - healedRc.left) != expCollapsedW) {
                    uiErr = 49;
                }
            }

            // 4f. Verify HoverBubble early-hover trigger during opening spring + DIB reuse + text buffer release
            if (uiErr == 0) {
                s2.showHoverBubble = true;
                dock.expandSpring.value = 0.25f;
                dock.expandSpring.target = 1.0f;
                dock.hoverSlot = -1;

                float slideDip = dock.CurrentSlideOffsetDip();
                float cxDip = Theme::ShadowPad + Theme::PanelWidth * 0.5f + slideDip;
                float cyDip = Theme::ShadowPad + Theme::HeaderHeight +
                              1.0f * (Theme::SlotHeight + Theme::SlotGap) + Theme::SlotHeight * 0.5f;
                LPARAM lpHoverEarly = MAKELPARAM((short)std::round(cxDip * dock.dpiScale),
                                                 (short)std::round(cyDip * dock.dpiScale));
                SendMessageW(dock.hwnd, WM_MOUSEMOVE, 0, lpHoverEarly);
                if (dock.hoverSlot != 1) {
                    uiErr = 53;
                }

                dock.expandSpring.Snap(1.0f);
                SendMessageW(dock.hwnd, WM_TIMER, DockWindow::kTimerBubbleHover, 0);
                if (!dock.hoverBubble.IsShowingOrAppearing() || dock.hoverBubble.currentSlot != 1 ||
                    dock.hoverBubble.canvas.widthPx <= 10 || !IsWindowVisible(dock.hoverBubble.hwnd)) {
                    uiErr = 54;
                }

                HBITMAP bubbleDib = dock.hoverBubble.canvas.dib;
                dock.hoverBubble.HideImmediate();
                if (dock.hoverBubble.canvas.dib != bubbleDib || !dock.hoverBubble.bodyText.empty()) {
                    uiErr = 55;
                }
            }

            // 4g. First-principles capsule reachability self-heal regression:
            //     (1) External Z-order occluder -> heals ONCE, then circuit-breaks against same persistent occluder
            //     (2) Layered-surface click-through (no occluder above) -> recommits collapsed frame ONLY
            if (uiErr == 0) {
                dock.expandSpring.Snap(0.0f);
                dock.RenderAndPresent("selftest-probe-base");
                dock.animRunning = false;

                HWND fakeOccluder = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0xDEADBEEF));
                bool simOccludedAbove = true;
                int simSwpCount = 0;
                dock.hitProbeProvider = [&](POINT) -> HWND { return fakeOccluder; };
                dock.aboveInZOrderProvider = [&](HWND cand, HWND) -> bool { return simOccludedAbove && (cand == fakeOccluder); };
                dock.swpTopmostProvider = [&](HWND) -> bool { ++simSwpCount; return true; };

                dock.watchdogZHealCount = 0;
                dock.watchdogSurfaceRecommitCount = 0;
                dock.lastHealedOccluder = nullptr;
                dock.lastZHealTick = 0;

                // 1st watchdog tick: detects persistent Z-order occlusion and performs 1 heal cycle (with Win11 fallback if still above)
                dock.OnWatchdogTimer();
                int swpAfterFirstHeal = simSwpCount;
                if (dock.watchdogZHealCount != 1 || swpAfterFirstHeal < 1 || dock.watchdogSurfaceRecommitCount != 0) {
                    uiErr = 72;
                }

                // 2nd watchdog tick with SAME persistent occluder: circuit breaker prevents ping-pong loop!
                if (uiErr == 0) {
                    dock.lastZHealTick = 0; // Bypass cooldown to prove per-occluder circuit breaker triggers
                    dock.OnWatchdogTimer();
                    if (dock.watchdogZHealCount != 1 || simSwpCount != swpAfterFirstHeal) {
                        uiErr = 73;
                    }
                }

                // Layered-surface click-through (no occluder above): must recommit frame ONLY, zero Z-order raises!
                if (uiErr == 0) {
                    simOccludedAbove = false;
                    dock.lastHealedOccluder = nullptr;
                    dock.OnWatchdogTimer();
                    if (dock.watchdogSurfaceRecommitCount != 1 || simSwpCount != swpAfterFirstHeal || dock.lastCollapsedCenterAlpha < 180) {
                        uiErr = 74;
                    }
                }
            }

            if (dock.hoverBubble.hwnd) DestroyWindow(dock.hoverBubble.hwnd);
            if (dock.hwnd) DestroyWindow(dock.hwnd);
        }
    }
    CoUninitialize();
    if (uiErr != 0) return uiErr;

    // 5. Verify diagnostic log output & fresh-directory size-based rotation
    std::string logBytes;
    if (!StorageManager::ReadAllBytes(sm.logFilePath, logBytes) || logBytes.empty()) return 50;
    if (logBytes.find("[DISPLAY]") == std::string::npos || logBytes.find("[SELF_HEAL]") == std::string::npos) return 51;

    DiagLogger::Instance().maxBytes = 256;
    for (int i = 0; i < 8; ++i) {
        DiagLogger::Instance().Log("INFO", "ROTATE_TEST", L"Testing log file size rotation threshold line padding");
    }
    DiagLogger::Instance().maxBytes = 512 * 1024;
    std::string oldLogBytes;
    if (!StorageManager::ReadAllBytes(DiagLogger::Instance().oldLogFilePath, oldLogBytes) || oldLogBytes.empty()) return 52;

    // 6. Verify v0.6.0 Updater core primitives (SemVer, tag extraction, manifest security gate, SHA-256, NTFS swap, feedback URL & sanitized diagnostics)
    {
        // 6a. SemVer parse & compare
        SemVer v50 = ParseSemVer(L"v0.5.0");
        SemVer v60 = ParseSemVer(L"0.6.0");
        SemVer v100 = ParseSemVer(L"V0.10.0");
        if (!v50.valid || !v60.valid || !v100.valid) return 76;
        if (CompareSemVer(v50, v60) >= 0 || CompareSemVer(v60, v100) >= 0 || CompareSemVer(v60, ParseSemVer(L"v0.6.0")) != 0) return 77;
        if (ParseSemVer(L"0.6.0-beta.1").valid || ParseSemVer(L"0.06.0").valid || ParseSemVer(L"0.6").valid) return 78;

        // 6b. Release Location tag extraction
        if (ExtractTagFromReleaseLocation(L"https://github.com/Rex-Enough-Better/PromptPod/releases/tag/v0.7.1") != L"v0.7.1") return 79;
        if (ExtractTagFromReleaseLocation(L"/Rex-Enough-Better/PromptPod/releases/tag/v1.0.0?foo=bar") != L"v1.0.0") return 80;
        if (!ExtractTagFromReleaseLocation(L"https://github.com/Rex-Enough-Better/PromptPod/releases").empty()) return 81;

        // 6c. Strict UpdateManifest parser & host/SHA security checks
        std::string validMan =
            "{\"schemaVersion\":1,\"version\":\"0.7.0\",\"tag\":\"v0.7.0\","
            "\"releaseUrl\":\"https://github.com/Rex-Enough-Better/PromptPod/releases/tag/v0.7.0\","
            "\"notes\":\"New release\",\"assetName\":\"PromptPod-0.7.0.exe\","
            "\"assetUrl\":\"https://github.com/Rex-Enough-Better/PromptPod/releases/download/v0.7.0/PromptPod-0.7.0.exe\","
            "\"assetSize\":460000,"
            "\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}";
        UpdateManifest parsedMan{};
        if (!ParseUpdateManifestJson(validMan, L"0.7.0", parsedMan) || parsedMan.assetSize != 460000) return 82;
        if (ParseUpdateManifestJson(validMan, L"0.8.0", parsedMan)) return 83; // Version mismatch must reject

        // Foreign host assetUrl must reject
        std::string evilHostMan =
            "{\"schemaVersion\":1,\"version\":\"0.7.0\",\"assetUrl\":\"https://evil.example.com/PromptPod.exe\","
            "\"assetSize\":460000,\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}";
        if (ParseUpdateManifestJson(evilHostMan, L"0.7.0", parsedMan)) return 84;

        // Malformed SHA-256 must reject
        std::string badShaMan =
            "{\"schemaVersion\":1,\"version\":\"0.7.0\","
            "\"assetUrl\":\"https://github.com/Rex-Enough-Better/PromptPod/releases/download/v0.7.0/PromptPod-0.7.0.exe\","
            "\"assetSize\":460000,\"sha256\":\"short\"}";
        if (ParseUpdateManifestJson(badShaMan, L"0.7.0", parsedMan)) return 85;

        // 6d. BCrypt SHA-256 NIST standard vector ("abc")
        std::wstring shaTestPath = testDir + L"\\sha_vec.bin";
        if (!StorageManager::WriteAllBytesAtomic(shaTestPath, "abc")) return 86;
        std::wstring shaHex;
        if (!ComputeFileSha256Hex(shaTestPath, shaHex) ||
            shaHex != L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
            return 87;
        }

        // 6e. NTFS atomic binary swap with backup & verification
        std::wstring curBin = testDir + L"\\fake_app.exe";
        std::wstring newBin = testDir + L"\\fake_app.exe.new";
        if (!StorageManager::WriteAllBytesAtomic(curBin, "OLD_BINARY_V1")) return 88;
        if (!StorageManager::WriteAllBytesAtomic(newBin, "NEW_BINARY_V2")) return 89;
        std::wstring oldBak;
        if (!SwapBinaryWithBackup(curBin, newBin, oldBak) || oldBak.empty()) return 90;
        std::string afterCur, afterOld;
        if (!StorageManager::ReadAllBytes(curBin, afterCur) || afterCur != "NEW_BINARY_V2") return 91;
        if (!StorageManager::ReadAllBytes(oldBak, afterOld) || afterOld != "OLD_BINARY_V1") return 92;

        // 6f. URL encoding, GitHub Issue Form URL generation & privacy-safe diagnostics (must NOT leak user prompt content!)
        if (UrlEncodeUtf8(L"A B+C") != L"A%20B%2BC") return 93;
        std::wstring diag = BuildSanitizedDiagnostics(s2, sm, true);
        if (diag.find(L"PromptPod v" APP_VERSION_W) == std::wstring::npos) return 94;
        if (diag.find(L"Extra prompt body") != std::wstring::npos || diag.find(L"vault_extra_11") != std::wstring::npos) return 95;
        std::wstring issueUrl = BuildGitHubIssueUrl(true, L"测试标题", L"复现步骤", L"Windows 11", diag);
        if (issueUrl.rfind(L"https://github.com/Rex-Enough-Better/PromptPod/issues/new?template=bug_report.yml", 0) != 0 ||
            issueUrl.size() > 1900) {
            return 96;
        }

        // 6g. Packed version monotonic comparison for seamless newer-version single-instance takeover
        uint32_t pv050 = PackAppVersion(0, 5, 0);
        uint32_t pv060 = PackAppVersion(0, 6, 0);
        uint32_t pv061 = PackAppVersion(0, 6, 1);
        uint32_t pv100 = PackAppVersion(1, 0, 0);
        if (!(0u < pv050 && pv050 < pv060 && pv060 < pv061 && pv061 < pv100)) return 97;
    }

    // 7. Verify DarkEditScrollbar with borderless multi-line EDIT (no native WS_VSCROLL)
    {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HWND hHost = CreateWindowExW(0, L"STATIC", L"", WS_POPUP | WS_CLIPCHILDREN,
                                     0, 0, 480, 320, nullptr, nullptr, hInst, nullptr);
        if (!hHost) return 98;
        HWND hEd = CreateWindowExW(0, L"EDIT", L"Single line",
                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                   26, 20, 400, 180, hHost, (HMENU)9001, hInst, nullptr);
        HFONT hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        int sbErr = 0;
        if (!hEd || !hf) {
            sbErr = 99;
        } else {
            SendMessageW(hEd, WM_SETFONT, (WPARAM)hf, TRUE);
            DarkEditScrollbar sb;
            sb.Attach(hHost, hEd);
            sb.SetTrackLayout(D2D1::RectF(20.0f, 14.0f, 440.0f, 206.0f), 1.0f);

            // 7a. Short text must hide scrollbar completely (no WS_VSCROLL chrome!)
            DWORD style = (DWORD)GetWindowLongPtrW(hEd, GWL_STYLE);
            D2D1_RECT_F thumb{};
            auto mShort = sb.QueryMetrics();
            if ((style & WS_VSCROLL) != 0 || mShort.maxFirstLine != 0 || sb.CanScroll() || sb.ComputeThumb(mShort, thumb)) {
                sbErr = 100;
            }

            // 7b. Long 40-line content enables scrollbar, computes valid thumb, and supports clamped EM_LINESCROLL + wheel
            if (sbErr == 0) {
                std::wstring many;
                for (int i = 1; i <= 40; ++i) {
                    many += L"Prompt line " + std::to_wstring(i);
                    if (i < 40) many += L"\r\n";
                }
                SetWindowTextW(hEd, many.c_str());
                auto mLong = sb.QueryMetrics();
                if (mLong.totalLines < 40 || mLong.visibleLines <= 0 || mLong.maxFirstLine <= 0 ||
                    !sb.CanScroll() || !sb.ComputeThumb(mLong, thumb) ||
                    thumb.top < sb.trackVisRect.top - 0.1f || thumb.bottom > sb.trackVisRect.bottom + 0.1f) {
                    sbErr = 101;
                }

                // Wheel down one notch (-WHEEL_DELTA) must advance first visible line
                if (sbErr == 0) {
                    SendMessageW(hEd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
                    int afterWheel = sb.QueryMetrics().firstLine;
                    if (afterWheel <= 0 || afterWheel > mLong.maxFirstLine) {
                        sbErr = 102;
                    }
                }

                // Scroll to bottom must clamp cleanly to maxFirstLine (never overscroll past content)
                if (sbErr == 0) {
                    sb.ScrollByLines(999);
                    auto mBot = sb.QueryMetrics();
                    if (mBot.firstLine != mBot.maxFirstLine) {
                        sbErr = 103;
                    }
                    D2D1_RECT_F botThumb{};
                    if (!sb.ComputeThumb(mBot, botThumb) || std::fabs(botThumb.bottom - sb.trackVisRect.bottom) > 0.5f) {
                        sbErr = 104;
                    }
                    // Round-trip ThumbTopToLine at bottom and top
                    int rtBot = sb.ThumbTopToLine(botThumb.top, mBot, botThumb.bottom - botThumb.top);
                    int rtTop = sb.ThumbTopToLine(sb.trackVisRect.top, mBot, botThumb.bottom - botThumb.top);
                    if (rtBot != mBot.maxFirstLine || rtTop != 0) {
                        sbErr = 105;
                    }
                }

                // Shrinking text back to 1 line must auto-reset firstLine to 0 and hide scrollbar
                if (sbErr == 0) {
                    SetWindowTextW(hEd, L"Short again");
                    auto mBack = sb.QueryMetrics();
                    if (mBack.firstLine != 0 || mBack.maxFirstLine != 0 || sb.CanScroll()) {
                        sbErr = 106;
                    }
                }
            }
            sb.Detach();
            if (GetPropW(hEd, DarkEditScrollbar::kPropName) != nullptr) {
                sbErr = 107;
            }
        }
        if (hHost) DestroyWindow(hHost);
        if (hf) DeleteObject(hf);
        if (sbErr != 0) return sbErr;
    }

    // 8. Verify v0.6.2 state-machine, transactional drag, menu owner/UIPI, data safety & feedback regressions
    {
        // 8a. Export path guard must block active data.json and data.json.bak (including case/slash variants)
        std::wstring mixedDataPath = sm.dataFilePath;
        for (auto& c : mixedDataPath) {
            if (c == L'\\') c = L'/';
            else if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
        }
        if (!sm.IsProtectedInternalPath(sm.dataFilePath) ||
            !sm.IsProtectedInternalPath(mixedDataPath) ||
            !sm.IsProtectedInternalPath(sm.backupFilePath) ||
            sm.IsProtectedInternalPath(testDir + L"\\safe-export.json")) {
            return 108;
        }

        // 8b. Verified pre-import snapshot created by SaveExplicitReplacement before replacing valid data.json
        std::string beforeImportBytes;
        if (!StorageManager::ReadAllBytes(sm.dataFilePath, beforeImportBytes) || beforeImportBytes.empty()) return 109;
        AppState importedState = s2;
        importedState.vault.push_back(PromptItem{ L"imp_01", L"Imported Title", L"Imported Body", 0, NowUnixSeconds(), 0 });
        std::wstring snapPath, saveErr;
        if (!sm.SaveExplicitReplacement(importedState, &snapPath, &saveErr) || snapPath.empty()) return 110;
        std::string snapBytes;
        if (!StorageManager::ReadAllBytes(snapPath, snapBytes) || snapBytes != beforeImportBytes) return 111;

        // 8c. Read-only recovery distinction (recoveredFromBackup true vs false) + 1-click RestoreFromLastBackup
        {
            StorageManager recSm;
            recSm.Init(testDir + L"\\recovery_bak_case");
            AppState origState{};
            if (!recSm.LoadOrSeed(origState)) return 112;
            origState.vault[0].title = L"GoodBackupTitle";
            if (!recSm.Save(origState)) return 113;
            // Reload once cleanly to populate data.json.bak
            AppState cleanReload{};
            if (!recSm.LoadOrSeed(cleanReload)) return 114;

            // Corrupt data.json while keeping valid data.json.bak
            std::string corruptPayload = "{ \"schemaVersion\": 2, \"vault\": [ BROKEN ";
            if (!StorageManager::WriteAllBytesAtomic(recSm.dataFilePath, corruptPayload)) return 115;

            AppState fallbackState{};
            if (recSm.LoadOrSeed(fallbackState)) return 116;
            if (!recSm.readOnlyRecoveryMode || !recSm.recoveredFromBackup ||
                fallbackState.vault.empty() || fallbackState.vault[0].title != L"GoodBackupTitle") {
                return 117;
            }
            // RestoreFromLastBackup must archive corrupt file, restore .bak, and exit read-only mode
            std::wstring restErr;
            AppState restored{};
            if (!recSm.RestoreFromLastBackup(restored, restErr) ||
                recSm.readOnlyRecoveryMode || recSm.recoveredFromBackup ||
                restored.vault.empty() || restored.vault[0].title != L"GoodBackupTitle") {
                return 118;
            }
            // Corrupt data.json with NO .bak must set recoveredFromBackup = false
            DeleteFileW(recSm.backupFilePath.c_str());
            if (!StorageManager::WriteAllBytesAtomic(recSm.dataFilePath, corruptPayload)) return 119;
            AppState noBakState{};
            if (recSm.LoadOrSeed(noBakState)) return 120;
            if (!recSm.readOnlyRecoveryMode || recSm.recoveredFromBackup) return 121;
            DiagLogger::Instance().Init(sm.dataDir);
        }

        // 8d. Feedback URL overflow must report body truncation and NEVER silently chop user body
        {
            std::wstring hugeBody(2200, L'A');
            bool bodyTruncated = false;
            std::wstring url = BuildGitHubIssueUrl(true, L"长正文反馈", hugeBody, L"Windows 11", L"diag", &bodyTruncated);
            if (!bodyTruncated || url.size() > 1900 || url.find(L"AAAAAAAAAAAA") != std::wstring::npos) {
                return 122;
            }
        }

        // 8e. Dock retract state machine + MenuOwner isolation + UIPI foreground auto-close + transactional drag + stall self-heal + press ownership
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int smErr = 0;
        {
            DarkPopupMenu menu;
            DockWindow dock2;
            AppState st2 = s2;
            st2.dockEdge = DockEdge::Right;
            st2.ActiveSlots()[9].clear(); // Leave slot 10 empty to test empty-slot press-release ownership

            if (!menu.Init(GetModuleHandleW(nullptr)) ||
                !dock2.Init(GetModuleHandleW(nullptr), &st2, &sm, &menu, nullptr, nullptr, false)) {
                smErr = 123;
            } else {
                RECT simMon{ 0, 0, 1920, 1080 };
                RECT simWork{ 0, 0, 1920, 1040 };
                dock2.monitorProvider = makeFixedMon(simMon, simWork);
                POINT simCursor{ 200, 200 }; // Far outside right-docked panel
                BOOL simCursorOk = TRUE;
                dock2.cursorPosProvider = [&](POINT* outPt) -> BOOL {
                    if (!simCursorOk) return FALSE;
                    if (outPt) *outPt = simCursor;
                    return TRUE;
                };
                HWND simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x1111));
                menu.foregroundProvider = [&]() -> HWND { return simFg; };
                menu.cursorPosProvider = [&](POINT* outPt) -> BOOL {
                    if (outPt) *outPt = simCursor;
                    return TRUE;
                };

                // (1) Vault-owned menu must NOT block DockWindow auto-collapse
                dock2.ExpandAndReveal(0);
                dock2.expandSpring.Snap(1.0f);
                dock2.RenderAndPresent("selftest-v062-open");
                if (dock2.holdOpenUntilTick != 0 || dock2.handleHovered) smErr = 124;

                if (smErr == 0) {
                    std::vector<DarkMenuItem> mItems = { DarkMenuItem::Action(1, L"测试项") };
                    menu.ShowAt(POINT{ 400, 400 }, mItems, nullptr, 200.0f, MenuOwner::Vault);
                    dock2.mouseOutsideSince = GetTickCount64() - 300;
                    dock2.EvaluateCollapseState("selftest-vault-menu");
                    if (!menu.IsOpenFor(MenuOwner::Vault) || dock2.expandSpring.target != 0.0f) {
                        smErr = 125;
                    }
                    menu.Close();
                }

                // (2) Dock-owned menu blocks collapse while open, then UIPI foreground switch closes menu & retracts dock!
                if (smErr == 0) {
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v062-dockmenu");
                    std::vector<DarkMenuItem> mItems = { DarkMenuItem::Action(1, L"槽位菜单") };
                    menu.ShowAt(POINT{ 1700, 400 }, mItems, nullptr, 200.0f, MenuOwner::Dock);
                    dock2.mouseOutsideSince = GetTickCount64() - 300;
                    dock2.EvaluateCollapseState("selftest-dock-menu-open");
                    if (!menu.IsOpenFor(MenuOwner::Dock) || dock2.expandSpring.target < 0.5f) {
                        smErr = 126;
                    }
                    // Simulate Alt+Tab / elevated window foreground change (UIPI)
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x2222));
                    dock2.mouseOutsideSince = GetTickCount64() - 300;
                    menu.OnPollTimer();
                    if (menu.IsOpen() || dock2.expandSpring.target != 0.0f) {
                        smErr = 127;
                    }
                }

                // (3) Transactional header drag: move 10px then WM_CAPTURECHANGED -> MUST roll back to DockEdge::Right!
                if (smErr == 0) {
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v062-drag");
                    int hdrX = (int)std::round((Theme::ShadowPad + Theme::PanelWidth * 0.5f) * dock2.dpiScale);
                    int hdrY = (int)std::round((Theme::ShadowPad + 16.0f) * dock2.dpiScale);
                    SendMessageW(dock2.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(hdrX, hdrY));
                    SendMessageW(dock2.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(hdrX - 12, hdrY));
                    if (!dock2.isDragging || !dock2.dragPreviewFloating || st2.dockEdge != DockEdge::Right) {
                        smErr = 128; // Must NOT mutate dockEdge during WM_MOUSEMOVE preview!
                    }
                    SendMessageW(dock2.hwnd, WM_CAPTURECHANGED, 0, 0);
                    if (dock2.isDragging || dock2.dragPreviewFloating || st2.dockEdge != DockEdge::Right) {
                        smErr = 129; // Must roll back cleanly on capture loss!
                    }
                }

                // (4) Stale kTimerAutoRetract generation invalidated by ExpandAndReveal(0)
                if (smErr == 0) {
                    dock2.clipboardWriter = stubClipOk;
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v062-gen");
                    dock2.TriggerSlotCopy(0);
                    if (!dock2.pendingAutoRetract) smErr = 130;
                    uint64_t armedGen = dock2.pendingAutoRetractGen;
                    dock2.ExpandAndReveal(0); // Increments retractGen and cancels stale pending retract
                    dock2.pendingAutoRetract = true;
                    dock2.pendingAutoRetractGen = armedGen; // Re-inject stale gen to verify WM_TIMER rejects it
                    SendMessageW(dock2.hwnd, WM_TIMER, DockWindow::kTimerAutoRetract, 0);
                    if (dock2.expandSpring.target < 0.5f) {
                        smErr = 131;
                    }
                    dock2.pendingAutoRetract = false;
                }

                // (5) Docked-side physical edge bridge (Fitts's Law) + strict inner/vertical/multi-monitor bounds + GetCursorPos fail-closed
                if (smErr == 0) {
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v081-edge-right");
                    int midY = dock2.curWinY + dock2.curWinH / 2;
                    int cardLeftPx = dock2.curWinX + (int)std::round(Theme::ShadowPad * dock2.dpiScale);
                    int cardTopPx = dock2.curWinY + (int)std::round(Theme::ShadowPad * dock2.dpiScale);

                    // Right physical edge pixel (1919) MUST keep expanded panel open (both settled & mid-slide!)
                    simCursor = POINT{ 1919, midY };
                    if (!dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                    if (smErr == 0) {
                        dock2.mouseOutsideSince = GetTickCount64() - 400;
                        dock2.EvaluateCollapseState("selftest-right-edge-stay");
                        if (dock2.expandSpring.target < 0.5f) smErr = 132;
                    }
                    if (smErr == 0) {
                        dock2.expandSpring.value = 0.05f; // Early expand animation frame
                        if (!dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                        dock2.expandSpring.Snap(1.0f);
                    }
                    // Crossing to adjacent right monitor (x=1920), inner left outside card, or vertically outside card MUST return false
                    if (smErr == 0) {
                        simCursor = POINT{ 1920, midY };
                        if (dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                    }
                    if (smErr == 0) {
                        simCursor = POINT{ cardLeftPx - 2, midY };
                        if (dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                    }
                    if (smErr == 0) {
                        simCursor = POINT{ 1919, cardTopPx - 5 };
                        if (dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                    }

                    // Mirror verification for DockEdge::Left (x=0 stays open, x=-1 adjacent monitor & inner-right leave)
                    if (smErr == 0) {
                        st2.dockEdge = DockEdge::Left;
                        dock2.RenderAndPresent("selftest-v081-edge-left");
                        int cardRightPx = dock2.curWinX + (int)std::round((Theme::ShadowPad + Theme::PanelWidth) * dock2.dpiScale);
                        simCursor = POINT{ 0, midY };
                        if (!dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                        simCursor = POINT{ -1, midY };
                        if (smErr == 0 && dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                        simCursor = POINT{ cardRightPx + 2, midY };
                        if (smErr == 0 && dock2.IsCursorInsideInteractivePanel()) smErr = 132;
                        st2.dockEdge = DockEdge::Right;
                        dock2.RenderAndPresent("selftest-v081-edge-restore");
                    }

                    // GetCursorPos failure must remain fail-closed
                    simCursorOk = FALSE;
                    if (smErr == 0 && dock2.IsCursorInsideInteractivePanel()) {
                        smErr = 133;
                    }
                    simCursorOk = TRUE;
                    simCursor = POINT{ 200, 200 };
                }

                // (6) Watchdog expanded-state stall self-heal when leave-watch timer was dropped
                if (smErr == 0) {
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v062-stall");
                    dock2.animRunning = false;
                    dock2.StopLeaveWatchTimer();
                    dock2.mouseOutsideSince = GetTickCount64() - 400;
                    dock2.OnWatchdogTimer();
                    if (dock2.expandSpring.target != 0.0f) {
                        smErr = 134;
                    }
                }

                // (7) Press-release ownership: stray WM_LBUTTONUP on empty slot 10 without WM_LBUTTONDOWN must NOT open menu!
                if (smErr == 0) {
                    dock2.ExpandAndReveal(0);
                    dock2.expandSpring.Snap(1.0f);
                    dock2.RenderAndPresent("selftest-v062-press");
                    float emptyCx = Theme::ShadowPad + Theme::PanelWidth * 0.5f;
                    float emptyCy = Theme::ShadowPad + Theme::HeaderHeight +
                                    9.0f * (Theme::SlotHeight + Theme::SlotGap) + Theme::SlotHeight * 0.5f;
                    LPARAM lpEmpty = MAKELPARAM((short)std::round(emptyCx * dock2.dpiScale),
                                                (short)std::round(emptyCy * dock2.dpiScale));
                    SendMessageW(dock2.hwnd, WM_LBUTTONUP, 0, lpEmpty);
                    if (menu.IsOpen()) {
                        smErr = 135;
                    }
                }

                if (dock2.hoverBubble.hwnd) DestroyWindow(dock2.hoverBubble.hwnd);
                if (dock2.hwnd) DestroyWindow(dock2.hwnd);
                if (menu.hwnd) DestroyWindow(menu.hwnd);
            }
        }
        CoUninitialize();
        if (smErr != 0) return smErr;
    }

    // 9. Verify v0.7.0 onboarding, AutoStart stale-path detection, Vault decoupling/undo/duplicate,
    //    quick-swap sorting, multi-monitor anchor, short-screen scroll clamp & DarkPopupMenu keyboard nav
    {
        // 9a. AutoStart::GetStatus distinguishes Disabled / StaleOtherPath / EnabledCurrent
        wchar_t staleRegVal[96]{};
        wsprintfW(staleRegVal, L"PromptPod_StaleTest_%lu_%llu", (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
        if (AutoStart::GetStatus(staleRegVal) != AutoStart::Status::Disabled) return 136;
        HKEY hKey = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, AutoStart::kRunSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            const wchar_t* fakeMoved = L"\"C:\\OldMovedFolder\\PromptPod.exe\" --autostart";
            RegSetValueExW(hKey, staleRegVal, 0, REG_SZ, reinterpret_cast<const BYTE*>(fakeMoved),
                           (DWORD)((wcslen(fakeMoved) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
        if (AutoStart::GetStatus(staleRegVal) != AutoStart::Status::StaleOtherPath || AutoStart::IsEnabled(staleRegVal)) {
            AutoStart::SetEnabled(false, staleRegVal);
            return 137;
        }
        if (!AutoStart::SetEnabled(true, staleRegVal) || AutoStart::GetStatus(staleRegVal) != AutoStart::Status::EnabledCurrent) {
            AutoStart::SetEnabled(false, staleRegVal);
            return 138;
        }
        AutoStart::SetEnabled(false, staleRegVal);

        // 9b. VaultWindow All-view drag-to-reorder must reorder vault WITHOUT mutating active slots!
        {
            StorageManager decSm;
            decSm.Init(testDir + L"\\vault_decouple_case");
            AppState decSt{};
            if (!decSm.LoadOrSeed(decSt)) return 139;
            auto origSlots = decSt.ActiveSlots();
            VaultWindow vwDec;
            vwDec.Init(GetModuleHandleW(nullptr), &decSt, &decSm, nullptr, nullptr);
            vwDec.hwnd = CreateWindowExW(0, L"PromptPod_VaultWnd", L"DecTest", WS_POPUP, 0, 0, 760, 580, nullptr, nullptr, GetModuleHandleW(nullptr), &vwDec);
            if (!vwDec.hwnd) return 139;
            vwDec.dpiScale = 1.0f;
            vwDec.activeTab = 0;
            vwDec.vaultFilter = -1;
            vwDec.vaultListTopDip = 110.0f;
            vwDec.InvalidateCaches();
            if (!vwDec.BeginVaultDragForRow(0, 140.0f, 110.0f)) { DestroyWindow(vwDec.hwnd); return 139; }
            SendMessageW(vwDec.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, 216));
            // Mid-drag must NEVER mutate AppState
            if (!vwDec.isVaultDragging || decSt.vault[0].id != L"seed_01") { DestroyWindow(vwDec.hwnd); return 140; }
            SendMessageW(vwDec.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(100, 216));
            DestroyWindow(vwDec.hwnd);
            if (decSt.vault[0].id != L"seed_02" || decSt.vault[1].id != L"seed_01" || decSt.ActiveSlots() != origSlots) {
                return 140;
            }
            DiagLogger::Instance().Init(sm.dataDir);
        }

        // 9c. VaultWindow DeletePromptWithUndo + UndoLastDelete + DuplicatePrompt
        StorageManager vSm;
        vSm.Init(testDir + L"\\vault_ux_case");
        AppState vSt{};
        if (!vSm.LoadOrSeed(vSt)) return 141;
        vSt.vault[0].useCount = 7;
        vSm.Save(vSt);

        VaultWindow vw;
        vw.state = &vSt;
        vw.storage = &vSm;
        if (!vw.DeletePromptWithUndo(L"seed_01")) return 142;
        if (vSt.vault.size() != 11 || !vSt.ActiveSlots()[0].empty() || !vw.undoDelete.active) return 143;
        if (!vw.UndoLastDelete()) return 144;
        if (vSt.vault.size() != 12 || vSt.vault[0].id != L"seed_01" || vSt.vault[0].useCount != 7 || vSt.ActiveSlots()[0] != L"seed_01") {
            return 145;
        }
        if (!vw.DuplicatePrompt(L"seed_01") || vSt.vault.size() != 13 || vSt.vault[1].useCount != 0 ||
            vSt.FindSlotOfId(vSt.vault[1].id) != -1) {
            return 146;
        }
        DiagLogger::Instance().Init(sm.dataDir);

        // 9d. Search context snippet centers around a late body match
        std::wstring longBody(80, L'x');
        longBody += L" TARGET_NEEDLE_XYZ ";
        longBody += std::wstring(60, L'y');
        std::wstring snip = BuildSearchPreviewSnippet(longBody, L"target_needle", 80);
        if (snip.rfind(L"...", 0) != 0 || snip.find(L"TARGET_NEEDLE_XYZ") == std::wstring::npos) return 147;

        // 9e. DockWindow::SelectQuickSwapCandidates sorts unmounted items by lastUsedAt desc, then useCount desc
        AppState swapSt{};
        StorageManager::SeedDefaults(swapSt, false);
        swapSt.vault[4].useCount = 15; swapSt.vault[4].lastUsedAt = 100;
        swapSt.vault[6].useCount = 2;  swapSt.vault[6].lastUsedAt = 500;
        swapSt.vault[8].useCount = 9;  swapSt.vault[8].lastUsedAt = 100;
        auto cands = DockWindow::SelectQuickSwapCandidates(swapSt, 6);
        if (cands.size() != 6 || cands[0]->id != L"seed_07" || cands[1]->id != L"seed_05" || cands[2]->id != L"seed_09") {
            return 148;
        }

        // 9f-9i. Onboarding flow, multi-monitor anchor, short-screen clamp & DarkPopupMenu keyboard nav
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int v7Err = 0;
        {
            DarkPopupMenu menu7;
            DockWindow dock7;
            AppState st7{};
            StorageManager::SeedDefaults(st7, false);
            st7.onboardingCompleted = false;
            st7.hasDockAnchor = true;
            st7.dockAnchorX = 2500;
            st7.dockAnchorY = 500;

            if (!menu7.Init(GetModuleHandleW(nullptr)) ||
                !dock7.Init(GetModuleHandleW(nullptr), &st7, &sm, &menu7, nullptr, nullptr, false)) {
                v7Err = 149;
            } else {
                // 9g. Multi-monitor anchor must query secondary monitor refPt (2500, 500) before first present!
                POINT capturedRef{ 0, 0 };
                RECT secMon{ 1920, 0, 3840, 1080 };
                RECT secWork{ 1920, 0, 3840, 1040 };
                dock7.hasPresentedOnce = false;
                dock7.monitorProvider = [&](POINT ref, RECT* outMon) -> RECT {
                    capturedRef = ref;
                    if (outMon) *outMon = secMon;
                    return secWork;
                };
                dock7.RenderAndPresent("selftest-anchor");
                if (capturedRef.x != 2500 || capturedRef.y != 500 || dock7.curWinX != (3840 - dock7.curWinW)) {
                    v7Err = 150;
                }

                // 9f. Onboarding keeps panel open during Step 1 copy, advances step1Done, and finishes with capsule glow
                if (v7Err == 0) {
                    dock7.clipboardWriter = stubClipOk;
                    POINT farOut{ 100, 100 };
                    dock7.cursorPosProvider = [&](POINT* outPt) -> BOOL { if (outPt) *outPt = farOut; return TRUE; };
                    dock7.StartOnboarding(false);
                    dock7.mouseOutsideSince = GetTickCount64() - 500;
                    dock7.EvaluateCollapseState("selftest-onboard-open");
                    if (!dock7.IsOnboardingActive() || dock7.expandSpring.target < 0.5f) {
                        v7Err = 151;
                    }
                    dock7.TriggerSlotCopy(0);
                    if (!dock7.onboardingWnd.step1Done || dock7.pendingAutoRetract) {
                        v7Err = 152;
                    }
                    dock7.FinishOnboarding(true);
                    if (dock7.IsOnboardingActive() || !st7.onboardingCompleted ||
                        dock7.expandSpring.target != 0.0f || dock7.capsuleBreathStartTick == 0) {
                        v7Err = 153;
                    }
                }

                // 9h. Short-screen work area height clamp (e.g. 1366x768 @ 150% -> ~480 DIP work height)
                if (v7Err == 0) {
                    RECT shortMon{ 0, 0, 910, 512 };
                    RECT shortWork{ 0, 0, 910, 440 };
                    dock7.monitorProvider = makeFixedMon(shortMon, shortWork);
                    dock7.expandSpring.Snap(1.0f);
                    dock7.RenderAndPresent("selftest-short-screen");
                    if (dock7.curPanelHeightDip >= Theme::PanelHeight() - 10.0f ||
                        dock7.maxSlotScrollDip <= 10.0f) {
                        v7Err = 154;
                    }
                    SendMessageW(dock7.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
                    if (dock7.slotScrollDip <= 0.1f || dock7.slotScrollDip > dock7.maxSlotScrollDip) {
                        v7Err = 155;
                    }
                }

                // 9i. DarkPopupMenu keyboard navigation (VK_DOWN skips header/separator, VK_RETURN fires callback)
                if (v7Err == 0) {
                    int chosenCmd = -1;
                    std::vector<DarkMenuItem> kbItems = {
                        DarkMenuItem::Header(L"分组"),
                        DarkMenuItem::Separator(),
                        DarkMenuItem::Action(42, L"第一项"),
                        DarkMenuItem::Action(99, L"第二项")
                    };
                    menu7.ShowAt(POINT{ 400, 400 }, kbItems, [&](int cmd) { chosenCmd = cmd; }, 200.0f, MenuOwner::Tray);
                    SendMessageW(menu7.hwnd, WM_KEYDOWN, VK_DOWN, 0);
                    SendMessageW(menu7.hwnd, WM_KEYDOWN, VK_DOWN, 0);
                    SendMessageW(menu7.hwnd, WM_KEYDOWN, VK_RETURN, 0);
                    if (chosenCmd != 99 || menu7.IsOpen()) {
                        v7Err = 156;
                    }
                }

                if (dock7.hoverBubble.hwnd) DestroyWindow(dock7.hoverBubble.hwnd);
                if (dock7.hwnd) DestroyWindow(dock7.hwnd);
                if (menu7.hwnd) DestroyWindow(menu7.hwnd);
            }
        }
        CoUninitialize();
        if (v7Err != 0) return v7Err;
    }

    // 10. Verify v0.10.0 Multi-Select 4-Scene Deck (schema v4, visual width units, lossless v2/v3 seed upgrade,
    //     non-contiguous multi-select, scene prompt assign/remove, 4-scene cascade undo, sanitized diag & DockWindow 1..4 scenes)
    {
        // 10a. VisualWidthUnits, IsValidSceneName & ClampSceneName boundary checks (4 default names)
        if (VisualWidthUnits(L"深度复盘") != 8 || !IsValidSceneName(L"深度复盘")) return 160;
        if (VisualWidthUnits(L"深度复盘啊") != 10 || IsValidSceneName(L"深度复盘啊")) return 161;
        if (ClampSceneName(L"深度复盘啊", 0) != L"深度复盘") return 162;
        if (VisualWidthUnits(L"Coding01") != 8 || !IsValidSceneName(L"Coding01")) return 163;
        if (VisualWidthUnits(L"Coding012") != 9 || IsValidSceneName(L"Coding012")) return 164;
        if (ClampSceneName(L"Coding012", 1) != L"Coding01") return 165;
        if (VisualWidthUnits(L"AI开发者") != 8 || !IsValidSceneName(L"AI开发者") || IsValidSceneName(L"AI开发者1")) return 166;
        if (IsValidSceneName(L"") || IsValidSceneName(L"   ") || IsValidSceneName(L"A\nB") ||
            ClampSceneName(L"   ", 1) != L"产品" || ClampSceneName(L"   ", 2) != L"开发" || ClampSceneName(L"   ", 3) != L"写作") return 167;

        // 10b. Legacy v2 -> v4 lossless upgrade: preserves data.v2.bak.json, adds seed_11/seed_12, initializes empty Scene 1 ("产品"),
        //      and NEVER resurrects deleted seed_11 after upgrade!
        StorageManager mSm;
        mSm.Init(testDir + L"\\v2_migrate_case");
        std::string v2Json = "{\"schemaVersion\":2,\"dockEdge\":2,\"dockRatioMilli\":3200,\"showHoverBubble\":true,"
                             "\"slots\":[\"p1\",\"p2\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"],"
                             "\"vault\":[{\"id\":\"p1\",\"title\":\"T1\",\"content\":\"Body1\",\"useCount\":4,\"createdAt\":1,\"lastUsedAt\":2},"
                             "{\"id\":\"p2\",\"title\":\"T2\",\"content\":\"Body2\",\"useCount\":2,\"createdAt\":1,\"lastUsedAt\":3}]}";
        if (!StorageManager::WriteAllBytesAtomic(mSm.dataFilePath, v2Json)) return 168;
        AppState mSt{};
        if (!mSm.LoadOrSeed(mSt)) return 169;
        if (mSt.schemaVersion != 4 || mSt.seedPackRev != kCurrentSeedPackRev ||
            mSt.vault.size() != 4 || !mSt.FindById(L"seed_11") || !mSt.FindById(L"seed_12") ||
            mSt.scenes[0].slots[0] != L"p1" || mSt.scenes[0].slots[1] != L"p2" ||
            mSt.scenes[0].name != L"通用" || !mSt.scenes[0].enabled ||
            mSt.scenes[1].name != L"产品" || !mSt.scenes[1].enabled ||
            mSt.scenes[1].slots[0] != L"seed_11" || mSt.scenes[1].slots[1] != L"seed_12" ||
            mSt.scenes[2].enabled || mSt.scenes[3].enabled) return 170;

        std::string v2BakOnDisk, v4OnDisk;
        if (!StorageManager::ReadAllBytes(mSm.dataDir + L"\\data.v2.bak.json", v2BakOnDisk) || v2BakOnDisk != v2Json) return 172;
        if (!StorageManager::ReadAllBytes(mSm.dataFilePath, v4OnDisk) || v4OnDisk.find("\"schemaVersion\": 4") == std::string::npos) return 173;

        // Verify deleting seed_11 after upgrade stays deleted across reload (seedPackRev prevents resurrection)
        {
            AppState delCheck = mSt;
            delCheck.RemoveFromScene(1, L"seed_11");
            delCheck.vault.erase(std::remove_if(delCheck.vault.begin(), delCheck.vault.end(),
                [](const PromptItem& x) { return x.id == L"seed_11"; }), delCheck.vault.end());
            if (!mSm.Save(delCheck)) return 171;
            AppState reloaded{};
            if (!mSm.LoadOrSeed(reloaded) || reloaded.FindById(L"seed_11") != nullptr || reloaded.FindById(L"seed_12") == nullptr) return 171;
            if (!mSm.Save(mSt)) return 171;
        }

        // 10b-v3. Legacy v3 user who ALREADY populated Scene 1 ("思考" with p1) must NOT have Scene 1 overwritten!
        {
            StorageManager v3Sm;
            v3Sm.Init(testDir + L"\\v3_nonoverwrite_case");
            std::string v3Json = "{\"schemaVersion\":3,\"dockEdge\":2,\"dockRatioMilli\":3200,\"showHoverBubble\":true,"
                                 "\"multiSceneEnabled\":true,\"sceneCount\":2,\"activeSceneIndex\":1,"
                                 "\"scenes\":[{\"name\":\"通用\",\"slots\":[\"p1\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]},"
                                 "{\"name\":\"思考\",\"slots\":[\"p2\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]},"
                                 "{\"name\":\"开发\",\"slots\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]}],"
                                 "\"vault\":[{\"id\":\"p1\",\"title\":\"T1\",\"content\":\"B1\",\"useCount\":1,\"createdAt\":1,\"lastUsedAt\":1},"
                                 "{\"id\":\"p2\",\"title\":\"T2\",\"content\":\"B2\",\"useCount\":1,\"createdAt\":1,\"lastUsedAt\":1}]}";
            if (!StorageManager::WriteAllBytesAtomic(v3Sm.dataFilePath, v3Json)) return 195;
            AppState v3St{};
            if (!v3Sm.LoadOrSeed(v3St)) return 196;
            if (v3St.vault.size() != 4 || !v3St.FindById(L"seed_11") || !v3St.FindById(L"seed_12") ||
                v3St.scenes[1].name != L"思考" || v3St.scenes[1].slots[0] != L"p2" ||
                !v3St.scenes[0].enabled || !v3St.scenes[1].enabled || v3St.scenes[2].enabled || v3St.scenes[3].enabled ||
                v3St.activeSceneIndex != 1) return 197;
            std::string v3Bak;
            if (!StorageManager::ReadAllBytes(v3Sm.dataDir + L"\\data.v3.bak.json", v3Bak) || v3Bak != v3Json) return 198;
            DiagLogger::Instance().Init(sm.dataDir);
        }

        // 10c. 4-scene multi-select, at-least-1 guard, cross-scene sharing, AssignToSceneFirstEmpty/RemoveFromScene,
        //      v2 compat export, sanitized diagnostics & 4-scene cascade delete/undo
        mSt.SetSceneEnabled(0, true);
        mSt.SetSceneEnabled(1, true);
        mSt.SetSceneEnabled(2, true);
        mSt.SetSceneEnabled(3, true);
        mSt.scenes[1].name = L"SecPrj"; // 6 ASCII units, tests sanitized diagnostics non-leak
        mSt.scenes[2].name = L"Coding";
        mSt.scenes[3].name = L"写作";
        mSt.scenes[1].slots[0] = L"p1"; // Same Prompt ID shared across all 4 scenes
        mSt.scenes[1].slots[1] = L"p1"; // Duplicate within same scene -> must be cleared by SanitizeSlots
        mSt.scenes[2].slots[3] = L"p1";
        mSt.scenes[2].slots[0] = L"p2";
        if (mSt.AssignToSceneFirstEmpty(3, L"p1") != 0) return 174;
        mSt.SanitizeSlots();
        if (mSt.scenes[0].slots[0] != L"p1" || mSt.scenes[1].slots[0] != L"p1" || !mSt.scenes[1].slots[1].empty() ||
            mSt.scenes[2].slots[3] != L"p1" || mSt.scenes[3].slots[0] != L"p1" ||
            mSt.CountOtherEnabledScenesWithId(L"p1") != 3) return 174;

        // Unchecking 3 scenes leaves 1; attempting to uncheck the last scene MUST fail cleanly while preserving hidden scenes!
        mSt.activeSceneIndex = 3;
        if (!mSt.SetSceneEnabled(1, false) || !mSt.SetSceneEnabled(2, false) || !mSt.SetSceneEnabled(3, false)) return 175;
        if (mSt.ToggleSceneEnabled(0)) return 175; // Cannot disable the only enabled scene!
        if (mSt.EffectiveActiveSceneIndex() != 0 || mSt.EnabledSceneCount() != 1 || mSt.IsMultiSceneActive() ||
            mSt.scenes[1].name != L"SecPrj" || mSt.scenes[2].slots[3] != L"p1" || mSt.scenes[3].slots[0] != L"p1") return 175;

        // Non-contiguous subset {0, 2, 3}
        mSt.SetSceneEnabled(2, true);
        mSt.SetSceneEnabled(3, true);
        int visSub[kMaxScenes]{};
        if (mSt.GetEnabledScenes(visSub) != 3 || visSub[0] != 0 || visSub[1] != 2 || visSub[2] != 3) return 176;
        mSt.SetSceneEnabled(1, true);
        if (!mSm.Save(mSt)) return 177;

        // v2 compat export preserves all vault items + scenes[0].slots without scenes key
        std::string v2Export = SerializeStateToJson(mSt, true);
        AppState v2Parsed{};
        if (v2Export.find("\"schemaVersion\": 2") == std::string::npos || v2Export.find("\"scenes\"") != std::string::npos ||
            !ParseStateFromJson(v2Export, v2Parsed) || v2Parsed.vault.size() != 4 || v2Parsed.scenes[0].slots[0] != L"p1") return 178;

        // Sanitized diagnostics reports multiScene state WITHOUT leaking custom scene name ("SecPrj")
        std::wstring msDiag = BuildSanitizedDiagnostics(mSt, mSm, false);
        if (msDiag.find(L"multiScene=on(count=4,active=0)") == std::wstring::npos || msDiag.find(L"SecPrj") != std::wstring::npos) return 179;

        // Cascade delete across all 4 scenes + undo restores all 4 scenes
        VaultWindow vwMs;
        vwMs.state = &mSt;
        vwMs.storage = &mSm;
        if (!vwMs.DeletePromptWithUndo(L"p1")) return 180;
        if (!mSt.scenes[0].slots[0].empty() || !mSt.scenes[1].slots[0].empty() ||
            !mSt.scenes[2].slots[3].empty() || !mSt.scenes[3].slots[0].empty()) return 181;
        if (!vwMs.UndoLastDelete()) return 182;
        if (mSt.scenes[0].slots[0] != L"p1" || mSt.scenes[1].slots[0] != L"p1" ||
            mSt.scenes[2].slots[3] != L"p1" || mSt.scenes[3].slots[0] != L"p1") return 183;

        // 10e. DockWindow single-scene (471 DIP), non-contiguous 2-scene ({0,2}), 4-scene (507 DIP, 5 DIP gap),
        //      click/wheel scene switch, isolated copy/drag & panel right-click app menu
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int v8Err = 0;
        {
            DockWindow dock8;
            std::wstring clip8;
            int appMenuCalls = 0;
            dock8.clipboardWriter = [&](const std::wstring& txt) -> bool { clip8 = txt; return true; };
            mSt.SetSceneEnabled(1, false);
            mSt.SetSceneEnabled(2, false);
            mSt.SetSceneEnabled(3, false);
            if (!dock8.Init(GetModuleHandleW(nullptr), &mSt, &mSm, nullptr, nullptr,
                            [&](POINT) { ++appMenuCalls; }, false)) {
                v8Err = 184;
            } else {
                RECT simMon{ 0, 0, 1920, 1080 };
                RECT simWork{ 0, 0, 1920, 1040 };
                dock8.monitorProvider = makeFixedMon(simMon, simWork);
                dock8.expandSpring.Snap(1.0f);
                dock8.RenderAndPresent("selftest-v10-single");
                if (std::fabs(dock8.curPanelHeightDip - 471.0f) > 0.5f) v8Err = 185;

                // Right-clicking panel margin triggers app menu
                if (v8Err == 0) {
                    D2D1_RECT_F pb8 = dock8.PanelBoxDip();
                    LPARAM lpMargin = MAKELPARAM((short)std::round((pb8.left + 4.0f) * dock8.dpiScale),
                                                 (short)std::round((pb8.bottom - 4.0f) * dock8.dpiScale));
                    SendMessageW(dock8.hwnd, WM_RBUTTONUP, 0, lpMargin);
                    if (appMenuCalls != 1) v8Err = 193;
                }

                // Non-contiguous subset {0, 2}: clicking button pos 1 switches directly to Scene 2!
                if (v8Err == 0) {
                    mSt.SetSceneEnabled(2, true);
                    mSt.activeSceneIndex = 0;
                    mSt.scenes[2].slots[0] = L"p2";
                    mSt.scenes[2].slots[1] = L"p1";
                    dock8.RenderAndPresent("selftest-v10-subset");
                    if (std::fabs(dock8.curPanelHeightDip - 507.0f) > 0.5f) v8Err = 186;
                    D2D1_RECT_F b1 = dock8.SceneBtnRectDip(1, 2);
                    LPARAM lpB1 = MAKELPARAM((short)std::round(((b1.left + b1.right) * 0.5f) * dock8.dpiScale),
                                             (short)std::round(((b1.top + b1.bottom) * 0.5f) * dock8.dpiScale));
                    SendMessageW(dock8.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpB1);
                    SendMessageW(dock8.hwnd, WM_LBUTTONUP, 0, lpB1);
                    if (mSt.activeSceneIndex != 2) v8Err = 187;

                    D2D1_RECT_F sl0 = dock8.SlotRectDip(0);
                    LPARAM lpSl0 = MAKELPARAM((short)std::round(((sl0.left + sl0.right) * 0.5f) * dock8.dpiScale),
                                              (short)std::round(((sl0.top + sl0.bottom) * 0.5f) * dock8.dpiScale));
                    SendMessageW(dock8.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                    SendMessageW(dock8.hwnd, WM_LBUTTONUP, 0, lpSl0);
                    if (clip8 != L"Body2") v8Err = 188;

                    // Drag slot 0 -> slot 1 in Scene 2 must NOT disturb Scene 0
                    D2D1_RECT_F sl1 = dock8.SlotRectDip(1);
                    LPARAM lpSl1 = MAKELPARAM((short)std::round(((sl1.left + sl1.right) * 0.5f) * dock8.dpiScale),
                                              (short)std::round(((sl1.top + sl1.bottom) * 0.5f) * dock8.dpiScale));
                    SendMessageW(dock8.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                    SendMessageW(dock8.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSl1);
                    SendMessageW(dock8.hwnd, WM_LBUTTONUP, 0, lpSl1);
                    if (mSt.scenes[2].slots[0] != L"p1" || mSt.scenes[2].slots[1] != L"p2" || mSt.scenes[0].slots[0] != L"p1") {
                        v8Err = 189;
                    }
                }

                // All 4 scenes enabled -> 5 DIP gap, flush PaddingX alignment, click 4th button -> Scene 3, wheel cycles 3 -> 0
                if (v8Err == 0) {
                    mSt.SetSceneEnabled(1, true);
                    mSt.SetSceneEnabled(3, true);
                    dock8.RenderAndPresent("selftest-v10-four");
                    D2D1_RECT_F pb8 = dock8.PanelBoxDip();
                    D2D1_RECT_F s0r = dock8.SceneBtnRectDip(0, 4);
                    D2D1_RECT_F s1r = dock8.SceneBtnRectDip(1, 4);
                    D2D1_RECT_F s3r = dock8.SceneBtnRectDip(3, 4);
                    if (std::fabs((s0r.bottom - s0r.top) - 26.0f) > 0.2f ||
                        std::fabs((s1r.left - s0r.right) - 5.0f) > 0.2f ||
                        std::fabs(s0r.left - (pb8.left + Theme::PaddingX)) > 0.2f ||
                        std::fabs(s3r.right - (pb8.right - Theme::PaddingX)) > 0.2f) {
                        v8Err = 194;
                    }
                    LPARAM lpS3 = MAKELPARAM((short)std::round(((s3r.left + s3r.right) * 0.5f) * dock8.dpiScale),
                                             (short)std::round(((s3r.top + s3r.bottom) * 0.5f) * dock8.dpiScale));
                    SendMessageW(dock8.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpS3);
                    SendMessageW(dock8.hwnd, WM_LBUTTONUP, 0, lpS3);
                    if (mSt.activeSceneIndex != 3) v8Err = 190;

                    D2D1_RECT_F sb = dock8.SceneBarBoxDip();
                    int sx = dock8.curWinX + (int)std::round(((sb.left + sb.right) * 0.5f) * dock8.dpiScale);
                    int sy = dock8.curWinY + (int)std::round(((sb.top + sb.bottom) * 0.5f) * dock8.dpiScale);
                    SendMessageW(dock8.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM((short)sx, (short)sy));
                    if (mSt.activeSceneIndex != 0) v8Err = 191;
                }

                // Back to single checked scene -> height restores cleanly to compact 471 DIP
                if (v8Err == 0) {
                    mSt.SetSceneEnabled(1, false);
                    mSt.SetSceneEnabled(2, false);
                    mSt.SetSceneEnabled(3, false);
                    dock8.RenderAndPresent("selftest-v10-off");
                    if (std::fabs(dock8.curPanelHeightDip - 471.0f) > 0.5f) v8Err = 192;
                }

                if (dock8.hoverBubble.hwnd) DestroyWindow(dock8.hoverBubble.hwnd);
                if (dock8.hwnd) DestroyWindow(dock8.hwnd);
            }
        }
        CoUninitialize();
        DiagLogger::Instance().Init(sm.dataDir);
        if (v8Err != 0) return v8Err;
    }

    // 11. Verify v0.11.0 UX polish regressions (auto-title, feedback diag truncation, onboarding bubble guard & dynamic slots,
    //     post-copy slot-move auto-retract cancel, menu outside-close toggle & external-fg guard, toast/undo isolation & clear-scene undo)
    {
        // 11a. DeriveAutoPromptTitle strips leading Markdown punctuation and extracts clean short first line
        if (DeriveAutoPromptTitle(L"  ### 深度代码审查清单\n第二行正文") != L"深度代码审查清单") return 200;
        if (DeriveAutoPromptTitle(L"   \n\t  ") != L"未命名 Prompt") return 201;

        // 11b. BuildGitHubIssueUrl reports outBodyTruncated & outDiagTruncated accurately + IsRegisteredTargetDefinitivelyMissing check
        bool bodyTr = false, diagTr = false;
        BuildGitHubIssueUrl(true, L"Bug", L"Steps", L"Win11", L"short diag", &bodyTr, &diagTr);
        if (bodyTr || diagTr) return 202;
        std::wstring hugeDiag(2200, L'D');
        BuildGitHubIssueUrl(true, L"Bug", L"Steps", L"Win11", hugeDiag, &bodyTr, &diagTr);
        if (bodyTr || !diagTr) return 203;
        if (!AutoStart::IsRegisteredTargetDefinitivelyMissing(testDir + L"\\definitely_missing_promptpod.exe")) return 215;
        if (AutoStart::IsRegisteredTargetDefinitivelyMissing(testDir + L"\\no_such_parent_dir\\PromptPod.exe")) return 216;

        // 11c. VaultWindow toast decouples visual undo button from Ctrl+Z lifetime, clear-scene undo protects against overwrite,
        //      and Settings tab hides assign banner height
        {
            StorageManager uxSm;
            uxSm.Init(testDir + L"\\ux_v11_case");
            AppState uxSt{};
            if (!uxSm.LoadOrSeed(uxSt)) return 204;
            VaultWindow vw11;
            vw11.state = &uxSt;
            vw11.storage = &uxSm;
            if (!vw11.DeletePromptWithUndo(L"seed_01") || !vw11.undoDelete.active || !vw11.toastShowsUndo) return 205;
            vw11.ShowToast(L"其他提示", false);
            if (vw11.toastShowsUndo || !vw11.undoDelete.active) return 206; // Visual undo hides on unrelated toast while Ctrl+Z stays alive!

            // Clear-scene undo restores full slot layout cleanly when scene is still empty
            uxSt.scenes[0].slots[0] = L"seed_02";
            uxSt.scenes[0].slots[1] = L"seed_03";
            uxSm.Save(uxSt);
            vw11.undoDelete = VaultWindow::UndoDeleteEntry{};
            vw11.undoDelete.active = true;
            vw11.undoDelete.isClearScene = true;
            vw11.undoDelete.clearedSceneIdx = 0;
            vw11.undoDelete.clearedSceneSlots = uxSt.scenes[0].slots;
            vw11.undoDelete.expireTick = GetTickCount64() + 5000;
            uxSt.ClearSceneSlots(0);
            if (!vw11.UndoLastDelete() || uxSt.scenes[0].slots[0] != L"seed_02" || uxSt.scenes[0].slots[1] != L"seed_03") return 207;

            // Clear-scene undo refuses to clobber if user mounted a new prompt into that scene after clearing
            vw11.undoDelete.active = true;
            vw11.undoDelete.isClearScene = true;
            vw11.undoDelete.clearedSceneIdx = 0;
            vw11.undoDelete.expireTick = GetTickCount64() + 5000;
            uxSt.ClearSceneSlots(0);
            uxSt.AssignToSlot(4, L"seed_04");
            if (vw11.UndoLastDelete() || uxSt.scenes[0].slots[4] != L"seed_04") return 217;

            // Settings tab (2) must NOT reserve assignTargetSlot banner height
            vw11.assignTargetSlot = 2;
            vw11.activeTab = 0;
            float topTab0 = vw11.ComputeContentTopDip();
            vw11.activeTab = 2;
            float topTab2 = vw11.ComputeContentTopDip();
            if (std::fabs((topTab0 - topTab2) - 40.0f) > 0.5f) return 208;

            // 11e. v0.11.1 Vault scene/unmounted filter view: filters accurately, sorts scene view strictly by slot 01..10,
            //      NEVER mutates dock activeSceneIndex, falls back cleanly when scene disabled or assignTargetSlot active!
            {
                AppState fltSt{};
                StorageManager::SeedDefaults(fltSt, false); // Scene 0: seed_01/seed_02; Scene 1: seed_11/seed_12; 8 unmounted
                // Reverse Scene 1 slots (slot 00 = seed_12, slot 01 = seed_11) to prove slot-order sorting overrides vault order!
                fltSt.scenes[1].slots[0] = L"seed_12";
                fltSt.scenes[1].slots[1] = L"seed_11";
                fltSt.activeSceneIndex = 0;

                VaultWindow vwFlt;
                vwFlt.state = &fltSt;
                vwFlt.storage = &uxSm;
                vwFlt.activeTab = 0;
                vwFlt.assignTargetSlot = -1;

                // All view -> 12 items, Unmounted count -> 8
                vwFlt.vaultFilter = -1;
                vwFlt.InvalidateCaches();
                vwFlt.EnsureVaultCache();
                if (vwFlt.cachedFilteredIndices.size() != 12 || vwFlt.CountUnmountedVaultItems() != 8) return 219;

                // Scene 1 ("产品") view -> strictly 2 items in slot order [seed_12, seed_11], activeSceneIndex untouched (still 0)!
                vwFlt.vaultFilter = 1;
                vwFlt.InvalidateCaches();
                vwFlt.EnsureVaultCache();
                if (vwFlt.cachedFilteredIndices.size() != 2 ||
                    fltSt.vault[vwFlt.cachedFilteredIndices[0]].id != L"seed_12" ||
                    fltSt.vault[vwFlt.cachedFilteredIndices[1]].id != L"seed_11" ||
                    fltSt.activeSceneIndex != 0 ||
                    vwFlt.TargetSceneForVaultAction() != 1) {
                    return 220;
                }

                // Unmounted view -> strictly 8 items, none mounted in Scene 0 or Scene 1
                vwFlt.vaultFilter = -2;
                vwFlt.InvalidateCaches();
                vwFlt.EnsureVaultCache();
                if (vwFlt.cachedFilteredIndices.size() != 8) return 221;
                for (size_t vi : vwFlt.cachedFilteredIndices) {
                    if (fltSt.IsMountedInAnyEnabledScene(fltSt.vault[vi].id)) return 222;
                }

                // assignTargetSlot mode forces EffectiveVaultFilter back to -1 (All)
                vwFlt.vaultFilter = 1;
                vwFlt.assignTargetSlot = 3;
                if (vwFlt.EffectiveVaultFilter() != -1 || vwFlt.TargetSceneForVaultAction() != 0) return 223;
                vwFlt.assignTargetSlot = -1;

                // Disabling Scene 1 automatically falls EffectiveVaultFilter back to -1 (All)
                fltSt.SetSceneEnabled(1, false);
                if (vwFlt.EffectiveVaultFilter() != -1) return 224;
            }
            DiagLogger::Instance().Init(sm.dataDir);
        }

        // 11d. DockWindow onboarding suppresses hover bubble, resolves dynamic slots on empty/full deck,
        //      cancels pendingAutoRetract on moving to another slot, and DarkPopupMenu records outside-close toggle
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int uxErr = 0;
        {
            DarkPopupMenu menu11;
            DockWindow dock11;
            AppState st11{};
            StorageManager::SeedDefaults(st11, true); // Full 10 slots in Scene 0!
            if (!menu11.Init(GetModuleHandleW(nullptr)) ||
                !dock11.Init(GetModuleHandleW(nullptr), &st11, &sm, &menu11, nullptr, nullptr, false)) {
                uxErr = 209;
            } else {
                // Full 10-slot replay: step1Slot=0, targetSlot=-1 (no fake empty slot highlight!), and hover bubble suppressed!
                dock11.StartOnboarding(true);
                if (dock11.onboardingWnd.step1Slot != 0 || dock11.onboardingWnd.targetSlot != -1) uxErr = 210;
                if (uxErr == 0) {
                    dock11.hoverSlot = 0;
                    dock11.SyncHoverBubble(true);
                    if (dock11.hoverBubble.IsShowingOrAppearing()) uxErr = 211;
                }
                dock11.FinishOnboarding(false);

                // Moving cursor to a different slot during pendingAutoRetract cancels auto-retract
                if (uxErr == 0) {
                    dock11.clipboardWriter = stubClipOk;
                    dock11.expandSpring.Snap(1.0f);
                    dock11.RenderAndPresent("selftest-v11-retract");
                    dock11.TriggerSlotCopy(0);
                    if (!dock11.pendingAutoRetract) uxErr = 212;
                    D2D1_RECT_F s1r = dock11.SlotRectDip(1);
                    LPARAM lpS1 = MAKELPARAM((short)std::round(((s1r.left + s1r.right) * 0.5f) * dock11.dpiScale),
                                             (short)std::round(((s1r.top + s1r.bottom) * 0.5f) * dock11.dpiScale));
                    SendMessageW(dock11.hwnd, WM_MOUSEMOVE, 0, lpS1);
                    if (dock11.pendingAutoRetract) uxErr = 213;
                }

                // External scene change via Refresh() clears transient copiedSlot / slotScrollDip cleanly
                if (uxErr == 0) {
                    dock11.copiedSlot = 0;
                    dock11.slotScrollDip = 24.0f;
                    dock11.lastRenderedSceneIdx = 0;
                    st11.SetSceneEnabled(1, true);
                    st11.activeSceneIndex = 1;
                    dock11.Refresh();
                    if (dock11.copiedSlot != -1 || dock11.slotScrollDip > 0.1f) uxErr = 218;
                }

                // DarkPopupMenu outside-click close matches exact trigger region spatially
                if (uxErr == 0) {
                    std::vector<DarkMenuItem> mi = { DarkMenuItem::Action(1, L"项") };
                    POINT trigPt{ 400, 400 };
                    POINT farPt{ 650, 650 };
                    menu11.ShowAt(trigPt, mi, nullptr, 180.0f, MenuOwner::Vault);
                    menu11.Close(true, &trigPt);
                    if (!menu11.WasJustClosedByOutsideClickAt(MenuOwner::Vault, trigPt) ||
                        menu11.WasJustClosedByOutsideClickAt(MenuOwner::Vault, farPt) ||
                        menu11.WasJustClosedByOutsideClickAt(MenuOwner::Dock, trigPt)) {
                        uxErr = 214;
                    }
                }

                if (dock11.hoverBubble.hwnd) DestroyWindow(dock11.hoverBubble.hwnd);
                if (dock11.hwnd) DestroyWindow(dock11.hwnd);
                if (menu11.hwnd) DestroyWindow(menu11.hwnd);
            }
        }
        CoUninitialize();
        if (uxErr != 0) return uxErr;
    }

    // 12. Verify v0.12.0 unified motion grammar, interruptibility, OS Reduced Motion, and idle timer stop
    {
        // 12a. Reduced Motion override & Spring1D continuous mid-flight retargeting
        Motion::SetReducedMotionOverride(1);
        if (!Motion::IsReducedMotion()) { Motion::SetReducedMotionOverride(-1); return 225; }
        {
            Spring1D sp = Spring1D::Fluid(0.0f);
            sp.target = 100.0f;
            bool stillMoving = sp.Step(0.016f);
            if (stillMoving || std::fabs(sp.value - 100.0f) > 0.01f) { Motion::SetReducedMotionOverride(-1); return 226; }
            float v = 0.0f;
            if (Motion::ExpApproach(v, 1.0f, 18.0f, 0.016f) || std::fabs(v - 1.0f) > 0.01f) { Motion::SetReducedMotionOverride(-1); return 227; }
            if (std::fabs(Motion::RejectShakeOffset(1000ULL, 1045ULL, 260ULL, 5.0f)) > 0.001f) { Motion::SetReducedMotionOverride(-1); return 228; }
        }

        Motion::SetReducedMotionOverride(0);
        if (Motion::IsReducedMotion()) { Motion::SetReducedMotionOverride(-1); return 229; }
        {
            Spring1D sp = Spring1D::Fluid(0.0f);
            sp.target = 100.0f;
            for (int k = 0; k < 4; ++k) sp.Step(0.016f);
            float midVal = sp.value;
            float midVel = sp.velocity;
            if (midVal <= 5.0f || midVal >= 98.0f || midVel <= 0.0f) { Motion::SetReducedMotionOverride(-1); return 230; }
            // Mid-flight interrupt/retarget to 0 must preserve continuous position without instant snap
            sp.target = 0.0f;
            sp.Step(0.016f);
            if (std::fabs(sp.value - midVal) > 40.0f) { Motion::SetReducedMotionOverride(-1); return 231; }
            for (int k = 0; k < 80; ++k) sp.Step(0.016f);
            if (!sp.IsSettled() || std::fabs(sp.value) > 0.5f) { Motion::SetReducedMotionOverride(-1); return 232; }
            if (std::fabs(Motion::RejectShakeOffset(1000ULL, 1045ULL, 260ULL, 5.0f)) < 0.5f) { Motion::SetReducedMotionOverride(-1); return 233; }
        }

        // 12b. HoverBubbleWindow height morphing, DarkPopupMenu emerge & clickability, DockWindow motion & idle stop
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int mErr = 0;
        {
            DarkPopupMenu menu12;
            DockWindow dock12;
            AppState st12{};
            StorageManager::SeedDefaults(st12, true);
            st12.SetSceneEnabled(1, true);
            st12.vault[0].content = L"短内容";
            st12.vault[1].content = L"第一行\n第二行\n第三行\n第四行\n第五行\n第六行\n第七行\n第八行\n第九行\n第十行";
            if (!menu12.Init(GetModuleHandleW(nullptr)) ||
                !dock12.Init(GetModuleHandleW(nullptr), &st12, &sm, &menu12, nullptr, nullptr, false)) {
                mErr = 234;
            } else {
                // Bubble snaps height on fresh open, morphs smoothly on switching to taller slot, snaps on Reduced Motion
                dock12.expandSpring.Snap(1.0f);
                dock12.RenderAndPresent("selftest-v12-init");
                dock12.hoverSlot = 0;
                dock12.SyncHoverBubble(true);
                float h0 = dock12.hoverBubble.heightSpring.value;
                if (std::fabs(h0 - dock12.hoverBubble.heightSpring.target) > 0.5f) mErr = 235;
                if (mErr == 0) {
                    dock12.hoverSlot = 1;
                    dock12.SyncHoverBubble(true);
                    if (std::fabs(dock12.hoverBubble.heightSpring.target - h0) < 20.0f ||
                        std::fabs(dock12.hoverBubble.heightSpring.value - dock12.hoverBubble.heightSpring.target) < 5.0f) {
                        mErr = 236;
                    }
                    Motion::SetReducedMotionOverride(1);
                    dock12.hoverSlot = 0;
                    dock12.SyncHoverBubble(true);
                    if (std::fabs(dock12.hoverBubble.heightSpring.value - dock12.hoverBubble.heightSpring.target) > 0.5f) {
                        mErr = 237;
                    }
                    Motion::SetReducedMotionOverride(0);
                    dock12.hoverBubble.HideImmediate();
                }

                // DarkPopupMenu mid-emerge inverse-transform boundary hit test (top edge of item 1 visually shifted)
                if (mErr == 0) {
                    int clickedId = -1;
                    std::vector<DarkMenuItem> mi = { DarkMenuItem::Action(42, L"第一项"), DarkMenuItem::Action(43, L"第二项") };
                    menu12.ShowAt(POINT{ 400, 400 }, mi, [&](int id) { clickedId = id; }, 180.0f, MenuOwner::Vault);
                    if (!menu12.visible || menu12.openSpring.value >= 0.99f || !menu12.animActive) mErr = 238;
                    if (mErr == 0 && menu12.itemRects.size() >= 2) {
                        // Compute exact screen-mapped visual top-inner edge of row 1 at current openSpring frame
                        float a = Motion::EaseOutCubic(menu12.lastRenderOpenT);
                        float sc = 0.955f + 0.045f * a;
                        float ax = menu12.openFlipLeft ? (DarkPopupMenu::kPad + menu12.menuW) : DarkPopupMenu::kPad;
                        float ay = menu12.openFlipUp ? (DarkPopupMenu::kPad + menu12.menuH) : DarkPopupMenu::kPad;
                        float dySh = (menu12.openFlipUp ? 4.0f : -4.0f) * (1.0f - a);
                        D2D1_RECT_F r1 = menu12.itemRects[1];
                        float visX = ax + ((r1.left + r1.right) * 0.5f - ax) * sc;
                        float visY = ay + ((r1.top + 1.0f) - ay) * sc + dySh;
                        LPARAM lp1 = MAKELPARAM((short)std::round(visX * menu12.dpiScale),
                                                (short)std::round(visY * menu12.dpiScale));
                        SendMessageW(menu12.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp1);
                        SendMessageW(menu12.hwnd, WM_LBUTTONUP, 0, lp1);
                        if (clickedId != 43 || menu12.visible || menu12.animActive) mErr = 239;
                    }
                }

                // DockWindow: Reduced Motion copy stops 10ms anim timer; normal motion onboarding pulse stops after 2.4s; slot drag rollback
                if (mErr == 0) {
                    Motion::SetReducedMotionOverride(1);
                    dock12.clipboardWriter = stubClipFail;
                    dock12.TriggerSlotCopy(0);
                    if (dock12.copyFailedSlot != 0 || dock12.animRunning) mErr = 240;
                    Motion::SetReducedMotionOverride(0);
                }
                if (mErr == 0) {
                    dock12.SwitchActiveScene(1);
                    if (st12.activeSceneIndex != 1 || !dock12.animRunning) mErr = 241;
                    // Verify onboarding pulse naturally stops animRunning under NORMAL motion once 2.4s expires
                    dock12.StartOnboarding(true);
                    dock12.onboardPulseStartTick = GetTickCount64() - DockWindow::kOnboardPulseDurationMs - 200;
                    dock12.scenePillXSpring.Snap(dock12.scenePillXSpring.target);
                    dock12.scenePillWSpring.Snap(dock12.scenePillWSpring.target);
                    dock12.SyncSceneColorTarget(true);
                    SendMessageW(dock12.hwnd, WM_TIMER, DockWindow::kTimerAnim, 0);
                    if (dock12.animRunning) mErr = 242;
                    dock12.FinishOnboarding(false);
                }
                if (mErr == 0) {
                    // Verify slot drag blocks wheel scene switch AND capture loss rolls back temporary swap
                    dock12.SwitchActiveScene(0);
                    auto origSlots = st12.scenes[0].slots;
                    dock12.pressedSlot = 0;
                    dock12.slotDragSceneIdx = 0;
                    dock12.slotDragOrigSlots = origSlots;
                    dock12.isSlotDragging = true;
                    std::swap(st12.scenes[0].slots[0], st12.scenes[0].slots[1]);
                    SendMessageW(dock12.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
                    if (st12.activeSceneIndex != 0) mErr = 246;
                    SendMessageW(dock12.hwnd, WM_CAPTURECHANGED, 0, 0);
                    if (dock12.isSlotDragging || st12.scenes[0].slots != origSlots) mErr = 247;
                }

                if (dock12.hoverBubble.hwnd) DestroyWindow(dock12.hoverBubble.hwnd);
                if (dock12.hwnd) DestroyWindow(dock12.hwnd);
                if (menu12.hwnd) DestroyWindow(menu12.hwnd);
            }
        }
        CoUninitialize();
        if (mErr != 0) { Motion::SetReducedMotionOverride(-1); return mErr; }

        // 12c. VaultWindow tab pill, RowShift physics, transactional drag rollback & Reduced Motion snap
        {
            AppState vSt{};
            StorageManager::SeedDefaults(vSt, false);
            VaultWindow vw12;
            vw12.state = &vSt;
            vw12.storage = &sm;
            vw12.activeTab = 0;
            vw12.SyncTabPillTarget(true);
            Motion::SetReducedMotionOverride(0);
            // Without an active HWND, StartWindowAnimTimer fails -> P0 #5 fallback must snap springs to final state
            vw12.SwitchTab(1);
            if (std::fabs(vw12.tabPillXSpring.value - 140.0f) > 0.5f ||
                std::fabs(vw12.rankEnterSpring.value - 1.0f) > 0.01f) {
                Motion::SetReducedMotionOverride(-1);
                return 243;
            }
            vw12.AddRowShift(L"seed_01", 76.0f);
            if (std::fabs(vw12.GetRowShift(L"seed_01") - 76.0f) > 0.5f) {
                Motion::SetReducedMotionOverride(-1);
                return 244;
            }
            // Transactional vault card drag: preview buffer moves while AppState stays strictly unmutated; shortcuts blocked; Esc cancels
            std::wstring firstId = vSt.vault[0].id;
            vw12.activeTab = 0;
            vw12.vaultFilter = -1;
            vw12.InvalidateCaches();
            if (!vw12.BeginVaultDragForRow(0, 140.0f, 110.0f)) {
                Motion::SetReducedMotionOverride(-1);
                return 245;
            }
            vw12.isVaultDragging = true;
            vw12.vaultDragCurDipY = vw12.vaultListTopDip + 2.0f * 76.0f;
            vw12.StepVaultDragReorder();
            if (vw12.vaultDragRow != 2 || vw12.vaultDragPreviewOrder.size() < 3 ||
                vSt.vault[vw12.vaultDragPreviewOrder[2]].id != firstId ||
                vSt.vault[0].id != firstId) {
                Motion::SetReducedMotionOverride(-1);
                return 245;
            }
            // Shortcuts like Enter must be blocked during drag; Esc must cancel cleanly
            if (!vw12.HandleShortcutKey(VK_RETURN, false) || !vw12.isVaultDragging) {
                Motion::SetReducedMotionOverride(-1);
                return 245;
            }
            if (!vw12.HandleShortcutKey(VK_ESCAPE, false) || vw12.isVaultDragging || vSt.vault[0].id != firstId) {
                Motion::SetReducedMotionOverride(-1);
                return 245;
            }
            Motion::SetReducedMotionOverride(1);
            vw12.SwitchTab(2);
            if (std::fabs(vw12.tabPillXSpring.value - 234.0f) > 0.5f) {
                Motion::SetReducedMotionOverride(-1);
                return 248;
            }
        }
        Motion::SetReducedMotionOverride(-1);
    }

    // 13. v0.13.0 regression: zero-flicker 2D morphing bubble + gap grace, dual-pane scene picker filters, and --replace handoff
    {
        // 13a. ScenePromptsDialog 3-way filter buckets & IsMountedInAnyScene (including disabled scenes!)
        AppState scSt{};
        StorageManager::SeedDefaults(scSt, false);
        int nNotCur = 0, nUnm = 0, nAll = 0;
        ScenePromptsDialog::CountFilterBuckets(scSt, 0, nNotCur, nUnm, nAll);
        if (nAll != 12 || nNotCur != 10 || nUnm != 8) return 250;

        auto fNotCur = ScenePromptsDialog::FilterVaultCandidates(scSt, 0, ScenePromptsDialog::PickerFilter::NotInCurrent, L"");
        auto fUnm    = ScenePromptsDialog::FilterVaultCandidates(scSt, 0, ScenePromptsDialog::PickerFilter::UnmountedAll, L"");
        auto fAll    = ScenePromptsDialog::FilterVaultCandidates(scSt, 0, ScenePromptsDialog::PickerFilter::All, L"");
        if (fNotCur.size() != 10 || fUnm.size() != 8 || fAll.size() != 12) return 251;
        for (size_t vi : fNotCur) {
            if (scSt.FindSlotOfIdInScene(0, scSt.vault[vi].id) >= 0) return 252;
        }
        for (size_t vi : fUnm) {
            if (scSt.IsMountedInAnyScene(scSt.vault[vi].id)) return 253;
        }

        // Mounting into disabled scene 2 ('开发') at explicit slot 4 ('05') must immediately reduce UnmountedAll count from 8 -> 7
        std::wstring pickedId = scSt.vault[fUnm[0]].id;
        scSt.AssignToSlotInScene(2, 4, pickedId);
        if (scSt.FindSlotOfIdInScene(2, pickedId) != 4 || !scSt.IsMountedInAnyScene(pickedId)) return 254;
        ScenePromptsDialog::CountFilterBuckets(scSt, 0, nNotCur, nUnm, nAll);
        if (nNotCur != 10 || nUnm != 7 || nAll != 12) return 255;

        // Case-insensitive search across filters
        auto fSearch = ScenePromptsDialog::FilterVaultCandidates(scSt, 0, ScenePromptsDialog::PickerFilter::All, L"ux");
        if (fSearch.empty()) return 256;

        // 13b. --replace single-instance takeover logic (allows same-version silent hot reload while preserving default behavior)
        uint32_t v13 = PackAppVersion(0, 13, 0);
        uint32_t v12 = PackAppVersion(0, 12, 0);
        if (ShouldTakeOverExistingInstance(v13, v13, false)) return 257;
        if (!ShouldTakeOverExistingInstance(v13, v13, true)) return 258;
        if (!ShouldTakeOverExistingInstance(v12, v13, false)) return 259;
        if (ShouldTakeOverExistingInstance(v13, v12, false)) return 260;
        if (!ShouldTakeOverExistingInstance(v13, v12, true)) return 261;

        // 13c. HoverBubbleWindow 2D width+height morphing, DIB capacity stability, 130 ms gap grace, and mid-fade interruption
        Motion::SetReducedMotionOverride(0);
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int bErr = 0;
        {
            AppState bSt{};
            StorageManager::SeedDefaults(bSt, false);
            bSt.showHoverBubble = true;
            bSt.vault[0].title = L"短词";
            bSt.vault[0].content = L"短内容";
            bSt.vault[1].title = L"长提示词标题较宽展示测试";
            bSt.vault[1].content.clear();
            for (int k = 0; k < 18; ++k) bSt.vault[1].content += L"这是一段用于验证二维平滑形变与宽度自适应扩展的较长正文内容。\n";
            bSt.AssignToSlotInScene(0, 0, bSt.vault[0].id);
            bSt.AssignToSlotInScene(0, 1, bSt.vault[1].id);

            DockWindow dock13;
            dock13.monitorProvider = makeFixedMon(RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1040 });
            if (!dock13.Init(GetModuleHandleW(nullptr), &bSt, &sm, nullptr, nullptr, nullptr, false)) {
                bErr = 262;
            } else {
                dock13.expandSpring.Snap(1.0f);
                dock13.RenderAndPresent("selftest-v13-init");

                // Open on short slot 0
                dock13.hoverSlot = 0;
                dock13.SyncHoverBubble(true);
                HBITMAP dibBefore = dock13.hoverBubble.canvas.dib;
                float w0 = dock13.hoverBubble.widthSpring.value;
                float h0 = dock13.hoverBubble.heightSpring.value;
                if (!dock13.hoverBubble.IsShowingOrAppearing() || !dibBefore || w0 >= HoverBubbleWindow::kMaxCardW - 10.0f) bErr = 263;

                // Slide into 5 DIP inter-slot gap (hoverSlot = -1): MUST stay visible & arm grace timer (zero flicker!)
                if (bErr == 0) {
                    dock13.hoverSlot = -1;
                    dock13.SyncHoverBubble(false);
                    if (!dock13.bubbleHideGraceActive || !dock13.hoverBubble.IsShowingOrAppearing() ||
                        dock13.hoverBubble.alphaSpring.target < 0.5f) {
                        bErr = 264;
                    }
                }

                // Enter tall wide slot 1 within grace window: cancels grace, morphs width & height smoothly, starts cross-fade, zero DIB re-alloc!
                if (bErr == 0) {
                    dock13.hoverSlot = 1;
                    dock13.SyncHoverBubble(false);
                    if (dock13.bubbleHideGraceActive || dock13.hoverBubble.currentSlot != 1 ||
                        dock13.hoverBubble.prevSlot != 0 || dock13.hoverBubble.contentBlend > 0.05f ||
                        dock13.hoverBubble.widthSpring.target <= w0 + 15.0f ||
                        dock13.hoverBubble.heightSpring.target <= h0 + 40.0f ||
                        dock13.hoverBubble.canvas.dib != dibBefore) {
                        bErr = 265;
                    }
                }

                // Grace timeout starts smooth fade-out; re-entering slot 0 mid-fade MUST catch & morph in place!
                if (bErr == 0) {
                    dock13.hoverBubble.alphaSpring.Snap(1.0f);
                    dock13.hoverSlot = -1;
                    dock13.SyncHoverBubble(false);
                    SendMessageW(dock13.hwnd, WM_TIMER, DockWindow::kTimerBubbleHideGrace, 0);
                    if (dock13.bubbleHideGraceActive || dock13.hoverBubble.alphaSpring.target != 0.0f ||
                        !dock13.hoverBubble.IsAliveForMorph()) {
                        bErr = 266;
                    }
                    if (bErr == 0) {
                        dock13.hoverBubble.alphaSpring.value = 0.65f; // Mid-fade-out
                        dock13.hoverSlot = 0;
                        dock13.SyncHoverBubble(false);
                        if (dock13.hoverBubble.alphaSpring.target < 0.5f || dock13.hoverBubble.currentSlot != 0) {
                            bErr = 267;
                        }
                    }
                }

                // Reduced Motion snaps width/height/notch and clears cross-fade snapshot immediately
                if (bErr == 0) {
                    Motion::SetReducedMotionOverride(1);
                    dock13.hoverSlot = 1;
                    dock13.SyncHoverBubble(true);
                    if (std::fabs(dock13.hoverBubble.widthSpring.value - dock13.hoverBubble.widthSpring.target) > 0.5f ||
                        dock13.hoverBubble.prevBodyLayout != nullptr || dock13.hoverBubble.contentBlend < 0.99f) {
                        bErr = 268;
                    }
                    dock13.hoverBubble.HideImmediate();
                }

                if (dock13.hoverBubble.hwnd) DestroyWindow(dock13.hoverBubble.hwnd);
                if (dock13.hwnd) DestroyWindow(dock13.hwnd);
            }
        }
        CoUninitialize();
        Motion::SetReducedMotionOverride(-1);
        if (bErr != 0) return bErr;
    }

    // 14. v0.14.0 regression: VaultWindow scene/unmounted drag-to-reorder & ScenePromptsDialog cross-pane drag-and-drop + status toggle
    {
        Motion::SetReducedMotionOverride(0);
        StorageManager sm14;
        sm14.Init(testDir + L"\\v14_case");
        AppState st14{};
        StorageManager::SeedDefaults(st14, false);
        // Set up Scene 0 ("通用") with sparse slots: 00=seed_01, 02=seed_02, 05=seed_03 (gaps at 01, 03, 04 must be preserved!)
        st14.ClearSceneSlots(0);
        st14.scenes[0].slots[0] = L"seed_01";
        st14.scenes[0].slots[2] = L"seed_02";
        st14.scenes[0].slots[5] = L"seed_03";
        sm14.Save(st14);

        // 14a. VaultWindow scene/unmounted drag-to-reorder via real hidden HWND (private preview buffer, EN_CHANGE cancel, disk commit, read-only failure)
        {
            VaultWindow vw14;
            int stateChangeCalls = 0;
            vw14.Init(GetModuleHandleW(nullptr), &st14, &sm14, nullptr, [&]() { ++stateChangeCalls; });
            vw14.hwnd = CreateWindowExW(0, L"PromptPod_VaultWnd", L"V14Test", WS_POPUP, 0, 0, 760, 580,
                                        nullptr, nullptr, GetModuleHandleW(nullptr), &vw14);
            if (!vw14.hwnd) return 270;
            vw14.CreateSearchControl();
            vw14.dpiScale = 1.0f;
            vw14.activeTab = 0;
            vw14.vaultFilter = 0; // Scene 0 ("通用")
            vw14.vaultListTopDip = 110.0f;
            vw14.InvalidateCaches();
            vw14.EnsureVaultCache();
            if (vw14.cachedFilteredIndices.size() != 3) { DestroyWindow(vw14.hwnd); return 270; }

            // (1) Drag row 2 (seed_03 in slot 05) up to row 0 -> preview updates while AppState stays untouched; EN_CHANGE cancels cleanly
            if (!vw14.BeginVaultDragForRow(2, 290.0f, 262.0f)) { DestroyWindow(vw14.hwnd); return 271; }
            SendMessageW(vw14.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 128));
            if (!vw14.isVaultDragging || vw14.vaultDragRow != 0 ||
                vw14.vaultDragPreviewSlots[0] != L"seed_03" || !vw14.vaultDragPreviewSlots[1].empty() ||
                vw14.vaultDragPreviewSlots[2] != L"seed_01" || vw14.vaultDragPreviewSlots[5] != L"seed_02" ||
                st14.scenes[0].slots[0] != L"seed_01") { // AppState MUST remain untouched mid-drag!
                DestroyWindow(vw14.hwnd);
                return 272;
            }
            SetWindowTextW(vw14.hSearchEdit, L"x"); // Fires EN_CHANGE -> MUST cancel drag without mutating state!
            if (vw14.isVaultDragging || st14.scenes[0].slots[0] != L"seed_01" || stateChangeCalls != 0) {
                DestroyWindow(vw14.hwnd);
                return 273;
            }
            SetWindowTextW(vw14.hSearchEdit, L"");

            // (2) Full DOWN->MOVE->UP commit in Scene 0 sparse slots + verify disk persistence + onStateChanged
            if (!vw14.BeginVaultDragForRow(2, 290.0f, 262.0f)) { DestroyWindow(vw14.hwnd); return 271; }
            SendMessageW(vw14.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 128));
            SendMessageW(vw14.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(120, 128));
            if (st14.scenes[0].slots[0] != L"seed_03" || !st14.scenes[0].slots[1].empty() ||
                st14.scenes[0].slots[2] != L"seed_01" || !st14.scenes[0].slots[3].empty() ||
                !st14.scenes[0].slots[4].empty() || st14.scenes[0].slots[5] != L"seed_02" ||
                stateChangeCalls != 1) {
                DestroyWindow(vw14.hwnd);
                return 272;
            }
            {
                AppState diskCheck{};
                if (!sm14.LoadOrSeed(diskCheck) || diskCheck.scenes[0].slots[0] != L"seed_03" || diskCheck.scenes[0].slots[5] != L"seed_02") {
                    DestroyWindow(vw14.hwnd);
                    return 272;
                }
            }

            // (3) Non-active enabled Scene 1 ("产品") drag commit must NOT alter activeSceneIndex (0)
            st14.activeSceneIndex = 0;
            vw14.vaultFilter = 1;
            vw14.InvalidateCaches();
            vw14.EnsureVaultCache();
            if (vw14.cachedFilteredIndices.size() != 2 || !vw14.BeginVaultDragForRow(0, 130.0f, 110.0f)) {
                DestroyWindow(vw14.hwnd);
                return 274;
            }
            SendMessageW(vw14.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 210));
            SendMessageW(vw14.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(120, 210));
            if (st14.scenes[1].slots[0] != L"seed_12" || st14.scenes[1].slots[1] != L"seed_11" ||
                st14.activeSceneIndex != 0 || stateChangeCalls != 2) {
                DestroyWindow(vw14.hwnd);
                return 274;
            }

            // (4) Unmounted-filtered drag commits ONLY unmounted vault items without moving mounted items or calling onStateChanged
            vw14.vaultFilter = -2;
            vw14.InvalidateCaches();
            vw14.EnsureVaultCache();
            if (vw14.cachedFilteredIndices.size() < 3) { DestroyWindow(vw14.hwnd); return 275; }
            size_t u0Idx = vw14.cachedFilteredIndices[0];
            size_t u1Idx = vw14.cachedFilteredIndices[1];
            std::wstring u0Id = st14.vault[u0Idx].id;
            std::wstring u1Id = st14.vault[u1Idx].id;
            std::wstring mountedZeroId = st14.vault[0].id;
            if (!vw14.BeginVaultDragForRow(0, 130.0f, 110.0f)) { DestroyWindow(vw14.hwnd); return 275; }
            SendMessageW(vw14.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 210));
            if (st14.vault[u0Idx].id != u0Id) { DestroyWindow(vw14.hwnd); return 276; } // Unmutated mid-drag!
            SendMessageW(vw14.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(120, 210));
            if (st14.vault[u0Idx].id != u1Id || st14.vault[u1Idx].id != u0Id ||
                st14.vault[0].id != mountedZeroId || stateChangeCalls != 2) {
                DestroyWindow(vw14.hwnd);
                return 276;
            }
            {
                AppState diskCheck{};
                if (!sm14.LoadOrSeed(diskCheck) || diskCheck.vault[u0Idx].id != u1Id || diskCheck.vault[u1Idx].id != u0Id) {
                    DestroyWindow(vw14.hwnd);
                    return 276;
                }
            }

            // (5) Read-only save failure leaves AppState untouched and displays error toast
            sm14.readOnlyRecoveryMode = true;
            vw14.vaultFilter = 0;
            vw14.InvalidateCaches();
            if (!vw14.BeginVaultDragForRow(0, 130.0f, 110.0f)) { sm14.readOnlyRecoveryMode = false; DestroyWindow(vw14.hwnd); return 277; }
            SendMessageW(vw14.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 210));
            SendMessageW(vw14.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(120, 210));
            bool roOk = (st14.scenes[0].slots[0] == L"seed_03" && st14.scenes[0].slots[2] == L"seed_01" &&
                         stateChangeCalls == 2 && vw14.toastIsError);
            sm14.readOnlyRecoveryMode = false;
            DestroyWindow(vw14.hwnd);
            if (!roOk) return 277;

            // Restore canonical sparse Scene 0 layout for 14b
            st14.ClearSceneSlots(0);
            st14.scenes[0].slots[0] = L"seed_01";
            st14.scenes[0].slots[2] = L"seed_02";
            st14.scenes[0].slots[5] = L"seed_03";
            sm14.Save(st14);
        }

        // 14b. ScenePromptsDialog full cross-pane drag-and-drop, boundary guards, Reduced Motion edge-scroll, undo & natural timer settle
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int dlgErr = 0;
        {
            int mutatedCnt = 0;
            ScenePromptsDialog dlg(GetModuleHandleW(nullptr), &st14, &sm14, nullptr, 0,
                                   [](void* p) { ++(*static_cast<int*>(p)); }, &mutatedCnt);
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = ScenePromptsDialog::DlgWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"PromptPod_ScenePromptsDlg_Test";
            RegisterClassExW(&wc);
            dlg.dpiScale = 1.0f;
            dlg.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Test", WS_POPUP, 0, 0, 800, 560, nullptr, nullptr, wc.hInstance, &dlg);
            if (!dlg.hwnd) {
                dlgErr = 278;
            } else {
                dlg.EnsureResources();
                LayeredCanvas off;
                auto paintOff = [&](int w = 800, int h = 560) {
                    if (off.Begin(w, h, 1.0f)) {
                        dlg.PaintContent(off.rt.Get(), off.solidBrush.Get(), (float)w, (float)h);
                        off.rt->EndDraw();
                    }
                };
                paintOff();
                if (dlg.filteredIndices.size() != 9 || dlg.drawnRightCards.size() < 2) dlgErr = 278;

                // (1) Action buttons inside right card (kind 70 '+ 加入' / kind 71 '编辑') MUST NOT start right-card drag (kind 72)!
                if (dlgErr == 0) {
                    const auto& c0 = dlg.drawnRightCards[0].cardRect;
                    LPARAM lpActBtn = MAKELPARAM((short)(c0.right - 20.0f), (short)((c0.top + c0.bottom) * 0.5f));
                    SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpActBtn);
                    if (dlg.rightDragPending || dlg.pressedKind != 70) dlgErr = 278;
                    SendMessageW(dlg.hwnd, WM_CANCELMODE, 0, 0);
                }

                // (2) Real mouse Right -> Left drag onto empty Left Slot 01 + upward refill shift (+64 DIP)
                std::wstring dropPid, belowPid;
                if (dlgErr == 0) {
                    dropPid = dlg.drawnRightCards[0].pid;
                    belowPid = dlg.drawnRightCards[1].pid;
                    const auto& c0 = dlg.drawnRightCards[0].hitRect;
                    LPARAM lpDown = MAKELPARAM((short)(c0.left + 80.0f), (short)((c0.top + c0.bottom) * 0.5f));
                    D2D1_RECT_F sl1 = dlg.LeftSlotRectDip(1);
                    LPARAM lpDrop = MAKELPARAM((short)((sl1.left + sl1.right) * 0.5f), (short)((sl1.top + sl1.bottom) * 0.5f));

                    SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpDown);
                    SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpDrop);
                    if (!dlg.isRightDragging || dlg.rightDragMorphSpring.target < 0.99f ||
                        dlg.rightDragHoverSlot != 1 || dlg.filteredIndices.size() != 9) {
                        dlgErr = 279;
                    }
                    if (dlgErr == 0) {
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpDrop);
                        if (st14.scenes[0].slots[1] != dropPid || mutatedCnt != 1 ||
                            dlg.isRightDragging || dlg.filteredIndices.size() != 8) {
                            dlgErr = 280;
                        }
                        if (dlgErr == 0 && (std::fabs(dlg.GetRightRowShift(belowPid) - ScenePromptsDialog::kRightStride) > 0.5f || !dlg.animActive)) {
                            dlgErr = 281;
                        }
                    }
                }

                // (3) Right -> Left drop onto OCCUPIED Left Slot 02 (replaces cleanly, displaced seed_02 fades in smoothly & stays in vault!)
                if (dlgErr == 0) {
                    std::wstring dropPid2 = st14.vault[dlg.filteredIndices[0]].id;
                    if (!dlg.HandleRightCardDropOnSlot(2, dropPid2) ||
                        st14.scenes[0].slots[2] != dropPid2 || !st14.FindById(L"seed_02") ||
                        dlg.rightFadeInPid != L"seed_02") {
                        dlgErr = 282;
                    }
                }

                // (4) WM_LBUTTONUP coordinate re-evaluation: hover over left slot on MOVE, then release in right pane -> MUST return, NOT mount!
                if (dlgErr == 0) {
                    dlg.rightRowShifts.clear();
                    dlg.rightFadeInPid.clear();
                    paintOff();
                    const auto& c0 = dlg.drawnRightCards[0].hitRect;
                    LPARAM lpDown = MAKELPARAM((short)(c0.left + 80.0f), (short)((c0.top + c0.bottom) * 0.5f));
                    D2D1_RECT_F sl4 = dlg.LeftSlotRectDip(4);
                    LPARAM lpOverLeft = MAKELPARAM((short)((sl4.left + sl4.right) * 0.5f), (short)((sl4.top + sl4.bottom) * 0.5f));
                    LPARAM lpUpRight = MAKELPARAM(520, 260);
                    SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpDown);
                    SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpOverLeft);
                    if (dlg.rightDragHoverSlot != 4) dlgErr = 283;
                    if (dlgErr == 0) {
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpUpRight);
                        if (!st14.scenes[0].slots[4].empty() || !dlg.rightDragReturning || dlg.rightDragMorphSpring.target > 0.01f) {
                            dlgErr = 283;
                        }
                    }
                    dlg.CancelRightDrag();
                }

                // (5) Left-slot drag: AppState unmutated mid-drag, search-bar (400,80) & out-of-window (900,260) MUST NOT remove,
                //     no-op drag skips disk write, and valid right-pane drop removes + arms 5s single-slot undo (preserved on save fail!)
                if (dlgErr == 0) {
                    paintOff();
                    std::wstring origSl0 = st14.scenes[0].slots[0];
                    std::wstring origSl1 = st14.scenes[0].slots[1];
                    D2D1_RECT_F sl0 = dlg.LeftSlotRectDip(0);
                    D2D1_RECT_F sl1 = dlg.LeftSlotRectDip(1);
                    LPARAM lpSl0 = MAKELPARAM((short)((sl0.left + sl0.right) * 0.5f), (short)((sl0.top + sl0.bottom) * 0.5f));
                    LPARAM lpSl1 = MAKELPARAM((short)((sl1.left + sl1.right) * 0.5f), (short)((sl1.top + sl1.bottom) * 0.5f));
                    LPARAM lpSearchBar = MAKELPARAM(400, 80);
                    LPARAM lpOutWin = MAKELPARAM(900, 260);
                    LPARAM lpRight = MAKELPARAM(540, 260);

                    // 5a. Mid-drag swap stays in preview buffer only; dropping over search bar (400,80) neither removes nor reorders
                    SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                    SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSl1);
                    if (!dlg.isSlotDragging || dlg.slotDragPreviewSlots[1] != origSl0 || st14.scenes[0].slots[0] != origSl0) dlgErr = 284;
                    if (dlgErr == 0) {
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSearchBar);
                        if (dlg.slotDragOverRight) dlgErr = 284;
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpSearchBar);
                        if (st14.scenes[0].slots[0] != origSl0 || st14.scenes[0].slots[1] != origSl1) dlgErr = 284;
                    }

                    // 5b. Drag into right remove zone then release out-of-window (900,260) -> MUST NOT remove!
                    if (dlgErr == 0) {
                        SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpRight);
                        if (!dlg.slotDragOverRight) dlgErr = 284;
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpOutWin);
                        if (st14.scenes[0].slots[0] != origSl0) dlgErr = 284;
                    }

                    // 5c. Drag slot 0 down then back to slot 0 (no order change) -> MUST skip disk write / NotifyMutated
                    if (dlgErr == 0) {
                        int mutBefore = mutatedCnt;
                        SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSl1);
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSl0);
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpSl0);
                        if (mutatedCnt != mutBefore || st14.scenes[0].slots[0] != origSl0) dlgErr = 284;
                    }

                    // 5d. Valid Left -> Right drag-to-remove + 5s single-slot undo (save fail retains undo, normal save restores slot!)
                    if (dlgErr == 0) {
                        SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpSl0);
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpSl1);
                        SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpRight);
                        SendMessageW(dlg.hwnd, WM_LBUTTONUP, 0, lpRight);
                        if (!st14.scenes[0].slots[0].empty() || st14.scenes[0].slots[1] != origSl1 ||
                            !st14.FindById(origSl0) || !dlg.undoClearActive || !dlg.undoIsSingleSlot) {
                            dlgErr = 285;
                        }
                        if (dlgErr == 0) {
                            sm14.readOnlyRecoveryMode = true;
                            dlg.HandleUndoClear();
                            if (!st14.scenes[0].slots[0].empty() || !dlg.undoClearActive) dlgErr = 285;
                            sm14.readOnlyRecoveryMode = false;
                            if (dlgErr == 0) {
                                dlg.HandleUndoClear();
                                if (st14.scenes[0].slots[0] != origSl0 || dlg.undoClearActive) dlgErr = 285;
                            }
                        }
                    }
                }

                // (6) Reduced Motion: drop produces ZERO row shift animation, AND functional left edge-scroll works while animActive == false!
                if (dlgErr == 0) {
                    Motion::SetReducedMotionOverride(1);
                    dlg.StopAnimTimer();
                    dlg.rightRowShifts.clear();
                    dlg.EnsureFiltered();
                    std::wstring dropPid3 = st14.vault[dlg.filteredIndices[0]].id;
                    if (!dlg.HandleRightCardDropOnSlot(3, dropPid3) || !dlg.rightRowShifts.empty()) dlgErr = 286;
                    if (dlgErr == 0) {
                        paintOff(800, 360); // Short viewport so leftMaxScrollY > 20 DIP
                        if (dlg.leftMaxScrollY <= 10.0f || dlg.drawnRightCards.empty()) dlgErr = 286;
                        else {
                            const auto& c0 = dlg.drawnRightCards[0].hitRect;
                            LPARAM lpDown = MAKELPARAM((short)(c0.left + 80.0f), (short)((c0.top + c0.bottom) * 0.5f));
                            LPARAM lpEdgeBot = MAKELPARAM((short)(dlg.leftSlotsVp.left + 80.0f), (short)(dlg.leftSlotsVp.bottom - 10.0f));
                            SendMessageW(dlg.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpDown);
                            SendMessageW(dlg.hwnd, WM_MOUSEMOVE, MK_LBUTTON, lpEdgeBot);
                            if (!dlg.leftEdgeScrollActive || dlg.animActive) dlgErr = 286;
                            if (dlgErr == 0) {
                                SendMessageW(dlg.hwnd, WM_TIMER, ScenePromptsDialog::kTimerEdgeScroll, 0);
                                if (dlg.leftScrollY <= 0.5f) dlgErr = 286;
                            }
                            dlg.CancelRightDrag();
                            if (dlg.leftEdgeScrollActive) dlgErr = 286;
                        }
                        paintOff(800, 560);
                    }
                    Motion::SetReducedMotionOverride(0);
                }

                // (7) ToggleSceneEnabled ("已启用" / "未启用") + at-least-1 enabled scene guard
                if (dlgErr == 0) {
                    dlg.sceneIndex = 2;
                    if (st14.scenes[2].enabled) dlgErr = 287;
                    dlg.HandleToggleSceneEnabled();
                    if (!st14.scenes[2].enabled) dlgErr = 288;
                    dlg.HandleToggleSceneEnabled();
                    if (st14.scenes[2].enabled) dlgErr = 289;
                    // Guard: when only Scene 0 is enabled, disabling Scene 0 MUST fail with statusIsErr
                    st14.SetSceneEnabled(1, false);
                    dlg.sceneIndex = 0;
                    dlg.HandleToggleSceneEnabled();
                    if (!st14.scenes[0].enabled || !dlg.statusIsErr) dlgErr = 290;
                    st14.SetSceneEnabled(1, true);
                }

                // (8) Natural spring settle + expired micro-shake stops kTimerAnim (0% idle CPU guarantee)
                if (dlgErr == 0) {
                    if (dlg.lockedShakeTick != 0) dlg.lockedShakeTick = GetTickCount64() - 350;
                    if (dlg.slotLandFlashSlot >= 0) dlg.slotLandFlashTick = GetTickCount64() - 450;
                    dlg.rightDragMorphSpring.Snap(0.0f);
                    dlg.rightDragMorphSpring.target = 1.0f;
                    dlg.EnsureAnimTimer();
                    for (int step = 0; step < 120 && dlg.animActive; ++step) {
                        dlg.lastAnimQpc.QuadPart -= (dlg.qpcFreq.QuadPart / 60);
                        SendMessageW(dlg.hwnd, WM_TIMER, ScenePromptsDialog::kTimerAnim, 0);
                    }
                    if (dlg.animActive || dlg.lockedShakeTick != 0) dlgErr = 291;
                }

                DestroyWindow(dlg.hwnd);
            }
        }
        CoUninitialize();
        DiagLogger::Instance().Init(sm.dataDir);
        Motion::SetReducedMotionOverride(-1);
        if (dlgErr != 0) return dlgErr;
    }

    // 15. v0.14.1 Regression: right-click -> Settings first-try reliability
    //     15a. DarkPopupMenu click pressed near item edge mid-emerge still fires after emerge settles (while far drag cancels)
    //     15b. DarkPopupMenu Tray owner adopts transient Win11 tray/overflow foreground handoff within 450 ms, then closes on later switch
    //     15c. DockWindow right-click on collapsed capsule cancels kTimerHoverExpand so panel never covers open context menu
    //     15d. VaultWindow::Open re-clamps off-screen hidden window back onto visible monitor work area cleanly
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int r15Err = 0;
        Motion::SetReducedMotionOverride(0);
        if (D2DEngine::Instance().Init()) {
            DarkPopupMenu menu15;
            if (!menu15.Init(GetModuleHandleW(nullptr))) {
                r15Err = 295;
            } else {
                // 15a: Press near bottom edge of Settings row while openSpring is at 0.25, then finish emerge to 1.0 before WM_LBUTTONUP
                int firedCmd = 0;
                std::vector<DarkMenuItem> mi = {
                    DarkMenuItem::Action(1, L"Item A"),
                    DarkMenuItem::Action(6, L"打开设置...")
                };
                menu15.ShowAt(POINT{ 400, 400 }, mi, [&](int id) { firedCmd = id; }, 220.0f, MenuOwner::Dock);
                menu15.openSpring.Snap(0.25f);
                menu15.Render();
                float a = Motion::EaseOutCubic(menu15.lastRenderOpenT);
                float sc = 0.955f + 0.045f * a;
                float ax = menu15.openFlipLeft ? (DarkPopupMenu::kPad + menu15.menuW) : DarkPopupMenu::kPad;
                float ay = menu15.openFlipUp ? (DarkPopupMenu::kPad + menu15.menuH) : DarkPopupMenu::kPad;
                float dyShift = (menu15.openFlipUp ? 4.0f : -4.0f) * (1.0f - a);
                const auto& r1 = menu15.itemRects[1];
                float visX = ax + ((r1.left + r1.right) * 0.5f - ax) * sc;
                float visY = ay + ((r1.top + 0.6f) - ay) * sc + dyShift; // Very top edge of item 1 during emerge
                LPARAM lpEdge = MAKELPARAM((short)std::round(visX * menu15.dpiScale),
                                           (short)std::round(visY * menu15.dpiScale));
                SendMessageW(menu15.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpEdge);
                if (menu15.pressedIndex != 1 || !menu15.pressDuringEmerge) r15Err = 296;
                if (r15Err == 0) {
                    // Emerge completes before button up; top-edge pixel shifts slightly outside static rect, yet steady click MUST fire!
                    menu15.openSpring.Snap(1.0f);
                    menu15.Render();
                    SendMessageW(menu15.hwnd, WM_LBUTTONUP, 0, lpEdge);
                    if (firedCmd != 6 || menu15.IsOpen()) r15Err = 297;
                }
                // Dragging far away before release MUST still cancel cleanly
                if (r15Err == 0) {
                    firedCmd = 0;
                    menu15.ShowAt(POINT{ 400, 400 }, mi, [&](int id) { firedCmd = id; }, 220.0f, MenuOwner::Dock);
                    menu15.openSpring.Snap(0.25f);
                    menu15.Render();
                    SendMessageW(menu15.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lpEdge);
                    SendMessageW(menu15.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(4, 4));
                    if (firedCmd != 0 || !menu15.IsOpen()) r15Err = 298;
                    menu15.Close();
                }

                // 15b: Tray menu adopts first foreground transition within 450 ms, then closes on subsequent foreground switch
                if (r15Err == 0) {
                    HWND simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x3001));
                    menu15.foregroundProvider = [&]() -> HWND { return simFg; };
                    menu15.cursorPosProvider = [](POINT* outPt) -> BOOL {
                        if (outPt) *outPt = POINT{ 450, 430 };
                        return TRUE;
                    };
                    menu15.ShowAt(POINT{ 400, 400 }, mi, nullptr, 220.0f, MenuOwner::Tray);
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x3002)); // Overflow flyout auto-dismissed -> prev app
                    SendMessageW(menu15.hwnd, WM_TIMER, DarkPopupMenu::kTimerPoll, 0);
                    if (!menu15.IsOpen() || menu15.openForegroundHwnd != simFg) r15Err = 299;
                    if (r15Err == 0) {
                        simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x3003)); // Real Alt+Tab switch -> MUST close
                        SendMessageW(menu15.hwnd, WM_TIMER, DarkPopupMenu::kTimerPoll, 0);
                        if (menu15.IsOpen()) r15Err = 300;
                    }
                    menu15.foregroundProvider = nullptr;
                    menu15.cursorPosProvider = nullptr;
                }

                // 15c: DockWindow right-click on collapsed capsule cancels kTimerHoverExpand so Dock never covers open menu
                if (r15Err == 0) {
                    StorageManager sm15;
                    sm15.Init(testDir + L"\\r15_case");
                    AppState st15{};
                    sm15.LoadOrSeed(st15);
                    st15.onboardingCompleted = true;
                    st15.dockEdge = DockEdge::Right;
                    DockWindow dock15;
                    dock15.monitorProvider = makeFixedMon(RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1040 });
                    if (!dock15.Init(GetModuleHandleW(nullptr), &st15, &sm15, &menu15, nullptr, [&](POINT pt) {
                        menu15.ShowAt(pt, mi, nullptr, 220.0f, MenuOwner::Dock);
                    }, false)) {
                        r15Err = 301;
                    } else {
                        dock15.expandSpring.Snap(0.0f);
                        dock15.handleHovered = true;
                        SendMessageW(dock15.hwnd, WM_RBUTTONUP, 0, MAKELPARAM(4, 24));
                        if (!menu15.IsOpenFor(MenuOwner::Dock) || dock15.handleHovered) r15Err = 302;
                        if (r15Err == 0) {
                            dock15.handleHovered = true;
                            SendMessageW(dock15.hwnd, WM_TIMER, DockWindow::kTimerHoverExpand, 0);
                            if (dock15.expandSpring.target > 0.5f) r15Err = 303;
                        }
                        menu15.Close();
                        DestroyWindow(dock15.hwnd);
                    }
                }

                // 15d: VaultWindow::Open(2, -1) recovers an off-screen hidden window back into visible work area
                if (r15Err == 0) {
                    StorageManager sm15v;
                    sm15v.Init(testDir + L"\\r15v_case");
                    AppState st15v{};
                    sm15v.LoadOrSeed(st15v);
                    VaultWindow vw15;
                    vw15.Init(GetModuleHandleW(nullptr), &st15v, &sm15v, &menu15, nullptr);
                    vw15.Open(2, -1);
                    if (!vw15.hwnd || !IsWindowVisible(vw15.hwnd) || vw15.activeTab != 2) {
                        r15Err = 304;
                    } else {
                        // Simulate user closing VaultWindow after monitor unplug moved it far off-screen
                        SendMessageW(vw15.hwnd, WM_CLOSE, 0, 0);
                        SetWindowPos(vw15.hwnd, nullptr, -28000, -28000, 760, 580, SWP_NOZORDER | SWP_NOACTIVATE);
                        vw15.Open(2, -1);
                        RECT wr{};
                        GetWindowRect(vw15.hwnd, &wr);
                        if (!IsWindowVisible(vw15.hwnd) || MonitorFromRect(&wr, MONITOR_DEFAULTTONULL) == nullptr) {
                            r15Err = 305;
                        }
                        DestroyWindow(vw15.hwnd);
                    }
                }
                DestroyWindow(menu15.hwnd);
            }
        }
        CoUninitialize();
        DiagLogger::Instance().Init(sm.dataDir);
        Motion::SetReducedMotionOverride(-1);
        if (r15Err != 0) return r15Err;
    }

    // 16. v0.15.0 Regression: optional click-to-auto-paste (default off, persistence, safe guards, settle-window fg check & cancellation)
    {
        // 16a. Default false, JSON round-trip, legacy JSON fallback to false
        StorageManager sm16;
        sm16.Init(testDir + L"\\r16_case");
        AppState st16{};
        if (!sm16.LoadOrSeed(st16) || st16.autoPasteOnClick != false) return 310;
        st16.onboardingCompleted = true;
        st16.autoPasteOnClick = true;
        if (!sm16.Save(st16)) return 311;
        AppState st16Reload{};
        if (!sm16.LoadOrSeed(st16Reload) || st16Reload.autoPasteOnClick != true) return 312;
        AppState st16Legacy{};
        std::string v4NoKey = "{\"schemaVersion\":4,\"dockEdge\":2,\"dockRatioMilli\":3200,\"showHoverBubble\":true,\"scenes\":[{\"name\":\"通用\",\"enabled\":true,\"slots\":[\"p1\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]}],\"vault\":[{\"id\":\"p1\",\"title\":\"T\",\"content\":\"Body\",\"useCount\":0,\"createdAt\":1,\"lastUsedAt\":0}]}";
        if (!ParseStateFromJson(v4NoKey, st16Legacy) || st16Legacy.autoPasteOnClick != false) return 313;

        // Verify diagnostics string reflects autoPaste state without leaking prompt contents
        std::wstring diag16 = BuildSanitizedDiagnostics(st16Reload, sm16, false);
        if (diag16.find(L"autoPaste=on") == std::wstring::npos) return 314;

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int r16Err = 0;
        if (!D2DEngine::Instance().Init()) {
            r16Err = 315;
        } else {
            DockWindow dock16;
            dock16.monitorProvider = makeFixedMon(RECT{ 0, 0, 1920, 1080 }, RECT{ 0, 0, 1920, 1040 });
            std::wstring clip16;
            dock16.clipboardWriter = [&](const std::wstring& txt) -> bool {
                clip16 = txt;
                return true;
            };
            HWND simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5001));
            AutoPaste::TargetCheck simChk = AutoPaste::TargetCheck::Ready;
            int pasteCount = 0;
            bool pasteShouldOk = true;
            dock16.foregroundProvider = [&]() -> HWND { return simFg; };
            dock16.pasteTargetChecker = [&](HWND) -> AutoPaste::TargetCheck { return simChk; };
            dock16.pasteInjector = [&]() -> bool {
                ++pasteCount;
                return pasteShouldOk;
            };

            if (!dock16.Init(GetModuleHandleW(nullptr), &st16Reload, &sm16, nullptr, nullptr, nullptr, false)) {
                r16Err = 316;
            } else {
                // Real CheckForegroundTarget checks (nullptr & own HWND must NEVER be Ready)
                if (AutoPaste::CheckForegroundTarget(nullptr) != AutoPaste::TargetCheck::NoForeground ||
                    AutoPaste::CheckForegroundTarget(dock16.hwnd) != AutoPaste::TargetCheck::OwnProcess) {
                    r16Err = 317;
                }

                dock16.expandSpring.Snap(1.0f);
                dock16.Refresh();

                // 16b. autoPasteOnClick == false -> copy only, never schedules/invokes paste
                if (r16Err == 0) {
                    st16Reload.autoPasteOnClick = false;
                    pasteCount = 0;
                    clip16.clear();
                    dock16.TriggerSlotCopy(0);
                    if (clip16.empty() || dock16.pendingAutoPaste || dock16.copiedSlotWasAutoPasted || pasteCount != 0) {
                        r16Err = 318;
                    }
                }

                // 16c. autoPasteOnClick == true & Ready -> schedules settle timer, then injects Ctrl+V once and marks copiedSlotWasAutoPasted
                if (r16Err == 0) {
                    st16Reload.autoPasteOnClick = true;
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5001));
                    simChk = AutoPaste::TargetCheck::Ready;
                    pasteCount = 0;
                    clip16.clear();
                    dock16.TriggerSlotCopy(0);
                    if (clip16.empty() || !dock16.pendingAutoPaste || pasteCount != 0 || dock16.copiedSlotWasAutoPasted) {
                        r16Err = 319;
                    } else {
                        SendMessageW(dock16.hwnd, WM_TIMER, DockWindow::kTimerAutoPaste, 0);
                        if (dock16.pendingAutoPaste || pasteCount != 1 || !dock16.copiedSlotWasAutoPasted) {
                            r16Err = 320;
                        }
                    }
                }

                // 16d. Foreground changes during 35 ms settle window -> MUST abort paste cleanly
                if (r16Err == 0) {
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5001));
                    simChk = AutoPaste::TargetCheck::Ready;
                    pasteCount = 0;
                    dock16.TriggerSlotCopy(0);
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5002));
                    SendMessageW(dock16.hwnd, WM_TIMER, DockWindow::kTimerAutoPaste, 0);
                    if (pasteCount != 0 || dock16.copiedSlotWasAutoPasted || dock16.pendingAutoPaste) {
                        r16Err = 321;
                    }
                }

                // 16e. UIPI ElevatedBlocked -> copy only, no paste, shows helpful dock toast & skips immediate auto-retract
                if (r16Err == 0) {
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5003));
                    simChk = AutoPaste::TargetCheck::ElevatedBlocked;
                    pasteCount = 0;
                    dock16.ClearDockToast();
                    dock16.pendingAutoRetract = false;
                    dock16.TriggerSlotCopy(0);
                    if (dock16.pendingAutoPaste || pasteCount != 0 || dock16.copiedSlotWasAutoPasted ||
                        !dock16.IsDockToastActive() || dock16.pendingAutoRetract) {
                        r16Err = 322;
                    }
                    dock16.ClearDockToast();
                }

                // 16f. ModifierHeld (Shift+Click escape hatch) & Onboarding active -> copy only, never auto-paste
                if (r16Err == 0) {
                    simChk = AutoPaste::TargetCheck::ModifierHeld;
                    pasteCount = 0;
                    dock16.TriggerSlotCopy(0);
                    if (dock16.pendingAutoPaste || pasteCount != 0 || dock16.copiedSlotWasAutoPasted) {
                        r16Err = 323;
                    }
                }
                if (r16Err == 0) {
                    simChk = AutoPaste::TargetCheck::Ready;
                    dock16.StartOnboarding(true);
                    pasteCount = 0;
                    dock16.TriggerSlotCopy(0);
                    if (dock16.pendingAutoPaste || pasteCount != 0 || dock16.copiedSlotWasAutoPasted) {
                        r16Err = 324;
                    }
                    dock16.FinishOnboarding(false);
                }

                // 16g. Scene switch / ClearSceneTransientState invalidates pending auto-paste generation
                if (r16Err == 0) {
                    dock16.expandSpring.Snap(1.0f);
                    simFg = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x5001));
                    simChk = AutoPaste::TargetCheck::Ready;
                    pasteCount = 0;
                    dock16.TriggerSlotCopy(0);
                    if (!dock16.pendingAutoPaste) {
                        r16Err = 325;
                    } else {
                        dock16.ClearSceneTransientState();
                        SendMessageW(dock16.hwnd, WM_TIMER, DockWindow::kTimerAutoPaste, 0);
                        if (pasteCount != 0 || dock16.pendingAutoPaste) {
                            r16Err = 326;
                        }
                    }
                }

                DestroyWindow(dock16.hwnd);
            }
        }
        CoUninitialize();
        DiagLogger::Instance().Init(sm.dataDir);
        if (r16Err != 0) return r16Err;
    }

    // 17. v0.15.1 Regression: STARTF_USESHOWWINDOW (SW_HIDE / SW_SHOWMINNOACTIVE) must NEVER swallow
    // the very first BringWindowToForegroundReliable() on an overlapped window after SW_SHOWNOACTIVATE popups!
    {
        wchar_t selfExe[MAX_PATH * 4]{};
        DWORD nExe = GetModuleFileNameW(nullptr, selfExe, _countof(selfExe));
        if (nExe == 0 || nExe >= _countof(selfExe)) return 330;

        const WORD hostileSwModes[2] = { SW_HIDE, SW_SHOWMINNOACTIVE };
        for (int mi = 0; mi < 2; ++mi) {
            std::wstring cl = L"\"";
            cl += selfExe;
            cl += L"\" --self-test-startup-show";
            std::vector<wchar_t> clBuf(cl.begin(), cl.end());
            clBuf.push_back(L'\0');

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = hostileSwModes[mi];
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(selfExe, clBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                return 331 + mi;
            }
            DWORD waitRes = WaitForSingleObject(pi.hProcess, 8000);
            DWORD exitCode = 999;
            if (waitRes == WAIT_OBJECT_0) {
                GetExitCodeProcess(pi.hProcess, &exitCode);
            } else {
                TerminateProcess(pi.hProcess, 998);
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (exitCode != 0) return 333 + mi;
        }
    }

    return 0;
}

inline int RunStartupShowSelfTestChild(HINSTANCE hInst) {
    // Reproduce exact PromptPod startup sequence: WS_POPUP Dock + DarkPopupMenu with SW_SHOWNOACTIVATE
    // (which do NOT consume STARTF_USESHOWWINDOW), followed by first-ever overlapped VaultWindow open.
    HWND dock = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hInst, nullptr);
    if (!dock) return 11;
    ShowWindow(dock, SW_SHOWNOACTIVATE);
    SetWindowPos(dock, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

    HWND menu = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hInst, nullptr);
    if (!menu) { DestroyWindow(dock); return 12; }
    ShowWindow(menu, SW_SHOWNOACTIVATE);
    SetWindowPos(menu, HWND_TOPMOST, 0, 0, 10, 10, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(menu, SW_HIDE);

    HWND vault = CreateWindowExW(
        0, L"STATIC", L"PP_SelfTest_StartupShow",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        -30000, -30000, 320, 220, nullptr, nullptr, hInst, nullptr);
    if (!vault) { DestroyWindow(menu); DestroyWindow(dock); return 13; }

    BringWindowToForegroundReliable(vault, false);
    BOOL vis = IsWindowVisible(vault);
    BOOL iconic = IsIconic(vault);
    BOOL zoomed = IsZoomed(vault);

    DestroyWindow(vault);
    DestroyWindow(menu);
    DestroyWindow(dock);
    return (vis && !iconic && !zoomed) ? 0 : 14;
}

} // namespace pp

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    bool silentAutostart = false;
    bool forceReplace = false;
    bool selfTest = false;
    bool selfTestStartupShow = false;
    std::wstring selfTestDir;
    std::wstring customDataDir;
    DWORD waitPid = 0;
    std::wstring cleanupOldPath;

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--autostart") {
                silentAutostart = true;
            } else if (arg == L"--replace") {
                forceReplace = true;
            } else if (arg == L"--data-dir") {
                if (i + 1 < argc) customDataDir = argv[++i];
            } else if (arg == L"--self-test") {
                selfTest = true;
                if (i + 1 < argc) selfTestDir = argv[++i];
            } else if (arg == L"--self-test-startup-show") {
                selfTestStartupShow = true;
            } else if (arg == L"--wait-pid") {
                if (i + 1 < argc) waitPid = (DWORD)wcstoul(argv[++i], nullptr, 10);
            } else if (arg == L"--cleanup-old") {
                if (i + 1 < argc) cleanupOldPath = argv[++i];
            }
        }
        LocalFree(argv);
    }

    if (selfTestStartupShow) {
        return pp::RunStartupShowSelfTestChild(hInstance);
    }

    if (selfTest) {
        return pp::RunHeadlessSelfTest(selfTestDir);
    }

    // If spawned by in-place updater, wait cleanly for the previous process to exit and release its mutex
    if (waitPid != 0 && waitPid != GetCurrentProcessId()) {
        HANDLE hPrev = OpenProcess(SYNCHRONIZE, FALSE, waitPid);
        if (hPrev) {
            WaitForSingleObject(hPrev, 10000);
            CloseHandle(hPrev);
        }
    }

    // Single-instance guard keyed by canonicalized data directory path!
    std::wstring resolvedDir = customDataDir.empty() ? pp::StorageManager::ResolveDefaultDataDir() : customDataDir;
    std::wstring canonDir = pp::StorageManager::CanonicalizeDirPath(resolvedDir);
    std::wstring canonDef = pp::StorageManager::CanonicalizeDirPath(pp::StorageManager::ResolveDefaultDataDir());
    uint64_t dirHash = pp::HashPathFnv1a64(canonDir);

    std::wstring mutexName = APP_MUTEX_NAME_W;
    if (canonDir != canonDef) {
        wchar_t buf[96]{};
        wsprintfW(buf, L"Local\\PromptPod_SingleInstance_Dir_%016I64x", (unsigned long long)dirHash);
        mutexName = buf;
    }

    HANDLE hMutex = nullptr;
    DWORD mutexErr = 0;
    int maxAttempts = (waitPid != 0) ? 40 : 1;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        mutexErr = GetLastError();
        if (!hMutex) return 1;
        if (mutexErr != ERROR_ALREADY_EXISTS) break;
        if (attempt + 1 < maxAttempts) {
            CloseHandle(hMutex);
            hMutex = nullptr;
            Sleep(120);
        }
    }
    if (mutexErr == ERROR_ALREADY_EXISTS) {
        HWND existing = pp::FindExistingInstanceTrayHost(dirHash);
        uint32_t runningVer = existing ? (uint32_t)(ULONG_PTR)GetPropW(existing, L"PP_AppVerPacked") : 0;
        uint32_t currentVer = pp::PackAppVersion(APP_VER_MAJOR, APP_VER_MINOR, APP_VER_PATCH);

        // Seamless upgrade handoff: take over automatically if a NEWER version is launched or --replace is passed
        if (existing && pp::ShouldTakeOverExistingInstance(runningVer, currentVer, forceReplace)) {
            // Must close our duplicate handle BEFORE waiting, otherwise the named kernel mutex stays alive!
            CloseHandle(hMutex);
            hMutex = nullptr;

            DWORD oldPid = 0;
            GetWindowThreadProcessId(existing, &oldPid);
            HANDLE hOld = (oldPid != 0 && oldPid != GetCurrentProcessId())
                ? OpenProcess(SYNCHRONIZE, FALSE, oldPid)
                : nullptr;

            PostMessageW(existing, WM_CLOSE, 0, 0);
            if (hOld) {
                WaitForSingleObject(hOld, 5000);
                CloseHandle(hOld);
            }

            for (int attempt = 0; attempt < 40; ++attempt) {
                hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
                mutexErr = GetLastError();
                if (!hMutex) return 1;
                if (mutexErr != ERROR_ALREADY_EXISTS) break;
                if (attempt + 1 < 40) {
                    CloseHandle(hMutex);
                    hMutex = nullptr;
                    Sleep(100);
                }
            }
        }

        if (mutexErr == ERROR_ALREADY_EXISTS) {
            HWND live = pp::FindExistingInstanceTrayHost(dirHash);
            if (live) {
                PostMessageW(live, pp::WM_REVEAL_POD, 0, 0);
            }
            if (hMutex) CloseHandle(hMutex);
            return 0;
        }
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    pp::AppController app;
    if (!app.Init(hInstance, customDataDir, silentAutostart, dirHash)) {
        CoUninitialize();
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    // Sweep stale .old / .update.*.new files ONLY AFTER AppController::Init succeeds so failed boots retain rollback backup
    pp::CleanupStaleUpdateFiles(cleanupOldPath);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.Shutdown();
    CoUninitialize();
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
