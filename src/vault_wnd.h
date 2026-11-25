#pragma once

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include "version.h"
#include "model.h"
#include "theme.h"
#include "d2d_ctx.h"
#include "dark_menu.h"
#include "updater.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace pp {

inline void ApplyWin11DarkWindowAttributes(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    // LR_SHARED returns a system-managed shared resource handle, preventing per-dialog HICON leaks
    HICON hBig = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                               LR_DEFAULTCOLOR | LR_SHARED));
    HICON hSm  = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR | LR_SHARED));
    if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
    if (hSm)  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSm));
}

inline std::wstring CollapseWhitespaceSingleLine(const std::wstring& s, size_t maxChars = 110) {
    std::wstring out;
    out.reserve(std::min(s.size(), maxChars));
    bool lastSpace = false;
    for (wchar_t c : s) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == L' ') {
            if (!lastSpace && !out.empty()) {
                out.push_back(L' ');
                lastSpace = true;
            }
        } else {
            out.push_back(c);
            lastSpace = false;
        }
        if (out.size() >= maxChars) break;
    }
    return out;
}

inline size_t CountDisplayChars(const std::wstring& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') continue;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) ++i;
        ++n;
    }
    return n;
}

inline size_t FindIgnoreCasePos(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return 0;
    if (hay.size() < needle.size()) return std::wstring::npos;
    auto toLow = [](wchar_t c) -> wchar_t {
        return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
    };
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (toLow(hay[i + j]) != toLow(needle[j])) { ok = false; break; }
        }
        if (ok) return i;
    }
    return std::wstring::npos;
}

inline bool ContainsIgnoreCaseStr(const std::wstring& hay, const std::wstring& needle) {
    return needle.empty() || FindIgnoreCasePos(hay, needle) != std::wstring::npos;
}

inline std::wstring BuildSearchPreviewSnippet(const std::wstring& content, const std::wstring& query, size_t maxChars = 104) {
    std::wstring single = CollapseWhitespaceSingleLine(content, 2000);
    if (query.empty() || single.size() <= maxChars) {
        return (single.size() <= maxChars) ? single : single.substr(0, maxChars);
    }
    size_t pos = FindIgnoreCasePos(single, query);
    if (pos == std::wstring::npos || pos < 34) {
        return single.substr(0, maxChars);
    }
    size_t start = (pos > 24) ? (pos - 24) : 0;
    // Avoid splitting a surrogate pair
    if (start > 0 && single[start] >= 0xDC00 && single[start] <= 0xDFFF) --start;
    size_t len = std::min(maxChars - 3, single.size() - start);
    return L"..." + single.substr(start, len);
}

[[gnu::noinline]] inline void ClampDialogRectToWorkArea(const RECT& work, int& wPx, int& hPx, int& x, int& y) {
    int maxW = std::max(320, (int)(work.right - work.left) - 24);
    int maxH = std::max(260, (int)(work.bottom - work.top) - 24);
    wPx = std::min(wPx, maxW);
    hPx = std::min(hPx, maxH);
    x = work.left + ((int)(work.right - work.left) - wPx) / 2;
    y = work.top + ((int)(work.bottom - work.top) - hPx) / 2;
    x = std::clamp(x, (int)work.left + 8, std::max((int)work.left + 8, (int)work.right - wPx - 8));
    y = std::clamp(y, (int)work.top + 8, std::max((int)work.top + 8, (int)work.bottom - hPx - 8));
}

[[gnu::noinline]] inline float ComputeModalPlacement(HWND parent, float wDip, float hDip, int& x, int& y, int& wPx, int& hPx) {
    POINT anchorPt{ 0, 0 };
    if (parent && IsWindowVisible(parent)) {
        RECT pr{}; GetWindowRect(parent, &pr);
        anchorPt = { (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
    } else {
        GetCursorPos(&anchorPt);
    }
    RECT work = GetMonitorWorkAreaForPoint(anchorPt);
    float sc = GetDpiScaleForPoint(anchorPt, parent);
    wPx = (int)(wDip * sc);
    hPx = (int)(hDip * sc);
    ClampDialogRectToWorkArea(work, wPx, hPx, x, y);
    return sc;
}

[[gnu::noinline]] inline HWND CreateDarkModalWindow(HINSTANCE hInst, const wchar_t* clsName, WNDPROC proc,
                                                    const wchar_t* title, DWORD style,
                                                    HWND parent, float wDip, float hDip, float& outDpi, void* selfPtr) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.lpszClassName = clsName;
    RegisterClassExW(&wc);

    int x = 0, y = 0, wPx = 0, hPx = 0;
    outDpi = ComputeModalPlacement(parent, wDip, hDip, x, y, wPx, hPx);
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, clsName, title, style,
                             x, y, wPx, hPx, parent, nullptr, hInst, selfPtr);
    if (!h) {
        if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
        return nullptr;
    }
    ApplyWin11DarkWindowAttributes(h);
    return h;
}

[[gnu::noinline]] inline void EndDarkModalWindow(HWND& hwnd, HWND parent, bool gotQuit, WPARAM quitCode) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        if (!gotQuit) SetForegroundWindow(parent);
    }
    if (gotQuit) PostQuitMessage((int)quitCode);
}

[[gnu::noinline]] inline void EnsureSoftwareHwndRT(HWND hwnd, float dpiScale, ComPtr<ID2D1HwndRenderTarget>& rt, ComPtr<ID2D1SolidColorBrush>& brush) {
    if (rt && brush) return;
    brush.Reset();
    rt.Reset();
    RECT rc{};
    GetClientRect(hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);
    props.dpiX = 96.0f * dpiScale;
    props.dpiY = 96.0f * dpiScale;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU((UINT32)std::max<LONG>(1, rc.right - rc.left), (UINT32)std::max<LONG>(1, rc.bottom - rc.top)));
    ComPtr<ID2D1HwndRenderTarget> newRt;
    ComPtr<ID2D1SolidColorBrush> newBr;
    if (SUCCEEDED(D2DEngine::Instance().d2dFactory->CreateHwndRenderTarget(props, hp, newRt.GetAddressOf())) &&
        SUCCEEDED(newRt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), newBr.GetAddressOf()))) {
        rt = std::move(newRt);
        brush = std::move(newBr);
    }
}

[[gnu::noinline]] inline void DrawRim(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, float radius,
                                      const D2D1_COLOR_F& topColor, const D2D1_COLOR_F& bottomColor, float strokeWidth = 1.0f) {
    DrawTopLitBorder(rt, box, radius, topColor, bottomColor, strokeWidth);
}

[[gnu::noinline]] inline void DrawUiTextHex(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, IDWriteTextFormat* fmt,
                                            const D2D1_RECT_F& r, const wchar_t* raw, uint32_t hex, float alpha = 1.0f) {
    if (!rt || !br || !fmt || !raw || !*raw) return;
    std::wstring s = UiText(raw);
    DrawTextHex(rt, br, fmt, r, s.c_str(), (UINT32)s.size(), hex, alpha);
}

[[gnu::noinline]] inline HFONT CreateUiInputFont(float sizeDip, float dpiScale) {
    return CreateFontW(-(int)std::round(sizeDip * dpiScale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

[[gnu::noinline]] inline std::wstring DeriveAutoPromptTitle(const std::wstring& body) {
    size_t i = 0;
    while (i < body.size()) {
        wchar_t c = body[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
            c == L'#' || c == L'>' || c == L'-' || c == L'*' || c == L'\x2022') {
            ++i;
        } else {
            break;
        }
    }
    std::wstring line;
    size_t glyphs = 0;
    for (; i < body.size() && glyphs < 14; ++i) {
        wchar_t c = body[i];
        if (c == L'\r' || c == L'\n') break;
        if (c == L'\t') c = L' ';
        if (c == L' ' && !line.empty() && line.back() == L' ') continue;
        line.push_back(c);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < body.size() && body[i + 1] >= 0xDC00 && body[i + 1] <= 0xDFFF) {
            line.push_back(body[++i]);
        }
        ++glyphs;
    }
    while (!line.empty() && (line.back() == L' ' || line.back() == L':' || line.back() == L'\xFF1A')) line.pop_back();
    return line.empty() ? std::wstring(L"\x672A\x547D\x540D Prompt") : line;
}

[[gnu::noinline]] inline void DrawDarkPillButton(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, IDWriteTextFormat* fmt,
                                                 const D2D1_RECT_F& r, const wchar_t* text, bool hov,
                                                 uint32_t baseBg, uint32_t hoverBg, uint32_t textHex,
                                                 float borderAlpha = 0.12f, bool pressed = false, float alphaScale = 1.0f) {
    if (alphaScale <= 0.01f) return;
    float ins = (pressed && !Motion::IsReducedMotion()) ? 0.65f : 0.0f;
    D2D1_RECT_F dr = D2D1::RectF(r.left + ins, r.top + ins, r.right - ins, r.bottom - ins);
    uint32_t fillHex = pressed ? baseBg : (hov ? hoverBg : baseBg);
    br->SetColor(Rgba(fillHex, (pressed ? 0.86f : 1.0f) * alphaScale));
    rt->FillRoundedRectangle(D2D1::RoundedRect(dr, 6.0f, 6.0f), br);
    float topA = (pressed ? (borderAlpha * 0.7f) : (hov ? borderAlpha + 0.10f : borderAlpha)) * alphaScale;
    DrawRim(rt, dr, 6.0f, Rgba(0xFFFFFF, topA), Rgba(0xFFFFFF, 0.04f * alphaScale));
    if (fmt && text && *text) {
        std::wstring s = UiText(text);
        br->SetColor(Rgba(textHex, (pressed ? 0.85f : (hov ? 1.0f : 0.92f)) * alphaScale));
        rt->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, dr, br);
    }
}

[[gnu::noinline]] inline void DrawDarkCheckbox(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                               const D2D1_RECT_F& cb, bool checked, bool hovered,
                                               uint32_t accentHex = Theme::AccentCyan, bool locked = false,
                                               float checkProg = -1.0f) {
    float p = (checkProg >= 0.0f) ? std::clamp(checkProg, 0.0f, 1.0f) : (checked ? 1.0f : 0.0f);
    D2D1_COLOR_F offFill = Rgba(hovered ? 0x252B3B : 0x151924, locked ? 0.72f : 1.0f);
    D2D1_COLOR_F onFill  = Rgba(locked ? 0x1E3A5F : 0x2563EB, locked ? 0.72f : 1.0f);
    br->SetColor(LerpColor(offFill, onFill, p));
    rt->FillRoundedRectangle(D2D1::RoundedRect(cb, 4.0f, 4.0f), br);
    D2D1_COLOR_F offRim = Rgba(0xFFFFFF, hovered ? 0.28f : 0.18f);
    D2D1_COLOR_F onRim  = Rgba(accentHex, locked ? 0.36f : 0.72f);
    DrawRim(rt, cb, 4.0f, LerpColor(offRim, onRim, p), Rgba(0xFFFFFF, 0.05f));
    if (p > 0.02f) {
        DrawAnimatedCheckmark(rt, br, (cb.left + cb.right) * 0.5f, (cb.top + cb.bottom) * 0.5f,
                              (cb.right - cb.left) * 0.64f, p, Rgba(0xFFFFFF, (locked ? 0.65f : 1.0f) * std::min(1.0f, p * 2.0f)), 1.8f);
    }
}

// ============================================================================
// Dark Direct2D Capsule Scrollbar for Custom D2D Scrollable Viewports
// ============================================================================
class DarkViewportScrollbar {
public:
    D2D1_RECT_F trackVisRect{};
    D2D1_RECT_F hitRect{};
    bool hovered = false;
    bool dragging = false;
    float hoverAnim = 0.0f;
    float dragGrabOffsetY = 0.0f;

    bool StepAnim(float dt) {
        return Motion::ExpApproach(hoverAnim, (hovered || dragging) ? 1.0f : 0.0f, 22.0f, dt);
    }

    bool IsAnimActive() const {
        float tgt = (hovered || dragging) ? 1.0f : 0.0f;
        return std::fabs(hoverAnim - tgt) > 0.006f;
    }

    void SetViewport(const D2D1_RECT_F& vp) {
        trackVisRect = D2D1::RectF(vp.right - 9.5f, vp.top + 4.0f, vp.right - 4.5f, vp.bottom - 4.0f);
        hitRect = D2D1::RectF(vp.right - 15.0f, vp.top, vp.right, vp.bottom);
    }

    bool ComputeThumb(float scrollY, float maxScrollY, float viewportH, D2D1_RECT_F& outThumb) const {
        if (maxScrollY <= 0.5f) return false;
        float trackH = trackVisRect.bottom - trackVisRect.top;
        if (trackH < 16.0f) return false;
        float contentH = viewportH + maxScrollY;
        float ratio = std::clamp(viewportH / std::max(1.0f, contentH), 0.06f, 1.0f);
        float minThumbH = std::min(30.0f, trackH * 0.45f);
        float thumbH = std::clamp(trackH * ratio, minThumbH, trackH);
        float progress = std::clamp(scrollY / maxScrollY, 0.0f, 1.0f);
        float top = trackVisRect.top + progress * std::max(0.0f, trackH - thumbH);
        float activeW = 3.6f + 1.5f * std::clamp(hoverAnim, 0.0f, 1.0f);
        float cx = (trackVisRect.left + trackVisRect.right) * 0.5f;
        outThumb = D2D1::RectF(cx - activeW * 0.5f, top, cx + activeW * 0.5f, top + thumbH);
        return true;
    }

    float ThumbTopToScrollY(float thumbTop, float maxScrollY, float thumbH) const {
        if (maxScrollY <= 0.5f) return 0.0f;
        float trackH = trackVisRect.bottom - trackVisRect.top;
        float maxTravel = trackH - thumbH;
        if (maxTravel <= 0.5f) return 0.0f;
        float p = std::clamp((thumbTop - trackVisRect.top) / maxTravel, 0.0f, 1.0f);
        return p * maxScrollY;
    }

    bool OnLButtonDown(float dx, float dy, float& scrollY, float maxScrollY, float viewportH) {
        if (maxScrollY <= 0.5f) return false;
        if (dx < hitRect.left || dx > hitRect.right || dy < hitRect.top || dy > hitRect.bottom) return false;
        D2D1_RECT_F thumb{};
        if (!ComputeThumb(scrollY, maxScrollY, viewportH, thumb)) return false;
        float thumbH = thumb.bottom - thumb.top;
        dragging = true;
        hovered = true;
        hoverAnim = 1.0f;
        if (dy >= thumb.top - 2.0f && dy <= thumb.bottom + 2.0f) {
            dragGrabOffsetY = dy - thumb.top;
        } else {
            dragGrabOffsetY = thumbH * 0.5f;
            scrollY = ThumbTopToScrollY(dy - dragGrabOffsetY, maxScrollY, thumbH);
        }
        return true;
    }

    bool OnMouseMove(float dx, float dy, float& scrollY, float maxScrollY, float viewportH, bool& outNeedRedraw) {
        outNeedRedraw = false;
        if (dragging) {
            D2D1_RECT_F thumb{};
            if (ComputeThumb(scrollY, maxScrollY, viewportH, thumb)) {
                float nextY = ThumbTopToScrollY(dy - dragGrabOffsetY, maxScrollY, thumb.bottom - thumb.top);
                if (std::fabs(nextY - scrollY) > 0.1f) {
                    scrollY = nextY;
                    outNeedRedraw = true;
                }
            }
            return true;
        }
        bool inHit = (maxScrollY > 0.5f) && (dx >= hitRect.left && dx <= hitRect.right && dy >= hitRect.top && dy <= hitRect.bottom);
        if (inHit != hovered) {
            hovered = inHit;
            if (Motion::IsReducedMotion()) hoverAnim = hovered ? 1.0f : 0.0f;
            outNeedRedraw = true;
        }
        return inHit;
    }

    void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, float scrollY, float maxScrollY, float viewportH) const {
        if (!rt || !br) return;
        D2D1_RECT_F thumb{};
        if (!ComputeThumb(scrollY, maxScrollY, viewportH, thumb)) return;
        float ha = std::clamp(hoverAnim, 0.0f, 1.0f);
        if (ha > 0.02f) {
            float trRad = (trackVisRect.right - trackVisRect.left) * 0.5f;
            br->SetColor(Rgba(0xFFFFFF, 0.055f * ha));
            rt->FillRoundedRectangle(D2D1::RoundedRect(trackVisRect, trRad, trRad), br);
        }
        float rad = (thumb.right - thumb.left) * 0.5f;
        D2D1_COLOR_F col = dragging
            ? Rgba(Theme::AccentCyan, 0.86f)
            : LerpColor(Rgba(0x94A3B8, 0.32f), Rgba(0x94A3B8, 0.68f), ha);
        br->SetColor(col);
        rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, rad, rad), br);
    }
};

// ============================================================================
// Dark Direct2D Capsule Scrollbar Companion for Borderless Multi-line Win32 EDIT
// Removes ugly light-mode native WS_VSCROLL chrome while keeping 100% native IME,
// selection, undo, word-wrap, precision mouse wheel & thumb drag/track jump.
// ============================================================================
class DarkEditScrollbar {
public:
    struct Metrics {
        int totalLines = 1;
        int visibleLines = 1;
        int firstLine = 0;
        int maxFirstLine = 0;
        int lineHeightPx = 18;
    };

    HWND parent = nullptr;
    HWND hEdit = nullptr;
    WNDPROC origProc = nullptr;
    float dpiScale = 1.0f;

    D2D1_RECT_F boxDip{};
    D2D1_RECT_F trackVisRect{};
    D2D1_RECT_F hitRect{};

    bool hovered = false;
    bool dragging = false;
    float dragGrabOffsetY = 0.0f;
    int wheelAccum = 0;

    int lastTotalLines = -1;
    int lastVisibleLines = -1;
    int lastFirstLine = -1;

    static constexpr const wchar_t* kPropName = L"PP_DarkEditScroll";

    ~DarkEditScrollbar() {
        Detach();
    }

    void Attach(HWND parentWnd, HWND editWnd) {
        Detach();
        parent = parentWnd;
        hEdit = editWnd;
        wheelAccum = 0;
        hovered = false;
        dragging = false;
        lastTotalLines = lastVisibleLines = lastFirstLine = -1;
        if (hEdit && IsWindow(hEdit)) {
            SetPropW(hEdit, kPropName, this);
            origProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubProc)));
        }
    }

    void Detach() {
        if (hEdit && IsWindow(hEdit) && origProc) {
            SetWindowLongPtrW(hEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(origProc));
            RemovePropW(hEdit, kPropName);
        }
        hEdit = nullptr;
        origProc = nullptr;
        hovered = false;
        dragging = false;
        wheelAccum = 0;
    }

    void SetTrackLayout(const D2D1_RECT_F& containerBoxDip, float scale) {
        dpiScale = (scale > 0.1f) ? scale : 1.0f;
        boxDip = containerBoxDip;
        trackVisRect = D2D1::RectF(
            containerBoxDip.right - 9.5f,
            containerBoxDip.top + 6.0f,
            containerBoxDip.right - 4.5f,
            containerBoxDip.bottom - 6.0f
        );
        hitRect = D2D1::RectF(
            containerBoxDip.right - 14.0f,
            containerBoxDip.top + 2.0f,
            containerBoxDip.right,
            containerBoxDip.bottom - 2.0f
        );
    }

    Metrics QueryMetrics() const {
        Metrics m{};
        if (!hEdit || !IsWindow(hEdit)) return m;

        m.totalLines = std::max(1, (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0));
        m.firstLine = std::max(0, (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0));

        RECT rcFmt{};
        SendMessageW(hEdit, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rcFmt));
        if (rcFmt.bottom <= rcFmt.top) {
            GetClientRect(hEdit, &rcFmt);
        }

        int lineH = 0;
        if (m.totalLines >= 2) {
            int probeLine = std::min(m.firstLine, m.totalLines - 2);
            int c0 = (int)SendMessageW(hEdit, EM_LINEINDEX, (WPARAM)probeLine, 0);
            int c1 = (int)SendMessageW(hEdit, EM_LINEINDEX, (WPARAM)(probeLine + 1), 0);
            if (c0 >= 0 && c1 > c0) {
                DWORD p0 = (DWORD)SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)c0, 0);
                DWORD p1 = (DWORD)SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)c1, 0);
                int dy = (int)(short)HIWORD(p1) - (int)(short)HIWORD(p0);
                if (dy >= 6 && dy <= 200) lineH = dy;
            }
        }
        if (lineH <= 0) {
            HDC hdc = GetDC(hEdit);
            if (hdc) {
                HFONT hf = reinterpret_cast<HFONT>(SendMessageW(hEdit, WM_GETFONT, 0, 0));
                HGDIOBJ old = hf ? SelectObject(hdc, hf) : nullptr;
                TEXTMETRICW tm{};
                if (GetTextMetricsW(hdc, &tm) && tm.tmHeight > 0) {
                    lineH = tm.tmHeight;
                }
                if (old) SelectObject(hdc, old);
                ReleaseDC(hEdit, hdc);
            }
        }
        if (lineH <= 0) lineH = std::max(12, (int)std::round(18.0f * dpiScale));

        m.lineHeightPx = lineH;
        int fmtH = std::max<int>(1, rcFmt.bottom - rcFmt.top);
        m.visibleLines = std::max(1, fmtH / lineH);
        m.maxFirstLine = std::max(0, m.totalLines - m.visibleLines);
        return m;
    }

    bool CanScroll() const {
        return QueryMetrics().maxFirstLine > 0;
    }

    bool ComputeThumb(const Metrics& m, D2D1_RECT_F& outThumb) const {
        if (m.maxFirstLine <= 0) return false;
        float trackH = trackVisRect.bottom - trackVisRect.top;
        if (trackH < 12.0f) return false;

        float ratio = std::clamp((float)m.visibleLines / (float)std::max(1, m.totalLines), 0.05f, 1.0f);
        float minThumbH = std::min(28.0f, trackH * 0.45f);
        float thumbH = std::clamp(trackH * ratio, minThumbH, trackH);
        float progress = std::clamp((float)m.firstLine / (float)m.maxFirstLine, 0.0f, 1.0f);
        float maxTravel = std::max(0.0f, trackH - thumbH);
        float top = trackVisRect.top + progress * maxTravel;

        float activeW = (hovered || dragging) ? 5.0f : 3.8f;
        float cx = (trackVisRect.left + trackVisRect.right) * 0.5f;
        outThumb = D2D1::RectF(cx - activeW * 0.5f, top, cx + activeW * 0.5f, top + thumbH);
        return true;
    }

    int ThumbTopToLine(float thumbTopDip, const Metrics& m, float thumbHeightDip) const {
        if (m.maxFirstLine <= 0) return 0;
        float trackH = trackVisRect.bottom - trackVisRect.top;
        float maxTravel = trackH - thumbHeightDip;
        if (maxTravel <= 0.5f) return 0;
        float progress = std::clamp((thumbTopDip - trackVisRect.top) / maxTravel, 0.0f, 1.0f);
        return (int)std::round(progress * (float)m.maxFirstLine);
    }

    void InvalidateTrack() const {
        if (!parent || !IsWindow(parent)) return;
        if (hitRect.right > hitRect.left && hitRect.bottom > hitRect.top) {
            RECT rc{};
            rc.left   = (LONG)std::floor((hitRect.left - 2.0f) * dpiScale);
            rc.top    = (LONG)std::floor((hitRect.top - 2.0f) * dpiScale);
            rc.right  = (LONG)std::ceil((hitRect.right + 2.0f) * dpiScale);
            rc.bottom = (LONG)std::ceil((hitRect.bottom + 2.0f) * dpiScale);
            InvalidateRect(parent, &rc, FALSE);
        } else {
            InvalidateRect(parent, nullptr, FALSE);
        }
    }

    void ScrollToLine(int targetLine) {
        if (!hEdit || !IsWindow(hEdit)) return;
        Metrics m = QueryMetrics();
        int clamped = std::clamp(targetLine, 0, m.maxFirstLine);
        int delta = clamped - m.firstLine;
        if (delta != 0) {
            SendMessageW(hEdit, EM_LINESCROLL, 0, (LPARAM)delta);
        }
        SyncAndNotifyParent(delta != 0);
    }

    void ScrollByLines(int deltaLines) {
        if (!hEdit || !IsWindow(hEdit) || deltaLines == 0) return;
        Metrics m = QueryMetrics();
        ScrollToLine(m.firstLine + deltaLines);
    }

    void SyncAndNotifyParent(bool force = false) {
        if (!hEdit || !IsWindow(hEdit)) return;
        Metrics m = QueryMetrics();
        // If text shrank so everything fits again, ensure top line resets to 0
        if (m.maxFirstLine == 0 && m.firstLine > 0) {
            SendMessageW(hEdit, EM_LINESCROLL, 0, (LPARAM)(-m.firstLine));
            m = QueryMetrics();
            force = true;
        }
        if (force || m.totalLines != lastTotalLines || m.visibleLines != lastVisibleLines || m.firstLine != lastFirstLine) {
            lastTotalLines = m.totalLines;
            lastVisibleLines = m.visibleLines;
            lastFirstLine = m.firstLine;
            if (m.maxFirstLine == 0 && hovered && !dragging) {
                hovered = false;
            }
            InvalidateTrack();
        }
    }

    bool OnMouseWheel(WPARAM wp) {
        if (!hEdit || !IsWindow(hEdit)) return false;
        if (LOWORD(wp) & MK_CONTROL) return false;
        short delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta == 0) return true;

        if ((wheelAccum > 0 && delta < 0) || (wheelAccum < 0 && delta > 0)) {
            wheelAccum = 0;
        }
        wheelAccum += delta;

        UINT sysLines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &sysLines, 0);
        if (sysLines == 0) return true;

        Metrics m = QueryMetrics();
        int linesPerNotch = (sysLines == WHEEL_PAGESCROLL)
            ? std::max(1, m.visibleLines - 1)
            : std::clamp((int)sysLines, 1, 12);
        int threshold = std::max(1, WHEEL_DELTA / linesPerNotch);
        int steps = wheelAccum / threshold;
        if (steps != 0) {
            wheelAccum -= steps * threshold;
            ScrollToLine(m.firstLine - steps);
        }
        return true;
    }

    bool OnParentMouseWheel(WPARAM wp, POINT screenPt) {
        if (!parent || !IsWindow(parent)) return false;
        POINT cpt = screenPt;
        ScreenToClient(parent, &cpt);
        float dx = (float)cpt.x / dpiScale;
        float dy = (float)cpt.y / dpiScale;
        if (dx >= boxDip.left && dx <= boxDip.right && dy >= boxDip.top && dy <= boxDip.bottom) {
            return OnMouseWheel(wp);
        }
        return false;
    }

    bool OnParentMouseMove(float dx, float dy) {
        if (dragging) {
            Metrics m = QueryMetrics();
            D2D1_RECT_F thumb{};
            if (ComputeThumb(m, thumb)) {
                float thumbH = thumb.bottom - thumb.top;
                int target = ThumbTopToLine(dy - dragGrabOffsetY, m, thumbH);
                ScrollToLine(target);
            }
            return true;
        }
        bool inHit = (dx >= hitRect.left && dx <= hitRect.right && dy >= hitRect.top && dy <= hitRect.bottom);
        bool nextHover = inHit && CanScroll();
        if (nextHover != hovered) {
            hovered = nextHover;
            InvalidateTrack();
        }
        return nextHover;
    }

    void OnParentMouseLeave() {
        if (!dragging && hovered) {
            hovered = false;
            InvalidateTrack();
        }
    }

    bool OnParentLButtonDown(float dx, float dy) {
        if (dx < hitRect.left || dx > hitRect.right || dy < hitRect.top || dy > hitRect.bottom) {
            return false;
        }
        Metrics m = QueryMetrics();
        D2D1_RECT_F thumb{};
        if (!ComputeThumb(m, thumb)) return false;

        float thumbH = thumb.bottom - thumb.top;
        dragging = true;
        hovered = true;
        if (dy >= thumb.top - 2.0f && dy <= thumb.bottom + 2.0f) {
            dragGrabOffsetY = dy - thumb.top;
        } else {
            dragGrabOffsetY = thumbH * 0.5f;
            int target = ThumbTopToLine(dy - dragGrabOffsetY, m, thumbH);
            ScrollToLine(target);
        }
        if (parent && IsWindow(parent)) SetCapture(parent);
        InvalidateTrack();
        return true;
    }

    bool OnParentLButtonUp() {
        if (!dragging) return false;
        dragging = false;
        if (parent && GetCapture() == parent) ReleaseCapture();
        InvalidateTrack();
        return true;
    }

    void OnParentCaptureChanged() {
        if (dragging) {
            dragging = false;
            InvalidateTrack();
        }
    }

    void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br) {
        if (!rt || !br) return;
        Metrics m = QueryMetrics();
        lastTotalLines = m.totalLines;
        lastVisibleLines = m.visibleLines;
        lastFirstLine = m.firstLine;

        D2D1_RECT_F thumb{};
        if (!ComputeThumb(m, thumb)) return;

        if (hovered || dragging) {
            float trRad = (trackVisRect.right - trackVisRect.left) * 0.5f;
            br->SetColor(Rgba(0xFFFFFF, 0.055f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(trackVisRect, trRad, trRad), br);
        }

        float rad = (thumb.right - thumb.left) * 0.5f;
        D2D1_COLOR_F thumbColor = dragging
            ? Rgba(Theme::AccentCyan, 0.80f)
            : (hovered ? Rgba(0x94A3B8, 0.62f) : Rgba(0x94A3B8, 0.30f));
        br->SetColor(thumbColor);
        rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, rad, rad), br);
    }

private:
    static LRESULT CALLBACK EditSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = static_cast<DarkEditScrollbar*>(GetPropW(h, kPropName));
        if (!self || !self->origProc) return DefWindowProcW(h, msg, wp, lp);
        WNDPROC orig = self->origProc;

        if (msg == WM_NCDESTROY) {
            RemovePropW(h, kPropName);
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
            self->hEdit = nullptr;
            self->origProc = nullptr;
            return CallWindowProcW(orig, h, msg, wp, lp);
        }

        if (msg == WM_MOUSEWHEEL) {
            if (self->OnMouseWheel(wp)) return 0;
        }

        LRESULT res = CallWindowProcW(orig, h, msg, wp, lp);

        switch (msg) {
            case WM_KEYDOWN:
            case WM_CHAR:
            case WM_PASTE:
            case WM_CUT:
            case WM_CLEAR:
            case WM_UNDO:
            case WM_SETTEXT:
            case EM_REPLACESEL:
            case WM_VSCROLL:
            case EM_SCROLL:
            case EM_LINESCROLL:
            case EM_SCROLLCARET:
            case WM_LBUTTONUP:
            case WM_SIZE:
            case WM_SETFONT:
            case WM_TIMER:
                self->SyncAndNotifyParent(false);
                break;
            case WM_MOUSEMOVE:
                if (wp & MK_LBUTTON) self->SyncAndNotifyParent(false);
                break;
        }
        return res;
    }
};

// ============================================================================
// Modal Dark Prompt Editor Dialog (100% Native Windows IME + Multi-line Edit)
// ============================================================================
class PromptEditorDialog {
public:
    using CommitCallback = std::function<bool(const std::wstring& title, const std::wstring& content, std::wstring& outErr)>;

    struct Result {
        bool accepted = false;
        std::wstring title;
        std::wstring content;
    };

    static Result Show(HINSTANCE hInst, HWND parent, const std::wstring& dialogHeading,
                       const std::wstring& initTitle = L"", const std::wstring& initContent = L"",
                       CommitCallback onCommit = nullptr) {
        PromptEditorDialog dlg(hInst, dialogHeading, initTitle, initContent, std::move(onCommit));
        dlg.RunModal(parent);
        return dlg.result;
    }

private:
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HWND hEditTitle = nullptr;
    HWND hEditBody = nullptr;
    HFONT hFontInput = nullptr;
    HBRUSH hBrushEditBg = nullptr;
    DarkEditScrollbar bodyScroll;

    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtLabel;
    ComPtr<IDWriteTextFormat> fmtCount;
    ComPtr<IDWriteTextFormat> fmtBtn;
    ComPtr<IDWriteTextFormat> fmtHint;

    std::wstring heading;
    std::wstring initialTitle;
    std::wstring initialContent;
    std::wstring initialContentLf;
    CommitCallback commitCallback;
    std::wstring errorHint;
    float dpiScale = 1.0f;
    int hoverBtn = 0; // 0 none, 1 cancel, 2 save
    int pressedBtn = 0; // 0 none, 1 cancel, 2 save
    int focusedEdit = 0; // 0 none, 1 title, 2 body
    float titleFocusAnim = 0.0f;
    float bodyFocusAnim = 0.0f;
    ULONGLONG bodyShakeTick = 0;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;
    bool trackingLeave = false;
    bool dirtyConfirmArmed = false;
    ULONGLONG dirtyConfirmTick = 0;
    bool done = false;
    Result result{};

    static constexpr UINT_PTR kTimerAnim = 1101;

    D2D1_RECT_F rcCancelBtn{};
    D2D1_RECT_F rcSaveBtn{};

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            titleFocusAnim = (focusedEdit == 1) ? 1.0f : 0.0f;
            bodyFocusAnim = (focusedEdit == 2) ? 1.0f : 0.0f;
            bodyShakeTick = 0;
            if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    [[gnu::noinline]] void OnAnimTick() {
        float dt = StepDeltaQpc(qpcFreq, lastAnimQpc);
        bool m1 = Motion::ExpApproach(titleFocusAnim, focusedEdit == 1 ? 1.0f : 0.0f, 22.0f, dt);
        bool m2 = Motion::ExpApproach(bodyFocusAnim,  focusedEdit == 2 ? 1.0f : 0.0f, 22.0f, dt);
        bool sh = (!Motion::IsReducedMotion() && bodyShakeTick != 0 && (GetTickCount64() - bodyShakeTick) < 270);
        if (!sh) bodyShakeTick = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (!m1 && !m2 && !sh) {
            KillTimer(hwnd, kTimerAnim);
            animActive = false;
        }
    }

    PromptEditorDialog(HINSTANCE inst, const std::wstring& hd, const std::wstring& t, const std::wstring& c, CommitCallback cb)
        : hInst(inst), heading(UiText(hd)), initialTitle(t), initialContent(c),
          initialContentLf(NormalizeLfFromEdit(c)), commitCallback(std::move(cb)) {}

    ~PromptEditorDialog() {
        if (hFontInput) DeleteObject(hFontInput);
        if (hBrushEditBg) DeleteObject(hBrushEditBg);
    }

    static std::wstring NormalizeCrlfForEdit(const std::wstring& s) {
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

    static std::wstring NormalizeLfFromEdit(const std::wstring& s) {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') {
                out.push_back(L'\n');
                ++i;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    static std::wstring Trim(const std::wstring& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
        size_t b = s.size();
        while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
        return s.substr(a, b - a);
    }

    static std::wstring GetWndText(HWND h) {
        int len = GetWindowTextLengthW(h);
        if (len <= 0) return {};
        std::wstring s((size_t)len + 1, L'\0');
        int actual = GetWindowTextW(h, s.data(), len + 1);
        s.resize(actual > 0 ? (size_t)actual : 0);
        return s;
    }

    bool HasUnsavedChanges() const {
        if (!hEditTitle || !hEditBody) return false;
        std::wstring curTitle = GetWndText(hEditTitle);
        std::wstring curBody = NormalizeLfFromEdit(GetWndText(hEditBody));
        return (curTitle != initialTitle) || (curBody != initialContentLf);
    }

    void TryCommit() {
        dirtyConfirmArmed = false;
        std::wstring t = Trim(GetWndText(hEditTitle));
        std::wstring c = NormalizeLfFromEdit(GetWndText(hEditBody));
        if (Trim(c).empty()) {
            errorHint = UiText(L"\x8BF7\x586B\x5199 Prompt \x6B63\x6587\x5185\x5BB9");
            bodyShakeTick = GetTickCount64();
            SetFocus(hEditBody);
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (t.empty()) {
            t = DeriveAutoPromptTitle(c);
        }
        if (commitCallback) {
            std::wstring commitErr;
            if (!commitCallback(t, c, commitErr)) {
                errorHint = UiText(commitErr.empty()
                    ? L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x6570\x636E\x6587\x4EF6"
                    : commitErr);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        result.accepted = true;
        result.title = std::move(t);
        result.content = std::move(c);
        done = true;
        DestroyWindow(hwnd);
    }

    void RequestCancel() {
        if (HasUnsavedChanges()) {
            ULONGLONG now = GetTickCount64();
            if (!dirtyConfirmArmed || (now - dirtyConfirmTick) > 4500) {
                dirtyConfirmArmed = true;
                dirtyConfirmTick = now;
                errorHint = UiText(L"有未保存修改，再按一次 Esc 或取消即可放弃");
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        result.accepted = false;
        done = true;
        DestroyWindow(hwnd);
    }

    void RunModal(HWND parent) {
        hwnd = CreateDarkModalWindow(hInst, L"PromptPod_EditorDlg", DlgWndProc, heading.c_str(),
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
                                     parent, 510.0f, 440.0f, dpiScale, this);
        if (!hwnd) return;
        SetupChildControls();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(initialTitle.empty() ? hEditTitle : hEditBody);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) {
                gotQuit = true;
                quitCode = msg.wParam;
                break;
            }
            if (gm < 0) break;

            bool isOwnMsg = (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd));
            if (isOwnMsg && msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    if ((msg.lParam & 0x40000000) != 0) continue; // Ignore auto-repeat hold
                    RequestCancel();
                    continue;
                }
                if (msg.wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    TryCommit();
                    continue;
                }
                if (msg.wParam == VK_RETURN && msg.hwnd == hEditTitle) {
                    SetFocus(hEditBody);
                    continue;
                }
                if (msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
                    (msg.hwnd == hEditTitle || msg.hwnd == hEditBody)) {
                    SendMessageW(msg.hwnd, EM_SETSEL, 0, -1);
                    continue;
                }
                if (msg.wParam == VK_TAB) {
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (!shift && msg.hwnd == hEditTitle) {
                        SetFocus(hEditBody);
                        continue;
                    }
                    if (shift && msg.hwnd == hEditBody) {
                        SetFocus(hEditTitle);
                        continue;
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        EndDarkModalWindow(hwnd, parent, gotQuit, quitCode);
    }

    void LayoutChildControls() {
        if (!hwnd) return;
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float ch = (float)(crc.bottom - crc.top) / dpiScale;

        int titleX = (int)((26.0f) * dpiScale);
        int titleY = (int)((78.0f) * dpiScale);
        int titleW = (int)((cw - 52.0f) * dpiScale);
        int titleH = (int)((26.0f) * dpiScale);
        if (hEditTitle) {
            SetWindowPos(hEditTitle, nullptr, titleX, titleY, std::max(40, titleW), titleH, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        int bodyX = (int)(26.0f * dpiScale);
        int bodyY = (int)(146.0f * dpiScale);
        int bodyW = (int)((cw - 60.0f) * dpiScale);
        int bodyH = (int)((ch - 212.0f) * dpiScale);
        if (hEditBody) {
            SetWindowPos(hEditBody, nullptr, bodyX, bodyY, std::max(40, bodyW), std::max(40, bodyH), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        bodyScroll.SetTrackLayout(D2D1::RectF(20.0f, 140.0f, cw - 20.0f, ch - 60.0f), dpiScale);

        rcSaveBtn = D2D1::RectF(cw - 104.0f, ch - 46.0f, cw - 20.0f, ch - 14.0f);
        rcCancelBtn = D2D1::RectF(cw - 196.0f, ch - 46.0f, cw - 114.0f, ch - 14.0f);
    }

    void RebuildEditFont() {
        HFONT nextFont = CreateUiInputFont(13.5f, dpiScale);
        if (nextFont) {
            if (hEditTitle) SendMessageW(hEditTitle, WM_SETFONT, (WPARAM)nextFont, TRUE);
            if (hEditBody)  SendMessageW(hEditBody,  WM_SETFONT, (WPARAM)nextFont, TRUE);
            if (hFontInput) DeleteObject(hFontInput);
            hFontInput = nextFont;
        }
    }

    void SetupChildControls() {
        dpiScale = GetDpiScaleForWindow(hwnd);
        hBrushEditBg = CreateSolidBrush(RGB(27, 31, 43));

        auto& eng = D2DEngine::Instance();
        fmtHeading = eng.CreateFormat(15.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        fmtLabel = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_MEDIUM);
        fmtCount = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtBtn = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtHint = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);

        hEditTitle = CreateWindowExW(
            0, L"EDIT", initialTitle.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 24,
            hwnd, (HMENU)1001, hInst, nullptr
        );
        SendMessageW(hEditTitle, EM_SETLIMITTEXT, 80, 0);

        std::wstring bodyInit = NormalizeCrlfForEdit(initialContent);
        hEditBody = CreateWindowExW(
            0, L"EDIT", bodyInit.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 100, 100,
            hwnd, (HMENU)1002, hInst, nullptr
        );
        SendMessageW(hEditBody, EM_SETLIMITTEXT, 65535, 0);
        bodyScroll.Attach(hwnd, hEditBody);

        RebuildEditFont();
        LayoutChildControls();
    }

    void EnsureD2D() {
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureD2D();
        if (rt && brush) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            float ch = (float)(crc.bottom - crc.top) / dpiScale;

            rt->BeginDraw();
            rt->Clear(Rgba(0x11141C, 1.0f));

            // Heading
            DrawTextHex(rt.Get(), brush.Get(), fmtHeading.Get(), D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f),
                        heading.c_str(), (UINT32)heading.size(), Theme::TextPrimary);

            // Title input label & live char count
            size_t titleChars = hEditTitle ? CountDisplayChars(GetWndText(hEditTitle)) : 0;
            size_t bodyChars  = hEditBody  ? CountDisplayChars(GetWndText(hEditBody))  : 0;
            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 48.0f, cw - 92.0f, 68.0f),
                          L"\x6807\x9898\xFF08\x9009\x586B\xFF0C\x7559\x7A7A\x81EA\x52A8\x53D6\x6B63\x6587\x9996\x884C\xFF09", Theme::TextSecondary);
            wchar_t tcBuf[32]{};
            wsprintfW(tcBuf, L"%u / 80 \u5B57", (unsigned)titleChars);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtCount.Get(), D2D1::RectF(cw - 92.0f, 48.0f, cw - 20.0f, 68.0f),
                          tcBuf, titleChars > 12 ? 0xFBBF24 : Theme::TextMuted, 0.92f);
            DrawInputBox(rt.Get(), brush.Get(), D2D1::RectF(20.0f, 72.0f, cw - 20.0f, 110.0f), 7.0f, titleFocusAnim, false);

            // Body input label & live char count
            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 118.0f, cw - 110.0f, 138.0f),
                          L"Prompt \x6B63\x6587\xFF08\x5DE6\x952E\x70B9\x51FB\x69FD\x4F4D\x65F6\x5B8C\x6574\x590D\x5236\xFF09", Theme::TextSecondary);
            wchar_t bcBuf[48]{};
            wsprintfW(bcBuf, L"\u5171 %u \u5B57", (unsigned)bodyChars);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtCount.Get(), D2D1::RectF(cw - 110.0f, 118.0f, cw - 20.0f, 138.0f),
                          bcBuf, Theme::TextMuted, 0.92f);
            ULONGLONG nowT = GetTickCount64();
            float bodyShakeX = Motion::RejectShakeOffset(bodyShakeTick, nowT, 260, 4.0f);
            bool bodyErrRim = (bodyShakeTick != 0 && (nowT - bodyShakeTick) < 500);
            DrawInputBox(rt.Get(), brush.Get(), D2D1::RectF(20.0f + bodyShakeX, 140.0f, cw - 20.0f + bodyShakeX, ch - 60.0f),
                         7.0f, bodyFocusAnim, bodyErrRim);
            bodyScroll.Draw(rt.Get(), brush.Get());

            // Hint or shortcut tip on bottom left
            const wchar_t* footRaw = errorHint.empty()
                ? L"\u63D0\u793A\uFF1A\u6309 Ctrl+Enter \u5FEB\u901F\u4FDD\u5B58\uFF0CEsc \u53D6\u6D88"
                : errorHint.c_str();
            DrawUiTextHex(rt.Get(), brush.Get(), fmtHint.Get(), D2D1::RectF(20.0f, ch - 46.0f, rcCancelBtn.left - 10.0f, ch - 14.0f),
                          footRaw, errorHint.empty() ? Theme::TextMuted : Theme::DangerRed, errorHint.empty() ? 0.9f : 0.95f);

            // Cancel & Save buttons
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcCancelBtn, L"\x53D6\x6D88",
                               hoverBtn == 1, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.12f, pressedBtn == 1);
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcSaveBtn, L"\x4FDD\x5B58",
                               hoverBtn == 2, 0x3B7BF6, 0x4F8EF7, 0xFFFFFF, 0.28f, pressedBtn == 2);

            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                rt.Reset();
            }
        }
        EndPaint(hwnd, &ps);
    }

    static bool PtInDip(const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    static LRESULT CALLBACK DlgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        PromptEditorDialog* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<PromptEditorDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<PromptEditorDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(241, 245, 249));
                SetBkColor(hdc, RGB(27, 31, 43));
                return (LRESULT)self->hBrushEditBg;
            }
            case WM_COMMAND: {
                WORD id = LOWORD(wp);
                WORD code = HIWORD(wp);
                if (id == 1001 || id == 1002) {
                    if (code == EN_SETFOCUS) {
                        self->focusedEdit = (id == 1001) ? 1 : 2;
                        if (Motion::IsReducedMotion()) {
                            self->titleFocusAnim = (self->focusedEdit == 1) ? 1.0f : 0.0f;
                            self->bodyFocusAnim  = (self->focusedEdit == 2) ? 1.0f : 0.0f;
                        } else {
                            self->EnsureAnimTimer();
                        }
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        if (self->focusedEdit == ((id == 1001) ? 1 : 2)) {
                            self->focusedEdit = 0;
                            if (Motion::IsReducedMotion()) {
                                self->titleFocusAnim = 0.0f;
                                self->bodyFocusAnim = 0.0f;
                            } else {
                                self->EnsureAnimTimer();
                            }
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    } else if (code == EN_CHANGE) {
                        self->dirtyConfirmArmed = false;
                        self->errorHint.clear();
                        if (id == 1002) {
                            self->bodyScroll.SyncAndNotifyParent(false);
                        }
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (id == 1002 && (code == EN_UPDATE || code == EN_VSCROLL)) {
                        self->bodyScroll.SyncAndNotifyParent(false);
                    }
                }
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                float sc = GetDpiScaleForWindow(h);
                mmi->ptMinTrackSize.x = (LONG)(420.0f * sc);
                mmi->ptMinTrackSize.y = (LONG)(340.0f * sc);
                return 0;
            }
            case WM_SIZE:
                if (self->rt) {
                    self->rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                }
                self->LayoutChildControls();
                self->bodyScroll.SyncAndNotifyParent(true);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            case WM_TIMER:
                if (wp == kTimerAnim) self->OnAnimTick();
                return 0;
            case WM_PAINT:
                self->OnPaint();
                return 0;
            case WM_MOUSEWHEEL: {
                POINT spt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                if (self->bodyScroll.OnParentMouseWheel(wp, spt)) return 0;
                break;
            }
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverBtn != 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedBtn = 0;
                if (self->bodyScroll.OnParentLButtonDown(x, y)) return 0;
                if (PtInDip(self->rcCancelBtn, x, y)) {
                    self->pressedBtn = 1;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                } else if (PtInDip(self->rcSaveBtn, x, y)) {
                    self->pressedBtn = 2;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                if (self->bodyScroll.OnParentMouseMove(x, y)) {
                    if (self->hoverBtn != 0) {
                        self->hoverBtn = 0;
                        InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
                int nh = 0;
                if (PtInDip(self->rcCancelBtn, x, y)) nh = 1;
                else if (PtInDip(self->rcSaveBtn, x, y)) nh = 2;
                if (nh != self->hoverBtn) {
                    self->hoverBtn = nh;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE: {
                self->trackingLeave = false;
                self->bodyScroll.OnParentMouseLeave();
                if (self->hoverBtn != 0) {
                    self->hoverBtn = 0;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (self->bodyScroll.OnParentLButtonUp()) return 0;
                int downBtn = self->pressedBtn;
                self->pressedBtn = 0;
                if (GetCapture() == h) ReleaseCapture();
                if (downBtn == 0) return 0;
                InvalidateRect(h, nullptr, FALSE);
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                if (downBtn == 1 && PtInDip(self->rcCancelBtn, x, y)) self->RequestCancel();
                else if (downBtn == 2 && PtInDip(self->rcSaveBtn, x, y)) self->TryCommit();
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (self->pressedBtn != 0) { self->pressedBtn = 0; InvalidateRect(h, nullptr, FALSE); }
                self->bodyScroll.OnParentCaptureChanged();
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset();
                self->rt.Reset();
                auto* suggested = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->RebuildEditFont();
                self->LayoutChildControls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_CLOSE:
                self->RequestCancel();
                return 0;
            case WM_DESTROY:
                self->bodyScroll.Detach();
                self->done = true;
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                self->hwnd = nullptr;
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// ============================================================================
// Modal Dark Confirmation Card (Used for Destructive/Replacement Actions like JSON Import)
// ============================================================================
class DarkConfirmDialog {
public:
    static bool Show(HINSTANCE hInst, HWND parent, const std::wstring& title,
                     const std::wstring& body, const std::wstring& confirmLabel = L"确认",
                     const std::wstring& cancelLabel = L"取消", bool isDanger = false) {
        DarkConfirmDialog dlg(hInst, title, body, confirmLabel, cancelLabel, isDanger);
        dlg.RunModal(parent);
        return dlg.accepted;
    }

private:
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtBody;
    ComPtr<IDWriteTextFormat> fmtBtn;

    std::wstring heading;
    std::wstring bodyText;
    std::wstring confirmText;
    std::wstring cancelText;
    bool dangerMode = false;
    float dpiScale = 1.0f;
    int hoverBtn = 0;   // 1 cancel, 2 confirm
    int pressedBtn = 0; // 1 cancel, 2 confirm
    int focusBtn = 1;   // 1 cancel (default on danger), 2 confirm (default on normal)
    bool kbFocusMoved = false;
    Spring1D focusXSpring = Spring1D::Snappy(0.0f);
    Spring1D focusWSpring = Spring1D::Snappy(82.0f);
    bool focusRingInit = false;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;
    bool trackingLeave = false;
    bool done = false;
    bool accepted = false;

    static constexpr UINT_PTR kTimerAnim = 1201;

    D2D1_RECT_F rcCancelBtn{};
    D2D1_RECT_F rcConfirmBtn{};

    void SyncFocusRing(bool snap) {
        const D2D1_RECT_F& fr = (focusBtn == 1) ? rcCancelBtn : rcConfirmBtn;
        if (fr.right <= fr.left) return;
        if (snap || !focusRingInit || Motion::IsReducedMotion()) {
            focusXSpring.Snap(fr.left - 2.0f);
            focusWSpring.Snap((fr.right - fr.left) + 4.0f);
            focusRingInit = true;
        } else {
            focusXSpring.target = fr.left - 2.0f;
            focusWSpring.target = (fr.right - fr.left) + 4.0f;
            if (hwnd && !animActive) {
                if (qpcFreq.QuadPart == 0) QueryPerformanceFrequency(&qpcFreq);
                QueryPerformanceCounter(&lastAnimQpc);
                if (SetTimer(hwnd, kTimerAnim, 10, nullptr) != 0) animActive = true;
            }
        }
    }

    DarkConfirmDialog(HINSTANCE inst, const std::wstring& hd, const std::wstring& body,
                      const std::wstring& okLbl, const std::wstring& cancelLbl, bool isDanger)
        : hInst(inst), heading(UiText(hd)), bodyText(UiText(body)),
          confirmText(UiText(okLbl)), cancelText(UiText(cancelLbl)), dangerMode(isDanger),
          focusBtn(isDanger ? 1 : 2) {}

    static bool PtInDip(const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    void EnsureD2D() {
        if (!fmtHeading) {
            auto& eng = D2DEngine::Instance();
            fmtHeading = eng.CreateFormat(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtBody    = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_LEADING);
            fmtBtn     = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            if (fmtBody) {
                fmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                fmtBody->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            }
        }
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    void LayoutButtons() {
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float ch = (float)(crc.bottom - crc.top) / dpiScale;
        rcConfirmBtn = D2D1::RectF(cw - 118.0f, ch - 48.0f, cw - 20.0f, ch - 16.0f);
        rcCancelBtn  = D2D1::RectF(cw - 210.0f, ch - 48.0f, cw - 128.0f, ch - 16.0f);
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureD2D();
        if (rt && brush) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            float ch = (float)(crc.bottom - crc.top) / dpiScale;
            LayoutButtons();

            rt->BeginDraw();
            rt->Clear(Rgba(0x11141C, 1.0f));

            DrawTextHex(rt.Get(), brush.Get(), fmtHeading.Get(), D2D1::RectF(20.0f, 16.0f, cw - 20.0f, 44.0f),
                        heading.c_str(), (UINT32)heading.size(), Theme::TextPrimary);

            D2D1_RECT_F infoBox = D2D1::RectF(20.0f, 52.0f, cw - 20.0f, ch - 64.0f);
            FillRMRimHex(rt.Get(), brush.Get(), infoBox, 8.0f, Theme::CardBg, 1.0f,
                         dangerMode ? Theme::DangerRed : Theme::AccentCyan, dangerMode ? 0.42f : 0.28f, 0xFFFFFF, 0.05f);
            DrawTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                        D2D1::RectF(infoBox.left + 14.0f, infoBox.top + 12.0f, infoBox.right - 14.0f, infoBox.bottom - 10.0f),
                        bodyText.c_str(), (UINT32)bodyText.size(), Theme::TextSecondary, 0.96f);

            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcCancelBtn, cancelText.c_str(),
                               hoverBtn == 1, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.14f, pressedBtn == 1);
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcConfirmBtn, confirmText.c_str(),
                               hoverBtn == 2,
                               dangerMode ? 0xB91C1C : 0x3B7BF6,
                               dangerMode ? 0xDC2626 : 0x4F8EF7,
                               0xFFFFFF, 0.28f, pressedBtn == 2);
            if (focusBtn == 1 || focusBtn == 2) {
                if (!focusRingInit) SyncFocusRing(true);
                const D2D1_RECT_F& fr = (focusBtn == 1) ? rcCancelBtn : rcConfirmBtn;
                D2D1_RECT_F ring = D2D1::RectF(focusXSpring.value, fr.top - 2.0f, focusXSpring.value + focusWSpring.value, fr.bottom + 2.0f);
                rt->DrawRoundedRectangle(D2D1::RoundedRect(ring, 7.5f, 7.5f),
                                         Br(Rgba(focusBtn == 2 && dangerMode ? Theme::DangerRed : Theme::AccentCyan, 0.76f)), 1.4f);
            }

            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                rt.Reset();
            }
        }
        EndPaint(hwnd, &ps);
    }

    void Finish(bool ok) {
        accepted = ok;
        done = true;
        DestroyWindow(hwnd);
    }

    void RunModal(HWND parent) {
        hwnd = CreateDarkModalWindow(hInst, L"PromptPod_ConfirmDlg", DlgWndProc, heading.c_str(),
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                     parent, 456.0f, 244.0f, dpiScale, this);
        if (!hwnd) return;
        LayoutButtons();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) {
                gotQuit = true;
                quitCode = msg.wParam;
                break;
            }
            if (gm < 0) break;

            bool isOwnMsg = (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd));
            if (isOwnMsg && msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    Finish(false);
                    continue;
                }
                if (msg.wParam == VK_TAB || msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) {
                    focusBtn = (focusBtn == 1) ? 2 : 1;
                    kbFocusMoved = true;
                    SyncFocusRing(false);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    continue;
                }
                if (msg.wParam == VK_SPACE) {
                    if (focusBtn == 1) Finish(false);
                    else if (!dangerMode || kbFocusMoved) Finish(true);
                    continue;
                }
                // Never allow accidental bare Enter to execute destructive confirmation unless user explicitly navigated to Confirm!
                if (msg.wParam == VK_RETURN) {
                    if (!dangerMode || kbFocusMoved) Finish(focusBtn == 2);
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        EndDarkModalWindow(hwnd, parent, gotQuit, quitCode);
    }

    static LRESULT CALLBACK DlgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        DarkConfirmDialog* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<DarkConfirmDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DarkConfirmDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_TIMER:
                if (wp == kTimerAnim) {
                    float dt = StepDeltaQpc(self->qpcFreq, self->lastAnimQpc);
                    bool m1 = self->focusXSpring.Step(dt, 0.15f, 1.0f);
                    bool m2 = self->focusWSpring.Step(dt, 0.15f, 1.0f);
                    InvalidateRect(h, nullptr, FALSE);
                    if (!m1 && !m2) {
                        KillTimer(h, kTimerAnim);
                        self->animActive = false;
                    }
                }
                return 0;
            case WM_PAINT:
                self->OnPaint();
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverBtn != 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedBtn = 0;
                if (PtInDip(self->rcCancelBtn, x, y)) {
                    self->pressedBtn = 1;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                } else if (PtInDip(self->rcConfirmBtn, x, y)) {
                    self->pressedBtn = 2;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                int nh = 0;
                if (PtInDip(self->rcCancelBtn, x, y)) nh = 1;
                else if (PtInDip(self->rcConfirmBtn, x, y)) nh = 2;
                if (nh != self->hoverBtn) {
                    self->hoverBtn = nh;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->trackingLeave = false;
                if (self->hoverBtn != 0) {
                    self->hoverBtn = 0;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONUP: {
                int downBtn = self->pressedBtn;
                self->pressedBtn = 0;
                if (GetCapture() == h) ReleaseCapture();
                if (downBtn == 0) return 0;
                InvalidateRect(h, nullptr, FALSE);
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                if (downBtn == 1 && PtInDip(self->rcCancelBtn, x, y)) self->Finish(false);
                else if (downBtn == 2 && PtInDip(self->rcConfirmBtn, x, y)) self->Finish(true);
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (self->pressedBtn != 0) { self->pressedBtn = 0; InvalidateRect(h, nullptr, FALSE); }
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset();
                self->rt.Reset();
                auto* suggested = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutButtons();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_CLOSE:
                self->Finish(false);
                return 0;
            case WM_DESTROY:
                self->done = true;
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                self->hwnd = nullptr;
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// ============================================================================
// Modal Dark Scene Rename Dialog (Live 8 Visual-Width-Unit Validator + Native IME)
// ============================================================================
class SceneRenameDialog {
public:
    using CommitCallback = std::function<bool(const std::wstring& newName, std::wstring& outErr)>;

    static bool Show(HINSTANCE hInst, HWND parent, int sceneIdx, const std::wstring& currentName, CommitCallback onCommit) {
        SceneRenameDialog dlg(hInst, sceneIdx, currentName, std::move(onCommit));
        dlg.RunModal(parent);
        return dlg.accepted;
    }

private:
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HWND hEdit = nullptr;
    HFONT hFont = nullptr;
    HBRUSH hBrushBg = nullptr;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtLabel;
    ComPtr<IDWriteTextFormat> fmtCount;
    ComPtr<IDWriteTextFormat> fmtBtn;

    int sceneIndex = 0;
    std::wstring initName;
    std::wstring errorHint;
    CommitCallback commitCb;
    float dpiScale = 1.0f;
    int hoverBtn = 0;
    int pressedBtn = 0;
    bool focused = false;
    float focusAnim = 0.0f;
    ULONGLONG errShakeTick = 0;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;
    bool trackingLeave = false;
    bool done = false;
    bool accepted = false;
    D2D1_RECT_F rcCancel{};
    D2D1_RECT_F rcSave{};

    static constexpr UINT_PTR kTimerAnim = 1301;

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            focusAnim = focused ? 1.0f : 0.0f;
            errShakeTick = 0;
            if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    SceneRenameDialog(HINSTANCE inst, int idx, const std::wstring& cur, CommitCallback cb)
        : hInst(inst), sceneIndex(idx), initName(cur), commitCb(std::move(cb)) {}

    ~SceneRenameDialog() {
        if (hFont) DeleteObject(hFont);
        if (hBrushBg) DeleteObject(hBrushBg);
    }

    std::wstring GetEditText() const {
        if (!hEdit) return {};
        int len = GetWindowTextLengthW(hEdit);
        if (len <= 0) return {};
        std::wstring s((size_t)len + 1, L'\0');
        int n = GetWindowTextW(hEdit, s.data(), len + 1);
        s.resize(n > 0 ? (size_t)n : 0);
        return s;
    }

    void TryCommit() {
        std::wstring raw = GetEditText();
        std::wstring trimmed = TrimSceneName(raw);
        if (trimmed.empty()) {
            errorHint = UiText(L"场景名称不能为空");
            errShakeTick = GetTickCount64();
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
            SetFocus(hEdit);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        int units = VisualWidthUnits(trimmed);
        if (units > kMaxSceneNameWidthUnits) {
            errorHint = UiText(L"名称过长（最多 4 个汉字或 8 个英文），请精简");
            errShakeTick = GetTickCount64();
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
            SetFocus(hEdit);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (commitCb) {
            std::wstring err;
            if (!commitCb(trimmed, err)) {
                errorHint = UiText(err.empty() ? L"保存失败：无法写入数据文件" : err);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        accepted = true;
        done = true;
        DestroyWindow(hwnd);
    }

    void Layout() {
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float ch = (float)(crc.bottom - crc.top) / dpiScale;
        if (hEdit) {
            SetWindowPos(hEdit, nullptr, (int)(26.0f * dpiScale), (int)(80.0f * dpiScale),
                         std::max(80, (int)((cw - 52.0f) * dpiScale)), (int)(26.0f * dpiScale),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        rcSave   = D2D1::RectF(cw - 104.0f, ch - 46.0f, cw - 20.0f, ch - 14.0f);
        rcCancel = D2D1::RectF(cw - 196.0f, ch - 46.0f, cw - 114.0f, ch - 14.0f);
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (!fmtHeading) {
            auto& eng = D2DEngine::Instance();
            fmtHeading = eng.CreateFormat(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtLabel   = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_MEDIUM);
            fmtCount   = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmtBtn     = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
        if (rt && brush) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            Layout();

            std::wstring trimmed = TrimSceneName(GetEditText());
            int units = VisualWidthUnits(trimmed);
            bool overLimit = (units == 0 || units > kMaxSceneNameWidthUnits);

            rt->BeginDraw();
            rt->Clear(Rgba(0x11141C, 1.0f));

            wchar_t hdBuf[48]{}, cntBuf[32]{};
            wsprintfW(hdBuf, L"重命名场景 %d", sceneIndex + 1);
            wsprintfW(cntBuf, L"已用 %d / %d", units, kMaxSceneNameWidthUnits);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtHeading.Get(), D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f), hdBuf, Theme::TextPrimary);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 48.0f, cw - 92.0f, 68.0f),
                          L"短名称（最多 4 个汉字或 8 个英文字母）", Theme::TextSecondary);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtCount.Get(), D2D1::RectF(cw - 94.0f, 48.0f, cw - 20.0f, 68.0f),
                          cntBuf, overLimit ? Theme::DangerRed : Theme::AccentCyan, 0.96f);

            float shakeX = Motion::RejectShakeOffset(errShakeTick, GetTickCount64(), 260, 4.0f);
            DrawInputBox(rt.Get(), brush.Get(), D2D1::RectF(20.0f + shakeX, 73.0f, cw - 20.0f + shakeX, 111.0f), 7.0f, focusAnim, overLimit);

            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 118.0f, cw - 20.0f, 142.0f),
                          errorHint.empty() ? L"示例：通用、产品、开发、写作、Coding（Enter 保存）" : errorHint.c_str(),
                          errorHint.empty() ? Theme::TextMuted : Theme::DangerRed, errorHint.empty() ? 0.92f : 0.96f);

            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcCancel, L"取消",
                               hoverBtn == 1, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.12f, pressedBtn == 1);
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcSave, L"保存",
                               !overLimit && hoverBtn == 2,
                               overLimit ? 0x1C2230 : 0x3B7BF6,
                               overLimit ? 0x1C2230 : 0x4F8EF7,
                               overLimit ? Theme::TextMuted : 0xFFFFFF,
                               overLimit ? 0.08f : 0.28f, !overLimit && pressedBtn == 2);

            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) { brush.Reset(); rt.Reset(); }
        }
        EndPaint(hwnd, &ps);
    }

    void RunModal(HWND parent) {
        hwnd = CreateDarkModalWindow(hInst, L"PromptPod_SceneRenameDlg", DlgProc, L"重命名场景",
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                     parent, 416.0f, 228.0f, dpiScale, this);
        if (!hwnd) return;
        hBrushBg = CreateSolidBrush(RGB(27, 31, 43));
        hFont = CreateUiInputFont(13.5f, dpiScale);
        hEdit = CreateWindowExW(0, L"EDIT", initName.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 100, 24, hwnd, (HMENU)3001, hInst, nullptr);
        if (hEdit) {
            if (hFont) SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hEdit, EM_SETLIMITTEXT, 16, 0);
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
        }
        Layout();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hEdit);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) { gotQuit = true; quitCode = msg.wParam; break; }
            if (gm < 0) break;
            if ((msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) && msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) { done = true; DestroyWindow(hwnd); continue; }
                if (msg.wParam == VK_RETURN) { TryCommit(); continue; }
                if (msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && msg.hwnd == hEdit) {
                    SendMessageW(hEdit, EM_SETSEL, 0, -1);
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        EndDarkModalWindow(hwnd, parent, gotQuit, quitCode);
    }

    static bool PtIn(const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    static LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        SceneRenameDialog* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = static_cast<SceneRenameDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SceneRenameDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wp, RGB(241, 245, 249));
                SetBkColor((HDC)wp, RGB(27, 31, 43));
                return (LRESULT)self->hBrushBg;
            case WM_COMMAND:
                if (LOWORD(wp) == 3001) {
                    if (HIWORD(wp) == EN_SETFOCUS) {
                        self->focused = true;
                        if (Motion::IsReducedMotion()) self->focusAnim = 1.0f; else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (HIWORD(wp) == EN_KILLFOCUS) {
                        self->focused = false;
                        if (Motion::IsReducedMotion()) self->focusAnim = 0.0f; else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (HIWORD(wp) == EN_CHANGE) {
                        self->errorHint.clear();
                        InvalidateRect(h, nullptr, FALSE);
                    }
                }
                return 0;
            case WM_TIMER:
                if (wp == kTimerAnim) {
                    float dt = StepDeltaQpc(self->qpcFreq, self->lastAnimQpc);
                    bool mF = Motion::ExpApproach(self->focusAnim, self->focused ? 1.0f : 0.0f, 22.0f, dt);
                    bool sh = (!Motion::IsReducedMotion() && self->errShakeTick != 0 && (GetTickCount64() - self->errShakeTick) < 270);
                    if (!sh) self->errShakeTick = 0;
                    InvalidateRect(h, nullptr, FALSE);
                    if (!mF && !sh) {
                        KillTimer(h, kTimerAnim);
                        self->animActive = false;
                    }
                }
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverBtn != 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_PAINT: self->OnPaint(); return 0;
            case WM_LBUTTONDOWN: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale, y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedBtn = PtIn(self->rcCancel, x, y) ? 1 : (PtIn(self->rcSave, x, y) ? 2 : 0);
                if (self->pressedBtn) { SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) { TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 }; if (TrackMouseEvent(&tme)) self->trackingLeave = true; }
                float x = (float)(short)LOWORD(lp) / self->dpiScale, y = (float)(short)HIWORD(lp) / self->dpiScale;
                int nh = PtIn(self->rcCancel, x, y) ? 1 : (PtIn(self->rcSave, x, y) ? 2 : 0);
                if (nh != self->hoverBtn) { self->hoverBtn = nh; InvalidateRect(h, nullptr, FALSE); }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->trackingLeave = false;
                if (self->hoverBtn != 0) { self->hoverBtn = 0; InvalidateRect(h, nullptr, FALSE); }
                return 0;
            case WM_LBUTTONUP: {
                int down = self->pressedBtn; self->pressedBtn = 0;
                if (GetCapture() == h) ReleaseCapture();
                if (down != 0) InvalidateRect(h, nullptr, FALSE);
                float x = (float)(short)LOWORD(lp) / self->dpiScale, y = (float)(short)HIWORD(lp) / self->dpiScale;
                if (down == 1 && PtIn(self->rcCancel, x, y)) { self->done = true; DestroyWindow(h); }
                else if (down == 2 && PtIn(self->rcSave, x, y)) self->TryCommit();
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (self->pressedBtn != 0) { self->pressedBtn = 0; InvalidateRect(h, nullptr, FALSE); }
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset(); self->rt.Reset();
                auto* sug = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
                HFONT nf = CreateUiInputFont(13.5f, self->dpiScale);
                if (nf) {
                    if (self->hEdit) SendMessageW(self->hEdit, WM_SETFONT, (WPARAM)nf, TRUE);
                    if (self->hFont) DeleteObject(self->hFont);
                    self->hFont = nf;
                }
                self->Layout();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_CLOSE: self->done = true; DestroyWindow(h); return 0;
            case WM_DESTROY: self->done = true; return 0;
            case WM_NCDESTROY: SetWindowLongPtrW(h, GWLP_USERDATA, 0); self->hwnd = nullptr; return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// ============================================================================
// Modal Dual-Pane Scene Workbench (1:1 Dock 10-Slot Deck + Filterable Vault Picker)
// ============================================================================
class ScenePromptsDialog {
public:
    using MutatedFn = void(*)(void*);

    enum class PickerFilter : int {
        NotInCurrent = 0, // Exclude prompts already mounted in current scene
        UnmountedAll = 1, // Exclude prompts mounted in ANY scene (unassigned inventory only)
        All          = 2  // Show all vault prompts with scene membership badges
    };

    [[gnu::noinline]] static std::vector<size_t> FilterVaultCandidates(
        const AppState& st, int sceneIdx, PickerFilter filter, const std::wstring& query) {
        std::vector<size_t> out;
        out.reserve(st.vault.size());
        for (size_t i = 0; i < st.vault.size(); ++i) {
            const auto& item = st.vault[i];
            if (filter == PickerFilter::NotInCurrent) {
                if (st.FindSlotOfIdInScene(sceneIdx, item.id) >= 0) continue;
            } else if (filter == PickerFilter::UnmountedAll) {
                if (st.IsMountedInAnyScene(item.id)) continue;
            }
            if (!query.empty() &&
                !ContainsIgnoreCaseStr(item.title, query) &&
                !ContainsIgnoreCaseStr(item.content, query)) {
                continue;
            }
            out.push_back(i);
        }
        return out;
    }

    [[gnu::noinline]] static void CountFilterBuckets(
        const AppState& st, int sceneIdx, int& outNotInCur, int& outUnmountedAll, int& outAll) {
        outNotInCur = 0;
        outUnmountedAll = 0;
        outAll = (int)st.vault.size();
        for (const auto& item : st.vault) {
            if (st.FindSlotOfIdInScene(sceneIdx, item.id) < 0) ++outNotInCur;
            if (!st.IsMountedInAnyScene(item.id)) ++outUnmountedAll;
        }
    }

    [[gnu::noinline]] static void Show(HINSTANCE hInst, HWND parent, AppState* st, StorageManager* sm,
                                       DarkPopupMenu* menu, int sceneIdx, MutatedFn onMutated, void* mutatedCtx) {
        if (!st || !sm || sceneIdx < 0 || sceneIdx >= kMaxScenes) return;
        ScenePromptsDialog dlg(hInst, st, sm, menu, sceneIdx, onMutated, mutatedCtx);
        dlg.RunModal(parent);
    }

private:
    struct Zone {
        // 1:NewPrompt, 2:Done, 3:ToggleSceneEnabled, 4:RenameScene, 5:ClearScene, 6:UndoClear,
        // 7:ClearSearch, 8:ClearTargetSlot, 9:NewFromSearch,
        // 20..23:SwitchScene(0..3), 30..32:Filter(NotInCur/UnmountedAll/All),
        // 40..49:LeftSlotBody(0..9), 50..59:LeftSlotEdit(0..9), 60..69:LeftSlotRemove(0..9),
        // 70:RightCardAction(pid), 71:RightCardEdit(pid)
        int kind = 0;
        std::wstring pid;
        D2D1_RECT_F rect{};
    };

    static constexpr float kTopBarH     = 58.0f;
    static constexpr float kFooterH     = 46.0f;
    static constexpr float kLeftPaneX   = 18.0f;
    static constexpr float kLeftPaneW   = 296.0f;
    static constexpr float kRightPaneX  = 328.0f;
    static constexpr float kSlotH       = 36.0f;
    static constexpr float kSlotGap     = 4.0f;
    static constexpr float kSlotStride  = kSlotH + kSlotGap;
    static constexpr UINT_PTR kTimerAnim       = 1401;
    static constexpr UINT_PTR kTimerUndoExpire = 1402;
    static constexpr const wchar_t* kSearchProp = L"PP_ScenePickerSearch";

    HINSTANCE hInst = nullptr;
    AppState* state = nullptr;
    StorageManager* storage = nullptr;
    DarkPopupMenu* popupMenu = nullptr;
    int sceneIndex = 0;
    MutatedFn onMutated = nullptr;
    void* mutatedCtx = nullptr;
    HWND hwnd = nullptr;
    HWND hSearchEdit = nullptr;
    HFONT hSearchFont = nullptr;
    HBRUSH hSearchBrush = nullptr;
    WNDPROC origSearchProc = nullptr;

    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtTab;
    ComPtr<IDWriteTextFormat> fmtTitle;
    ComPtr<IDWriteTextFormat> fmtBody;
    ComPtr<IDWriteTextFormat> fmtBadge;
    ComPtr<IDWriteTextFormat> fmtBtn;

    PickerFilter pickerFilter = PickerFilter::NotInCurrent;
    std::wstring pickerQuery;
    bool searchFocused = false;
    float searchFocusAnim = 0.0f;
    float clearSearchAnim = 0.0f;
    int targetSlot = -1; // -1 = auto first empty, 0..9 = explicit slot target from left deck

    std::vector<size_t> filteredIndices;
    bool filterDirty = true;
    std::vector<Zone> zones;
    DarkViewportScrollbar scrollbar;

    float dpiScale = 1.0f;
    float scrollY = 0.0f;
    float scrollTargetY = 0.0f;
    float maxScrollY = 0.0f;
    float viewportH = 300.0f;

    float leftScrollY = 0.0f;
    float leftScrollTargetY = 0.0f;
    float leftMaxScrollY = 0.0f;
    float leftSlotsTopDip = 104.0f;
    D2D1_RECT_F leftSlotsVp{};

    int hoverZoneIdx = -1;
    int pressedKind = 0;
    std::wstring pressedPid;
    std::wstring statusText;
    bool statusIsErr = false;
    bool trackingLeave = false;
    bool done = false;

    // Left 10-slot transactional drag-to-reorder state (mirrors DockWindow!)
    int pressedSlot = -1;
    bool isSlotDragging = false;
    std::array<std::wstring, 10> slotDragOrigSlots{};
    float slotDragStartY = 0.0f;
    float slotDragGrabOffsetY = 0.0f;
    float slotDragCurY = 0.0f;
    std::array<Spring1D, 10> slotShiftSpring{};
    Spring1D dragLiftSpring = Spring1D::Snappy(0.0f);

    // 5-second inline undo for Clear Scene
    bool undoClearActive = false;
    int undoClearSceneIdx = -1;
    std::array<std::wstring, 10> undoClearSlots{};
    ULONGLONG undoClearExpireTick = 0;

    // Unified sliding indicators & micro-shake state
    Spring1D scenePillXSpring = Spring1D::Snappy(21.0f);
    Spring1D scenePillWSpring = Spring1D::Snappy(110.0f);
    bool scenePillInit = false;
    D2D1_COLOR_F sceneAccCol = Rgba(Theme::AccentCyan);
    D2D1_COLOR_F sceneAccTarget = Rgba(Theme::AccentCyan);

    Spring1D filterPillXSpring = Spring1D::Snappy(328.0f);
    Spring1D filterPillWSpring = Spring1D::Snappy(108.0f);
    bool filterPillInit = false;
    D2D1_COLOR_F filterAccCol = Rgba(Theme::AccentCyan);
    D2D1_COLOR_F filterAccTarget = Rgba(Theme::AccentCyan);

    ULONGLONG fullShakeTick = 0;
    ULONGLONG lockedShakeTick = 0;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;

    ScenePromptsDialog(HINSTANCE inst, AppState* st, StorageManager* sm, DarkPopupMenu* menu,
                       int sc, MutatedFn cb, void* ctx)
        : hInst(inst), state(st), storage(sm), popupMenu(menu), sceneIndex(sc), onMutated(cb), mutatedCtx(ctx) {
        for (auto& sp : slotShiftSpring) sp = Spring1D::Settle(0.0f);
        uint32_t acc = Theme::SceneTextAccent(sceneIndex);
        sceneAccCol = sceneAccTarget = Rgba(acc);
        filterAccCol = filterAccTarget = Rgba(acc);
    }

    ~ScenePromptsDialog() {
        DetachSearchControl();
        if (hSearchFont) DeleteObject(hSearchFont);
        if (hSearchBrush) DeleteObject(hSearchBrush);
    }

    void DetachSearchControl() {
        if (hSearchEdit && IsWindow(hSearchEdit) && origSearchProc) {
            SetWindowLongPtrW(hSearchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(origSearchProc));
            RemovePropW(hSearchEdit, kSearchProp);
        }
        origSearchProc = nullptr;
    }

    void NotifyMutated() {
        filterDirty = true;
        if (onMutated) onMutated(mutatedCtx);
    }

    void EnsureFiltered() {
        if (!filterDirty || !state) return;
        filteredIndices = FilterVaultCandidates(*state, sceneIndex, pickerFilter, pickerQuery);
        filterDirty = false;
    }

    static std::wstring FormatSlot2(int idx) {
        wchar_t b[8]{};
        wsprintfW(b, L"%02d", idx + 1);
        return b;
    }

    D2D1_RECT_F LeftSlotRectDip(int i) const {
        float top = leftSlotsTopDip + (float)i * kSlotStride - leftScrollY;
        float rightInset = (leftMaxScrollY > 0.5f) ? 10.0f : 8.0f;
        return D2D1::RectF(kLeftPaneX + 8.0f, top, kLeftPaneX + kLeftPaneW - rightInset, top + kSlotH);
    }

    void SnapAllAnims() {
        scrollY = scrollTargetY;
        leftScrollY = leftScrollTargetY;
        scrollbar.hoverAnim = (scrollbar.hovered || scrollbar.dragging) ? 1.0f : 0.0f;
        searchFocusAnim = searchFocused ? 1.0f : 0.0f;
        clearSearchAnim = pickerQuery.empty() ? 0.0f : 1.0f;
        scenePillXSpring.Snap(scenePillXSpring.target);
        scenePillWSpring.Snap(scenePillWSpring.target);
        sceneAccCol = sceneAccTarget;
        filterPillXSpring.Snap(filterPillXSpring.target);
        filterPillWSpring.Snap(filterPillWSpring.target);
        filterAccCol = filterAccTarget;
        dragLiftSpring.Snap(isSlotDragging ? 1.0f : 0.0f);
        for (auto& sp : slotShiftSpring) sp.Snap(0.0f);
        fullShakeTick = 0;
        lockedShakeTick = 0;
    }

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            SnapAllAnims();
            if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void StopAnimTimer() {
        if (animActive && hwnd && IsWindow(hwnd)) {
            KillTimer(hwnd, kTimerAnim);
            animActive = false;
        }
    }

    [[gnu::noinline]] void OnAnimTick() {
        float dt = StepDeltaQpc(qpcFreq, lastAnimQpc);
        bool moving = false;
        moving |= Motion::ExpApproach(scrollY, scrollTargetY, 20.0f, dt, 0.25f);
        moving |= Motion::ExpApproach(leftScrollY, leftScrollTargetY, 20.0f, dt, 0.25f);
        moving |= scrollbar.StepAnim(dt);
        moving |= Motion::ExpApproach(searchFocusAnim, searchFocused ? 1.0f : 0.0f, 22.0f, dt);
        moving |= Motion::ExpApproach(clearSearchAnim, pickerQuery.empty() ? 0.0f : 1.0f, 22.0f, dt);
        moving |= scenePillXSpring.Step(dt, 0.15f, 1.0f);
        moving |= scenePillWSpring.Step(dt, 0.15f, 1.0f);
        moving |= Motion::ApproachColorRgb(sceneAccCol, sceneAccTarget, 18.0f, dt);
        moving |= filterPillXSpring.Step(dt, 0.15f, 1.0f);
        moving |= filterPillWSpring.Step(dt, 0.15f, 1.0f);
        moving |= Motion::ApproachColorRgb(filterAccCol, filterAccTarget, 18.0f, dt);
        moving |= dragLiftSpring.Step(dt);
        for (auto& sp : slotShiftSpring) {
            moving |= sp.Step(dt, 0.15f, 1.2f);
        }
        ULONGLONG now = GetTickCount64();
        if (!Motion::IsReducedMotion() && fullShakeTick != 0) {
            if (now - fullShakeTick < 270) moving = true;
            else fullShakeTick = 0;
        }
        if (!Motion::IsReducedMotion() && lockedShakeTick != 0) {
            if (now - lockedShakeTick < 270) moving = true;
            else lockedShakeTick = 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        if (!moving) StopAnimTimer();
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    void EnsureResources() {
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
        if (!fmtHeading) {
            auto& eng = D2DEngine::Instance();
            fmtHeading = eng.CreateFormat(13.0f, DWRITE_FONT_WEIGHT_BOLD);
            fmtTab     = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtTitle   = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtBody    = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtBadge   = eng.CreateFormat(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtBtn     = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    void RebuildSearchFont() {
        HFONT nf = CreateUiInputFont(12.5f, dpiScale);
        if (nf) {
            if (hSearchEdit) SendMessageW(hSearchEdit, WM_SETFONT, (WPARAM)nf, TRUE);
            if (hSearchFont) DeleteObject(hSearchFont);
            hSearchFont = nf;
        }
    }

    void UpdateSearchCueBanner() {
        if (!hSearchEdit || !IsWindow(hSearchEdit) || !state) return;
        std::wstring cue;
        if (targetSlot >= 0 && targetSlot < 10) {
            cue = UiText(L"搜索要放入「" + state->scenes[(size_t)sceneIndex].name + L" · 槽位 " + FormatSlot2(targetSlot) + L"」的 Prompt...");
        } else {
            cue = UiText(L"搜索词舱标题或正文（加入「" + state->scenes[(size_t)sceneIndex].name + L"」）");
        }
        SendMessageW(hSearchEdit, 0x1501 /*EM_SETCUEBANNER*/, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
    }

    void LayoutSearchControl() {
        if (!hwnd || !hSearchEdit) return;
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float searchR = cw - 134.0f;
        float sx = kRightPaneX + 8.0f;
        float sy = kTopBarH + 14.0f;
        float sw = std::max(80.0f, (searchR - kRightPaneX) - 34.0f);
        SetWindowPos(hSearchEdit, nullptr,
                     (int)std::round(sx * dpiScale), (int)std::round(sy * dpiScale),
                     (int)std::round(sw * dpiScale), (int)std::round(21.0f * dpiScale),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SetupSearchControl() {
        dpiScale = GetDpiScaleForWindow(hwnd);
        if (!hSearchBrush) hSearchBrush = CreateSolidBrush(RGB(27, 31, 43));
        hSearchEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 180, 22,
            hwnd, (HMENU)4001, hInst, nullptr
        );
        if (hSearchEdit) {
            SetPropW(hSearchEdit, kSearchProp, this);
            origSearchProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hSearchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchSubProc)));
        }
        RebuildSearchFont();
        UpdateSearchCueBanner();
        LayoutSearchControl();
    }

    void ClearSearchQuery() {
        pickerQuery.clear();
        scrollY = scrollTargetY = 0.0f;
        filterDirty = true;
        if (hSearchEdit && IsWindow(hSearchEdit)) {
            SetWindowTextW(hSearchEdit, L"");
            SetFocus(hSearchEdit);
        }
        if (Motion::IsReducedMotion()) clearSearchAnim = 0.0f;
        else EnsureAnimTimer();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void CancelSlotDrag(bool rollback) {
        bool wasDragging = isSlotDragging;
        isSlotDragging = false;
        pressedSlot = -1;
        dragLiftSpring.Snap(0.0f);
        if (hwnd && GetCapture() == hwnd) ReleaseCapture();
        if (rollback && wasDragging && state) {
            state->scenes[(size_t)sceneIndex].slots = slotDragOrigSlots;
            for (auto& sp : slotShiftSpring) sp.Snap(0.0f);
        }
    }

    void SwitchScene(int nextIdx) {
        if (nextIdx < 0 || nextIdx >= kMaxScenes || nextIdx == sceneIndex) return;
        CancelSlotDrag(true);
        if (popupMenu && popupMenu->IsOpenFor(MenuOwner::Vault)) popupMenu->Close();
        sceneIndex = nextIdx;
        targetSlot = -1;
        scrollY = scrollTargetY = 0.0f;
        leftScrollY = leftScrollTargetY = 0.0f;
        for (auto& sp : slotShiftSpring) sp.Snap(0.0f);
        filterDirty = true;
        statusText.clear();
        statusIsErr = false;
        UpdateSearchCueBanner();
        if (!Motion::IsReducedMotion()) EnsureAnimTimer();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] int AddZone(int kind, const D2D1_RECT_F& r, const std::wstring& pid = L"") {
        int idx = (int)zones.size();
        zones.push_back(Zone{ kind, pid, r });
        return idx;
    }

    [[gnu::noinline]] int AddClippedZone(int kind, const D2D1_RECT_F& r, const D2D1_RECT_F& clip, const std::wstring& pid = L"") {
        D2D1_RECT_F cr = D2D1::RectF(
            std::max(r.left, clip.left), std::max(r.top, clip.top),
            std::min(r.right, clip.right), std::min(r.bottom, clip.bottom));
        if (cr.right <= cr.left || cr.bottom <= cr.top) return -1;
        return AddZone(kind, cr, pid);
    }

    void DrawBtn(const D2D1_RECT_F& r, const wchar_t* txt, int zIdx, uint32_t bg, uint32_t bgHov, uint32_t fg, float rimA = 0.14f, float alpha = 1.0f) {
        bool isPressed = (zIdx >= 0 && zIdx < (int)zones.size() &&
                          zones[(size_t)zIdx].kind == pressedKind &&
                          zones[(size_t)zIdx].pid == pressedPid);
        DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), r, txt, zIdx == hoverZoneIdx, bg, bgHov, fg, rimA, isPressed, alpha);
    }

    [[gnu::noinline]] void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureResources();
        if (rt && brush && state) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            float ch = (float)(crc.bottom - crc.top) / dpiScale;
            rt->BeginDraw();
            PaintContent(rt.Get(), brush.Get(), cw, ch);
            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                rt.Reset();
            }
        }
        EndPaint(hwnd, &ps);
    }

    [[gnu::noinline]] void PaintContent(ID2D1RenderTarget* rtPtr, ID2D1SolidColorBrush* brPtr, float cw, float ch) {
            struct RtWrap { ID2D1RenderTarget* p; ID2D1RenderTarget* Get() const { return p; } ID2D1RenderTarget* operator->() const { return p; } } rt{ rtPtr };
            struct BrWrap { ID2D1SolidColorBrush* p; ID2D1SolidColorBrush* Get() const { return p; } ID2D1SolidColorBrush* operator->() const { return p; } } brush{ brPtr };
            auto Br = [&](const D2D1_COLOR_F& c) -> ID2D1SolidColorBrush* { brush.p->SetColor(c); return brush.p; };
            auto DrawBtn = [&](const D2D1_RECT_F& r, const wchar_t* txt, int zIdx, uint32_t bg, uint32_t bgHov, uint32_t fg, float rimA = 0.14f, float alpha = 1.0f) {
                bool isPressed = (zIdx >= 0 && zIdx < (int)zones.size() &&
                                  zones[(size_t)zIdx].kind == pressedKind &&
                                  zones[(size_t)zIdx].pid == pressedPid);
                DrawDarkPillButton(rt.p, brush.p, fmtBtn.Get(), r, txt, zIdx == hoverZoneIdx, bg, bgHov, fg, rimA, isPressed, alpha);
            };
            zones.clear();
            ULONGLONG nowTick = GetTickCount64();

            rt->Clear(Rgba(0x0F1219, 1.0f));

            // === Top Bar: 4-Scene Segmented Switcher + Quick Scene Controls ===
            rt->FillRectangle(D2D1::RectF(0, 0, cw, kTopBarH), Br(Rgba(0x141822, 1.0f)));
            rt->DrawLine(D2D1::Point2F(0, kTopBarH), D2D1::Point2F(cw, kTopBarH), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);

            const auto& curSc = state->scenes[(size_t)sceneIndex];
            uint32_t curAccTop = Theme::AccentCyan, curAccBot = Theme::AccentIndigo;
            Theme::SceneAccentColors(sceneIndex, curAccTop, curAccBot);
            (void)curAccBot;
            uint32_t curTxtAcc = Theme::SceneTextAccent(sceneIndex);

            float rightToolsW = 212.0f;
            D2D1_RECT_F segBg = D2D1::RectF(18.0f, 12.0f, std::max(260.0f, cw - rightToolsW - 26.0f), 46.0f);
            FillRMRimHex(rt.Get(), brush.Get(), segBg, 8.0f, 0x0D0F15, 1.0f, 0xFFFFFF, 0.08f);

            float tabGap = 4.0f;
            float innerW = (segBg.right - segBg.left) - 6.0f - tabGap * (float)(kMaxScenes - 1);
            float tabW = std::max(44.0f, innerW / (float)kMaxScenes);
            D2D1_RECT_F scTabs[kMaxScenes]{};
            int scZones[kMaxScenes]{};
            for (int s = 0; s < kMaxScenes; ++s) {
                float lx = segBg.left + 3.0f + (float)s * (tabW + tabGap);
                float rx = (s == kMaxScenes - 1) ? (segBg.right - 3.0f) : (lx + tabW);
                scTabs[s] = D2D1::RectF(lx, segBg.top + 3.0f, rx, segBg.bottom - 3.0f);
                scZones[s] = AddZone(20 + s, scTabs[s]);
            }

            D2D1_RECT_F selScR = scTabs[sceneIndex];
            if (!scenePillInit || Motion::IsReducedMotion()) {
                scenePillXSpring.Snap(selScR.left);
                scenePillWSpring.Snap(selScR.right - selScR.left);
                sceneAccCol = sceneAccTarget = Rgba(curTxtAcc);
                scenePillInit = true;
            } else {
                scenePillXSpring.target = selScR.left;
                scenePillWSpring.target = selScR.right - selScR.left;
                sceneAccTarget = Rgba(curTxtAcc);
            }

            D2D1_RECT_F slideSc = D2D1::RectF(scenePillXSpring.value, selScR.top,
                                              scenePillXSpring.value + scenePillWSpring.value, selScR.bottom);
            FillRMRim(rt.Get(), brush.Get(), slideSc, 6.0f, Rgba(0x202A40, 1.0f),
                      D2D1::ColorF(sceneAccCol.r, sceneAccCol.g, sceneAccCol.b, 0.50f), Rgba(0xFFFFFF, 0.05f));

            for (int s = 0; s < kMaxScenes; ++s) {
                bool isSel = (s == sceneIndex);
                bool isHov = (scZones[s] == hoverZoneIdx);
                if (!isSel && isHov) {
                    FillRRHex(rt.Get(), brush.Get(), scTabs[s], 6.0f, 0xFFFFFF, 0.05f);
                }
                uint32_t sTop = Theme::AccentCyan, sBot = Theme::AccentIndigo;
                Theme::SceneAccentColors(s, sTop, sBot);
                (void)sBot;
                uint32_t sTxt = Theme::SceneTextAccent(s);
                bool sEn = state->scenes[(size_t)s].enabled;
                float cy = (scTabs[s].top + scTabs[s].bottom) * 0.5f;
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(scTabs[s].left + 11.0f, cy), 3.1f, 3.1f),
                                Br(Rgba(sTop, sEn ? 0.96f : 0.34f)));
                wchar_t lbl[48]{};
                _snwprintf_s(lbl, _countof(lbl), _TRUNCATE, L"%ls %d", state->scenes[(size_t)s].name.c_str(), state->CountMountedInScene(s));
                DrawUiTextHex(rt.Get(), brush.Get(), fmtTab.Get(),
                              D2D1::RectF(scTabs[s].left + 16.0f, scTabs[s].top, scTabs[s].right - 3.0f, scTabs[s].bottom),
                              lbl, isSel ? sTxt : (sEn ? Theme::TextSecondary : Theme::TextMuted),
                              isSel ? 1.0f : (isHov ? 0.96f : (sEn ? 0.86f : 0.62f)));
            }

            // Top-right scene actions: Enable/Hide | Rename | Clear
            float lockShake = Motion::RejectShakeOffset(lockedShakeTick, nowTick, 260, 3.8f);
            D2D1_RECT_F rcToggleEn = D2D1::RectF(cw - 224.0f + lockShake, 15.0f, cw - 144.0f + lockShake, 43.0f);
            D2D1_RECT_F rcRename   = D2D1::RectF(cw - 138.0f, 15.0f, cw - 80.0f, 43.0f);
            D2D1_RECT_F rcClear    = D2D1::RectF(cw - 74.0f,  15.0f, cw - 18.0f, 43.0f);
            int mountedCnt = state->CountMountedInScene(sceneIndex);
            int zTogEn = AddZone(3, rcToggleEn);
            int zRen   = AddZone(4, rcRename);
            int zClr   = (mountedCnt > 0) ? AddZone(5, rcClear) : -2;
            if (curSc.enabled) {
                DrawBtn(rcToggleEn, L"已启用", zTogEn, 0x16324A, 0x1E4262, Theme::AccentCyan, 0.28f);
            } else {
                DrawBtn(rcToggleEn, L"立即启用", zTogEn, 0x3D2B14, 0x52391A, 0xFCD34D, 0.34f);
            }
            DrawBtn(rcRename, L"重命名", zRen, 0x222736, 0x2D3448, Theme::TextSecondary, 0.12f);
            DrawBtn(rcClear,  L"清空",   zClr, 0x222736, 0x3A1E26, mountedCnt > 0 ? Theme::DangerRed : Theme::TextMuted,
                    mountedCnt > 0 ? 0.14f : 0.06f, mountedCnt > 0 ? 1.0f : 0.45f);

            // === Main Split Workspace ===
            float wsTop = kTopBarH + 8.0f;
            float wsBot = ch - kFooterH - 6.0f;

            // --- LEFT PANE: 10 Fixed Muscle-Memory Slots (1:1 Dock Mirror!) ---
            D2D1_RECT_F leftBox = D2D1::RectF(kLeftPaneX, wsTop, kLeftPaneX + kLeftPaneW, wsBot);
            FillRMRimHex(rt.Get(), brush.Get(), leftBox, 11.0f, 0x131722, 0.98f, curAccTop, 0.32f, 0xFFFFFF, 0.05f);

            // Left sub-header
            DrawUiTextHex(rt.Get(), brush.Get(), fmtHeading.Get(),
                          D2D1::RectF(leftBox.left + 12.0f, leftBox.top + 8.0f, leftBox.left + 116.0f, leftBox.top + 32.0f),
                          L"贴边 10 槽位", Theme::TextPrimary, 0.96f);

            float fullShake = Motion::RejectShakeOffset(fullShakeTick, nowTick, 260, 3.8f);
            D2D1_RECT_F cntPill = D2D1::RectF(leftBox.left + 110.0f + fullShake, leftBox.top + 10.0f,
                                              leftBox.left + 164.0f + fullShake, leftBox.top + 30.0f);
            bool isFull = (mountedCnt >= 10);
            wchar_t cntBuf[24]{};
            wsprintfW(cntBuf, L"%d / 10", mountedCnt);
            FillRRHex(rt.Get(), brush.Get(), cntPill, 5.0f, isFull ? 0xF59E0B : curAccTop, 0.16f);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), cntPill, cntBuf, isFull ? 0xFCD34D : curTxtAcc, 0.96f);

            if (targetSlot >= 0 && targetSlot < 10) {
                D2D1_RECT_F tgtPill = D2D1::RectF(leftBox.right - 98.0f, leftBox.top + 9.0f, leftBox.right - 10.0f, leftBox.top + 31.0f);
                int zClrTgt = AddZone(8, tgtPill);
                std::wstring tl = L"目标 " + FormatSlot2(targetSlot) + L" ×";
                DrawBtn(tgtPill, tl.c_str(), zClrTgt, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.35f);
            } else {
                DrawUiTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                              D2D1::RectF(leftBox.left + 170.0f, leftBox.top + 9.0f, leftBox.right - 10.0f, leftBox.top + 31.0f),
                              L"拖拽排序 · 右键", Theme::TextMuted, 0.82f);
            }

            // Left 10-slot scrollable viewport
            leftSlotsTopDip = leftBox.top + 38.0f;
            leftSlotsVp = D2D1::RectF(leftBox.left + 2.0f, leftSlotsTopDip - 2.0f, leftBox.right - 2.0f, leftBox.bottom - 6.0f);
            float leftVpH = std::max(60.0f, leftSlotsVp.bottom - leftSlotsVp.top);
            float leftFullH = 10.0f * kSlotH + 9.0f * kSlotGap + 4.0f;
            leftMaxScrollY = std::max(0.0f, leftFullH - leftVpH);
            leftScrollTargetY = std::clamp(leftScrollTargetY, 0.0f, leftMaxScrollY);
            leftScrollY = std::clamp(leftScrollY, 0.0f, leftMaxScrollY);

            rt->PushAxisAlignedClip(leftSlotsVp, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const auto& slots = state->scenes[(size_t)sceneIndex].slots;
            int passes = isSlotDragging ? 2 : 1;
            for (int pass = 0; pass < passes; ++pass) {
                for (int i = 0; i < 10; ++i) {
                    bool isDragged = (isSlotDragging && i == pressedSlot);
                    if (pass == 0 && isDragged) {
                        D2D1_RECT_F sr = LeftSlotRectDip(i);
                        if (sr.bottom >= leftSlotsVp.top && sr.top <= leftSlotsVp.bottom) {
                            FillRMRimHex(rt.Get(), brush.Get(), sr, 7.5f, 0x0C0F16, 0.78f,
                                         Theme::AccentCyan, 0.32f + 0.16f * dragLiftSpring.value, Theme::AccentIndigo, 0.18f);
                            std::wstring s2 = FormatSlot2(i);
                            DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(),
                                        D2D1::RectF(sr.left + 6.0f, sr.top + 7.0f, sr.left + 30.0f, sr.bottom - 7.0f),
                                        s2.c_str(), 2, Theme::AccentCyan, 0.65f);
                        }
                        continue;
                    }
                    if (pass == 1 && !isDragged) continue;

                    D2D1_RECT_F baseR = LeftSlotRectDip(i);
                    float yOff = (pass == 1) ? (slotDragCurY - baseR.top) : slotShiftSpring[(size_t)i].value;
                    D2D1_RECT_F card = D2D1::RectF(baseR.left, baseR.top + yOff, baseR.right, baseR.bottom + yOff);
                    if (card.bottom < leftSlotsVp.top - 4.0f || card.top > leftSlotsVp.bottom + 4.0f) continue;

                    const PromptItem* item = state->FindById(slots[(size_t)i]);
                    bool isTarget = (i == targetSlot);
                    int zCard = (pass == 0) ? AddClippedZone(40 + i, card, leftSlotsVp) : -2;
                    bool cardHov = (zCard >= 0 && zCard == hoverZoneIdx && !isSlotDragging);
                    std::wstring idxStr = FormatSlot2(i);

                    if (pass == 1) {
                        float lift = std::clamp(dragLiftSpring.value, 0.0f, 1.0f);
                        float ex = Motion::IsReducedMotion() ? 2.0f : (2.2f * lift);
                        card.left -= ex;
                        card.right += ex;
                        D2D1_RECT_F sh = D2D1::RectF(card.left - 3.0f * lift, card.top + 4.0f * lift,
                                                     card.right + 3.0f * lift, card.bottom + 5.0f * lift);
                        FillRRHex(rt.Get(), brush.Get(), sh, 8.5f, 0x000000, 0.42f * lift);
                    }

                    if (item) {
                        int zEd  = -2;
                        int zRem = -2;
                        D2D1_RECT_F rRem = D2D1::RectF(card.right - 26.0f, card.top + 7.0f, card.right - 5.0f, card.bottom - 7.0f);
                        D2D1_RECT_F rEd  = D2D1::RectF(card.right - 64.0f, card.top + 7.0f, card.right - 30.0f, card.bottom - 7.0f);
                        if (pass == 0) {
                            zEd  = AddClippedZone(50 + i, rEd,  leftSlotsVp, item->id);
                            zRem = AddClippedZone(60 + i, rRem, leftSlotsVp, item->id);
                        }
                        bool anyHov = cardHov || (zEd >= 0 && zEd == hoverZoneIdx) || (zRem >= 0 && zRem == hoverZoneIdx);

                        uint32_t bg = (pass == 1) ? 0x26314A : (isTarget ? 0x1E304C : (anyHov ? Theme::CardHoverBg : Theme::CardBg));
                        uint32_t rimH = (pass == 1 || isTarget) ? Theme::AccentCyan : (anyHov ? Theme::AccentBlue : 0xFFFFFF);
                        float rimA = (pass == 1) ? 0.85f : (isTarget ? 0.78f : (anyHov ? 0.40f : 0.08f));
                        FillRMRimHex(rt.Get(), brush.Get(), card, 7.5f, bg, 0.96f, rimH, rimA, 0xFFFFFF, 0.03f, (pass == 1 || isTarget) ? 1.3f : 1.0f);

                        D2D1_RECT_F idxBox = D2D1::RectF(card.left + 7.0f, card.top + 8.0f, card.left + 30.0f, card.bottom - 8.0f);
                        FillRRHex(rt.Get(), brush.Get(), idxBox, 4.5f, (isTarget || anyHov || pass == 1) ? curAccTop : 0xFFFFFF,
                                  (isTarget || anyHov || pass == 1) ? 0.16f : 0.06f);
                        DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), idxBox, idxStr.c_str(), 2,
                                    (isTarget || anyHov || pass == 1) ? curTxtAcc : Theme::TextSecondary, 0.96f);

                        float txtRight = (pass == 0 && (anyHov || isTarget)) ? (rEd.left - 5.0f) : (card.right - 10.0f);
                        DrawTextHex(rt.Get(), brush.Get(), fmtTitle.Get(),
                                    D2D1::RectF(idxBox.right + 7.0f, card.top, txtRight, card.bottom),
                                    item->title.c_str(), (UINT32)item->title.size(), Theme::TextPrimary, 0.97f);

                        if (pass == 0 && (anyHov || isTarget)) {
                            DrawBtn(rEd,  L"改", zEd,  0x222736, 0x2E374D, Theme::TextSecondary, 0.10f);
                            DrawBtn(rRem, L"×",  zRem, 0x222736, 0x4A1D28, Theme::DangerRed,     0.10f);
                        }
                    } else {
                        uint32_t bg = isTarget ? 0x162942 : (cardHov ? Theme::CardBg : Theme::CardEmptyBg);
                        uint32_t rimH = isTarget ? Theme::AccentCyan : (cardHov ? Theme::AccentBlue : 0xFFFFFF);
                        float rimA = isTarget ? 0.78f : (cardHov ? 0.30f : 0.04f);
                        FillRMRimHex(rt.Get(), brush.Get(), card, 7.5f, bg, isTarget ? 0.96f : (cardHov ? 0.88f : 0.56f),
                                     rimH, rimA, 0xFFFFFF, 0.02f, isTarget ? 1.3f : 1.0f);

                        D2D1_RECT_F idxBox = D2D1::RectF(card.left + 7.0f, card.top + 8.0f, card.left + 30.0f, card.bottom - 8.0f);
                        DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), idxBox, idxStr.c_str(), 2,
                                    isTarget ? Theme::AccentCyan : Theme::TextMuted, isTarget ? 0.98f : (cardHov ? 0.86f : 0.48f));
                        std::wstring eLbl = isTarget
                            ? (L"正在为槽位 " + idxStr + L" 选词 →")
                            : (cardHov ? L"+ 点击指定此槽位" : L"+ 空槽位");
                        DrawUiTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                                      D2D1::RectF(idxBox.right + 7.0f, card.top, card.right - 8.0f, card.bottom),
                                      eLbl.c_str(), isTarget ? Theme::AccentCyan : (cardHov ? Theme::TextSecondary : Theme::TextMuted),
                                      isTarget ? 0.98f : (cardHov ? 0.92f : 0.54f));
                    }
                }
            }
            if (leftMaxScrollY > 0.5f) {
                float thumbH = std::clamp(leftVpH * (leftVpH / std::max(1.0f, leftFullH)), 24.0f, leftVpH - 8.0f);
                float prog = std::clamp(leftScrollY / leftMaxScrollY, 0.0f, 1.0f);
                float tTop = leftSlotsVp.top + 4.0f + prog * std::max(0.0f, leftVpH - 8.0f - thumbH);
                FillRRHex(rt.Get(), brush.Get(), D2D1::RectF(leftBox.right - 6.5f, tTop, leftBox.right - 3.5f, tTop + thumbH),
                          1.5f, curAccTop, 0.45f);
            }
            rt->PopAxisAlignedClip();

            // --- RIGHT PANE: Search + 3-Option Filter Bar + Filterable Vault Picker ---
            D2D1_RECT_F searchBox = D2D1::RectF(kRightPaneX, wsTop, cw - 134.0f, wsTop + 32.0f);
            DrawInputBox(rt.Get(), brush.Get(), searchBox, 7.5f, searchFocusAnim, false);

            if (!pickerQuery.empty() || clearSearchAnim > 0.02f) {
                D2D1_RECT_F rcClr = D2D1::RectF(searchBox.right - 26.0f, searchBox.top + 4.0f, searchBox.right - 4.0f, searchBox.bottom - 4.0f);
                int zClrQ = !pickerQuery.empty() ? AddZone(7, rcClr) : -2;
                float a = !pickerQuery.empty() ? std::max(clearSearchAnim, Motion::IsReducedMotion() ? 1.0f : 0.0f) : clearSearchAnim;
                if (zClrQ >= 0 && zClrQ == hoverZoneIdx) FillRRHex(rt.Get(), brush.Get(), rcClr, 5.0f, 0xFFFFFF, 0.10f * a);
                DrawTextHex(rt.Get(), brush.Get(), fmtTab.Get(), rcClr, L"\x00D7", 1,
                            (zClrQ >= 0 && zClrQ == hoverZoneIdx) ? Theme::TextPrimary : Theme::TextMuted, 0.92f * a);
            }

            D2D1_RECT_F rcNew = D2D1::RectF(cw - 126.0f, wsTop, cw - 18.0f, wsTop + 32.0f);
            int zNew = AddZone(1, rcNew);
            DrawBtn(rcNew, L"+ 新建 Prompt", zNew, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.25f);

            // 3-Option Filter Bar: 未入本场景 N | 未分配 M | 全部 K
            int cntNotInCur = 0, cntUnmounted = 0, cntAll = 0;
            CountFilterBuckets(*state, sceneIndex, cntNotInCur, cntUnmounted, cntAll);
            float filterTop = wsTop + 40.0f;
            float filterBot = filterTop + 26.0f;

            struct FPill {
                PickerFilter f;
                int kind;
                std::wstring label;
                uint32_t acc;
                float w;
                D2D1_RECT_F rect{};
                int zIdx = -1;
            };
            wchar_t b0[32]{}, b1[32]{}, b2[32]{};
            wsprintfW(b0, L"未入本场景 %d", cntNotInCur);
            wsprintfW(b1, L"未分配 %d", cntUnmounted);
            wsprintfW(b2, L"全部 %d", cntAll);
            FPill fpills[3] = {
                { PickerFilter::NotInCurrent, 30, UiText(b0), curTxtAcc,          106.0f },
                { PickerFilter::UnmountedAll, 31, UiText(b1), Theme::AccentCyan,   86.0f },
                { PickerFilter::All,          32, UiText(b2), Theme::AccentBlue,   74.0f }
            };
            float fGap = 6.0f;
            float fx = kRightPaneX;
            float selFX = fx, selFW = fpills[0].w;
            uint32_t selFAcc = curTxtAcc;
            bool selFHov = false;
            for (int k = 0; k < 3; ++k) {
                fpills[k].rect = D2D1::RectF(fx, filterTop, fx + fpills[k].w, filterBot);
                fpills[k].zIdx = AddZone(fpills[k].kind, fpills[k].rect);
                bool sel = (pickerFilter == fpills[k].f);
                if (sel) {
                    selFX = fpills[k].rect.left;
                    selFW = fpills[k].w;
                    selFAcc = fpills[k].acc;
                    selFHov = (fpills[k].zIdx == hoverZoneIdx);
                } else {
                    bool hov = (fpills[k].zIdx == hoverZoneIdx);
                    FillRMRimHex(rt.Get(), brush.Get(), fpills[k].rect, 6.0f, hov ? 0x222838 : 0x151923, 1.0f,
                                 0xFFFFFF, hov ? 0.16f : 0.07f, 0xFFFFFF, 0.03f);
                }
                fx += fpills[k].w + fGap;
            }
            if (!filterPillInit || Motion::IsReducedMotion()) {
                filterPillXSpring.Snap(selFX);
                filterPillWSpring.Snap(selFW);
                filterAccCol = filterAccTarget = Rgba(selFAcc);
                filterPillInit = true;
            } else {
                filterPillXSpring.target = selFX;
                filterPillWSpring.target = selFW;
                filterAccTarget = Rgba(selFAcc);
            }
            D2D1_RECT_F fSlide = D2D1::RectF(filterPillXSpring.value, filterTop,
                                             filterPillXSpring.value + filterPillWSpring.value, filterBot);
            FillRMRim(rt.Get(), brush.Get(), fSlide, 6.0f, Rgba(selFHov ? 0x283552 : 0x202A40, 1.0f),
                      D2D1::ColorF(filterAccCol.r, filterAccCol.g, filterAccCol.b, 0.48f), Rgba(0xFFFFFF, 0.05f));
            for (int k = 0; k < 3; ++k) {
                bool sel = (pickerFilter == fpills[k].f);
                bool hov = (fpills[k].zIdx == hoverZoneIdx);
                DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), fpills[k].rect,
                            fpills[k].label.c_str(), (UINT32)fpills[k].label.size(),
                            sel ? fpills[k].acc : Theme::TextSecondary, (sel || hov) ? 1.0f : 0.88f);
            }
            if (targetSlot >= 0 && fx + 96.0f <= cw - 18.0f) {
                std::wstring th = L"点选放入槽位 " + FormatSlot2(targetSlot);
                DrawUiTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                              D2D1::RectF(fx + 6.0f, filterTop + 4.0f, cw - 18.0f, filterBot),
                              th.c_str(), Theme::AccentCyan, 0.90f);
            }

            // Candidate list viewport
            float listTop = filterBot + 8.0f;
            float listBot = wsBot;
            D2D1_RECT_F vp = D2D1::RectF(kRightPaneX - 2.0f, listTop, cw - 6.0f, listBot);
            viewportH = std::max(60.0f, vp.bottom - vp.top);
            scrollbar.SetViewport(vp);

            EnsureFiltered();
            float cardH = 58.0f;
            float stride = 64.0f;
            float totalH = (float)filteredIndices.size() * stride + 6.0f;
            maxScrollY = std::max(0.0f, totalH - viewportH);
            scrollTargetY = std::clamp(scrollTargetY, 0.0f, maxScrollY);
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float cardRight = cw - (maxScrollY > 0.5f ? 22.0f : 18.0f);
            float y = listTop - scrollY;
            for (size_t vi : filteredIndices) {
                if (vi >= state->vault.size()) continue;
                const auto& item = state->vault[vi];
                D2D1_RECT_F card = D2D1::RectF(kRightPaneX, y, cardRight, y + cardH);
                if (card.bottom >= listTop && card.top <= listBot) {
                    int inCurSlot = state->FindSlotOfIdInScene(sceneIndex, item.id);
                    bool inCur = (inCurSlot >= 0);

                    D2D1_RECT_F rAct  = D2D1::RectF(card.right - 68.0f,  card.top + 16.0f, card.right - 8.0f,  card.bottom - 16.0f);
                    D2D1_RECT_F rEdit = D2D1::RectF(card.right - 114.0f, card.top + 16.0f, card.right - 74.0f, card.bottom - 16.0f);
                    int zCard = AddClippedZone(70, card,  vp, item.id);
                    int zEdit = AddClippedZone(71, rEdit, vp, item.id);
                    int zAct  = AddClippedZone(70, rAct,  vp, item.id);
                    bool rowHov = (zCard == hoverZoneIdx) || (zAct == hoverZoneIdx);

                    FillRMRimHex(rt.Get(), brush.Get(), card, 8.0f,
                                 rowHov ? 0x222838 : (inCur ? 0x1A2132 : Theme::CardBg), 0.96f,
                                 inCur ? curTxtAcc : 0xFFFFFF, inCur ? 0.36f : (rowHov ? 0.18f : 0.08f), 0xFFFFFF, 0.03f);

                    // Scene membership badges on top-left (shows exactly which scenes contain this prompt!)
                    float bx = card.left + 10.0f;
                    float maxBadgeRight = rEdit.left - 80.0f;
                    int shownBadges = 0;
                    int hiddenBadges = 0;

                    auto drawSceneChip = [&](int scIdx, int slIdx, bool isCurrent) {
                        uint32_t acc = Theme::SceneTextAccent(scIdx);
                        bool en = state->scenes[(size_t)scIdx].enabled;
                        std::wstring tag = isCurrent
                            ? (L"本场景 · " + FormatSlot2(slIdx))
                            : (state->scenes[(size_t)scIdx].name + L" · " + FormatSlot2(slIdx));
                        float chipW = isCurrent ? 70.0f : std::clamp(38.0f + (float)VisualWidthUnits(state->scenes[(size_t)scIdx].name) * 5.2f, 54.0f, 76.0f);
                        if (shownBadges < 2 && bx + chipW <= maxBadgeRight) {
                            D2D1_RECT_F chip = D2D1::RectF(bx, card.top + 8.0f, bx + chipW, card.top + 26.0f);
                            FillRRHex(rt.Get(), brush.Get(), chip, 4.5f, acc, isCurrent ? 0.20f : (en ? 0.14f : 0.09f));
                            DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), chip, tag.c_str(), acc, en ? 0.96f : 0.72f);
                            bx = chip.right + 4.0f;
                            ++shownBadges;
                        } else {
                            ++hiddenBadges;
                        }
                    };

                    if (inCur) drawSceneChip(sceneIndex, inCurSlot, true);
                    for (int s = 0; s < kMaxScenes; ++s) {
                        if (s == sceneIndex) continue;
                        int osl = state->FindSlotOfIdInScene(s, item.id);
                        if (osl >= 0) drawSceneChip(s, osl, false);
                    }
                    if (hiddenBadges > 0 && bx + 32.0f <= maxBadgeRight) {
                        wchar_t hb[16]{};
                        wsprintfW(hb, L"+%d", hiddenBadges);
                        D2D1_RECT_F chip = D2D1::RectF(bx, card.top + 8.0f, bx + 28.0f, card.top + 26.0f);
                        FillRRHex(rt.Get(), brush.Get(), chip, 4.5f, Theme::AccentIndigo, 0.14f);
                        DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), chip, hb, (UINT32)wcslen(hb), 0xA5B4FC, 0.92f);
                        bx = chip.right + 4.0f;
                    }
                    if (shownBadges == 0 && hiddenBadges == 0) {
                        D2D1_RECT_F chip = D2D1::RectF(bx, card.top + 8.0f, bx + 48.0f, card.top + 26.0f);
                        FillRRHex(rt.Get(), brush.Get(), chip, 4.5f, 0xFFFFFF, 0.05f);
                        DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), chip, L"未分配", Theme::TextMuted, 0.90f);
                        bx = chip.right + 5.0f;
                    }

                    // Prompt title + context preview
                    DrawTextHex(rt.Get(), brush.Get(), fmtTitle.Get(),
                                D2D1::RectF(bx + 3.0f, card.top + 6.0f, rEdit.left - 8.0f, card.top + 28.0f),
                                item.title.c_str(), (UINT32)item.title.size(), Theme::TextPrimary, 0.97f);
                    std::wstring prev = BuildSearchPreviewSnippet(item.content, pickerQuery, 76);
                    DrawTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                                D2D1::RectF(card.left + 10.0f, card.top + 31.0f, rEdit.left - 8.0f, card.bottom - 7.0f),
                                prev.c_str(), (UINT32)prev.size(), Theme::TextSecondary, 0.78f);

                    DrawBtn(rEdit, L"编辑", zEdit, 0x222736, 0x2D3448, Theme::TextSecondary, 0.10f);

                    if (inCur) {
                        if (targetSlot >= 0 && targetSlot != inCurSlot) {
                            std::wstring mvLbl = L"移至 " + FormatSlot2(targetSlot);
                            DrawBtn(rAct, mvLbl.c_str(), zAct, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.28f);
                        } else {
                            DrawBtn(rAct, L"移出", zAct, 0x242938, 0x4A1D28, Theme::TextSecondary, 0.12f);
                        }
                    } else if (targetSlot >= 0) {
                        std::wstring putLbl = L"放入 " + FormatSlot2(targetSlot);
                        DrawBtn(rAct, putLbl.c_str(), zAct, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.30f);
                    } else {
                        DrawBtn(rAct, L"+ 加入", zAct, isFull ? 0x1C2130 : 0x1E3354, isFull ? 0x242A3C : 0x2A4774,
                                isFull ? Theme::TextMuted : Theme::AccentCyan, isFull ? 0.08f : 0.24f);
                    }
                }
                y += stride;
            }

            if (filteredIndices.empty()) {
                std::wstring emptyMsg;
                if (!pickerQuery.empty()) {
                    emptyMsg = UiText(L"未找到包含「" + CollapseWhitespaceSingleLine(pickerQuery, 18) + L"」的 Prompt");
                } else if (pickerFilter == PickerFilter::UnmountedAll) {
                    emptyMsg = UiText(L"所有词舱 Prompt 均已分配到场景中");
                } else if (pickerFilter == PickerFilter::NotInCurrent) {
                    emptyMsg = UiText(L"词舱全部 Prompt 均已加入「" + curSc.name + L"」");
                } else {
                    emptyMsg = UiText(L"词舱暂无条目，点击右上角「+ 新建 Prompt」立即添加");
                }
                DrawTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                            D2D1::RectF(kRightPaneX + 6.0f, listTop + 20.0f, cardRight, listTop + 46.0f),
                            emptyMsg.c_str(), (UINT32)emptyMsg.size(), Theme::TextMuted, 0.94f);
                float ebx = kRightPaneX + 6.0f, ebt = listTop + 52.0f, ebb = listTop + 80.0f;
                if (!pickerQuery.empty()) {
                    D2D1_RECT_F r1 = D2D1::RectF(ebx, ebt, ebx + 82.0f, ebb);
                    DrawBtn(r1, L"清空搜索", AddClippedZone(7, r1, vp), 0x222736, 0x2D3448, Theme::TextSecondary, 0.12f);
                    ebx += 90.0f;
                    if (pickerFilter != PickerFilter::All) {
                        D2D1_RECT_F r2 = D2D1::RectF(ebx, ebt, ebx + 92.0f, ebb);
                        DrawBtn(r2, L"搜全部词舱", AddClippedZone(32, r2, vp), 0x222736, 0x2D3448, Theme::TextPrimary, 0.14f);
                        ebx += 100.0f;
                    }
                    D2D1_RECT_F r3 = D2D1::RectF(ebx, ebt, ebx + 118.0f, ebb);
                    DrawBtn(r3, L"+ 新建此 Prompt", AddClippedZone(9, r3, vp), 0x1E3354, 0x284570, Theme::AccentCyan, 0.24f);
                } else if (pickerFilter == PickerFilter::UnmountedAll) {
                    D2D1_RECT_F r1 = D2D1::RectF(ebx, ebt, ebx + 112.0f, ebb);
                    DrawBtn(r1, L"看未入本场景", AddClippedZone(30, r1, vp), 0x1E3354, 0x284570, Theme::AccentCyan, 0.24f);
                    ebx += 120.0f;
                    D2D1_RECT_F r2 = D2D1::RectF(ebx, ebt, ebx + 84.0f, ebb);
                    DrawBtn(r2, L"查看全部", AddClippedZone(32, r2, vp), 0x222736, 0x2D3448, Theme::TextPrimary, 0.12f);
                } else if (pickerFilter == PickerFilter::NotInCurrent) {
                    D2D1_RECT_F r1 = D2D1::RectF(ebx, ebt, ebx + 110.0f, ebb);
                    DrawBtn(r1, L"+ 新建 Prompt", AddClippedZone(1, r1, vp), 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.25f);
                    ebx += 118.0f;
                    D2D1_RECT_F r2 = D2D1::RectF(ebx, ebt, ebx + 84.0f, ebb);
                    DrawBtn(r2, L"查看全部", AddClippedZone(32, r2, vp), 0x222736, 0x2D3448, Theme::TextPrimary, 0.12f);
                }
            }

            rt->PopAxisAlignedClip();
            scrollbar.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, viewportH);

            // === Bottom Footer Bar ===
            float footTop = ch - kFooterH;
            rt->DrawLine(D2D1::Point2F(0, footTop), D2D1::Point2F(cw, footTop), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);

            bool showUndo = (undoClearActive && nowTick < undoClearExpireTick);
            float statusRight = showUndo ? (cw - 200.0f) : (cw - 114.0f);
            const wchar_t* footRaw = statusText.empty()
                ? (!curSc.enabled
                    ? L"此场景当前未在贴边面板显示，编排后点右上角「立即启用」即可生效"
                    : L"左侧 10 槽位与贴边面板 1:1 实时同步，所有调整即时保存")
                : statusText.c_str();
            uint32_t footCol = statusText.empty()
                ? (!curSc.enabled ? 0xFBBF24 : Theme::TextMuted)
                : (statusIsErr ? Theme::DangerRed : Theme::CopyGreen);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtBody.Get(),
                          D2D1::RectF(18.0f, footTop + 13.0f, statusRight, ch - 10.0f), footRaw, footCol, 0.94f);

            if (showUndo) {
                D2D1_RECT_F rcUndo = D2D1::RectF(cw - 190.0f, footTop + 9.0f, cw - 110.0f, ch - 9.0f);
                DrawBtn(rcUndo, L"撤销清空", AddZone(6, rcUndo), 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.30f);
            }
            D2D1_RECT_F rcDone = D2D1::RectF(cw - 102.0f, footTop + 9.0f, cw - 18.0f, ch - 9.0f);
            DrawBtn(rcDone, L"完成", AddZone(2, rcDone), 0x222736, 0x2D3448, Theme::TextPrimary, 0.16f);
    }

    int HitZone(float x, float y) const {
        for (int i = (int)zones.size() - 1; i >= 0; --i) {
            const auto& r = zones[(size_t)i].rect;
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
        }
        return -1;
    }

    bool CommitCandidate(AppState& candidate, const std::wstring& okMsg) {
        if (!storage->Save(candidate)) {
            statusText = UiText(storage->readOnlyRecoveryMode
                ? L"只读保护模式：无法保存，请先恢复备份"
                : L"保存失败：无法写入数据文件");
            statusIsErr = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return false;
        }
        *state = std::move(candidate);
        statusText = UiText(okMsg);
        statusIsErr = false;
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    [[gnu::noinline]] void HandleRightCardAction(const std::wstring& pid) {
        if (!state || !storage) return;
        int curSlot = state->FindSlotOfIdInScene(sceneIndex, pid);
        AppState candidate = *state;
        wchar_t msg[128]{};
        if (targetSlot >= 0 && targetSlot < 10) {
            int dst = targetSlot;
            candidate.AssignToSlotInScene(sceneIndex, dst, pid);
            _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已放入「%ls · 槽位 %02d」",
                         state->scenes[(size_t)sceneIndex].name.c_str(), dst + 1);
            if (CommitCandidate(candidate, msg)) {
                targetSlot = -1;
                UpdateSearchCueBanner();
            }
            return;
        }
        if (curSlot >= 0) {
            candidate.RemoveFromScene(sceneIndex, pid);
            _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已从「%ls」移出（词舱数据已保留）",
                         state->scenes[(size_t)sceneIndex].name.c_str());
            CommitCandidate(candidate, msg);
        } else {
            int assigned = candidate.AssignToSceneFirstEmpty(sceneIndex, pid);
            if (assigned < 0) {
                statusText = UiText(L"该场景已满 10 格，可先点左侧某槽位进行替换或移出");
                statusIsErr = true;
                fullShakeTick = GetTickCount64();
                if (!Motion::IsReducedMotion()) EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已加入「%ls · 槽位 %02d」",
                         state->scenes[(size_t)sceneIndex].name.c_str(), assigned + 1);
            CommitCandidate(candidate, msg);
        }
    }

    [[gnu::noinline]] void HandleRemoveSlot(int slIdx) {
        if (!state || !storage || slIdx < 0 || slIdx >= 10) return;
        if (state->scenes[(size_t)sceneIndex].slots[(size_t)slIdx].empty()) return;
        AppState candidate = *state;
        candidate.scenes[(size_t)sceneIndex].slots[(size_t)slIdx].clear();
        wchar_t msg[128]{};
        _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已将槽位 %02d 移出「%ls」（词舱已保留）",
                     slIdx + 1, state->scenes[(size_t)sceneIndex].name.c_str());
        if (CommitCandidate(candidate, msg) && targetSlot == slIdx) {
            targetSlot = -1;
            UpdateSearchCueBanner();
        }
    }

    [[gnu::noinline]] void HandleSwapSlots(int a, int b) {
        if (!state || !storage || a < 0 || a >= 10 || b < 0 || b >= 10 || a == b) return;
        AppState candidate = *state;
        auto& sl = candidate.scenes[(size_t)sceneIndex].slots;
        std::swap(sl[(size_t)a], sl[(size_t)b]);
        if (CommitCandidate(candidate, L"已调整槽位顺序")) {
            if (!Motion::IsReducedMotion()) {
                float delta = (float)(a - b) * kSlotStride;
                slotShiftSpring[(size_t)b].value += delta;
                slotShiftSpring[(size_t)b].target = 0.0f;
                slotShiftSpring[(size_t)a].value -= delta;
                slotShiftSpring[(size_t)a].target = 0.0f;
                EnsureAnimTimer();
            }
        }
    }

    [[gnu::noinline]] void HandleToggleSceneEnabled() {
        if (!state || !storage) return;
        AppState candidate = *state;
        bool wasEn = candidate.scenes[(size_t)sceneIndex].enabled;
        if (!candidate.ToggleSceneEnabled(sceneIndex)) {
            lockedShakeTick = GetTickCount64();
            statusText = UiText(L"至少需保留 1 个启用场景");
            statusIsErr = true;
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        std::wstring scName = candidate.scenes[(size_t)sceneIndex].name;
        CommitCandidate(candidate, wasEn
            ? (L"已隐藏场景「" + scName + L"」（槽位编排已完整保留）")
            : (L"已启用场景「" + scName + L"」，贴边面板已同步显示"));
    }

    [[gnu::noinline]] void HandleRenameScene() {
        if (!state || !storage) return;
        int sc = sceneIndex;
        bool ok = SceneRenameDialog::Show(hInst, hwnd, sc, state->scenes[(size_t)sc].name,
            [this, sc](const std::wstring& newName, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                candidate.scenes[(size_t)sc].name = newName;
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"只读保护模式：请先恢复备份或导入 data.json"
                        : L"保存失败：无法写入数据文件";
                    return false;
                }
                *state = std::move(candidate);
                return true;
            });
        if (!ok) return;
        statusText = UiText(L"已更新场景名称为「" + state->scenes[(size_t)sc].name + L"」");
        statusIsErr = false;
        UpdateSearchCueBanner();
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void HandleClearScene() {
        if (!state || !storage) return;
        int mounted = state->CountMountedInScene(sceneIndex);
        if (mounted == 0) return;
        std::wstring scName = state->scenes[(size_t)sceneIndex].name;
        wchar_t body[256]{};
        _snwprintf_s(body, _countof(body), _TRUNCATE,
                      L"确定要清空场景「%ls」的 %d 个已装配槽位吗？\n\n"
                      L"所有 Prompt 正文、使用次数与其他场景编排都会完整保留在词舱中，不会删除任何条目。",
                      scName.c_str(), mounted);
        if (!DarkConfirmDialog::Show(hInst, hwnd, L"清空「" + scName + L"」槽位", body, L"确认清空", L"取消", true)) return;

        auto prevSlots = state->scenes[(size_t)sceneIndex].slots;
        AppState candidate = *state;
        candidate.ClearSceneSlots(sceneIndex);
        if (CommitCandidate(candidate, L"已清空「" + scName + L"」全部槽位（词舱已保留）")) {
            undoClearActive = true;
            undoClearSceneIdx = sceneIndex;
            undoClearSlots = prevSlots;
            undoClearExpireTick = GetTickCount64() + 5000;
            targetSlot = -1;
            UpdateSearchCueBanner();
            if (hwnd) SetTimer(hwnd, kTimerUndoExpire, 5050, nullptr);
        }
    }

    [[gnu::noinline]] void HandleUndoClear() {
        if (!undoClearActive || !state || !storage || GetTickCount64() > undoClearExpireTick) {
            undoClearActive = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        int sc = undoClearSceneIdx;
        if (sc < 0 || sc >= kMaxScenes) return;
        if (state->CountMountedInScene(sc) > 0) {
            undoClearActive = false;
            statusText = UiText(L"该场景已重新编排，未覆盖现有槽位");
            statusIsErr = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        AppState candidate = *state;
        candidate.scenes[(size_t)sc].slots = undoClearSlots;
        candidate.SanitizeSlots();
        undoClearActive = false;
        if (hwnd) KillTimer(hwnd, kTimerUndoExpire);
        CommitCandidate(candidate, L"已恢复「" + state->scenes[(size_t)sc].name + L"」原有槽位");
    }

    [[gnu::noinline]] void HandleNewPrompt(int explicitSlot = -1, const std::wstring& initTitle = L"") {
        if (!state || !storage) return;
        int effSlot = (explicitSlot >= 0 && explicitSlot < 10) ? explicitSlot : targetSlot;
        bool wasFull = (effSlot < 0 && state->CountMountedInScene(sceneIndex) >= 10);
        wchar_t hdBuf[128]{};
        if (effSlot >= 0) {
            _snwprintf_s(hdBuf, _countof(hdBuf), _TRUNCATE, L"新建 Prompt 并放入「%ls · 槽位 %02d」",
                         state->scenes[(size_t)sceneIndex].name.c_str(), effSlot + 1);
        } else if (wasFull) {
            wcscpy_s(hdBuf, L"新建 Prompt 到词舱（当前场景已满）");
        } else {
            _snwprintf_s(hdBuf, _countof(hdBuf), _TRUNCATE, L"新建 Prompt 并加入「%ls」",
                         state->scenes[(size_t)sceneIndex].name.c_str());
        }
        int mountedSlot = -1;
        auto res = PromptEditorDialog::Show(hInst, hwnd, hdBuf, initTitle, L"",
            [this, effSlot, &mountedSlot](const std::wstring& title, const std::wstring& content, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                PromptItem item{};
                item.id = GeneratePromptId(&candidate);
                item.title = title;
                item.content = content;
                item.createdAt = NowUnixSeconds();
                candidate.vault.insert(candidate.vault.begin(), item);
                if (effSlot >= 0 && effSlot < 10) {
                    candidate.AssignToSlotInScene(sceneIndex, effSlot, item.id);
                    mountedSlot = effSlot;
                } else {
                    mountedSlot = candidate.AssignToSceneFirstEmpty(sceneIndex, item.id);
                }
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"只读保护模式：请先恢复备份或导入 data.json"
                        : L"保存失败：无法写入数据文件";
                    return false;
                }
                *state = std::move(candidate);
                return true;
            });
        if (!res.accepted) return;
        if (!pickerQuery.empty()) {
            pickerQuery.clear();
            if (hSearchEdit && IsWindow(hSearchEdit)) SetWindowTextW(hSearchEdit, L"");
        }
        if (effSlot >= 0 && targetSlot == effSlot) {
            targetSlot = -1;
            UpdateSearchCueBanner();
        }
        scrollY = scrollTargetY = 0.0f;
        if (mountedSlot >= 0) {
            wchar_t mb[96]{};
            _snwprintf_s(mb, _countof(mb), _TRUNCATE, L"已新建并放入「%ls · 槽位 %02d」",
                         state->scenes[(size_t)sceneIndex].name.c_str(), mountedSlot + 1);
            statusText = UiText(mb);
        } else {
            statusText = UiText(L"已存入词舱，移出或指定左侧槽位后即可加入");
        }
        statusIsErr = false;
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void HandleEditPrompt(const std::wstring& pid) {
        if (!state || !storage) return;
        PromptItem* it = state->FindById(pid);
        if (!it) return;
        auto res = PromptEditorDialog::Show(hInst, hwnd, L"编辑 Prompt", it->title, it->content,
            [this, pid](const std::wstring& title, const std::wstring& content, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                PromptItem* target = candidate.FindById(pid);
                if (!target) { outErr = L"原 Prompt 已不存在"; return false; }
                target->title = title;
                target->content = content;
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"只读保护模式：请先恢复备份或导入 data.json"
                        : L"保存失败：无法写入数据文件";
                    return false;
                }
                *state = std::move(candidate);
                return true;
            });
        if (!res.accepted) return;
        statusText = UiText(L"已更新 Prompt 内容");
        statusIsErr = false;
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void ShowSlotContextMenu(int slIdx, POINT screenPt) {
        if (!popupMenu || !state || slIdx < 0 || slIdx >= 10) return;
        const std::wstring pid = state->scenes[(size_t)sceneIndex].slots[(size_t)slIdx];
        const PromptItem* cur = state->FindById(pid);
        std::wstring s2 = FormatSlot2(slIdx);
        std::vector<DarkMenuItem> items;
        if (cur) {
            items.push_back(DarkMenuItem::Header(state->scenes[(size_t)sceneIndex].name + L" · 槽位 " + s2 + L" · " + cur->title));
            items.push_back(DarkMenuItem::Action(1, L"编辑此 Prompt..."));
            items.push_back(DarkMenuItem::Action(2, L"新建并替换此格..."));
            items.push_back(DarkMenuItem::Action(3, L"从右侧挑选替换此格"));
            items.push_back(DarkMenuItem::Separator());
            if (slIdx > 0) items.push_back(DarkMenuItem::Action(4, L"上移一格"));
            if (slIdx < 9) items.push_back(DarkMenuItem::Action(5, L"下移一格"));
            items.push_back(DarkMenuItem::Separator());
            items.push_back(DarkMenuItem::Action(6, L"移出此场景（保留在词舱）", L"", false, true));
        } else {
            items.push_back(DarkMenuItem::Header(state->scenes[(size_t)sceneIndex].name + L" · 空槽位 " + s2));
            items.push_back(DarkMenuItem::Action(2, L"+ 新建 Prompt 放入此格..."));
            items.push_back(DarkMenuItem::Action(3, L"设为右侧选词目标槽位"));
        }
        popupMenu->ShowAt(screenPt, items, [this, slIdx, pid](int cmd) {
            if (cmd == 1 && !pid.empty()) HandleEditPrompt(pid);
            else if (cmd == 2) HandleNewPrompt(slIdx);
            else if (cmd == 3) {
                targetSlot = slIdx;
                UpdateSearchCueBanner();
                if (hSearchEdit && IsWindow(hSearchEdit)) SetFocus(hSearchEdit);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (cmd == 4 && slIdx > 0) HandleSwapSlots(slIdx, slIdx - 1);
            else if (cmd == 5 && slIdx < 9) HandleSwapSlots(slIdx, slIdx + 1);
            else if (cmd == 6) HandleRemoveSlot(slIdx);
        }, 232.0f, MenuOwner::Vault);
    }

    bool HandleShortcutKey(WPARAM vk, bool fromSearchEdit) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (popupMenu && popupMenu->IsOpenFor(MenuOwner::Vault)) {
            if (popupMenu->HandleForwardedKey(vk)) return true;
        }
        if (ctrl && !alt && vk == 'F') {
            if (hSearchEdit && IsWindow(hSearchEdit)) {
                SetFocus(hSearchEdit);
                SendMessageW(hSearchEdit, EM_SETSEL, 0, -1);
            }
            return true;
        }
        if (ctrl && !alt && vk == 'N') {
            HandleNewPrompt();
            return true;
        }
        if (ctrl && !alt && vk == 'A' && fromSearchEdit && hSearchEdit) {
            SendMessageW(hSearchEdit, EM_SETSEL, 0, -1);
            return true;
        }
        if (vk == VK_ESCAPE) {
            if (isSlotDragging || pressedSlot >= 0) {
                CancelSlotDrag(true);
                InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            if (!pickerQuery.empty()) {
                ClearSearchQuery();
                return true;
            }
            if (targetSlot >= 0) {
                targetSlot = -1;
                UpdateSearchCueBanner();
                InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            done = true;
            DestroyWindow(hwnd);
            return true;
        }
        return false;
    }

    static LRESULT CALLBACK SearchSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = static_cast<ScenePromptsDialog*>(GetPropW(h, kSearchProp));
        if (!self || !self->origSearchProc) return DefWindowProcW(h, msg, wp, lp);
        WNDPROC orig = self->origSearchProc;
        if (msg == WM_NCDESTROY) {
            RemovePropW(h, kSearchProp);
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
            self->origSearchProc = nullptr;
            return CallWindowProcW(orig, h, msg, wp, lp);
        }
        if (msg == WM_KEYDOWN) {
            if (self->HandleShortcutKey(wp, true)) return 0;
        }
        if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
        return CallWindowProcW(orig, h, msg, wp, lp);
    }

    [[gnu::noinline]] void RunModal(HWND parent) {
        std::wstring cap = UiText(L"编辑场景台面 -- 词舱 PromptPod");
        hwnd = CreateDarkModalWindow(hInst, L"PromptPod_ScenePromptsDlg", DlgWndProc, cap.c_str(),
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
                                     parent, 800.0f, 560.0f, dpiScale, this);
        if (!hwnd) return;
        // Enable double-click support on dialog class
        SetClassLongPtrW(hwnd, GCL_STYLE, GetClassLongPtrW(hwnd, GCL_STYLE) | CS_DBLCLKS);
        SetupSearchControl();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) { gotQuit = true; quitCode = msg.wParam; break; }
            if (gm < 0) break;
            if ((msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) && msg.message == WM_KEYDOWN) {
                if (msg.hwnd != hSearchEdit && HandleShortcutKey(msg.wParam, false)) continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (popupMenu && popupMenu->IsOpenFor(MenuOwner::Vault)) popupMenu->Close();
        EndDarkModalWindow(hwnd, parent, gotQuit, quitCode);
    }

    static LRESULT CALLBACK DlgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        ScenePromptsDialog* self = nullptr;
        if (msg == WM_NCCREATE) {
            self = static_cast<ScenePromptsDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<ScenePromptsDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)wp, RGB(241, 245, 249));
                SetBkColor((HDC)wp, RGB(27, 31, 43));
                return (LRESULT)self->hSearchBrush;
            case WM_SETTINGCHANGE:
                Motion::RefreshSystemReducedMotion();
                if (Motion::IsReducedMotion()) {
                    self->SnapAllAnims();
                    self->StopAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                float sc = GetDpiScaleForWindow(h);
                mmi->ptMinTrackSize.x = (LONG)(680.0f * sc);
                mmi->ptMinTrackSize.y = (LONG)(460.0f * sc);
                return 0;
            }
            case WM_SIZE:
                if (self->rt) self->rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                self->scenePillInit = false;
                self->filterPillInit = false;
                self->LayoutSearchControl();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset(); self->rt.Reset();
                auto* sug = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->RebuildSearchFont();
                self->LayoutSearchControl();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == 4001) {
                    WORD code = HIWORD(wp);
                    if (code == EN_SETFOCUS) {
                        self->searchFocused = true;
                        if (Motion::IsReducedMotion()) self->searchFocusAnim = 1.0f; else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        self->searchFocused = false;
                        if (Motion::IsReducedMotion()) self->searchFocusAnim = 0.0f; else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_CHANGE) {
                        int len = GetWindowTextLengthW(self->hSearchEdit);
                        std::wstring q((size_t)len + 1, L'\0');
                        if (len > 0) GetWindowTextW(self->hSearchEdit, q.data(), len + 1);
                        q.resize((size_t)len);
                        self->pickerQuery = q;
                        self->scrollY = self->scrollTargetY = 0.0f;
                        self->filterDirty = true;
                        if (Motion::IsReducedMotion()) self->clearSearchAnim = q.empty() ? 0.0f : 1.0f;
                        else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    }
                }
                return 0;
            case WM_TIMER:
                if (wp == kTimerAnim) self->OnAnimTick();
                else if (wp == kTimerUndoExpire) {
                    KillTimer(h, kTimerUndoExpire);
                    self->undoClearActive = false;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_PAINT: self->OnPaint(); return 0;
            case WM_MOUSEWHEEL: {
                if (self->scrollbar.dragging || self->isSlotDragging || self->pressedSlot >= 0) return 0;
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                POINT cpt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                ScreenToClient(h, &cpt);
                float cx = (float)cpt.x / self->dpiScale;
                if (cx < kRightPaneX - 4.0f && self->leftMaxScrollY > 0.5f) {
                    self->leftScrollTargetY = std::clamp(self->leftScrollTargetY - (float)delta * 0.35f, 0.0f, self->leftMaxScrollY);
                    if (Motion::IsReducedMotion()) self->leftScrollY = self->leftScrollTargetY;
                    else { Motion::ExpApproach(self->leftScrollY, self->leftScrollTargetY, 20.0f, 0.016f, 0.25f); self->EnsureAnimTimer(); }
                } else {
                    self->scrollTargetY = std::clamp(self->scrollTargetY - (float)delta * 0.45f, 0.0f, self->maxScrollY);
                    if (Motion::IsReducedMotion()) self->scrollY = self->scrollTargetY;
                    else { Motion::ExpApproach(self->scrollY, self->scrollTargetY, 20.0f, 0.016f, 0.25f); self->EnsureAnimTimer(); }
                }
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    if (self->isSlotDragging) SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    else SetCursor(LoadCursorW(nullptr, self->hoverZoneIdx >= 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_RBUTTONUP: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                POINT spt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                ClientToScreen(h, &spt);
                if (x >= self->leftSlotsVp.left && x <= self->leftSlotsVp.right &&
                    y >= self->leftSlotsVp.top && y <= self->leftSlotsVp.bottom) {
                    for (int i = 0; i < 10; ++i) {
                        D2D1_RECT_F sr = self->LeftSlotRectDip(i);
                        if (x >= sr.left && x <= sr.right && y >= sr.top && y <= sr.bottom) {
                            self->ShowSlotContextMenu(i, spt);
                            return 0;
                        }
                    }
                }
                return 0;
            }
            case WM_LBUTTONDBLCLK: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                int hz = self->HitZone(x, y);
                if (hz >= 0 && self->zones[(size_t)hz].kind >= 40 && self->zones[(size_t)hz].kind < 50) {
                    int sl = self->zones[(size_t)hz].kind - 40;
                    const std::wstring& pid = self->state->scenes[(size_t)self->sceneIndex].slots[(size_t)sl];
                    if (self->state->FindById(pid)) self->HandleEditPrompt(pid);
                    else self->HandleNewPrompt(sl);
                }
                return 0;
            }
            case WM_LBUTTONDOWN: {
                if (self->popupMenu && self->popupMenu->IsOpen()) self->popupMenu->Close(true);
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedKind = 0; self->pressedPid.clear();
                if (self->scrollbar.OnLButtonDown(x, y, self->scrollY, self->maxScrollY, self->viewportH)) {
                    self->scrollTargetY = self->scrollY;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                int hz = self->HitZone(x, y);
                if (hz >= 0) {
                    int k = self->zones[(size_t)hz].kind;
                    if (k >= 40 && k < 50) {
                        int sl = k - 40;
                        if (self->state->FindById(self->state->scenes[(size_t)self->sceneIndex].slots[(size_t)sl]) != nullptr) {
                            self->pressedSlot = sl;
                            self->isSlotDragging = false;
                            self->slotDragOrigSlots = self->state->scenes[(size_t)self->sceneIndex].slots;
                            self->slotDragStartY = y;
                            D2D1_RECT_F sr = self->LeftSlotRectDip(sl);
                            self->slotDragGrabOffsetY = y - sr.top;
                            self->slotDragCurY = sr.top;
                            SetCapture(h);
                            InvalidateRect(h, nullptr, FALSE);
                            return 0;
                        }
                    }
                    self->pressedKind = k;
                    self->pressedPid = self->zones[(size_t)hz].pid;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;

                if (self->pressedSlot >= 0) {
                    if (!self->isSlotDragging && std::fabs(y - self->slotDragStartY) >= 5.0f) {
                        self->isSlotDragging = true;
                        self->targetSlot = -1;
                        self->UpdateSearchCueBanner();
                        self->dragLiftSpring.Snap(Motion::IsReducedMotion() ? 1.0f : 0.0f);
                        self->dragLiftSpring.target = 1.0f;
                        if (!Motion::IsReducedMotion()) self->EnsureAnimTimer();
                    }
                    if (self->isSlotDragging) {
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                        float firstTop = self->leftSlotsTopDip - self->leftScrollY;
                        float lastTop = firstTop + 9.0f * kSlotStride;
                        self->slotDragCurY = std::clamp(y - self->slotDragGrabOffsetY, firstTop - 4.0f, lastTop + 4.0f);
                        float dragMidY = self->slotDragCurY + kSlotH * 0.5f;
                        auto& sl = self->state->scenes[(size_t)self->sceneIndex].slots;
                        while (self->pressedSlot < 9) {
                            float nextMidY = firstTop + (float)(self->pressedSlot + 1) * kSlotStride + kSlotH * 0.5f;
                            if (dragMidY > nextMidY - 4.0f) {
                                std::swap(sl[(size_t)self->pressedSlot], sl[(size_t)(self->pressedSlot + 1)]);
                                if (!Motion::IsReducedMotion()) {
                                    self->slotShiftSpring[(size_t)self->pressedSlot].value += kSlotStride;
                                    self->slotShiftSpring[(size_t)self->pressedSlot].target = 0.0f;
                                }
                                self->pressedSlot += 1;
                            } else break;
                        }
                        while (self->pressedSlot > 0) {
                            float prevMidY = firstTop + (float)(self->pressedSlot - 1) * kSlotStride + kSlotH * 0.5f;
                            if (dragMidY < prevMidY + 4.0f) {
                                std::swap(sl[(size_t)self->pressedSlot], sl[(size_t)(self->pressedSlot - 1)]);
                                if (!Motion::IsReducedMotion()) {
                                    self->slotShiftSpring[(size_t)self->pressedSlot].value -= kSlotStride;
                                    self->slotShiftSpring[(size_t)self->pressedSlot].target = 0.0f;
                                }
                                self->pressedSlot -= 1;
                            } else break;
                        }
                        if (!Motion::IsReducedMotion()) self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    }
                }

                bool sbRedraw = false;
                if (self->scrollbar.OnMouseMove(x, y, self->scrollY, self->maxScrollY, self->viewportH, sbRedraw)) {
                    if (self->scrollbar.dragging) self->scrollTargetY = self->scrollY;
                    if (self->hoverZoneIdx != -1) { self->hoverZoneIdx = -1; sbRedraw = true; }
                    if (self->scrollbar.IsAnimActive()) self->EnsureAnimTimer();
                    if (sbRedraw) InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->scrollbar.IsAnimActive()) self->EnsureAnimTimer();
                if (sbRedraw) InvalidateRect(h, nullptr, FALSE);
                int nh = self->HitZone(x, y);
                if (nh != self->hoverZoneIdx) {
                    self->hoverZoneIdx = nh;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->trackingLeave = false;
                if (self->scrollbar.hovered && !self->scrollbar.dragging) {
                    self->scrollbar.hovered = false;
                    if (Motion::IsReducedMotion()) self->scrollbar.hoverAnim = 0.0f;
                    else self->EnsureAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                }
                if (self->hoverZoneIdx != -1) {
                    self->hoverZoneIdx = -1;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONUP: {
                if (self->scrollbar.dragging) {
                    self->scrollbar.dragging = false;
                    if (GetCapture() == h) ReleaseCapture();
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->pressedSlot >= 0) {
                    int finSlot = self->pressedSlot;
                    bool wasDrag = self->isSlotDragging;
                    self->pressedSlot = -1;
                    self->isSlotDragging = false;
                    if (GetCapture() == h) ReleaseCapture();
                    if (wasDrag) {
                        self->dragLiftSpring.target = 0.0f;
                        if (!self->storage->Save(*self->state)) {
                            self->state->scenes[(size_t)self->sceneIndex].slots = self->slotDragOrigSlots;
                            for (auto& sp : self->slotShiftSpring) sp.Snap(0.0f);
                            self->statusText = UiText(self->storage->readOnlyRecoveryMode
                                ? L"只读保护模式：无法保存槽位顺序"
                                : L"保存失败：无法写入数据文件");
                            self->statusIsErr = true;
                        } else {
                            if (!Motion::IsReducedMotion() && finSlot >= 0 && finSlot < 10) {
                                float landingTop = self->LeftSlotRectDip(finSlot).top;
                                float dropDelta = self->slotDragCurY - landingTop;
                                if (std::fabs(dropDelta) > 0.4f) {
                                    self->slotShiftSpring[(size_t)finSlot].Snap(dropDelta);
                                    self->slotShiftSpring[(size_t)finSlot].target = 0.0f;
                                }
                            }
                            self->statusText = UiText(L"已调整槽位顺序，贴边面板已同步");
                            self->statusIsErr = false;
                            self->NotifyMutated();
                        }
                        if (!Motion::IsReducedMotion()) self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else {
                        float x = (float)(short)LOWORD(lp) / self->dpiScale;
                        float y = (float)(short)HIWORD(lp) / self->dpiScale;
                        int hz = self->HitZone(x, y);
                        if (hz >= 0 && self->zones[(size_t)hz].kind == (40 + finSlot)) {
                            self->targetSlot = (self->targetSlot == finSlot) ? -1 : finSlot;
                            self->UpdateSearchCueBanner();
                            if (self->targetSlot >= 0 && self->hSearchEdit && IsWindow(self->hSearchEdit)) {
                                SetFocus(self->hSearchEdit);
                            }
                        }
                        InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
                int downK = self->pressedKind;
                std::wstring downPid = self->pressedPid;
                self->pressedKind = 0; self->pressedPid.clear();
                if (GetCapture() == h) ReleaseCapture();
                if (downK == 0) return 0;
                InvalidateRect(h, nullptr, FALSE);
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                int hz = self->HitZone(x, y);
                if (hz >= 0 && self->zones[(size_t)hz].kind == downK && self->zones[(size_t)hz].pid == downPid) {
                    if (downK == 1) self->HandleNewPrompt();
                    else if (downK == 2) { self->done = true; DestroyWindow(h); }
                    else if (downK == 3) self->HandleToggleSceneEnabled();
                    else if (downK == 4) self->HandleRenameScene();
                    else if (downK == 5) self->HandleClearScene();
                    else if (downK == 6) self->HandleUndoClear();
                    else if (downK == 7) self->ClearSearchQuery();
                    else if (downK == 8) {
                        self->targetSlot = -1;
                        self->UpdateSearchCueBanner();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (downK == 9) {
                        self->HandleNewPrompt(-1, CollapseWhitespaceSingleLine(self->pickerQuery, 24));
                    } else if (downK >= 20 && downK < 20 + kMaxScenes) {
                        self->SwitchScene(downK - 20);
                    } else if (downK >= 30 && downK <= 32) {
                        PickerFilter nextF = (downK == 30) ? PickerFilter::NotInCurrent
                                           : (downK == 31 ? PickerFilter::UnmountedAll : PickerFilter::All);
                        if (nextF != self->pickerFilter) {
                            self->pickerFilter = nextF;
                            self->scrollY = self->scrollTargetY = 0.0f;
                            self->filterDirty = true;
                            if (!Motion::IsReducedMotion()) self->EnsureAnimTimer();
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    } else if (downK >= 40 && downK < 50) {
                        int sl = downK - 40;
                        self->targetSlot = (self->targetSlot == sl) ? -1 : sl;
                        self->UpdateSearchCueBanner();
                        if (self->targetSlot >= 0 && self->hSearchEdit && IsWindow(self->hSearchEdit)) {
                            SetFocus(self->hSearchEdit);
                        }
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (downK >= 50 && downK < 60) {
                        self->HandleEditPrompt(downPid);
                    } else if (downK >= 60 && downK < 70) {
                        self->HandleRemoveSlot(downK - 60);
                    } else if (downK == 70) {
                        self->HandleRightCardAction(downPid);
                    } else if (downK == 71) {
                        self->HandleEditPrompt(downPid);
                    }
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
            case WM_CANCELMODE:
                if (self->pressedSlot >= 0 || self->isSlotDragging) {
                    self->CancelSlotDrag(true);
                    InvalidateRect(h, nullptr, FALSE);
                }
                if (self->pressedKind != 0) { self->pressedKind = 0; self->pressedPid.clear(); InvalidateRect(h, nullptr, FALSE); }
                self->scrollbar.dragging = false;
                return 0;
            case WM_CLOSE: self->done = true; DestroyWindow(h); return 0;
            case WM_DESTROY:
                self->StopAnimTimer();
                KillTimer(h, kTimerUndoExpire);
                self->DetachSearchControl();
                self->done = true;
                return 0;
            case WM_NCDESTROY: SetWindowLongPtrW(h, GWLP_USERDATA, 0); self->hwnd = nullptr; return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// ============================================================================
// Modal Dark Feedback Dialog (GitHub Issue Forms Pre-fill + Sanitized Diagnostics)
// ============================================================================
class FeedbackDialog {
public:
    enum class Outcome {
        Cancelled = 0,
        OpenedGitHub = 1,
        OpenedX = 2
    };

    struct Result {
        Outcome outcome = Outcome::Cancelled;
        bool includeDiagnostics = false;
        bool clipboardCopied = false;
        bool bodyCopiedToClipboard = false;
    };

    static Result Show(HINSTANCE hInst, HWND parent, const AppState* st, const StorageManager* sm) {
        FeedbackDialog dlg(hInst, st, sm);
        dlg.RunModal(parent);
        return dlg.result;
    }

private:
    HINSTANCE hInst = nullptr;
    const AppState* state = nullptr;
    const StorageManager* storage = nullptr;
    HWND hwnd = nullptr;
    HWND hEditTitle = nullptr;
    HWND hEditBody = nullptr;
    HFONT hFontInput = nullptr;
    HBRUSH hBrushEditBg = nullptr;
    DarkEditScrollbar bodyScroll;

    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtLabel;
    ComPtr<IDWriteTextFormat> fmtCount;
    ComPtr<IDWriteTextFormat> fmtBtn;
    ComPtr<IDWriteTextFormat> fmtHint;

    bool isBug = true;
    bool includeDiagnostics = true;
    float dpiScale = 1.0f;
    int hoverZone = 0;   // 1:BugTab, 2:FeatureTab, 3:DiagCheck, 4:XLink, 5:CopyDiag, 6:Submit
    int pressedZone = 0; // Press-release ownership
    int focusedEdit = 0; // 0 none, 1 title, 2 body
    Spring1D catPillXSpring = Spring1D::Snappy(20.0f);
    Spring1D catPillWSpring = Spring1D::Snappy(132.0f);
    float titleFocusAnim = 0.0f;
    float bodyFocusAnim = 0.0f;
    float diagCheckAnim = 1.0f;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;
    bool trackingLeave = false;
    bool dirtyConfirmArmed = false;
    ULONGLONG dirtyConfirmTick = 0;
    bool done = false;
    Result result{};
    std::wstring inlineStatus;
    bool inlineStatusOk = true;

    static constexpr UINT_PTR kTimerAnim = 1501;

    D2D1_RECT_F rcBugTab{};
    D2D1_RECT_F rcFeatTab{};
    D2D1_RECT_F rcDiagRow{};
    D2D1_RECT_F rcXLink{};
    D2D1_RECT_F rcCopyBtn{};
    D2D1_RECT_F rcSubmitBtn{};

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            catPillXSpring.Snap(catPillXSpring.target);
            catPillWSpring.Snap(catPillWSpring.target);
            titleFocusAnim = (focusedEdit == 1) ? 1.0f : 0.0f;
            bodyFocusAnim = (focusedEdit == 2) ? 1.0f : 0.0f;
            diagCheckAnim = includeDiagnostics ? 1.0f : 0.0f;
            if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    [[gnu::noinline]] void OnAnimTick() {
        float dt = StepDeltaQpc(qpcFreq, lastAnimQpc);
        bool m1 = catPillXSpring.Step(dt, 0.15f, 1.0f);
        bool m2 = catPillWSpring.Step(dt, 0.15f, 1.0f);
        bool m3 = Motion::ExpApproach(titleFocusAnim, focusedEdit == 1 ? 1.0f : 0.0f, 22.0f, dt);
        bool m4 = Motion::ExpApproach(bodyFocusAnim,  focusedEdit == 2 ? 1.0f : 0.0f, 22.0f, dt);
        bool m5 = Motion::ExpApproach(diagCheckAnim,  includeDiagnostics ? 1.0f : 0.0f, 22.0f, dt);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (!m1 && !m2 && !m3 && !m4 && !m5) {
            KillTimer(hwnd, kTimerAnim);
            animActive = false;
        }
    }

    FeedbackDialog(HINSTANCE inst, const AppState* st, const StorageManager* sm)
        : hInst(inst), state(st), storage(sm) {}

    ~FeedbackDialog() {
        if (hFontInput) DeleteObject(hFontInput);
        if (hBrushEditBg) DeleteObject(hBrushEditBg);
    }

    static std::wstring GetWndText(HWND h) {
        int len = GetWindowTextLengthW(h);
        if (len <= 0) return {};
        std::wstring s((size_t)len + 1, L'\0');
        int actual = GetWindowTextW(h, s.data(), len + 1);
        s.resize(actual > 0 ? (size_t)actual : 0);
        return s;
    }

    static std::wstring Trim(const std::wstring& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
        size_t b = s.size();
        while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
        return s.substr(a, b - a);
    }

    static std::wstring NormalizeLf(const std::wstring& s) {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') {
                out.push_back(L'\n');
                ++i;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    void CopyDiagnosticsOnly() {
        if (!state || !storage) return;
        std::wstring diag = BuildSanitizedDiagnostics(*state, *storage, includeDiagnostics);
        if (CopyTextToClipboard(hwnd, diag)) {
            inlineStatus = UiText(L"已复制脱敏诊断摘要（不含任何 Prompt 内容）");
            inlineStatusOk = true;
        } else {
            inlineStatus = UiText(L"复制失败：剪贴板被其他程序占用");
            inlineStatusOk = false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void OpenXFallback() {
        bool copied = false;
        if (state && storage && isBug && includeDiagnostics) {
            std::wstring diag = BuildSanitizedDiagnostics(*state, *storage, true);
            copied = CopyTextToClipboard(hwnd, diag);
        }
        INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", APP_X_URL_W, nullptr, nullptr, SW_SHOWNORMAL));
        if (r <= 32) {
            inlineStatus = UiText(L"打开默认浏览器失败，请手动访问 X 主页");
            inlineStatusOk = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        result.outcome = Outcome::OpenedX;
        result.includeDiagnostics = isBug && includeDiagnostics;
        result.clipboardCopied = copied;
        result.bodyCopiedToClipboard = false;
        done = true;
        DestroyWindow(hwnd);
    }

    void SubmitToGitHub() {
        std::wstring title = Trim(GetWndText(hEditTitle));
        std::wstring body = Trim(NormalizeLf(GetWndText(hEditBody)));
        std::wstring osStr = GetRealWindowsVersionString();
        std::wstring diag;
        bool wantDiag = isBug && includeDiagnostics;
        if (state && storage && wantDiag) {
            diag = BuildSanitizedDiagnostics(*state, *storage, true);
        }

        bool bodyTruncated = false;
        bool diagTruncated = false;
        std::wstring url = BuildGitHubIssueUrl(isBug, title, body, osStr, diag, &bodyTruncated, &diagTruncated);

        // Only touch user's clipboard when content/diagnostics exceeded URL limit and could not be pre-filled!
        // Copy BEFORE opening GitHub so a clipboard failure NEVER tells the user to paste stale clipboard content into a public Issue!
        bool copied = false;
        if (bodyTruncated || (diagTruncated && !diag.empty())) {
            std::wstring clipPayload;
            if (bodyTruncated) {
                clipPayload = body;
                if (diagTruncated && !diag.empty()) {
                    if (!clipPayload.empty()) clipPayload += L"\n\n---\n\n";
                    clipPayload += diag;
                }
            } else {
                clipPayload = diag;
            }
            copied = CopyTextToClipboard(hwnd, clipPayload);
            if (!copied) {
                inlineStatus = UiText(L"内容较长且写入剪贴板失败，请释放剪贴板占用后重试");
                inlineStatusOk = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }

        INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (r <= 32) {
            inlineStatus = UiText(L"打开默认浏览器失败，请手动访问 GitHub Issues");
            inlineStatusOk = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        DiagLogger::Instance().LogFmt("INFO", "FEEDBACK", L"Opened GitHub Issue form (type=%ls, diag=%d, bodyClip=%d, diagClip=%d, clipOk=%d)",
                                      isBug ? L"bug" : L"feature", wantDiag ? 1 : 0, bodyTruncated ? 1 : 0, diagTruncated ? 1 : 0, copied ? 1 : 0);
        result.outcome = Outcome::OpenedGitHub;
        result.includeDiagnostics = diagTruncated;
        result.clipboardCopied = copied;
        result.bodyCopiedToClipboard = bodyTruncated && copied;
        done = true;
        DestroyWindow(hwnd);
    }

    bool HasDraft() const {
        if (!hEditTitle || !hEditBody) return false;
        return !Trim(GetWndText(hEditTitle)).empty() || !Trim(GetWndText(hEditBody)).empty();
    }

    void Cancel() {
        if (HasDraft()) {
            ULONGLONG now = GetTickCount64();
            if (!dirtyConfirmArmed || (now - dirtyConfirmTick) > 4500) {
                dirtyConfirmArmed = true;
                dirtyConfirmTick = now;
                inlineStatus = UiText(L"已输入反馈内容，再按一次 Esc 或关闭即可放弃");
                inlineStatusOk = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        result.outcome = Outcome::Cancelled;
        done = true;
        DestroyWindow(hwnd);
    }

    void LayoutChildControls() {
        if (!hwnd) return;
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float ch = (float)(crc.bottom - crc.top) / dpiScale;

        rcBugTab  = D2D1::RectF(20.0f, 52.0f, 152.0f, 82.0f);
        rcFeatTab = D2D1::RectF(162.0f, 52.0f, 276.0f, 82.0f);

        int titleX = (int)(26.0f * dpiScale);
        int titleY = (int)(116.0f * dpiScale);
        int titleW = (int)((cw - 52.0f) * dpiScale);
        int titleH = (int)(26.0f * dpiScale);
        if (hEditTitle) {
            SetWindowPos(hEditTitle, nullptr, titleX, titleY, std::max(40, titleW), titleH, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        float bodyBotReserve = isBug ? 110.0f : 80.0f;
        int bodyX = (int)(26.0f * dpiScale);
        int bodyY = (int)(182.0f * dpiScale);
        int bodyW = (int)((cw - 60.0f) * dpiScale);
        int bodyH = (int)((ch - 188.0f - bodyBotReserve) * dpiScale);
        if (hEditBody) {
            SetWindowPos(hEditBody, nullptr, bodyX, bodyY, std::max(40, bodyW), std::max(60, bodyH), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        bodyScroll.SetTrackLayout(D2D1::RectF(20.0f, 176.0f, cw - 20.0f, ch - bodyBotReserve), dpiScale);

        rcDiagRow   = isBug ? D2D1::RectF(20.0f, ch - 102.0f, cw - 20.0f, ch - 76.0f) : D2D1::RectF(0, 0, 0, 0);
        rcXLink     = D2D1::RectF(20.0f, ch - 46.0f, cw - (isBug ? 258.0f : 152.0f), ch - 14.0f);
        rcCopyBtn   = isBug ? D2D1::RectF(cw - 246.0f, ch - 46.0f, cw - 146.0f, ch - 14.0f) : D2D1::RectF(0, 0, 0, 0);
        rcSubmitBtn = D2D1::RectF(cw - 138.0f, ch - 46.0f, cw - 20.0f, ch - 14.0f);
    }

    void RebuildEditFont() {
        HFONT nextFont = CreateUiInputFont(13.0f, dpiScale);
        if (nextFont) {
            if (hEditTitle) SendMessageW(hEditTitle, WM_SETFONT, (WPARAM)nextFont, TRUE);
            if (hEditBody)  SendMessageW(hEditBody,  WM_SETFONT, (WPARAM)nextFont, TRUE);
            if (hFontInput) DeleteObject(hFontInput);
            hFontInput = nextFont;
        }
    }

    void SetupChildControls() {
        dpiScale = GetDpiScaleForWindow(hwnd);
        hBrushEditBg = CreateSolidBrush(RGB(27, 31, 43));

        auto& eng = D2DEngine::Instance();
        fmtHeading = eng.CreateFormat(15.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        fmtLabel   = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_MEDIUM);
        fmtBtn     = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtHint    = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);

        hEditTitle = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 100, 24,
            hwnd, (HMENU)3001, hInst, nullptr
        );
        SendMessageW(hEditTitle, EM_SETLIMITTEXT, 100, 0);

        hEditBody = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 100, 100,
            hwnd, (HMENU)3002, hInst, nullptr
        );
        SendMessageW(hEditBody, EM_SETLIMITTEXT, 4000, 0);
        bodyScroll.Attach(hwnd, hEditBody);

        RebuildEditFont();
        LayoutChildControls();
    }

    void EnsureD2D() {
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureD2D();
        if (rt && brush) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            float ch = (float)(crc.bottom - crc.top) / dpiScale;

            rt->BeginDraw();
            rt->Clear(Rgba(0x11141C, 1.0f));

            DrawUiTextHex(rt.Get(), brush.Get(), fmtHeading.Get(), D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f),
                          L"提交问题反馈或功能建议", Theme::TextPrimary);

            // Category pills with smooth sliding active glass indicator
            FillRMRimHex(rt.Get(), brush.Get(), rcBugTab,  7.0f, (!isBug && hoverZone == 1) ? 0x222736 : 0x181C26, 1.0f, 0xFFFFFF, (!isBug && hoverZone == 1) ? 0.16f : 0.08f);
            FillRMRimHex(rt.Get(), brush.Get(), rcFeatTab, 7.0f, (isBug && hoverZone == 2)  ? 0x222736 : 0x181C26, 1.0f, 0xFFFFFF, (isBug && hoverZone == 2)  ? 0.16f : 0.08f);
            FillRMRimHex(rt.Get(), brush.Get(), D2D1::RectF(catPillXSpring.value, rcBugTab.top, catPillXSpring.value + catPillWSpring.value, rcBugTab.bottom),
                         7.0f, 0x1E3354, 1.0f, Theme::AccentCyan, 0.55f);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtBtn.Get(), rcBugTab,  L"Bug 问题反馈", isBug ? Theme::AccentCyan : Theme::TextSecondary, 0.98f);
            DrawUiTextHex(rt.Get(), brush.Get(), fmtBtn.Get(), rcFeatTab, L"功能建议",    !isBug ? Theme::AccentCyan : Theme::TextSecondary, 0.98f);

            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 90.0f, cw - 20.0f, 108.0f),
                          L"简短标题（选填，将自动预填到 GitHub Issue 表单）", Theme::TextSecondary);
            DrawInputBox(rt.Get(), brush.Get(), D2D1::RectF(20.0f, 110.0f, cw - 20.0f, 148.0f), 7.0f, titleFocusAnim, false);

            DrawUiTextHex(rt.Get(), brush.Get(), fmtLabel.Get(), D2D1::RectF(20.0f, 156.0f, cw - 20.0f, 174.0f),
                          isBug ? L"问题现象与复现步骤（选填，也可直接在浏览器中补充截图）" : L"期望功能与使用场景（选填）",
                          Theme::TextSecondary);
            float bodyBotReserve = isBug ? 110.0f : 80.0f;
            DrawInputBox(rt.Get(), brush.Get(), D2D1::RectF(20.0f, 176.0f, cw - 20.0f, ch - bodyBotReserve), 7.0f, bodyFocusAnim, false);
            bodyScroll.Draw(rt.Get(), brush.Get());

            // Diagnostics checkbox row (ONLY shown in Bug report mode -- not needed for feature requests!)
            if (isBug) {
                D2D1_RECT_F cb = D2D1::RectF(rcDiagRow.left, rcDiagRow.top + 4.0f, rcDiagRow.left + 16.0f, rcDiagRow.top + 20.0f);
                DrawDarkCheckbox(rt.Get(), brush.Get(), cb, includeDiagnostics, hoverZone == 3, Theme::AccentCyan, false, diagCheckAnim);
                DrawUiTextHex(rt.Get(), brush.Get(), fmtHint.Get(),
                              D2D1::RectF(cb.right + 8.0f, rcDiagRow.top + 2.0f, rcDiagRow.right, rcDiagRow.bottom),
                              L"附带脱敏环境与诊断摘要（绝不含任何 Prompt 内容）",
                              hoverZone == 3 ? Theme::TextPrimary : Theme::TextSecondary, 0.94f);
            }

            // Inline status line
            if (!inlineStatus.empty()) {
                DrawTextHex(rt.Get(), brush.Get(), fmtHint.Get(), D2D1::RectF(20.0f, ch - 72.0f, cw - 20.0f, ch - 52.0f),
                            inlineStatus.c_str(), (UINT32)inlineStatus.size(), inlineStatusOk ? Theme::CopyGreen : Theme::DangerRed, 0.95f);
            } else {
                DrawUiTextHex(rt.Get(), brush.Get(), fmtHint.Get(), D2D1::RectF(20.0f, ch - 72.0f, cw - 20.0f, ch - 52.0f),
                              L"反馈统一沉淀在 GitHub Issues，提交后可在页面直接粘贴截图或追踪进度", Theme::TextMuted, 0.92f);
            }

            // Bottom left X fallback link
            DrawUiTextHex(rt.Get(), brush.Get(), fmtHint.Get(),
                          D2D1::RectF(rcXLink.left, rcXLink.top + 8.0f, rcXLink.right, rcXLink.bottom),
                          L"无 GitHub 账号？前往 X（" APP_X_HANDLE_W L"）",
                          hoverZone == 4 ? Theme::AccentCyan : Theme::AccentBlue, hoverZone == 4 ? 1.0f : 0.88f);

            if (isBug) {
                DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcCopyBtn, L"复制诊断信息",
                                   hoverZone == 5, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.14f, pressedZone == 5);
            }
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcSubmitBtn, L"前往 GitHub 提交",
                               hoverZone == 6, 0x3B7BF6, 0x4F8EF7, 0xFFFFFF, 0.28f, pressedZone == 6);

            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                rt.Reset();
            }
        }
        EndPaint(hwnd, &ps);
    }

    static bool PtInDip(const D2D1_RECT_F& r, float x, float y) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    int HitZone(float x, float y) const {
        if (PtInDip(rcBugTab, x, y)) return 1;
        if (PtInDip(rcFeatTab, x, y)) return 2;
        if (isBug && PtInDip(rcDiagRow, x, y)) return 3;
        if (PtInDip(rcXLink, x, y)) return 4;
        if (isBug && PtInDip(rcCopyBtn, x, y)) return 5;
        if (PtInDip(rcSubmitBtn, x, y)) return 6;
        return 0;
    }

    void RunModal(HWND parent) {
        std::wstring cap = UiText(L"词舱 PromptPod · 意见反馈");
        hwnd = CreateDarkModalWindow(hInst, L"PromptPod_FeedbackDlg", DlgWndProc, cap.c_str(),
                                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                     parent, 548.0f, 472.0f, dpiScale, this);
        if (!hwnd) return;
        SetupChildControls();
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        SetFocus(hEditTitle);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) {
                gotQuit = true;
                quitCode = msg.wParam;
                break;
            }
            if (gm < 0) break;

            bool isOwnMsg = (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd));
            if (isOwnMsg && msg.message == WM_KEYDOWN) {
                if (msg.wParam == VK_ESCAPE) {
                    if ((msg.lParam & 0x40000000) != 0) continue; // Ignore auto-repeat hold
                    Cancel();
                    continue;
                }
                if (msg.wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    SubmitToGitHub();
                    continue;
                }
                if (msg.wParam == VK_RETURN && msg.hwnd == hEditTitle) {
                    SetFocus(hEditBody);
                    continue;
                }
                if (msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
                    (msg.hwnd == hEditTitle || msg.hwnd == hEditBody)) {
                    SendMessageW(msg.hwnd, EM_SETSEL, 0, -1);
                    continue;
                }
                if (msg.wParam == VK_TAB) {
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (!shift && msg.hwnd == hEditTitle) {
                        SetFocus(hEditBody);
                        continue;
                    }
                    if (shift && msg.hwnd == hEditBody) {
                        SetFocus(hEditTitle);
                        continue;
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        EndDarkModalWindow(hwnd, parent, gotQuit, quitCode);
    }

    static LRESULT CALLBACK DlgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        FeedbackDialog* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<FeedbackDialog*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<FeedbackDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(241, 245, 249));
                SetBkColor(hdc, RGB(27, 31, 43));
                return (LRESULT)self->hBrushEditBg;
            }
            case WM_COMMAND: {
                WORD id = LOWORD(wp);
                WORD code = HIWORD(wp);
                if (id == 3001 || id == 3002) {
                    if (code == EN_SETFOCUS) {
                        self->focusedEdit = (id == 3001) ? 1 : 2;
                        if (Motion::IsReducedMotion()) {
                            self->titleFocusAnim = (self->focusedEdit == 1) ? 1.0f : 0.0f;
                            self->bodyFocusAnim  = (self->focusedEdit == 2) ? 1.0f : 0.0f;
                        } else {
                            self->EnsureAnimTimer();
                        }
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        if (self->focusedEdit == ((id == 3001) ? 1 : 2)) {
                            self->focusedEdit = 0;
                            if (Motion::IsReducedMotion()) {
                                self->titleFocusAnim = 0.0f;
                                self->bodyFocusAnim = 0.0f;
                            } else {
                                self->EnsureAnimTimer();
                            }
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    } else if (code == EN_CHANGE) {
                        if (self->dirtyConfirmArmed) {
                            self->dirtyConfirmArmed = false;
                            self->inlineStatus.clear();
                            InvalidateRect(h, nullptr, FALSE);
                        }
                        if (id == 3002) self->bodyScroll.SyncAndNotifyParent(false);
                    } else if (id == 3002 && (code == EN_UPDATE || code == EN_VSCROLL)) {
                        self->bodyScroll.SyncAndNotifyParent(false);
                    }
                }
                return 0;
            }
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverZone != 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_TIMER:
                if (wp == kTimerAnim) self->OnAnimTick();
                return 0;
            case WM_PAINT:
                self->OnPaint();
                return 0;
            case WM_MOUSEWHEEL: {
                POINT spt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                if (self->bodyScroll.OnParentMouseWheel(wp, spt)) return 0;
                break;
            }
            case WM_LBUTTONDOWN: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedZone = 0;
                if (self->bodyScroll.OnParentLButtonDown(x, y)) return 0;
                int hz = self->HitZone(x, y);
                if (hz > 0) {
                    self->pressedZone = hz;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                if (self->bodyScroll.OnParentMouseMove(x, y)) {
                    if (self->hoverZone != 0) {
                        self->hoverZone = 0;
                        InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
                int nh = self->HitZone(x, y);
                if (nh != self->hoverZone) {
                    self->hoverZone = nh;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE: {
                self->trackingLeave = false;
                self->bodyScroll.OnParentMouseLeave();
                if (self->hoverZone != 0) {
                    self->hoverZone = 0;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                if (self->bodyScroll.OnParentLButtonUp()) return 0;
                int downZone = self->pressedZone;
                self->pressedZone = 0;
                if (GetCapture() == h) ReleaseCapture();
                if (downZone == 0) return 0;
                InvalidateRect(h, nullptr, FALSE);
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                int hz = self->HitZone(x, y);
                if (hz != downZone) return 0;
                if (hz == 1) {
                    self->isBug = true;
                    self->includeDiagnostics = true;
                    self->LayoutChildControls();
                    if (Motion::IsReducedMotion()) {
                        self->catPillXSpring.Snap(self->rcBugTab.left);
                        self->catPillWSpring.Snap(self->rcBugTab.right - self->rcBugTab.left);
                        self->diagCheckAnim = 1.0f;
                    } else {
                        self->catPillXSpring.target = self->rcBugTab.left;
                        self->catPillWSpring.target = self->rcBugTab.right - self->rcBugTab.left;
                        self->EnsureAnimTimer();
                    }
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hz == 2) {
                    self->isBug = false;
                    self->includeDiagnostics = false;
                    self->LayoutChildControls();
                    if (Motion::IsReducedMotion()) {
                        self->catPillXSpring.Snap(self->rcFeatTab.left);
                        self->catPillWSpring.Snap(self->rcFeatTab.right - self->rcFeatTab.left);
                        self->diagCheckAnim = 0.0f;
                    } else {
                        self->catPillXSpring.target = self->rcFeatTab.left;
                        self->catPillWSpring.target = self->rcFeatTab.right - self->rcFeatTab.left;
                        self->EnsureAnimTimer();
                    }
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hz == 3) {
                    self->includeDiagnostics = !self->includeDiagnostics;
                    if (Motion::IsReducedMotion()) self->diagCheckAnim = self->includeDiagnostics ? 1.0f : 0.0f;
                    else self->EnsureAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hz == 4) {
                    self->OpenXFallback();
                } else if (hz == 5) {
                    self->CopyDiagnosticsOnly();
                } else if (hz == 6) {
                    self->SubmitToGitHub();
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (self->pressedZone != 0) { self->pressedZone = 0; InvalidateRect(h, nullptr, FALSE); }
                self->bodyScroll.OnParentCaptureChanged();
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset();
                self->rt.Reset();
                auto* suggested = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->RebuildEditFont();
                self->LayoutChildControls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_CLOSE:
                self->Cancel();
                return 0;
            case WM_DESTROY:
                self->bodyScroll.Detach();
                self->done = true;
                return 0;
            case WM_NCDESTROY:
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                self->hwnd = nullptr;
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

// ============================================================================
// Vault & Usage Leaderboard Management Window (Dual-Tab Direct2D Center)
// ============================================================================
class VaultWindow {
    friend int RunHeadlessSelfTest(const std::wstring& testDir);
public:
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HWND hSearchEdit = nullptr;
    HFONT hSearchFont = nullptr;
    HBRUSH hSearchBrush = nullptr;

    AppState* state = nullptr;
    StorageManager* storage = nullptr;
    DarkPopupMenu* popupMenu = nullptr;
    UpdateManager* updater = nullptr;
    std::function<void()> onStateChanged;
    std::function<void()> onResetDockPos;
    std::function<void(DockEdge)> onSetDockEdge;
    std::function<std::wstring()> getDockLocationSummary;
    std::function<void()> onReplayOnboarding;

    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtTitle;
    ComPtr<IDWriteTextFormat> fmtTab;
    ComPtr<IDWriteTextFormat> fmtSection;
    ComPtr<IDWriteTextFormat> fmtCardTitle;
    ComPtr<IDWriteTextFormat> fmtCardBody;
    ComPtr<IDWriteTextFormat> fmtDescWrap;
    ComPtr<IDWriteTextFormat> fmtBadge;
    ComPtr<IDWriteTextFormat> fmtBigNum;
    ComPtr<IDWriteTextFormat> fmtKpiTitle;
    ComPtr<IDWriteTextFormat> fmtRank;

    int activeTab = 0;          // 0 = Vault, 1 = Leaderboard, 2 = Settings
    int assignTargetSlot = -1;  // -1 normal, 0..9 assigning to specific dock slot
    int vaultFilter = -1;       // Tab 0 view filter: -1 = All, 0..3 = specific enabled scene, -2 = Unmounted
    float scrollY = 0.0f;
    float scrollTargetY = 0.0f;
    float maxScrollY = 0.0f;
    float lastViewportH = 300.0f;
    float dpiScale = 1.0f;
    std::wstring searchQuery;
    WNDPROC origSearchProc = nullptr;
    bool searchFocused = false;

    // Cached filtered/ranked lists (invalidated on state/query mutation, never re-computed per frame!)
    std::vector<size_t> cachedFilteredIndices;
    std::vector<size_t> cachedRankedIndices;
    bool vaultCacheDirty = true;
    bool rankCacheDirty = true;
    int selectedVaultRow = -1; // Index into cachedFilteredIndices

    // Dark capsule viewport scrollbar shared across all 3 tabs
    DarkViewportScrollbar viewportScroll;

    // Unified Apple-grade motion state for VaultWindow
    struct RowShift {
        std::wstring id;
        Spring1D sp = Spring1D::Settle(0.0f);
    };
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animActive = false;

    Spring1D tabPillXSpring = Spring1D::Snappy(21.0f);
    Spring1D tabPillWSpring = Spring1D::Snappy(117.0f);
    bool tabPillInit = false;
    D2D1_COLOR_F tabRimCol = Rgba(Theme::AccentBlue);
    float newBtnAlpha = 1.0f;

    float searchFocusAnim = 0.0f;
    float clearBtnAnim = 0.0f;

    Spring1D filterPillXSpring = Spring1D::Snappy(18.0f);
    Spring1D filterPillWSpring = Spring1D::Snappy(68.0f);
    bool filterPillInit = false;
    D2D1_COLOR_F filterAccentCol = Rgba(Theme::AccentCyan);
    D2D1_COLOR_F filterAccentTarget = Rgba(Theme::AccentCyan);
    float cfgBtnAlpha = 0.0f;

    Spring1D rankEnterSpring = Spring1D::Fluid(1.0f);

    Spring1D swBubbleSpring = Spring1D::Snappy(1.0f);
    Spring1D swAutoStartSpring = Spring1D::Snappy(0.0f);
    Spring1D swAutoUpdSpring = Spring1D::Snappy(1.0f);
    bool settingsAnimInit = false;
    Spring1D edgePillXSpring = Spring1D::Snappy(0.0f);
    bool edgePillInit = false;
    Spring1D trayPillXSpring = Spring1D::Snappy(0.0f);
    Spring1D trayPillWSpring = Spring1D::Snappy(78.0f);
    bool trayPillInit = false;
    std::array<float, kMaxScenes> sceneCheckAnim{ 1.0f, 1.0f, 0.0f, 0.0f };
    int lockedSceneShakeIdx = -1;
    ULONGLONG lockedSceneShakeTick = 0;
    float updateProgAnim = 0.0f;
    float updateShimmerPhase = 0.0f;

    Spring1D toastSpring = Spring1D::Snappy(0.0f);
    bool toastClosing = false;

    std::vector<RowShift> rowShifts;
    float vaultDragGrabOffsetDipY = 0.0f;
    float vaultDragCurDipY = 0.0f;
    Spring1D vaultDragLiftSpring = Spring1D::Snappy(0.0f);

    // 5-second undo snapshot (supports both Prompt deletion across 4 scenes AND Scene slot clearing!)
    struct UndoDeleteEntry {
        bool active = false;
        bool isClearScene = false;
        PromptItem item{};
        size_t origIndex = 0;
        std::array<int, kMaxScenes> origSlots{ -1, -1, -1, -1 };
        int clearedSceneIdx = -1;
        std::array<std::wstring, 10> clearedSceneSlots{};
        ULONGLONG expireTick = 0;
    } undoDelete;

    bool pendingScrollToUpdate = false;
    std::wstring lastMenuTriggerId;
    int lastMenuTriggerKind = 0;

    // Inline feedback states (no ugly Win32 MessageBox)
    std::wstring copiedFlashId;
    ULONGLONG copiedFlashTick = 0;
    std::wstring toastMessage;
    bool toastIsError = false;
    bool toastShowsUndo = false;
    ULONGLONG toastTick = 0;
    ULONGLONG toastDurationMs = 2600;

    struct HitZone {
        // 1:Tab0, 2:Tab1, 7:Tab2(Settings), 3:New, 4:Import, 5:Export, 6:ClearAssign, 8:ClearSearch,
        // 10:Copy, 11:SlotAssign, 12:Edit, 13:Delete, 14:Duplicate, 15:CardMore, 16:DisableAutoStart, 17:NewFromSearch,
        // 20:ToggleHoverBubble, 21:ToggleAutoStart, 22:ResetDockEdge, 23:OpenDataDir, 24:OpenLog,
        // 25:CheckUpdate, 26:InstallUpdate, 27:OpenReleasesPage, 28:ToggleAutoUpdate,
        // 29:OpenGitHubHome, 30:OpenXHome, 31:OpenFeedback, 32:RestoreFromBackup,
        // 33:EdgeRight, 34:EdgeLeft, 35:UndoDelete, 36:EdgeFloat,
        // 37:TrayOpensVault, 38:TrayTogglesDock, 39:CopyDataDir, 40:ReplayOnboarding, 41:SelectCard,
        // 50..53:ToggleSceneEnabled(0..3), 54..57:ConfigScenePrompts(0..3),
        // 60..63:RenameScene(0..3), 64:OpenKhazixX, 65:OpenKhazixGitHub,
        // 70:FilterAll, 71..74:FilterScene(0..3), 75:FilterUnmounted
        int kind = 0;
        std::wstring promptId;
        D2D1_RECT_F rect{};
    };
    std::vector<HitZone> hitZones;
    struct DrawnCardHit {
        size_t vaultIdx = 0;
        int rowPos = 0;
        D2D1_RECT_F rect{};
        float cardTop = 0.0f;
    };
    static constexpr int kMaxDrawnCardHits = 32;
    DrawnCardHit drawnCardHits[kMaxDrawnCardHits]{};
    int drawnCardHitCount = 0;
    int hoverZoneIdx = -1;
    int pressedZoneKind = 0;
    std::wstring pressedZonePromptId;

    // Vault card drag-to-reorder state (Tab 0 when searchQuery is empty)
    int vaultDragIdx = -1;
    int vaultDragOrigIdx = -1;
    bool isVaultDragging = false;
    bool dragScrollTimerActive = false;
    float vaultDragStartDipY = 0.0f;
    float vaultDragLastDipY = 0.0f;
    float vaultListTopDip = 110.0f;
    float rankListTopDip = 154.0f;
    bool trackingLeave = false;
    bool editorModalOpen = false;
    bool d2dRecreateScheduled = false;

    static constexpr UINT_PTR kTimerFeedback   = 7701;
    static constexpr UINT_PTR kTimerDragScroll = 7702;
    static constexpr UINT_PTR kTimerAnim       = 7703;
    static constexpr UINT_PTR kTimerCopyClear  = 7704;
    static constexpr const wchar_t* kSearchProp = L"PP_VaultSearchOwner";

    bool IsEditorOpen() const { return editorModalOpen; }

    void AddRowShift(const std::wstring& pid, float deltaY) {
        if (Motion::IsReducedMotion() || pid.empty() || std::fabs(deltaY) < 0.4f) return;
        for (auto& rs : rowShifts) {
            if (rs.id == pid) {
                rs.sp.value += deltaY;
                rs.sp.target = 0.0f;
                return;
            }
        }
        RowShift rs{};
        rs.id = pid;
        rs.sp = Spring1D::Settle(deltaY);
        rs.sp.target = 0.0f;
        rowShifts.push_back(std::move(rs));
    }

    float GetRowShift(const std::wstring& pid) const {
        for (const auto& rs : rowShifts) {
            if (rs.id == pid) return rs.sp.value;
        }
        return 0.0f;
    }

    [[gnu::noinline]] void SyncSettingsSprings(bool snap) {
        if (!state) return;
        bool autoOn = (AutoStart::GetStatus() == AutoStart::Status::EnabledCurrent);
        float tBub = state->showHoverBubble ? 1.0f : 0.0f;
        float tAut = autoOn ? 1.0f : 0.0f;
        float tUpd = state->autoCheckUpdates ? 1.0f : 0.0f;
        if (snap || !settingsAnimInit || Motion::IsReducedMotion()) {
            swBubbleSpring.Snap(tBub);
            swAutoStartSpring.Snap(tAut);
            swAutoUpdSpring.Snap(tUpd);
            for (int s = 0; s < kMaxScenes; ++s) {
                sceneCheckAnim[(size_t)s] = state->scenes[(size_t)s].enabled ? 1.0f : 0.0f;
            }
            settingsAnimInit = true;
        } else {
            swBubbleSpring.target = tBub;
            swAutoStartSpring.target = tAut;
            swAutoUpdSpring.target = tUpd;
        }
    }

    void SyncTabPillTarget(bool snap) {
        float tx = (activeTab == 0) ? 21.0f : (activeTab == 1 ? 140.0f : 234.0f);
        float tw = (activeTab == 0) ? 117.0f : (activeTab == 1 ? 92.0f : 75.0f);
        D2D1_COLOR_F dstRim = (activeTab == 0) ? Rgba(Theme::AccentBlue) : (activeTab == 1 ? Rgba(Theme::AccentCyan) : Rgba(Theme::AccentIndigo));
        if (snap || !tabPillInit || Motion::IsReducedMotion()) {
            tabPillXSpring.Snap(tx);
            tabPillWSpring.Snap(tw);
            tabRimCol = dstRim;
            newBtnAlpha = (activeTab == 0) ? 1.0f : 0.0f;
            tabPillInit = true;
        } else {
            tabPillXSpring.target = tx;
            tabPillWSpring.target = tw;
        }
    }

    [[gnu::noinline]] void SwitchTab(int nextTab) {
        if (nextTab == activeTab) return;
        CancelVaultDrag(true);
        activeTab = nextTab;
        scrollY = 0.0f;
        scrollTargetY = 0.0f;
        rowShifts.clear();
        SyncTabPillTarget(false);
        if (activeTab == 1) {
            if (Motion::IsReducedMotion()) rankEnterSpring.Snap(1.0f);
            else { rankEnterSpring.Snap(0.0f); rankEnterSpring.target = 1.0f; }
        } else if (activeTab == 2) {
            edgePillInit = false;
            trayPillInit = false;
            SyncSettingsSprings(true);
        }
        if (!Motion::IsReducedMotion()) EnsureAnimTimer();
        UpdateSearchVisibility();
        if (activeTab == 0 && hSearchEdit && IsWindow(hSearchEdit)) SetFocus(hSearchEdit);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void SnapAllAnims() {
        scrollY = scrollTargetY;
        viewportScroll.hoverAnim = (viewportScroll.hovered || viewportScroll.dragging) ? 1.0f : 0.0f;
        rowShifts.clear();
        SyncTabPillTarget(true);
        searchFocusAnim = searchFocused ? 1.0f : 0.0f;
        clearBtnAnim = searchQuery.empty() ? 0.0f : 1.0f;
        filterPillXSpring.Snap(filterPillXSpring.target);
        filterPillWSpring.Snap(filterPillWSpring.target);
        filterAccentCol = filterAccentTarget;
        cfgBtnAlpha = (activeTab == 0 && assignTargetSlot < 0 && EffectiveVaultFilter() >= 0) ? 1.0f : 0.0f;
        rankEnterSpring.Snap(1.0f);
        vaultDragLiftSpring.Snap(isVaultDragging ? 1.0f : 0.0f);
        edgePillXSpring.Snap(edgePillXSpring.target);
        trayPillXSpring.Snap(trayPillXSpring.target);
        trayPillWSpring.Snap(trayPillWSpring.target);
        lockedSceneShakeTick = 0;
        lockedSceneShakeIdx = -1;
        SyncSettingsSprings(true);
        if (toastClosing) {
            toastMessage.clear();
            toastClosing = false;
            toastShowsUndo = false;
            toastSpring.Snap(0.0f);
        } else {
            toastSpring.Snap(toastSpring.target);
        }
    }

    [[gnu::noinline]] void CancelVaultDrag(bool rollback) {
        if (vaultDragIdx < 0 && !isVaultDragging) return;
        bool dragged = isVaultDragging;
        int curIdx = vaultDragIdx;
        int origIdx = vaultDragOrigIdx;
        vaultDragIdx = -1;
        vaultDragOrigIdx = -1;
        isVaultDragging = false;
        vaultDragLiftSpring.Snap(0.0f);
        if (dragScrollTimerActive && hwnd && IsWindow(hwnd)) {
            KillTimer(hwnd, kTimerDragScroll);
            dragScrollTimerActive = false;
        }
        if (hwnd && GetCapture() == hwnd) ReleaseCapture();
        if (rollback && dragged && state && curIdx >= 0 && origIdx >= 0 &&
            curIdx < (int)state->vault.size() && origIdx < (int)state->vault.size() && curIdx != origIdx) {
            ReorderVaultItemOnly(*state, (size_t)curIdx, (size_t)origIdx);
            rowShifts.clear();
            selectedVaultRow = origIdx;
            InvalidateCaches();
        }
        if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void UpdateDragScrollTimer(float dy, float ch) {
        bool need = isVaultDragging && maxScrollY > 0.5f &&
                    ((dy < vaultListTopDip + 28.0f && scrollY > 0.1f) ||
                     (dy > ch - 36.0f && scrollY < maxScrollY - 0.1f));
        if (need && !dragScrollTimerActive) {
            if (hwnd && SetTimer(hwnd, kTimerDragScroll, 30, nullptr) != 0) dragScrollTimerActive = true;
        } else if (!need && dragScrollTimerActive) {
            if (hwnd) KillTimer(hwnd, kTimerDragScroll);
            dragScrollTimerActive = false;
        }
    }

    [[gnu::noinline]] void StepVaultDragReorder() {
        float relMidY = (vaultDragCurDipY + 34.0f - vaultListTopDip) + scrollY;
        int target = std::clamp((int)std::floor(relMidY / 76.0f), 0, (int)state->vault.size() - 1);
        if (target == vaultDragIdx) return;
        int fromIdx = vaultDragIdx;
        int dir = (target > fromIdx) ? 1 : -1;
        if (ReorderVaultItemOnly(*state, (size_t)fromIdx, (size_t)target)) {
            for (int k = fromIdx; k != target; k += dir) {
                AddRowShift(state->vault[(size_t)k].id, (float)dir * 76.0f);
            }
            vaultDragIdx = target;
            selectedVaultRow = target;
            InvalidateCaches();
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
        }
    }

    const DrawnCardHit* HitTestDrawnCard(float dx, float dy) const {
        for (int i = drawnCardHitCount - 1; i >= 0; --i) {
            const auto& c = drawnCardHits[i];
            if (dx >= c.rect.left && dx <= c.rect.right && dy >= c.rect.top && dy <= c.rect.bottom) return &c;
        }
        return nullptr;
    }

    void EnsureAnimTimer() {
        if (!StartWindowAnimTimer(hwnd, kTimerAnim, animActive, qpcFreq, lastAnimQpc)) {
            SnapAllAnims();
            if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void StopAnimTimer() {
        if (animActive && hwnd && IsWindow(hwnd)) {
            KillTimer(hwnd, kTimerAnim);
            animActive = false;
        }
    }

    [[gnu::noinline]] void OnAnimTick() {
        if (!hwnd || !IsWindowVisible(hwnd)) {
            StopAnimTimer();
            return;
        }
        float dt = StepDeltaQpc(qpcFreq, lastAnimQpc);

        bool moving = false;
        moving |= Motion::ExpApproach(scrollY, scrollTargetY, 20.0f, dt, 0.25f);
        moving |= viewportScroll.StepAnim(dt);
        moving |= tabPillXSpring.Step(dt, 0.15f, 1.0f);
        moving |= tabPillWSpring.Step(dt, 0.15f, 1.0f);
        moving |= Motion::ExpApproach(newBtnAlpha, activeTab == 0 ? 1.0f : 0.0f, 22.0f, dt);

        D2D1_COLOR_F dstTabRim = (activeTab == 0) ? Rgba(Theme::AccentBlue) : (activeTab == 1 ? Rgba(Theme::AccentCyan) : Rgba(Theme::AccentIndigo));
        moving |= Motion::ApproachColorRgb(tabRimCol, dstTabRim, 18.0f, dt);

        moving |= Motion::ExpApproach(searchFocusAnim, searchFocused ? 1.0f : 0.0f, 22.0f, dt);
        moving |= Motion::ExpApproach(clearBtnAnim, searchQuery.empty() ? 0.0f : 1.0f, 22.0f, dt);

        moving |= filterPillXSpring.Step(dt, 0.15f, 1.0f);
        moving |= filterPillWSpring.Step(dt, 0.15f, 1.0f);
        moving |= Motion::ApproachColorRgb(filterAccentCol, filterAccentTarget, 18.0f, dt);
        moving |= Motion::ExpApproach(cfgBtnAlpha, (activeTab == 0 && assignTargetSlot < 0 && EffectiveVaultFilter() >= 0) ? 1.0f : 0.0f, 22.0f, dt);

        moving |= rankEnterSpring.Step(dt);
        moving |= vaultDragLiftSpring.Step(dt);

        // Step displaced vault cards and prune settled entries
        for (size_t i = 0; i < rowShifts.size(); ) {
            if (rowShifts[i].sp.Step(dt, 0.2f, 1.2f)) {
                moving = true;
                ++i;
            } else {
                rowShifts.erase(rowShifts.begin() + (ptrdiff_t)i);
            }
        }

        if (activeTab == 2 && state) {
            moving |= swBubbleSpring.Step(dt);
            moving |= swAutoStartSpring.Step(dt);
            moving |= swAutoUpdSpring.Step(dt);
            moving |= edgePillXSpring.Step(dt, 0.15f, 1.0f);
            moving |= trayPillXSpring.Step(dt, 0.15f, 1.0f);
            moving |= trayPillWSpring.Step(dt, 0.15f, 1.0f);
            for (int s = 0; s < kMaxScenes; ++s) {
                moving |= Motion::ExpApproach(sceneCheckAnim[(size_t)s], state->scenes[(size_t)s].enabled ? 1.0f : 0.0f, 22.0f, dt);
            }
            if (!Motion::IsReducedMotion() && lockedSceneShakeTick != 0) {
                if ((GetTickCount64() - lockedSceneShakeTick) < 270) moving = true;
                else { lockedSceneShakeTick = 0; lockedSceneShakeIdx = -1; }
            }
            if (updater) {
                auto snap = updater->GetSnapshot();
                if (snap.phase == UpdatePhase::Checking) {
                    if (!Motion::IsReducedMotion()) {
                        updateShimmerPhase += dt * 1.15f;
                        if (updateShimmerPhase > 1.0f) updateShimmerPhase -= 1.0f;
                        moving = true;
                    }
                } else if (snap.phase == UpdatePhase::Downloading || snap.phase == UpdatePhase::Installing) {
                    float tgt = (snap.phase == UpdatePhase::Installing) ? 1.0f : std::clamp((float)snap.downloadPercent / 100.0f, 0.04f, 1.0f);
                    moving |= Motion::ExpApproach(updateProgAnim, tgt, 12.0f, dt, 0.003f);
                }
            }
        }

        moving |= toastSpring.Step(dt);
        ULONGLONG nowTick = GetTickCount64();
        if (toastClosing && toastSpring.value <= 0.015f) {
            toastMessage.clear();
            toastClosing = false;
            toastShowsUndo = false;
            toastSpring.Snap(0.0f);
        }
        bool undoLive = (!Motion::IsReducedMotion() && !toastMessage.empty() && !toastClosing &&
                         toastShowsUndo && undoDelete.active && nowTick < undoDelete.expireTick);
        if (undoLive) moving = true;

        bool copyFlashLive = (!Motion::IsReducedMotion() && !copiedFlashId.empty() && (nowTick - copiedFlashTick) < 1150);
        if (copyFlashLive) moving = true;
        else if (!copiedFlashId.empty() && (nowTick - copiedFlashTick) >= 1150) copiedFlashId.clear();

        InvalidateRect(hwnd, nullptr, FALSE);
        if (!moving) {
            StopAnimTimer();
        }
    }

    void InvalidateCaches() {
        vaultCacheDirty = true;
        rankCacheDirty = true;
    }

    static bool ReorderVaultItemOnly(AppState& st, size_t fromIdx, size_t toIdx) {
        if (fromIdx >= st.vault.size() || toIdx >= st.vault.size() || fromIdx == toIdx) return false;
        int step = (toIdx > fromIdx) ? 1 : -1;
        size_t cur = fromIdx;
        while (cur != toIdx) {
            size_t nxt = (size_t)((int)cur + step);
            std::swap(st.vault[cur], st.vault[nxt]);
            cur = nxt;
        }
        return true;
    }

    [[gnu::noinline]] VaultWindow() = default;

    [[gnu::noinline]] ~VaultWindow() {
        if (hSearchEdit && IsWindow(hSearchEdit) && origSearchProc) {
            SetWindowLongPtrW(hSearchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(origSearchProc));
            RemovePropW(hSearchEdit, kSearchProp);
        }
        if (hSearchFont) DeleteObject(hSearchFont);
        if (hSearchBrush) DeleteObject(hSearchBrush);
    }

    bool Init(HINSTANCE inst, AppState* st, StorageManager* stor, DarkPopupMenu* menu, std::function<void()> onChange) {
        hInst = inst;
        state = st;
        storage = stor;
        popupMenu = menu;
        onStateChanged = std::move(onChange);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"PromptPod_VaultWnd";
        RegisterClassExW(&wc);
        return true;
    }

    int EffectiveVaultFilter() const {
        if (assignTargetSlot >= 0 && assignTargetSlot < 10) return -1;
        if (vaultFilter >= 0 && vaultFilter < kMaxScenes) {
            if (state && state->scenes[(size_t)vaultFilter].enabled) return vaultFilter;
            return -1;
        }
        return (vaultFilter == -2) ? -2 : -1;
    }

    int TargetSceneForVaultAction() const {
        if (activeTab == 0) {
            int f = EffectiveVaultFilter();
            if (f >= 0 && f < kMaxScenes) return f;
        }
        return state ? state->EffectiveActiveSceneIndex() : 0;
    }

    int CountUnmountedVaultItems() const {
        if (!state) return 0;
        int n = 0;
        for (const auto& item : state->vault) {
            if (!state->IsMountedInAnyEnabledScene(item.id)) ++n;
        }
        return n;
    }

    void Open(int tab = 0, int targetSlot = -1, bool scrollToUpdate = false) {
        activeTab = tab;
        assignTargetSlot = targetSlot;
        if (targetSlot >= 0) vaultFilter = -1;
        scrollY = 0.0f;
        scrollTargetY = 0.0f;
        rowShifts.clear();
        filterPillInit = false;
        edgePillInit = false;
        trayPillInit = false;
        SyncTabPillTarget(true);
        SyncSettingsSprings(true);
        if (tab == 1 && !Motion::IsReducedMotion()) {
            rankEnterSpring.Snap(0.0f);
            rankEnterSpring.target = 1.0f;
        } else {
            rankEnterSpring.Snap(1.0f);
        }
        pendingScrollToUpdate = (tab == 2 && scrollToUpdate);
        InvalidateCaches();

        if (!hwnd || !IsWindow(hwnd)) {
            POINT pt{};
            GetCursorPos(&pt);
            RECT work = GetMonitorWorkAreaForPoint(pt);
            float sc = GetDpiScaleForPoint(pt);

            int wPx = (int)(760.0f * sc);
            int hPx = (int)(580.0f * sc);
            int x = 0, y = 0;
            ClampDialogRectToWorkArea(work, wPx, hPx, x, y);

            std::wstring wndTitle = UiText(L"\x8BCD\x8231 PromptPod \x00B7 \x7BA1\x7406\x4E0E\x8BBE\x7F6E");
            hwnd = CreateWindowExW(
                0,
                L"PromptPod_VaultWnd",
                wndTitle.c_str(),
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                x, y, wPx, hPx,
                nullptr, nullptr, hInst, this
            );
            if (!hwnd) return;
            ApplyWin11DarkWindowAttributes(hwnd);
            CreateSearchControl();
        }

        UpdateSearchVisibility();
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
        if (activeTab == 0 && hSearchEdit && IsWindow(hSearchEdit)) {
            SetFocus(hSearchEdit);
        }
        if (storage && storage->readOnlyRecoveryMode && !storage->loadFailureHint.empty()) {
            ShowToast(storage->loadFailureHint, true);
        }
        if (!rankEnterSpring.IsSettled()) EnsureAnimTimer();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void OpenSettingsAtUpdate() {
        Open(2, -1, true);
    }

    void ShowOrFocus(int defaultTab = 0) {
        if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            Open(defaultTab, -1);
        }
    }

    void ShowExternalToast(const std::wstring& msg, bool isErr) {
        if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
            Open(2, -1);
        }
        ShowToast(msg, isErr);
    }

    void RefreshIfOpen() {
        InvalidateCaches();
        SyncSettingsSprings(false);
        if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            if (activeTab == 2 && updater) {
                auto ph = updater->GetSnapshot().phase;
                if (ph == UpdatePhase::Checking || ph == UpdatePhase::Downloading || ph == UpdatePhase::Installing) {
                    EnsureAnimTimer();
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void OpenFeedbackModal() {
        if (editorModalOpen) return;
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        editorModalOpen = true;
        auto res = FeedbackDialog::Show(hInst, parent, state, storage);
        editorModalOpen = false;
        if (res.outcome == FeedbackDialog::Outcome::OpenedGitHub) {
            if (res.bodyCopiedToClipboard) {
                ShowToast(L"已打开 GitHub Issues，完整反馈内容已复制到剪贴板，请按 Ctrl+V 粘贴", false);
            } else if (res.includeDiagnostics && res.clipboardCopied) {
                ShowToast(L"已打开 GitHub Issues，脱敏诊断已复制到剪贴板", false);
            } else if (res.includeDiagnostics && !res.clipboardCopied) {
                ShowToast(L"已打开 GitHub Issues（剪贴板被占用，脱敏诊断未复制）", true);
            } else {
                ShowToast(L"已打开 GitHub Issues 反馈页面", false);
            }
        } else if (res.outcome == FeedbackDialog::Outcome::OpenedX) {
            if (res.clipboardCopied) {
                ShowToast(L"已打开 X 主页，脱敏诊断已复制到剪贴板", false);
            } else {
                ShowToast(L"已打开 X 主页（剪贴板被占用，脱敏诊断未复制）", true);
            }
        }
    }

    bool OpenRenameSceneModal(int sceneIdx) {
        if (editorModalOpen || !state || !storage || sceneIdx < 0 || sceneIdx >= kMaxScenes) return false;
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        editorModalOpen = true;
        bool ok = SceneRenameDialog::Show(hInst, parent, sceneIdx, state->scenes[(size_t)sceneIdx].name,
            [this, sceneIdx](const std::wstring& newName, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                candidate.scenes[(size_t)sceneIdx].name = newName;
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"只读保护模式：请先恢复备份或导入 data.json"
                        : L"保存失败：无法写入数据文件";
                    return false;
                }
                *state = std::move(candidate);
                return true;
            });
        editorModalOpen = false;
        if (!ok) return false;
        if (onStateChanged) onStateChanged();
        ShowToast(L"已更新场景名称为「" + state->scenes[(size_t)sceneIdx].name + L"」", false);
        RefreshIfOpen();
        return true;
    }

    bool OpenScenePromptsModal(int sceneIdx) {
        if (editorModalOpen || !state || !storage || sceneIdx < 0 || sceneIdx >= kMaxScenes) return false;
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        editorModalOpen = true;
        ScenePromptsDialog::Show(hInst, parent, state, storage, popupMenu, sceneIdx, [](void* ctx) {
            auto* self = static_cast<VaultWindow*>(ctx);
            self->InvalidateCaches();
            if (self->onStateChanged) self->onStateChanged();
            self->RefreshIfOpen();
        }, this);
        editorModalOpen = false;
        InvalidateCaches();
        RefreshIfOpen();
        return true;
    }

    bool ConfirmClearSceneSlots(int sceneIdx) {
        if (editorModalOpen || !state || !storage || sceneIdx < 0 || sceneIdx >= kMaxScenes) return false;
        int mounted = state->CountMountedInScene(sceneIdx);
        if (mounted == 0) return false;
        std::wstring sceneName = state->scenes[(size_t)sceneIdx].name;
        wchar_t body[256]{};
        _snwprintf_s(body, _countof(body), _TRUNCATE,
                      L"确定要清空场景「%ls」的 %d 个已装配槽位吗？\n\n"
                      L"所有 Prompt 正文、使用次数与其他场景编排都会完整保留在词舱中，不会删除任何条目。",
                      sceneName.c_str(), mounted);
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        editorModalOpen = true;
        bool confirmed = DarkConfirmDialog::Show(hInst, parent, L"清空「" + sceneName + L"」槽位", body, L"确认清空", L"取消", true);
        editorModalOpen = false;
        if (!confirmed) return false;

        auto prevSlots = state->scenes[(size_t)sceneIdx].slots;
        AppState candidate = *state;
        candidate.ClearSceneSlots(sceneIdx);
        if (!storage->Save(candidate)) {
            ShowToast(L"清空失败：当前处于只读保护或数据文件不可写", true);
            return false;
        }
        *state = std::move(candidate);
        undoDelete = UndoDeleteEntry{};
        undoDelete.active = true;
        undoDelete.isClearScene = true;
        undoDelete.clearedSceneIdx = sceneIdx;
        undoDelete.clearedSceneSlots = prevSlots;
        undoDelete.expireTick = GetTickCount64() + 5000;
        InvalidateCaches();
        if (onStateChanged) onStateChanged();
        ShowToast(L"已清空「" + sceneName + L"」槽位（词舱已保留）", false, 5000, true);
        RefreshIfOpen();
        return true;
    }

    [[gnu::noinline]] bool OpenNewPromptModal(int targetSlot = -1, int* outMountedSlot = nullptr, const std::wstring& initTitle = L"", bool preserveOccupied = false, int explicitSceneIdx = -1) {
        if (outMountedSlot) *outMountedSlot = -1;
        if (editorModalOpen) return false;
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        int targetScene = (explicitSceneIdx >= 0 && explicitSceneIdx < kMaxScenes)
            ? explicitSceneIdx
            : (state ? state->EffectiveActiveSceneIndex() : 0);
        int effectiveTarget = targetSlot;
        if (preserveOccupied && state && effectiveTarget >= 0 && effectiveTarget < 10 &&
            state->FindById(state->scenes[(size_t)targetScene].slots[(size_t)effectiveTarget]) != nullptr) {
            effectiveTarget = state->FindFirstEmptySlotInScene(targetScene);
        }
        std::wstring sceneName = state ? state->scenes[(size_t)targetScene].name : std::wstring();
        std::wstring hd;
        if (effectiveTarget >= 0) {
            std::wstring slotTag = (state && state->IsMultiSceneActive())
                ? (L"「" + sceneName + L" · 槽位 " + FormatSlotNum(effectiveTarget) + L"」")
                : (L"槽位 " + FormatSlotNum(effectiveTarget));
            hd = L"\x65B0\x5EFA Prompt \x5E76\x653E\x5165" + slotTag;
        } else if (explicitSceneIdx >= 0 && state && state->IsMultiSceneActive()) {
            bool scFull = (state->CountMountedInScene(targetScene) >= 10);
            hd = scFull
                ? (L"新建 Prompt 到词舱（「" + sceneName + L"」已满）")
                : (L"新建 Prompt 并加入「" + sceneName + L"」");
        } else {
            hd = L"\x65B0\x5EFA Prompt \x5230\x8BCD\x8231";
        }
        int mountedSlot = -1;
        std::wstring createdId;
        editorModalOpen = true;
        auto res = PromptEditorDialog::Show(hInst, parent, hd, initTitle, L"",
            [this, targetScene, effectiveTarget, preserveOccupied, &mountedSlot, &createdId](const std::wstring& title, const std::wstring& content, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                PromptItem item{};
                item.id = GeneratePromptId(&candidate);
                item.title = title;
                item.content = content;
                item.createdAt = NowUnixSeconds();
                candidate.vault.insert(candidate.vault.begin(), item);

                if (effectiveTarget >= 0 && effectiveTarget < 10) {
                    int assignIdx = effectiveTarget;
                    if (preserveOccupied && candidate.FindById(candidate.scenes[(size_t)targetScene].slots[(size_t)assignIdx]) != nullptr) {
                        assignIdx = candidate.FindFirstEmptySlotInScene(targetScene); // Never clobber an occupied slot during onboarding!
                    }
                    if (assignIdx >= 0 && assignIdx < 10) {
                        candidate.AssignToSlotInScene(targetScene, assignIdx, item.id);
                        mountedSlot = assignIdx;
                    }
                } else {
                    int emptySlot = candidate.FindFirstEmptySlotInScene(targetScene);
                    if (emptySlot >= 0) {
                        candidate.AssignToSlotInScene(targetScene, emptySlot, item.id);
                        mountedSlot = emptySlot;
                    }
                }

                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"\x53EA\x8BFB\x4FDD\x62A4\x6A21\x5F0F\xFF1A\x8BF7\x5148\x6062\x590D\x5907\x4EFD\x6216\x5BFC\x5165 data.json"
                        : L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x6570\x636E\x6587\x4EF6\xFF0C\x8BF7\x7A0D\x540E\x91CD\x8BD5";
                    return false;
                }
                createdId = item.id;
                *state = std::move(candidate);
                return true;
            });
        editorModalOpen = false;
        if (!res.accepted) return false;

        if (outMountedSlot) *outMountedSlot = mountedSlot;
        // Clear stale search filter and reset incompatible view filter so the newly created Prompt is immediately visible!
        if (!searchQuery.empty()) {
            searchQuery.clear();
            if (hSearchEdit && IsWindow(hSearchEdit)) SetWindowTextW(hSearchEdit, L"");
        }
        int curFilter = EffectiveVaultFilter();
        if ((curFilter == -2 && mountedSlot >= 0) || (curFilter >= 0 && (curFilter != targetScene || mountedSlot < 0))) {
            vaultFilter = -1;
            UpdateSearchCueBanner();
        }
        InvalidateCaches();
        EnsureVaultCache();
        selectedVaultRow = 0;
        for (int i = 0; i < (int)cachedFilteredIndices.size(); ++i) {
            if (state->vault[cachedFilteredIndices[(size_t)i]].id == createdId) {
                selectedVaultRow = i;
                break;
            }
        }
        scrollY = 0.0f;
        scrollTargetY = 0.0f;
        EnsureSelectedVaultRowVisible();
        if (onStateChanged) onStateChanged();
        if (mountedSlot >= 0) {
            std::wstring dest = (state && state->IsMultiSceneActive())
                ? (L"「" + state->scenes[(size_t)targetScene].name + L" · 槽位 " + FormatSlotNum(mountedSlot) + L"」")
                : (L"槽位 " + FormatSlotNum(mountedSlot));
            ShowToast(L"已保存并放入" + dest, false);
        } else if (explicitSceneIdx >= 0 && state && state->IsMultiSceneActive()) {
            ShowToast(L"已存入词舱（「" + state->scenes[(size_t)targetScene].name + L"」已满 10 格）", false);
        } else {
            ShowToast(L"\x5DF2\x4FDD\x5B58\x65B0 Prompt", false);
        }
        RefreshIfOpen();
        return true;
    }

    void OpenEditPromptModal(const std::wstring& promptId) {
        if (editorModalOpen) return;
        PromptItem* item = state->FindById(promptId);
        if (!item) return;
        HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        editorModalOpen = true;
        auto res = PromptEditorDialog::Show(hInst, parent, L"\x7F16\x8F91 Prompt", item->title, item->content,
            [this, promptId](const std::wstring& title, const std::wstring& content, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                PromptItem* target = candidate.FindById(promptId);
                if (!target) {
                    outErr = L"保存失败：原 Prompt 条目已不存在";
                    return false;
                }
                target->title = title;
                target->content = content;
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"\x53EA\x8BFB\x4FDD\x62A4\x6A21\x5F0F\xFF1A\x8BF7\x5148\x6062\x590D\x5907\x4EFD\x6216\x5BFC\x5165 data.json"
                        : L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x6570\x636E\x6587\x4EF6\xFF0C\x8BF7\x7A0D\x540E\x91CD\x8BD5";
                    return false;
                }
                *state = std::move(candidate);
                return true;
            });
        editorModalOpen = false;
        if (!res.accepted) return;

        if (onStateChanged) onStateChanged();
        ShowToast(L"\x5DF2\x66F4\x65B0 Prompt", false);
        RefreshIfOpen();
    }

    void ExportBackupDialog() {
        std::wstring fileBuf(4096, L'\0');
        wcsncpy_s(fileBuf.data(), fileBuf.size(), L"PromptPod-backup.json", _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        ofn.lpstrFilter = L"\x8BCD\x8231\x5907\x4EFD (*.json)\0*.json\0\x65E7\x7248\x517C\x5BB9\x5907\x4EFD v2 (*.json)\0*.json\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuf.data();
        ofn.nMaxFile = (DWORD)fileBuf.size();
        ofn.lpstrDefExt = L"json";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&ofn)) {
            if (storage && storage->IsProtectedInternalPath(fileBuf.c_str())) {
                ShowToast(L"禁止覆盖现役 data.json 或 .bak，请另选备份路径", true);
                return;
            }
            bool compatV2 = (ofn.nFilterIndex == 2);
            if (StorageManager::WriteAllBytesAtomic(fileBuf.c_str(), SerializeStateToJson(*state, compatV2))) {
                ShowToast(compatV2 ? L"已导出旧版兼容备份（含全部词舱与主场景）" : L"\x5DF2\x5BFC\x51FA JSON \x5907\x4EFD", false);
            } else {
                ShowToast(L"\x5BFC\x51FA\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x76EE\x6807\x8DEF\x5F84", true);
            }
        } else if (CommDlgExtendedError() != 0) {
            ShowToast(L"\x6253\x5F00\x5BFC\x51FA\x5BF9\x8BDD\x6846\x5931\x8D25", true);
        }
    }

    void ImportBackupDialog() {
        std::wstring fileBuf(4096, L'\0');
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
        ofn.lpstrFilter = L"\x8BCD\x8231\x5907\x4EFD (*.json)\0*.json\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
        ofn.lpstrFile = fileBuf.data();
        ofn.nMaxFile = (DWORD)fileBuf.size();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            std::string bytes;
            AppState imported{};
            if (!StorageManager::ReadAllBytes(fileBuf.c_str(), bytes) || !ParseStateFromJson(bytes, imported)) {
                ShowToast(L"\x5BFC\x5165\x5931\x8D25\xFF1AJSON \x683C\x5F0F\x65E0\x6548\x6216\x5B57\x6BB5\x7F3A\x5931", true);
                return;
            }

            int importedMounted = imported.CountMountedInScene(imported.EffectiveActiveSceneIndex());
            wchar_t confirmBody[512]{};
            if (imported.IsMultiSceneActive()) {
                _snwprintf_s(
                    confirmBody, _countof(confirmBody), _TRUNCATE,
                    L"即将导入 %u 条 Prompt（含 %d 组场景，当前场景已装配 %d / 10 格），替换本地现有的 %u 条记录。\n\n"
                    L"覆盖前会自动备份当前词库到数据目录，并保留本机的贴边位置与偏好设置。",
                    (unsigned)imported.vault.size(), imported.EnabledSceneCount(), importedMounted, (unsigned)state->vault.size());
            } else {
                _snwprintf_s(
                    confirmBody, _countof(confirmBody), _TRUNCATE,
                    L"即将导入 %u 条 Prompt（已装配 %d / 10 格），替换本地现有的 %u 条记录。\n\n"
                    L"覆盖前会自动备份当前词库到数据目录，并保留本机的贴边位置与偏好设置。",
                    (unsigned)imported.vault.size(), importedMounted, (unsigned)state->vault.size());
            }

            HWND parent = (hwnd && IsWindowVisible(hwnd)) ? hwnd : nullptr;
            editorModalOpen = true;
            bool confirmed = DarkConfirmDialog::Show(hInst, parent, L"确认导入备份并覆盖词库", confirmBody, L"确认导入", L"取消", true);
            editorModalOpen = false;
            if (!confirmed) {
                return;
            }

            // Preserve current dock position & local preferences while importing vault & slot assignments
            imported.dockEdge = state->dockEdge;
            imported.dockRatioY = state->dockRatioY;
            imported.floatX = state->floatX;
            imported.floatY = state->floatY;
            imported.showHoverBubble = state->showHoverBubble;
            imported.autoCheckUpdates = state->autoCheckUpdates;
            imported.lastUpdateCheckAt = state->lastUpdateCheckAt;
            imported.onboardingCompleted = state->onboardingCompleted;
            imported.trayClickOpensVault = state->trayClickOpensVault;
            imported.hasDockAnchor = state->hasDockAnchor;
            imported.dockAnchorX = state->dockAnchorX;
            imported.dockAnchorY = state->dockAnchorY;

            // Persist FIRST (with verified pre-import snapshot / corrupt archive); only publish after atomic save succeeds!
            std::wstring snapPath;
            std::wstring saveErr;
            if (!storage->SaveExplicitReplacement(imported, &snapPath, &saveErr)) {
                ShowToast(saveErr.empty()
                    ? L"\x5BFC\x5165\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x672C\x5730\x6570\x636E"
                    : saveErr, true);
                return;
            }
            *state = std::move(imported);
            UpdateSearchVisibility();
            if (onStateChanged) onStateChanged();

            wchar_t msg[128]{};
            wsprintfW(msg, L"\x5DF2\x6210\x529F\x5BFC\x5165 %u \x6761 Prompt\xFF08\x5DF2\x7559\x5B58\x672C\x5730\x5FEB\x7167\xFF09", (unsigned)state->vault.size());
            ShowToast(msg, false);
            RefreshIfOpen();
        } else if (CommDlgExtendedError() != 0) {
            ShowToast(L"\x6253\x5F00\x5BFC\x5165\x5BF9\x8BDD\x6846\x5931\x8D25", true);
        }
    }

    bool DeletePromptWithUndo(const std::wstring& promptId) {
        if (!state || !storage) return false;
        size_t foundIdx = state->vault.size();
        for (size_t i = 0; i < state->vault.size(); ++i) {
            if (state->vault[i].id == promptId) { foundIdx = i; break; }
        }
        if (foundIdx >= state->vault.size()) return false;

        PromptItem removedItem = state->vault[foundIdx];
        std::array<int, kMaxScenes> prevSlots{ -1, -1, -1, -1 };
        for (int sc = 0; sc < kMaxScenes; ++sc) {
            prevSlots[(size_t)sc] = state->FindSlotOfIdInScene(sc, promptId);
        }

        AppState candidate = *state;
        for (int sc = 0; sc < kMaxScenes; ++sc) {
            for (auto& s : candidate.scenes[(size_t)sc].slots) {
                if (s == promptId) s.clear();
            }
        }
        candidate.vault.erase(candidate.vault.begin() + (ptrdiff_t)foundIdx);

        if (!storage->Save(candidate)) {
            ShowToast(L"\x5220\x9664\x5931\x8D25\xFF1A\x65E0\x6CD5\x5199\x5165\x6570\x636E\x6587\x4EF6", true);
            return false;
        }
        *state = std::move(candidate);
        undoDelete = UndoDeleteEntry{};
        undoDelete.active = true;
        undoDelete.isClearScene = false;
        undoDelete.item = std::move(removedItem);
        undoDelete.origIndex = foundIdx;
        undoDelete.origSlots = prevSlots;
        undoDelete.expireTick = GetTickCount64() + 5000;
        InvalidateCaches();
        if (onStateChanged) onStateChanged();
        ShowToast(L"已删除「" + CollapseWhitespaceSingleLine(undoDelete.item.title, 18) + L"」", false, 5000, true);
        return true;
    }

    bool UndoLastDelete() {
        if (!undoDelete.active || !state || !storage) return false;
        if (GetTickCount64() > undoDelete.expireTick) {
            undoDelete.active = false;
            return false;
        }
        AppState candidate = *state;
        bool wasClearScene = undoDelete.isClearScene;
        if (wasClearScene) {
            if (undoDelete.clearedSceneIdx >= 0 && undoDelete.clearedSceneIdx < kMaxScenes) {
                if (candidate.CountMountedInScene(undoDelete.clearedSceneIdx) > 0) {
                    undoDelete.active = false;
                    ShowToast(L"该场景已重新编排，未覆盖现有槽位", false);
                    return false;
                }
                candidate.scenes[(size_t)undoDelete.clearedSceneIdx].slots = undoDelete.clearedSceneSlots;
                candidate.SanitizeSlots();
            }
        } else {
            if (!candidate.FindById(undoDelete.item.id)) {
                size_t insIdx = std::min(undoDelete.origIndex, candidate.vault.size());
                candidate.vault.insert(candidate.vault.begin() + (ptrdiff_t)insIdx, undoDelete.item);
            }
            for (int sc = 0; sc < kMaxScenes; ++sc) {
                int origSl = undoDelete.origSlots[(size_t)sc];
                if (origSl >= 0 && origSl < 10 && candidate.scenes[(size_t)sc].slots[(size_t)origSl].empty()) {
                    candidate.scenes[(size_t)sc].slots[(size_t)origSl] = undoDelete.item.id;
                }
            }
        }
        if (!storage->Save(candidate)) {
            ShowToast(L"撤销失败：无法写入数据文件", true);
            return false;
        }
        *state = std::move(candidate);
        undoDelete.active = false;
        InvalidateCaches();
        if (onStateChanged) onStateChanged();
        ShowToast(wasClearScene ? L"已恢复该场景原有槽位" : L"已撤销删除并恢复原槽位与统计", false);
        return true;
    }

    bool DuplicatePrompt(const std::wstring& promptId) {
        if (!state || !storage) return false;
        size_t foundIdx = state->vault.size();
        for (size_t i = 0; i < state->vault.size(); ++i) {
            if (state->vault[i].id == promptId) { foundIdx = i; break; }
        }
        if (foundIdx >= state->vault.size()) return false;
        AppState candidate = *state;
        PromptItem copyItem = candidate.vault[foundIdx];
        copyItem.id = GeneratePromptId(&candidate);
        copyItem.title = UiText(copyItem.title + L"（副本）");
        copyItem.useCount = 0;
        copyItem.createdAt = NowUnixSeconds();
        copyItem.lastUsedAt = 0;
        candidate.vault.insert(candidate.vault.begin() + (ptrdiff_t)(foundIdx + 1), std::move(copyItem));
        if (!storage->Save(candidate)) {
            ShowToast(L"创建副本失败：无法写入数据文件", true);
            return false;
        }
        *state = std::move(candidate);
        InvalidateCaches();
        if (onStateChanged) onStateChanged();
        ShowToast(L"已在词舱创建副本", false);
        return true;
    }

private:
    static std::wstring FormatSlotNum(int idx) {
        wchar_t b[8]{};
        wsprintfW(b, L"%02d", idx + 1);
        return b;
    }

    [[gnu::noinline]] void ShowToast(const std::wstring& msg, bool isErr, ULONGLONG durationMs = 2600, bool keepUndo = false) {
        toastShowsUndo = (keepUndo && undoDelete.active);
        toastMessage = UiText(msg);
        toastIsError = isErr;
        toastClosing = false;
        toastTick = GetTickCount64();
        toastDurationMs = durationMs;
        if (Motion::IsReducedMotion()) {
            toastSpring.Snap(1.0f);
        } else {
            toastSpring.Snap(0.0f);
            toastSpring.target = 1.0f;
        }
        if (hwnd && IsWindow(hwnd)) {
            SetTimer(hwnd, kTimerFeedback, (UINT)durationMs, nullptr);
            if (!Motion::IsReducedMotion()) EnsureAnimTimer();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void AssignPromptToTargetSlot(const std::wstring& promptId) {
        if (!state || !storage || assignTargetSlot < 0 || assignTargetSlot >= 10) return;
        int assigned = assignTargetSlot;
        AppState candidate = *state;
        candidate.AssignToSlot(assigned, promptId);
        if (!storage->Save(candidate)) {
            ShowToast(L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x66F4\x65B0\x69FD\x4F4D", true);
            return;
        }
        *state = std::move(candidate);
        assignTargetSlot = -1;
        InvalidateCaches();
        UpdateSearchVisibility();
        if (onStateChanged) onStateChanged();
        std::wstring dest = state->IsMultiSceneActive()
            ? (L"「" + state->ActiveScene().name + L" · 槽位 " + FormatSlotNum(assigned) + L"」")
            : (L"槽位 " + FormatSlotNum(assigned));
        ShowToast(L"\x5DF2\x653E\x5165" + dest, false);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void EnsureVaultCache() {
        if (!vaultCacheDirty || !state) return;
        cachedFilteredIndices.clear();
        cachedFilteredIndices.reserve(state->vault.size());
        auto matchesQuery = [&](const PromptItem& item) {
            return searchQuery.empty() ||
                   ContainsIgnoreCase(item.title, searchQuery) ||
                   ContainsIgnoreCase(item.content, searchQuery);
        };
        int effFilter = EffectiveVaultFilter();
        if (effFilter >= 0 && effFilter < kMaxScenes) {
            // Scene view: display mounted prompts strictly in dock slot order (01 .. 10)
            const auto& sl = state->scenes[(size_t)effFilter].slots;
            for (int i = 0; i < 10; ++i) {
                if (sl[(size_t)i].empty()) continue;
                for (size_t vi = 0; vi < state->vault.size(); ++vi) {
                    if (state->vault[vi].id == sl[(size_t)i]) {
                        if (matchesQuery(state->vault[vi])) cachedFilteredIndices.push_back(vi);
                        break;
                    }
                }
            }
        } else {
            for (size_t vi = 0; vi < state->vault.size(); ++vi) {
                const auto& item = state->vault[vi];
                if (effFilter == -2 && state->IsMountedInAnyEnabledScene(item.id)) continue;
                if (matchesQuery(item)) {
                    cachedFilteredIndices.push_back(vi);
                }
            }
        }
        if (cachedFilteredIndices.empty()) {
            selectedVaultRow = -1;
        } else if (!searchQuery.empty()) {
            selectedVaultRow = std::clamp(selectedVaultRow < 0 ? 0 : selectedVaultRow, 0, (int)cachedFilteredIndices.size() - 1);
        } else if (selectedVaultRow >= (int)cachedFilteredIndices.size()) {
            selectedVaultRow = (int)cachedFilteredIndices.size() - 1;
        }
        vaultCacheDirty = false;
    }

    void EnsureRankCache() {
        if (!rankCacheDirty || !state) return;
        cachedRankedIndices.clear();
        cachedRankedIndices.reserve(state->vault.size());
        for (size_t i = 0; i < state->vault.size(); ++i) cachedRankedIndices.push_back(i);
        std::sort(cachedRankedIndices.begin(), cachedRankedIndices.end(), [this](size_t a, size_t b) {
            const auto& ia = state->vault[a];
            const auto& ib = state->vault[b];
            if (ia.useCount != ib.useCount) return ia.useCount > ib.useCount;
            if (ia.lastUsedAt != ib.lastUsedAt) return ia.lastUsedAt > ib.lastUsedAt;
            return a < b;
        });
        rankCacheDirty = false;
    }

    void EnsureSelectedVaultRowVisible() {
        if (activeTab != 0 || selectedVaultRow < 0) return;
        float rowTop = (float)selectedVaultRow * 76.0f;
        float rowBot = rowTop + 68.0f;
        float nextY = scrollTargetY;
        if (rowTop < nextY) {
            nextY = rowTop;
        } else if (rowBot > nextY + lastViewportH) {
            nextY = std::max(0.0f, rowBot - lastViewportH + 8.0f);
        }
        scrollTargetY = std::clamp(nextY, 0.0f, maxScrollY);
        if (Motion::IsReducedMotion()) {
            scrollY = scrollTargetY;
        } else {
            EnsureAnimTimer();
        }
    }

    void ClearSearchQueryAndFocus() {
        searchQuery.clear();
        selectedVaultRow = -1;
        scrollY = 0.0f;
        scrollTargetY = 0.0f;
        InvalidateCaches();
        if (hSearchEdit && IsWindow(hSearchEdit)) {
            SetWindowTextW(hSearchEdit, L"");
            SetFocus(hSearchEdit);
        }
        if (!Motion::IsReducedMotion()) EnsureAnimTimer();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void CopyPromptById(const std::wstring& pid) {
        if (!state) return;
        PromptItem* item = state->FindById(pid);
        if (!item) return;
        if (CopyTextToClipboard(hwnd, item->content)) {
            item->useCount += 1;
            item->lastUsedAt = NowUnixSeconds();
            storage->Save(*state);
            copiedFlashId = item->id;
            copiedFlashTick = GetTickCount64();
            InvalidateCaches();
            if (Motion::IsReducedMotion()) {
                if (hwnd && IsWindow(hwnd)) {
                    SetTimer(hwnd, kTimerCopyClear, 1100, nullptr);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else {
                EnsureAnimTimer();
            }
            if (onStateChanged) onStateChanged();
            ShowToast(L"已复制「" + CollapseWhitespaceSingleLine(item->title, 20) + L"」", false, 1800);
        } else {
            ShowToast(L"\x590D\x5236\x5931\x8D25\xFF1A\x526A\x8D34\x677F\x88AB\x5176\x4ED6\x7A0B\x5E8F\x5360\x7528", true);
        }
    }

    bool HandleShortcutKey(WPARAM vk, bool fromSearchEdit) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        // When a Vault popup menu is open, it exclusively owns navigation/confirm/Esc and blocks bleed-through shortcuts!
        if (popupMenu && popupMenu->IsOpenFor(MenuOwner::Vault)) {
            if (popupMenu->HandleForwardedKey(vk)) return true;
            if (vk == VK_TAB || (ctrl && !alt && (vk == 'Z' || vk == 'F' || vk == 'N'))) return true;
        }
        if (ctrl && !alt && vk == 'F') {
            if (activeTab != 0) {
                SwitchTab(0);
            }
            if (hSearchEdit && IsWindow(hSearchEdit)) {
                SetFocus(hSearchEdit);
                SendMessageW(hSearchEdit, EM_SETSEL, 0, -1);
            }
            return true;
        }
        if (ctrl && !alt && vk == 'N') {
            int sc = (assignTargetSlot < 0 && activeTab == 0 && EffectiveVaultFilter() >= 0) ? EffectiveVaultFilter() : -1;
            OpenNewPromptModal(assignTargetSlot, nullptr, L"", false, sc);
            return true;
        }
        if (ctrl && !alt && vk == 'A' && fromSearchEdit && hSearchEdit) {
            SendMessageW(hSearchEdit, EM_SETSEL, 0, -1);
            return true;
        }
        if (ctrl && !alt && vk == 'Z' && (!fromSearchEdit || searchQuery.empty()) && undoDelete.active) {
            UndoLastDelete();
            return true;
        }
        if (vk == VK_ESCAPE) {
            if (isVaultDragging || vaultDragIdx >= 0) {
                CancelVaultDrag(true);
                return true;
            }
            if (activeTab == 0 && !searchQuery.empty()) {
                ClearSearchQueryAndFocus();
            } else if (assignTargetSlot >= 0) {
                assignTargetSlot = -1;
                UpdateSearchVisibility();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return true;
        }
        if (activeTab == 0) {
            EnsureVaultCache();
            if (vk == VK_DOWN) {
                if (!cachedFilteredIndices.empty()) {
                    selectedVaultRow = (selectedVaultRow < 0) ? 0 : std::min((int)cachedFilteredIndices.size() - 1, selectedVaultRow + 1);
                    EnsureSelectedVaultRowVisible();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return true;
            }
            if (vk == VK_UP) {
                if (!cachedFilteredIndices.empty()) {
                    selectedVaultRow = (selectedVaultRow <= 0) ? 0 : (selectedVaultRow - 1);
                    EnsureSelectedVaultRowVisible();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return true;
            }
            if (vk == VK_RETURN) {
                if (!cachedFilteredIndices.empty()) {
                    int rowIdx = (selectedVaultRow >= 0 && selectedVaultRow < (int)cachedFilteredIndices.size()) ? selectedVaultRow : 0;
                    selectedVaultRow = rowIdx;
                    size_t vi = cachedFilteredIndices[(size_t)rowIdx];
                    if (vi < state->vault.size()) {
                        if (assignTargetSlot >= 0) AssignPromptToTargetSlot(state->vault[vi].id);
                        else CopyPromptById(state->vault[vi].id);
                    }
                }
                return true;
            }
        }
        return false;
    }

    static LRESULT CALLBACK SearchEditSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = static_cast<VaultWindow*>(GetPropW(h, kSearchProp));
        if (!self || !self->origSearchProc) return DefWindowProcW(h, msg, wp, lp);
        WNDPROC orig = self->origSearchProc;
        if (msg == WM_NCDESTROY) {
            RemovePropW(h, kSearchProp);
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(orig));
            self->origSearchProc = nullptr;
            return CallWindowProcW(orig, h, msg, wp, lp);
        }
        if (msg == WM_KEYDOWN) {
            if (self->HandleShortcutKey(wp, true)) return 0;
        }
        if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) {
            return 0; // Suppress system beep on handled Enter/Esc in single-line EDIT
        }
        return CallWindowProcW(orig, h, msg, wp, lp);
    }

    void RebuildSearchFont() {
        HFONT nextFont = CreateUiInputFont(12.5f, dpiScale);
        if (nextFont) {
            if (hSearchEdit) SendMessageW(hSearchEdit, WM_SETFONT, (WPARAM)nextFont, TRUE);
            if (hSearchFont) DeleteObject(hSearchFont);
            hSearchFont = nextFont;
        }
    }

    [[gnu::noinline]] void UpdateSearchCueBanner() {
        if (!hSearchEdit || !IsWindow(hSearchEdit)) return;
        std::wstring cue;
        if (assignTargetSlot >= 0 && assignTargetSlot < 10) {
            cue = UiText(L"搜索要放入槽位 " + FormatSlotNum(assignTargetSlot) + L" 的 Prompt（↑↓ 选择，Enter 放入）");
        } else {
            int eff = EffectiveVaultFilter();
            if (eff >= 0 && state) {
                cue = UiText(L"在「" + state->scenes[(size_t)eff].name + L"」中搜索（↑↓ 选择，Enter 复制，Esc 清空）");
            } else if (eff == -2) {
                cue = UiText(L"在未装配词舱中搜索（↑↓ 选择，Enter 复制，Esc 清空）");
            } else {
                cue = UiText(L"搜索标题或正文（↑↓ 选择，Enter 复制，Esc 清空）");
            }
        }
        SendMessageW(hSearchEdit, 0x1501 /*EM_SETCUEBANNER*/, TRUE, reinterpret_cast<LPARAM>(cue.c_str()));
    }

    void CreateSearchControl() {
        dpiScale = GetDpiScaleForWindow(hwnd);
        if (!hSearchBrush) hSearchBrush = CreateSolidBrush(RGB(27, 31, 43));
        hSearchEdit = CreateWindowExW(
            0, L"EDIT", searchQuery.c_str(),
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 180, 24,
            hwnd, (HMENU)2001, hInst, nullptr
        );
        if (hSearchEdit) {
            SetPropW(hSearchEdit, kSearchProp, this);
            origSearchProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(hSearchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchEditSubProc)));
        }
        RebuildSearchFont();
        UpdateSearchCueBanner();
        PositionSearchControl();
    }

    float ComputeContentTopDip() const {
        float topY = 66.0f;
        if (storage && storage->readOnlyRecoveryMode) {
            topY += 48.0f;
        }
        if ((activeTab == 0 || activeTab == 1) && assignTargetSlot >= 0 && assignTargetSlot < 10) {
            topY += 40.0f;
        }
        return topY;
    }

    void PositionSearchControl() {
        if (!hwnd || !hSearchEdit) return;
        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float topY = ComputeContentTopDip();
        int sx = (int)(26.0f * dpiScale);
        int sy = (int)((topY + 6.0f) * dpiScale);
        int sw = (int)((cw - 78.0f) * dpiScale); // Reserve 26 DIP on right for clear '×' button
        int sh = (int)(22.0f * dpiScale);
        SetWindowPos(hSearchEdit, nullptr, sx, sy, std::max(80, sw), sh, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void UpdateSearchVisibility() {
        if (!hSearchEdit) return;
        UpdateSearchCueBanner();
        PositionSearchControl();
        ShowWindow(hSearchEdit, activeTab == 0 ? SW_SHOW : SW_HIDE);
    }

    void EnsureD2D() {
        if (!fmtTitle) {
            auto& eng = D2DEngine::Instance();
            fmtTitle = eng.CreateFormat(15.0f, DWRITE_FONT_WEIGHT_BOLD);
            fmtTab = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtSection = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_BOLD);
            fmtCardTitle = eng.CreateFormat(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtCardBody = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtDescWrap = eng.CreateFormat(11.8f, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            if (fmtDescWrap) fmtDescWrap->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 16.5f, 13.0f);
            fmtBadge = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_MEDIUM, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtBigNum = eng.CreateFormat(22.0f, DWRITE_FONT_WEIGHT_BOLD);
            fmtKpiTitle = eng.CreateFormat(14.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtRank = eng.CreateFormat(13.0f, DWRITE_FONT_WEIGHT_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (rt && brush) return;
        dpiScale = GetDpiScaleForWindow(hwnd);
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    static bool ContainsIgnoreCase(const std::wstring& hay, const std::wstring& needle) {
        if (needle.empty()) return true;
        if (hay.size() < needle.size()) return false;
        auto toLow = [](wchar_t c) -> wchar_t {
            return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
        };
        for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                if (toLow(hay[i + j]) != toLow(needle[j])) { ok = false; break; }
            }
            if (ok) return true;
        }
        return false;
    }

    [[gnu::noinline]] int AddZone(int kind, const D2D1_RECT_F& r) {
        int idx = (int)hitZones.size();
        hitZones.push_back(HitZone{ kind, {}, r });
        return idx;
    }

    [[gnu::noinline]] int AddZone(int kind, const D2D1_RECT_F& r, const std::wstring& pid) {
        int idx = (int)hitZones.size();
        hitZones.push_back(HitZone{ kind, pid, r });
        return idx;
    }

    [[gnu::noinline]] int AddClippedZone(int kind, const D2D1_RECT_F& r, const D2D1_RECT_F& clip) {
        D2D1_RECT_F cr = D2D1::RectF(
            std::max(r.left, clip.left),
            std::max(r.top, clip.top),
            std::min(r.right, clip.right),
            std::min(r.bottom, clip.bottom)
        );
        if (cr.right <= cr.left || cr.bottom <= cr.top) return -1;
        int idx = (int)hitZones.size();
        hitZones.push_back(HitZone{ kind, {}, cr });
        return idx;
    }

    [[gnu::noinline]] int AddClippedZone(int kind, const D2D1_RECT_F& r, const D2D1_RECT_F& clip, const std::wstring& pid) {
        D2D1_RECT_F cr = D2D1::RectF(
            std::max(r.left, clip.left),
            std::max(r.top, clip.top),
            std::min(r.right, clip.right),
            std::min(r.bottom, clip.bottom)
        );
        if (cr.right <= cr.left || cr.bottom <= cr.top) return -1;
        int idx = (int)hitZones.size();
        hitZones.push_back(HitZone{ kind, pid, cr });
        return idx;
    }

    [[gnu::noinline]] void DrawUiText(IDWriteTextFormat* fmt, const D2D1_RECT_F& r, const wchar_t* raw, const D2D1_COLOR_F& col) {
        if (!fmt || !raw || !*raw) return;
        std::wstring s = UiText(raw);
        rt->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, Br(col));
    }

    [[gnu::noinline]] void DrawStatusBadge(const D2D1_RECT_F& r, const std::wstring& txt,
                                           uint32_t bgHex, float bgAlpha, uint32_t fgHex, float fgAlpha) {
        FillRRHex(rt.Get(), brush.Get(), r, 5.0f, bgHex, bgAlpha);
        DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), r, txt.c_str(), fgHex, fgAlpha);
    }

    [[gnu::noinline]] void DrawPillButton(const D2D1_RECT_F& r, const wchar_t* text, int zoneIdx,
                                          uint32_t baseBg, uint32_t hoverBg, uint32_t textHex,
                                          float borderAlpha = 0.12f, float alphaScale = 1.0f) {
        bool isPressed = (zoneIdx >= 0 && zoneIdx < (int)hitZones.size() &&
                          hitZones[(size_t)zoneIdx].kind == pressedZoneKind &&
                          hitZones[(size_t)zoneIdx].promptId == pressedZonePromptId);
        DrawDarkPillButton(rt.Get(), brush.Get(), fmtBadge.Get(), r, text, zoneIdx == hoverZoneIdx, baseBg, hoverBg, textHex, borderAlpha, isPressed, alphaScale);
    }

    void DrawPillButton(const D2D1_RECT_F& r, const std::wstring& text, int zoneIdx,
                        uint32_t baseBg, uint32_t hoverBg, uint32_t textHex,
                        float borderAlpha = 0.12f, float alphaScale = 1.0f) {
        DrawPillButton(r, text.c_str(), zoneIdx, baseBg, hoverBg, textHex, borderAlpha, alphaScale);
    }

    [[gnu::noinline]] void DrawCopyActionButton(const D2D1_RECT_F& r, int zoneIdx, const std::wstring& itemId, ULONGLONG nowTick) {
        bool isFlash = (copiedFlashId == itemId && nowTick >= copiedFlashTick && (nowTick - copiedFlashTick) < 1100);
        if (!isFlash) {
            DrawPillButton(r, L"\x590D\x5236", zoneIdx, 0x222736, 0x2D3448, Theme::TextPrimary);
            return;
        }
        bool reduced = Motion::IsReducedMotion();
        float elapsed = (float)(nowTick - copiedFlashTick);
        float enterT = reduced ? 1.0f : Motion::EaseOutCubic(std::clamp(elapsed / 140.0f, 0.0f, 1.0f));
        float exitT  = reduced ? 1.0f : ((elapsed > 860.0f) ? (1.0f - Motion::EaseOutCubic(std::clamp((elapsed - 860.0f) / 240.0f, 0.0f, 1.0f))) : 1.0f);
        float k = enterT * exitT;
        bool hov = (zoneIdx == hoverZoneIdx);
        D2D1_COLOR_F bgCol = LerpColor(Rgba(hov ? 0x2D3448 : 0x222736, 1.0f), Rgba(hov ? 0x18664B : 0x14533D, 1.0f), k);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), Br(bgCol));
        DrawRim(rt.Get(), r, 6.0f, LerpColor(Rgba(0xFFFFFF, 0.12f), Rgba(Theme::CopyGreen, 0.48f), k), Rgba(0xFFFFFF, 0.04f));
        float ckProg = reduced ? 1.0f : Motion::EaseOutCubic(std::clamp(elapsed / 140.0f, 0.0f, 1.0f));
        DrawAnimatedCheckmark(rt.Get(), brush.Get(), r.left + 10.0f, (r.top + r.bottom) * 0.5f, 7.6f, ckProg, Rgba(Theme::CopyGreen, k), 1.65f);
        if (fmtBadge) {
            std::wstring lbl = UiText(L"\x5DF2\x590D\x5236");
            rt->DrawTextW(lbl.c_str(), (UINT32)lbl.size(), fmtBadge.Get(),
                          D2D1::RectF(r.left + 12.0f, r.top, r.right - 2.0f, r.bottom),
                          Br(LerpColor(Rgba(Theme::TextPrimary, 0.92f), Rgba(Theme::CopyGreen, 1.0f), k)));
        }
    }

    [[gnu::noinline]] float DrawSectionHeader(float yPos, float cw, const D2D1_RECT_F& vp, const wchar_t* title) {
        if (yPos + 24.0f >= vp.top && yPos <= vp.bottom) {
            DrawUiTextHex(rt.Get(), brush.Get(), fmtSection.Get(), D2D1::RectF(22.0f, yPos + 2.0f, cw - 24.0f, yPos + 22.0f), title, Theme::AccentCyan, 0.88f);
        }
        return yPos + 24.0f;
    }

    [[gnu::noinline]] D2D1_RECT_F DrawGroupContainer(float yPos, float h, float cw, const D2D1_RECT_F& vp, bool accent = false) {
        D2D1_RECT_F g = D2D1::RectF(18.0f, yPos, cw - 20.0f, yPos + h);
        if (g.bottom >= vp.top && g.top <= vp.bottom) {
            FillRMRimHex(rt.Get(), brush.Get(), g, 10.0f, Theme::CardBg, 0.95f, accent ? Theme::AccentCyan : 0xFFFFFF, accent ? 0.38f : 0.11f);
        }
        return g;
    }

    [[gnu::noinline]] void DrawRowDivider(const D2D1_RECT_F& grp, float yDiv, const D2D1_RECT_F& vp) {
        if (yDiv >= vp.top && yDiv <= vp.bottom) {
            rt->DrawLine(D2D1::Point2F(grp.left + 16.0f, yDiv), D2D1::Point2F(grp.right - 16.0f, yDiv),
                         Br(Rgba(0xFFFFFF, 0.06f)), 1.0f);
        }
    }

    [[gnu::noinline]] void DrawRowText(const D2D1_RECT_F& grp, float rowTop, float rowH, float rightReserve,
                                       const D2D1_RECT_F& vp, const wchar_t* title, const wchar_t* desc,
                                       bool wrapDesc = true, uint32_t descHex = Theme::TextSecondary) {
        if (rowTop + rowH < vp.top || rowTop > vp.bottom) return;
        DrawUiTextHex(rt.Get(), brush.Get(), fmtCardTitle.Get(), D2D1::RectF(grp.left + 16.0f, rowTop + 10.0f, grp.right - rightReserve, rowTop + 31.0f),
                      title, Theme::TextPrimary, 0.98f);
        DrawUiTextHex(rt.Get(), brush.Get(), wrapDesc ? fmtDescWrap.Get() : fmtCardBody.Get(),
                      D2D1::RectF(grp.left + 16.0f, rowTop + 32.0f, grp.right - rightReserve, rowTop + rowH - 6.0f),
                      desc, descHex, 0.88f);
    }

    [[gnu::noinline]] void DrawKpiCard(int idx, float cardW, float contentTop, const wchar_t* label,
                                       const wchar_t* val, const wchar_t* sub, bool isTextVal) {
        float lx = 18.0f + (float)idx * (cardW + 10.0f);
        D2D1_RECT_F r = D2D1::RectF(lx, contentTop, lx + cardW, contentTop + 76.0f);
        FillRMRimHex(rt.Get(), brush.Get(), r, 9.0f, Theme::CardBg, 0.95f, 0xFFFFFF, 0.12f);
        DrawUiTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(), D2D1::RectF(r.left + 14.0f, r.top + 8.0f, r.right - 12.0f, r.top + 28.0f),
                      label, Theme::TextSecondary, 0.88f);
        IDWriteTextFormat* vFmt = isTextVal ? fmtKpiTitle.Get() : fmtBigNum.Get();
        if (val && *val) {
            DrawTextHex(rt.Get(), brush.Get(), vFmt,
                        D2D1::RectF(r.left + 14.0f, isTextVal ? (r.top + 30.0f) : (r.top + 27.0f), r.right - 12.0f, r.top + 56.0f),
                        val, (UINT32)wcslen(val), Theme::TextPrimary);
        }
        if (sub && *sub) {
            DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), D2D1::RectF(r.left + 14.0f, r.top + 54.0f, r.right - 12.0f, r.bottom - 4.0f),
                          sub, Theme::AccentCyan, 0.85f);
        }
    }

    // Compact preset clipped button helper for Settings / empty states (0=Secondary, 1=Primary, 2=Cyan, 3=AccentBlue, 4=SubtleDark, 5=SubtleCyan, 6=SubtleAccent)
    [[gnu::noinline]] void DrawClippedPresetBtn(float l, float t, float r, float b, int kind, const wchar_t* label, int style, const D2D1_RECT_F& vp) {
        D2D1_RECT_F rc = D2D1::RectF(l, t, r, b);
        int z = (kind >= 0) ? AddClippedZone(kind, rc, vp) : -2;
        uint32_t bg = 0x222736, hov = 0x2D3448, fg = Theme::TextSecondary;
        float rim = 0.12f;
        if (style == 1) { fg = Theme::TextPrimary; }
        else if (style == 2) { fg = Theme::AccentCyan; }
        else if (style == 3) { bg = 0x366EF0; hov = 0x4B82F8; fg = 0xFFFFFF; rim = 0.28f; }
        else if (style == 4) { bg = 0x1B1F2B; hov = 0x262C3D; }
        else if (style == 5) { bg = 0x1B1F2B; hov = 0x262C3D; fg = Theme::AccentCyan; rim = 0.16f; }
        else if (style == 6) { bg = 0x1E3354; hov = 0x284570; fg = Theme::AccentCyan; rim = 0.24f; }
        DrawPillButton(rc, label, z, bg, hov, fg, rim);
    }

    [[gnu::noinline]] void DrawToggleSwitch(const D2D1_RECT_F& r, float animT, int zoneIdx) {
        DrawAnimatedToggleSwitch(rt.Get(), brush.Get(), r, animT, zoneIdx == hoverZoneIdx, 1.0f);
    }

    [[gnu::noinline]] void DrawToggleRow(const D2D1_RECT_F& g, float ry, float rowH, int kind, float animT, const D2D1_RECT_F& vp) {
        if (ry + rowH < vp.top || ry > vp.bottom) return;
        D2D1_RECT_F rowHit = D2D1::RectF(g.left + 4.0f, ry + 2.0f, g.right - 4.0f, ry + rowH - 2.0f);
        int zRow = AddClippedZone(kind, rowHit, vp);
        if (zRow == hoverZoneIdx) FillRRHex(rt.Get(), brush.Get(), rowHit, 7.0f, 0xFFFFFF, 0.03f);
        float topY = ry + (rowH - 24.0f) * 0.5f;
        DrawToggleSwitch(D2D1::RectF(g.right - 62.0f, topY, g.right - 16.0f, topY + 24.0f), animT, zRow);
    }

    [[gnu::noinline]] void DrawSegLbl(const D2D1_RECT_F& r, const wchar_t* t, bool sel, int z) {
        if (!sel && z == hoverZoneIdx) FillRRHex(rt.Get(), brush.Get(), r, 5.5f, 0xFFFFFF, 0.05f);
        DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), r, t, sel ? Theme::AccentCyan : Theme::TextSecondary, (sel || z == hoverZoneIdx) ? 1.0f : 0.88f);
    }

    [[gnu::noinline]] void DrawAssignBtn(const D2D1_RECT_F& r, int zoneIdx) {
        std::wstring lbl = (assignTargetSlot >= 0) ? (L"\x653E\x5165 " + FormatSlotNum(assignTargetSlot)) : L"\x69FD\x4F4D \x25BE";
        DrawPillButton(r, lbl, zoneIdx,
                       assignTargetSlot >= 0 ? 0x1E3A5F : 0x222736,
                       assignTargetSlot >= 0 ? 0x284D7E : 0x2D3448,
                       assignTargetSlot >= 0 ? Theme::AccentCyan : Theme::AccentBlue);
    }

    void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureD2D();
        if (!rt || !brush) {
            EndPaint(hwnd, &ps);
            return;
        }

        RECT crc{};
        GetClientRect(hwnd, &crc);
        float cw = (float)(crc.right - crc.left) / dpiScale;
        float ch = (float)(crc.bottom - crc.top) / dpiScale;

        hitZones.clear();
        drawnCardHitCount = 0;
        ULONGLONG nowTick = GetTickCount64();

        rt->BeginDraw();
        rt->Clear(Rgba(0x0F1219, 1.0f));

        // --- Top Bar ---
        rt->FillRectangle(D2D1::RectF(0, 0, cw, 56.0f), Br(Rgba(0x141822, 1.0f)));
        rt->DrawLine(D2D1::Point2F(0, 56.0f), D2D1::Point2F(cw, 56.0f), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);

        // Segmented Tabs (0: Vault, 1: Leaderboard, 2: Settings)
        D2D1_RECT_F segBg = D2D1::RectF(18.0f, 12.0f, 312.0f, 44.0f);
        FillRMRimHex(rt.Get(), brush.Get(), segBg, 8.0f, 0x0D0F15, 1.0f, 0xFFFFFF, 0.08f);

        D2D1_RECT_F tab0R = D2D1::RectF(21.0f, 15.0f, 138.0f, 41.0f);
        D2D1_RECT_F tab1R = D2D1::RectF(140.0f, 15.0f, 232.0f, 41.0f);
        D2D1_RECT_F tab2R = D2D1::RectF(234.0f, 15.0f, 309.0f, 41.0f);
        int zTab0 = AddZone(1, tab0R);
        int zTab1 = AddZone(2, tab1R);
        int zTab2 = AddZone(7, tab2R);

        // Smooth Apple-style sliding segmented tab pill with color-blended top highlight
        if (!tabPillInit) SyncTabPillTarget(true);
        D2D1_RECT_F activeTabR = D2D1::RectF(tabPillXSpring.value, 15.0f, tabPillXSpring.value + tabPillWSpring.value, 41.0f);
        FillRMRim(rt.Get(), brush.Get(), activeTabR, 6.0f, Rgba(0x23293A, 1.0f),
                  D2D1::ColorF(tabRimCol.r, tabRimCol.g, tabRimCol.b, 0.38f), Rgba(0xFFFFFF, 0.06f));

        wchar_t t0Buf[64]{};
        wsprintfW(t0Buf, L"\x8BCD\x8231\x6536\x85CF %u", (unsigned)state->vault.size());
        DrawUiTextHex(rt.Get(), brush.Get(), fmtTab.Get(), tab0R, t0Buf, activeTab == 0 ? Theme::TextPrimary : Theme::TextSecondary, zTab0 == hoverZoneIdx ? 1.0f : 0.9f);
        DrawUiTextHex(rt.Get(), brush.Get(), fmtTab.Get(), tab1R, L"\x4F7F\x7528\x6392\x884C", activeTab == 1 ? Theme::TextPrimary : Theme::TextSecondary, zTab1 == hoverZoneIdx ? 1.0f : 0.9f);
        DrawUiTextHex(rt.Get(), brush.Get(), fmtTab.Get(), tab2R, L"\x8BBE\x7F6E", activeTab == 2 ? Theme::TextPrimary : Theme::TextSecondary, zTab2 == hoverZoneIdx ? 1.0f : 0.9f);
        if (updater && updater->HasAvailableUpdate()) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tab2R.right - 10.0f, tab2R.top + 8.0f), 3.4f, 3.4f),
                            Br(Rgba(Theme::AccentCyan, 0.98f)));
        }

        // Top-right action bar scoped strictly by Tab: smoothly fades with Tab 0
        if (activeTab == 0 || newBtnAlpha > 0.02f) {
            D2D1_RECT_F rcNew = D2D1::RectF(cw - 126.0f, 13.0f, cw - 18.0f, 43.0f);
            int zNew = (activeTab == 0) ? AddZone(3, rcNew) : -2;
            DrawPillButton(rcNew, L"+ \x65B0\x5EFA Prompt", zNew, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.25f, newBtnAlpha);
        }

        float contentTop = 66.0f;

        // Persistent Read-Only Recovery Banner (visible across all tabs until user restores/imports)
        if (storage && storage->readOnlyRecoveryMode) {
            D2D1_RECT_F rban = D2D1::RectF(18.0f, contentTop - 2.0f, cw - 18.0f, contentTop + 38.0f);
            FillRMRimHex(rt.Get(), brush.Get(), rban, 7.0f, 0x2D1B18, 0.96f, 0xF59E0B, 0.55f, 0xF59E0B, 0.16f);

            bool canRestore = storage->recoveredFromBackup;
            float rightReserve = canRestore ? 220.0f : 196.0f;
            const wchar_t* rRaw = canRestore
                ? L"数据文件异常，已进入只读保护（原文件已保留，当前展示上次备份）"
                : (!storage->loadFailureHint.empty()
                    ? storage->loadFailureHint.c_str()
                    : L"数据文件异常，已进入只读保护（原文件已保留，可导入备份恢复）");
            DrawUiTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                          D2D1::RectF(rban.left + 12.0f, rban.top, rban.right - rightReserve, rban.bottom), rRaw, 0xFCD34D, 0.98f);
            if (canRestore) {
                D2D1_RECT_F rcRestore = D2D1::RectF(rban.right - 210.0f, rban.top + 6.0f, rban.right - 106.0f, rban.bottom - 6.0f);
                DrawPillButton(rcRestore, L"一键恢复备份", AddZone(32, rcRestore), 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
            } else {
                D2D1_RECT_F rcImp = D2D1::RectF(rban.right - 186.0f, rban.top + 6.0f, rban.right - 106.0f, rban.bottom - 6.0f);
                DrawPillButton(rcImp, L"导入备份", AddZone(4, rcImp), 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
            }
            D2D1_RECT_F rcOpenDir = D2D1::RectF(rban.right - 98.0f, rban.top + 6.0f, rban.right - 8.0f, rban.bottom - 6.0f);
            DrawPillButton(rcOpenDir, L"打开数据目录", AddZone(23, rcOpenDir), 0x2A2024, 0x3A2C31, 0xFDE68A, 0.18f);
            contentTop += 48.0f;
        }

        // Optional slot assignment mode banner (ONLY shown on Vault / Leaderboard, hidden on Settings!)
        if ((activeTab == 0 || activeTab == 1) && assignTargetSlot >= 0 && assignTargetSlot < 10) {
            D2D1_RECT_F ban = D2D1::RectF(18.0f, contentTop - 2.0f, cw - 18.0f, contentTop + 30.0f);
            FillRMRimHex(rt.Get(), brush.Get(), ban, 7.0f, Theme::AccentBlue, 0.14f, Theme::AccentCyan, 0.40f, Theme::AccentBlue, 0.18f);

            std::wstring targetLabel = state->IsMultiSceneActive()
                ? (state->ActiveScene().name + L" \x00B7 \x69FD\x4F4D " + FormatSlotNum(assignTargetSlot))
                : (L"\x69FD\x4F4D " + FormatSlotNum(assignTargetSlot));
            std::wstring btxt = L"正在为「" + targetLabel +
                                (activeTab == 0
                                    ? (L"」挑选 Prompt -- 点「放入 " + FormatSlotNum(assignTargetSlot) + L"」、双击条目或按 Enter 即可完成")
                                    : (L"」挑选 Prompt -- 点「放入 " + FormatSlotNum(assignTargetSlot) + L"」或双击条目即可完成（Esc 取消）"));
            DrawUiTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                          D2D1::RectF(ban.left + 12.0f, ban.top, ban.right - 88.0f, ban.bottom), btxt.c_str(), Theme::AccentCyan, 0.96f);
            D2D1_RECT_F rcCancelAssign = D2D1::RectF(ban.right - 80.0f, ban.top + 5.0f, ban.right - 8.0f, ban.bottom - 5.0f);
            DrawPillButton(rcCancelAssign, L"\x53D6\x6D88\x6307\x5B9A", AddZone(6, rcCancelAssign), 0x1E2536, 0x2B354D, Theme::TextSecondary);
            contentTop += 40.0f;
        }

        if (activeTab == 0) {
            // Search box surround with smooth focus glow + fade-in right-side clear '×' button
            D2D1_RECT_F sBox = D2D1::RectF(18.0f, contentTop, cw - 20.0f, contentTop + 34.0f);
            DrawInputBox(rt.Get(), brush.Get(), sBox, 7.0f, searchFocusAnim, false);
            if (!searchQuery.empty() || clearBtnAnim > 0.02f) {
                float ca = !searchQuery.empty() ? std::max(clearBtnAnim, Motion::IsReducedMotion() ? 1.0f : 0.0f) : clearBtnAnim;
                D2D1_RECT_F rcClear = D2D1::RectF(sBox.right - 28.0f, sBox.top + 5.0f, sBox.right - 4.0f, sBox.bottom - 5.0f);
                int zClr = !searchQuery.empty() ? AddZone(8, rcClear) : -2;
                bool clrHov = (zClr >= 0 && zClr == hoverZoneIdx);
                if (clrHov) FillRRHex(rt.Get(), brush.Get(), rcClear, 5.0f, 0xFFFFFF, 0.09f * ca);
                DrawTextHex(rt.Get(), brush.Get(), fmtTab.Get(), rcClear, L"\u00D7", 1, clrHov ? Theme::TextPrimary : Theme::TextMuted, 0.95f * ca);
            }
            contentTop += 42.0f;

            // Vault scene/status filter bar with continuous sliding active pill & smooth accent morph
            int effFilter = EffectiveVaultFilter();
            if (assignTargetSlot < 0) {
                struct FilterPill {
                    int kind;
                    bool selected;
                    uint32_t accent;
                    std::wstring label;
                    float width;
                    D2D1_RECT_F rect;
                    int zoneIdx;
                };
                FilterPill pills[kMaxScenes + 2]{};
                int pillCnt = 0;
                auto pushPill = [&](int kind, bool sel, uint32_t acc, const std::wstring& txt) {
                    std::wstring u = UiText(txt);
                    float w = std::clamp((float)VisualWidthUnits(u) * 6.2f + 18.0f, 50.0f, 114.0f);
                    pills[pillCnt++] = FilterPill{ kind, sel, acc, std::move(u), w, {}, -1 };
                };

                wchar_t lb[48]{};
                wsprintfW(lb, L"\x5168\x90E8 %u", (unsigned)state->vault.size());
                pushPill(70, effFilter == -1, Theme::AccentCyan, lb);

                int vis[kMaxScenes]{};
                int enCnt = state->GetEnabledScenes(vis);
                for (int p = 0; p < enCnt; ++p) {
                    int s = vis[p];
                    _snwprintf_s(lb, _countof(lb), _TRUNCATE, L"%ls %d",
                                 state->scenes[(size_t)s].name.c_str(), state->CountMountedInScene(s));
                    pushPill(71 + s, effFilter == s, Theme::SceneTextAccent(s), lb);
                }

                wsprintfW(lb, L"\x672A\x88C5\x914D %d", CountUnmountedVaultItems());
                pushPill(75, effFilter == -2, Theme::AccentCyan, lb);

                float barRight = (effFilter >= 0) ? (cw - 112.0f) : (cw - 20.0f);
                float gap = 6.0f;
                float sumW = 0.0f;
                for (int i = 0; i < pillCnt; ++i) sumW += pills[i].width;
                float maxSum = std::max(80.0f, (barRight - 18.0f) - (float)(pillCnt - 1) * gap);
                if (sumW > maxSum && sumW > 1.0f) {
                    float sc = maxSum / sumW;
                    for (int i = 0; i < pillCnt; ++i) pills[i].width *= sc;
                }

                float px = 18.0f;
                float selX = 18.0f, selW = (pillCnt > 0 ? pills[0].width : 60.0f);
                uint32_t selAcc = Theme::AccentCyan;
                bool selHovered = false;
                for (int i = 0; i < pillCnt; ++i) {
                    pills[i].rect = D2D1::RectF(px, contentTop, px + pills[i].width, contentTop + 26.0f);
                    pills[i].zoneIdx = AddZone(pills[i].kind, pills[i].rect);
                    if (pills[i].selected) {
                        selX = pills[i].rect.left;
                        selW = pills[i].width;
                        selAcc = pills[i].accent;
                        selHovered = (pills[i].zoneIdx == hoverZoneIdx);
                    } else {
                        bool hov = (pills[i].zoneIdx == hoverZoneIdx);
                        bool prs = (pills[i].zoneIdx >= 0 && pills[i].zoneIdx < (int)hitZones.size() &&
                                    hitZones[(size_t)pills[i].zoneIdx].kind == pressedZoneKind &&
                                    hitZones[(size_t)pills[i].zoneIdx].promptId == pressedZonePromptId);
                        float ins = (prs && !Motion::IsReducedMotion()) ? 0.5f : 0.0f;
                        D2D1_RECT_F dr = D2D1::RectF(pills[i].rect.left + ins, pills[i].rect.top + ins,
                                                     pills[i].rect.right - ins, pills[i].rect.bottom - ins);
                        FillRMRimHex(rt.Get(), brush.Get(), dr, 6.0f, hov ? 0x222838 : 0x151923, prs ? 0.86f : 1.0f, 0xFFFFFF, hov ? 0.16f : 0.07f, 0xFFFFFF, 0.03f);
                    }
                    px += pills[i].width + gap;
                }

                if (!filterPillInit || Motion::IsReducedMotion()) {
                    filterPillXSpring.Snap(selX);
                    filterPillWSpring.Snap(selW);
                    filterAccentCol = Rgba(selAcc);
                    filterAccentTarget = Rgba(selAcc);
                    filterPillInit = true;
                } else {
                    bool moved = (std::fabs(filterPillXSpring.target - selX) > 0.2f ||
                                  std::fabs(filterPillWSpring.target - selW) > 0.2f);
                    filterPillXSpring.target = selX;
                    filterPillWSpring.target = selW;
                    filterAccentTarget = Rgba(selAcc);
                    if (moved) EnsureAnimTimer();
                }

                D2D1_RECT_F slideR = D2D1::RectF(filterPillXSpring.value, contentTop,
                                                 filterPillXSpring.value + filterPillWSpring.value, contentTop + 26.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(slideR, 6.0f, 6.0f),
                                         Br(Rgba(selHovered ? 0x283552 : 0x202A40, 1.0f)));
                DrawRim(rt.Get(), slideR, 6.0f,
                        D2D1::ColorF(filterAccentCol.r, filterAccentCol.g, filterAccentCol.b, 0.44f),
                        Rgba(0xFFFFFF, 0.05f));

                if (fmtBadge) {
                    for (int i = 0; i < pillCnt; ++i) {
                        bool hov = (pills[i].zoneIdx == hoverZoneIdx);
                        uint32_t txtCol = pills[i].selected ? pills[i].accent : Theme::TextSecondary;
                        rt->DrawTextW(pills[i].label.c_str(), (UINT32)pills[i].label.size(), fmtBadge.Get(), pills[i].rect,
                                      Br(Rgba(txtCol, (pills[i].selected || hov) ? 1.0f : 0.90f)));
                    }
                }

                if (effFilter >= 0 || (cfgBtnAlpha > 0.02f && px <= cw - 108.0f)) {
                    float ca = (effFilter >= 0) ? std::max(cfgBtnAlpha, Motion::IsReducedMotion() ? 1.0f : 0.0f) : cfgBtnAlpha;
                    D2D1_RECT_F rcCfg = D2D1::RectF(cw - 104.0f, contentTop, cw - 20.0f, contentTop + 26.0f);
                    int zCfg = (effFilter >= 0) ? AddZone(54 + effFilter, rcCfg) : -2;
                    DrawPillButton(rcCfg, L"\x7F16\x8F91\x53F0\x9762", zCfg, 0x1B263B, 0x253554, Theme::AccentCyan, 0.22f, ca);
                }
                contentTop += 34.0f;
            } else {
                contentTop += 2.0f;
            }

            // Clip scrollable list area and use cached filtered indices
            vaultListTopDip = contentTop;
            D2D1_RECT_F listViewport = D2D1::RectF(0.0f, contentTop, cw, ch - 8.0f);
            lastViewportH = std::max(60.0f, listViewport.bottom - listViewport.top);
            viewportScroll.SetViewport(listViewport);

            EnsureVaultCache();
            float totalContentH = (float)cachedFilteredIndices.size() * 76.0f + 16.0f;
            maxScrollY = std::max(0.0f, totalContentH - lastViewportH);
            scrollTargetY = std::clamp(scrollTargetY, 0.0f, maxScrollY);
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(listViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            int badgeScene = (effFilter >= 0) ? effFilter : state->EffectiveActiveSceneIndex();
            bool canDragVaultOrder = (searchQuery.empty() && effFilter == -1);
            int visibleCount = (int)cachedFilteredIndices.size();
            int passes = isVaultDragging ? 2 : 1;
            for (int pass = 0; pass < passes; ++pass) {
                float y = contentTop - scrollY;
                for (int rowPos = 0; rowPos < visibleCount; ++rowPos) {
                    size_t vi = cachedFilteredIndices[(size_t)rowPos];
                    const auto& item = state->vault[vi];
                    bool isBeingDragged = (isVaultDragging && (int)vi == vaultDragIdx);
                    if (pass == 0 && isBeingDragged) {
                        // Draw subtle recessed target well where the lifted card will land
                        D2D1_RECT_F well = D2D1::RectF(18.0f, y, cw - 20.0f, y + 68.0f);
                        if (well.bottom >= contentTop && well.top <= ch) {
                            FillRMRimHex(rt.Get(), brush.Get(), well, 9.0f, 0x0B0E15, 0.85f, Theme::AccentCyan, 0.30f, Theme::AccentCyan, 0.14f);
                        }
                        y += 76.0f;
                        continue;
                    }
                    if (pass == 1 && !isBeingDragged) {
                        y += 76.0f;
                        continue;
                    }

                    bool isSelectedRow = (rowPos == selectedVaultRow);
                    float cardTopY = (pass == 1) ? vaultDragCurDipY : (y + GetRowShift(item.id));
                    D2D1_RECT_F card = D2D1::RectF(18.0f, cardTopY, cw - 20.0f, cardTopY + 68.0f);
                    if (card.bottom >= contentTop - 16.0f && card.top <= ch + 16.0f) {
                        if (pass == 0) {
                            D2D1_RECT_F clippedCard = D2D1::RectF(
                                std::max(card.left, listViewport.left),
                                std::max(card.top, listViewport.top),
                                std::min(card.right, listViewport.right),
                                std::min(card.bottom, listViewport.bottom));
                            if (clippedCard.right > clippedCard.left && clippedCard.bottom > clippedCard.top &&
                                drawnCardHitCount < kMaxDrawnCardHits) {
                                drawnCardHits[drawnCardHitCount++] = DrawnCardHit{ vi, rowPos, clippedCard, card.top };
                            }
                        }
                        float lift = (pass == 1) ? std::clamp(vaultDragLiftSpring.value, 0.0f, 1.0f) : 0.0f;
                        D2D1_MATRIX_3X2_F preCardTf{};
                        if (pass == 1) {
                            rt->GetTransform(&preCardTf);
                            // Soft elevated shadow + crisp 1.012x direct-manipulation lift
                            for (int sh = 0; sh < 2; ++sh) {
                                float pad = (sh == 0 ? 6.0f : 2.5f) * lift;
                                D2D1_RECT_F sr = D2D1::RectF(card.left - pad * 0.5f, card.top + (sh == 0 ? 5.0f : 2.0f) * lift,
                                                             card.right + pad * 0.5f, card.bottom + pad);
                                rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 10.0f, 10.0f),
                                                         Br(Rgba(0x000000, (sh == 0 ? 0.24f : 0.36f) * lift)));
                            }
                            if (!Motion::IsReducedMotion() && lift > 0.01f) {
                                float sc = 1.0f + 0.012f * lift;
                                D2D1_POINT_2F cPt = D2D1::Point2F((card.left + card.right) * 0.5f, (card.top + card.bottom) * 0.5f);
                                rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, cPt) * preCardTf);
                            }
                        }

                        int slotIdx = state->FindSlotOfIdInScene(badgeScene, item.id);
                        uint32_t cardBgHex = (pass == 1) ? 0x263046 : (isSelectedRow ? 0x21283A : Theme::CardBg);
                        D2D1_COLOR_F cardRim = (pass == 1) ? Rgba(Theme::AccentCyan, 0.80f)
                                             : (isSelectedRow ? Rgba(Theme::AccentCyan, 0.56f)
                                                              : (slotIdx >= 0 ? Rgba(Theme::AccentBlue, 0.26f) : Rgba(0xFFFFFF, 0.10f)));
                        FillRMRim(rt.Get(), brush.Get(), card, 9.0f, Rgba(cardBgHex, (pass == 1) ? 0.99f : 0.96f),
                                  cardRim, Rgba(0xFFFFFF, 0.04f), ((pass == 1) || isSelectedRow) ? 1.25f : 1.0f);

                        // Subtle 6-dot drag grip on far left ONLY in unfiltered All-vault view
                        float leftContentOffset = 12.0f;
                        if (canDragVaultOrder) {
                            leftContentOffset = 24.0f;
                            auto* gripBr = Br(Rgba((pass == 1) ? Theme::AccentCyan : Theme::TextMuted, (pass == 1) ? 0.95f : 0.45f));
                            for (int gr = 0; gr < 3; ++gr) {
                                for (int gc = 0; gc < 2; ++gc) {
                                    float gx = card.left + 10.5f + (float)gc * 4.5f;
                                    float gy = card.top + 26.0f + (float)gr * 6.5f;
                                    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gx, gy), 1.15f, 1.15f), gripBr);
                                }
                            }
                        }

                        // Slot status pill (single-scene: '槽位 01'; multi-scene: '<场景> · 01' + optional '+N 场景')
                        float pillRight = card.left + leftContentOffset;
                        if (!state->IsMultiSceneActive()) {
                            D2D1_RECT_F statusPill = D2D1::RectF(pillRight, card.top + 11.0f, pillRight + 62.0f, card.top + 31.0f);
                            pillRight = statusPill.right;
                            if (slotIdx >= 0) {
                                DrawStatusBadge(statusPill, L"\x69FD\x4F4D " + FormatSlotNum(slotIdx), Theme::AccentCyan, 0.15f, Theme::AccentCyan, 0.95f);
                            } else {
                                DrawStatusBadge(statusPill, L"\x672A\x88C5\x914D", 0xFFFFFF, 0.05f, Theme::TextMuted, 0.95f);
                            }
                        } else {
                            int firstOtherScene = -1, firstOtherSlot = -1;
                            int otherCnt = state->CountOtherEnabledScenesWithId(item.id, &firstOtherScene, &firstOtherSlot, badgeScene);
                            int shownScene = (slotIdx >= 0) ? badgeScene : firstOtherScene;
                            int shownSlot  = (slotIdx >= 0) ? slotIdx : firstOtherSlot;
                            int extraScenes = (slotIdx >= 0) ? otherCnt : (otherCnt > 1 ? otherCnt - 1 : 0);
                            if (shownScene >= 0 && shownSlot >= 0) {
                                uint32_t acc = Theme::SceneTextAccent(shownScene);
                                D2D1_RECT_F statusPill = D2D1::RectF(pillRight, card.top + 11.0f, pillRight + 86.0f, card.top + 31.0f);
                                pillRight = statusPill.right;
                                DrawStatusBadge(statusPill, state->scenes[(size_t)shownScene].name + L" \x00B7 " + FormatSlotNum(shownSlot),
                                                acc, slotIdx >= 0 ? 0.16f : 0.14f, acc, slotIdx >= 0 ? 0.96f : 0.94f);
                                if (extraScenes > 0) {
                                    D2D1_RECT_F subPill = D2D1::RectF(pillRight + 4.0f, card.top + 11.0f, pillRight + 52.0f, card.top + 31.0f);
                                    pillRight = subPill.right;
                                    wchar_t ob[24]{};
                                    wsprintfW(ob, L"+%d \x573A\x666F", extraScenes);
                                    DrawStatusBadge(subPill, ob, Theme::AccentIndigo, 0.14f, 0xA5B4FC, 0.92f);
                                }
                            } else {
                                D2D1_RECT_F statusPill = D2D1::RectF(pillRight, card.top + 11.0f, pillRight + 56.0f, card.top + 31.0f);
                                pillRight = statusPill.right;
                                DrawStatusBadge(statusPill, L"\x672A\x88C5\x914D", 0xFFFFFF, 0.05f, Theme::TextMuted, 0.95f);
                            }
                        }

                    // Prompt title + optional usage count (hide '0 次'; right bar converged to 3 primary + '···' = 206 DIP)
                    float actionLeft = card.right - 206.0f;
                    float countReserve = (item.useCount > 0) ? 62.0f : 8.0f;
                    DrawTextHex(rt.Get(), brush.Get(), fmtCardTitle.Get(),
                                D2D1::RectF(pillRight + 8.0f, card.top + 9.0f, actionLeft - countReserve, card.top + 33.0f),
                                item.title.c_str(), (UINT32)item.title.size(), Theme::TextPrimary, 0.98f);

                    if (item.useCount > 0) {
                        wchar_t cntBuf[48]{};
                        wsprintfW(cntBuf, L"%I64u \x6B21", (unsigned long long)item.useCount);
                        DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(),
                                      D2D1::RectF(actionLeft - 60.0f, card.top + 11.0f, actionLeft - 4.0f, card.top + 31.0f),
                                      cntBuf, Theme::TextSecondary, 0.85f);
                    }

                    // Body preview (context snippet centered on match when searching!)
                    std::wstring preview = BuildSearchPreviewSnippet(item.content, searchQuery, 108);
                    DrawTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                                D2D1::RectF(card.left + leftContentOffset, card.top + 37.0f, actionLeft - 8.0f, card.bottom - 8.0f),
                                preview.c_str(), (UINT32)preview.size(), Theme::TextSecondary, 0.80f);

                        // 4 clean actions: Copy (48) | Slot (60) | Edit (44) | More '···' (32)
                        float btnTop = card.top + 19.0f;
                        float btnBot = card.top + 49.0f;
                        D2D1_RECT_F rCopy   = D2D1::RectF(card.right - 202.0f, btnTop, card.right - 154.0f, btnBot);
                        D2D1_RECT_F rAssign = D2D1::RectF(card.right - 149.0f, btnTop, card.right - 89.0f,  btnBot);
                        D2D1_RECT_F rEdit   = D2D1::RectF(card.right - 84.0f,  btnTop, card.right - 40.0f,  btnBot);
                        D2D1_RECT_F rMore   = D2D1::RectF(card.right - 36.0f,  btnTop, card.right - 8.0f,   btnBot);

                        int zCopy   = (pass == 0) ? AddClippedZone(10, rCopy, listViewport, item.id) : -2;
                        int zAssign = (pass == 0) ? AddClippedZone(11, rAssign, listViewport, item.id) : -2;
                        int zEdit   = (pass == 0) ? AddClippedZone(12, rEdit, listViewport, item.id) : -2;
                        int zMore   = (pass == 0) ? AddClippedZone(15, rMore, listViewport, item.id) : -2;

                        DrawCopyActionButton(rCopy, zCopy, item.id, nowTick);
                        DrawAssignBtn(rAssign, zAssign);

                        DrawPillButton(rEdit, L"\x7F16\x8F91", zEdit, 0x222736, 0x2D3448, Theme::TextSecondary);
                        DrawPillButton(rMore, L"\x00B7\x00B7\x00B7", zMore, 0x1D2230, 0x2B3348, Theme::TextSecondary, 0.08f);

                        if (pass == 1) {
                            rt->SetTransform(preCardTf);
                        }
                    }
                    y += 76.0f;
                }
            }

            if (visibleCount == 0 && fmtCardBody) {
                std::wstring emptyMsg;
                if (!searchQuery.empty()) {
                    std::wstring qShort = CollapseWhitespaceSingleLine(searchQuery, 20);
                    if (effFilter >= 0) {
                        emptyMsg = UiText(L"在「" + state->scenes[(size_t)effFilter].name + L"」中未找到包含「" + qShort + L"」的 Prompt");
                    } else if (effFilter == -2) {
                        emptyMsg = UiText(L"在未装配词舱中未找到包含「" + qShort + L"」的 Prompt");
                    } else {
                        emptyMsg = UiText(L"未找到包含「" + qShort + L"」的 Prompt");
                    }
                } else if (effFilter >= 0) {
                    emptyMsg = UiText(L"场景「" + state->scenes[(size_t)effFilter].name + L"」暂未装配 Prompt，可从词舱勾选或新建加入");
                } else if (effFilter == -2) {
                    emptyMsg = UiText(L"当前所有词舱 Prompt 均已装配到贴边面板");
                } else {
                    emptyMsg = UiText(L"\x8BCD\x8231\x6682\x65E0 Prompt\xFF0C\x70B9\x51FB\x53F3\x4E0A\x89D2\x300C+ \x65B0\x5EFA Prompt\x300D\x5F00\x59CB\x79EF\x7D2F");
                }
                DrawTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                            D2D1::RectF(24.0f, contentTop + 26.0f, cw - 24.0f, contentTop + 54.0f),
                            emptyMsg.c_str(), (UINT32)emptyMsg.size(), Theme::TextMuted, 0.95f);
                float bx = 24.0f, bt = contentTop + 60.0f, bb = contentTop + 88.0f;
                if (!searchQuery.empty()) {
                    DrawClippedPresetBtn(bx, bt, bx + 84.0f, bb, 8, L"清空搜索", 0, listViewport);
                    bx += 94.0f;
                    if (effFilter != -1) {
                        DrawClippedPresetBtn(bx, bt, bx + 98.0f, bb, 70, L"搜全部词舱", 1, listViewport);
                        bx += 108.0f;
                    }
                    DrawClippedPresetBtn(bx, bt, bx + 128.0f, bb, 17, L"+ 新建此 Prompt", 6, listViewport);
                } else if (effFilter >= 0) {
                    DrawClippedPresetBtn(bx, bt, bx + 132.0f, bb, 54 + effFilter, L"编辑此场景台面", 6, listViewport);
                    bx += 142.0f;
                    DrawClippedPresetBtn(bx, bt, bx + 114.0f, bb, 3, L"+ 新建并加入", 1, listViewport);
                } else if (effFilter == -2) {
                    DrawClippedPresetBtn(bx, bt, bx + 108.0f, bb, 70, L"查看全部词舱", 2, listViewport);
                }
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        } else if (activeTab == 1) {
            // --- Tab 1: Usage Analytics & Leaderboard (smooth entrance sweep & KPI settle!) ---
            float rankT = Motion::EaseOutQuart(std::clamp(rankEnterSpring.value, 0.0f, 1.0f));
            float cardW = (cw - 38.0f - 20.0f) / 3.0f;
            int mountedCount = state->CountMountedInScene(state->EffectiveActiveSceneIndex());

            EnsureRankCache();
            uint64_t totalUses = state->TotalCopies();
            uint64_t dispTotalUses = (rankT >= 0.995f) ? totalUses : (uint64_t)std::round((double)totalUses * (double)rankT);
            uint64_t topCount = (!cachedRankedIndices.empty() && state->vault[cachedRankedIndices[0]].useCount > 0)
                ? state->vault[cachedRankedIndices[0]].useCount : 1;

            // 3 Summary KPI cards (KPI 3 uses crisp fmtKpiTitle so titles never truncate awkwardly)
            wchar_t bTotal[32]{}, bVault[32]{}, bMounted[64]{};
            wsprintfW(bTotal, L"%I64u", (unsigned long long)dispTotalUses);
            wsprintfW(bVault, L"%u", (unsigned)state->vault.size());
            if (state->IsMultiSceneActive()) {
                _snwprintf_s(bMounted, _countof(bMounted), _TRUNCATE, L"%ls \x00B7 \x5DF2\x88C5\x914D %d / 10 \x683C",
                              state->ActiveScene().name.c_str(), mountedCount);
            } else {
                wsprintfW(bMounted, L"\x5DF2\x88C5\x914D %d / 10 \x683C", mountedCount);
            }
            const wchar_t* topTitle = (!cachedRankedIndices.empty() && state->vault[cachedRankedIndices[0]].useCount > 0)
                ? state->vault[cachedRankedIndices[0]].title.c_str() : L"--";
            wchar_t bTopSub[48]{};
            if (!cachedRankedIndices.empty() && state->vault[cachedRankedIndices[0]].useCount > 0) {
                wsprintfW(bTopSub, L"%I64u \x6B21\x590D\x5236", (unsigned long long)state->vault[cachedRankedIndices[0]].useCount);
            }

            DrawKpiCard(0, cardW, contentTop, L"\x7D2F\x8BA1\x590D\x5236\x603B\x6B21\x6570", bTotal, L"", false);
            DrawKpiCard(1, cardW, contentTop, L"\x8BCD\x8231\x6536\x85CF\x603B\x6570", bVault, bMounted, false);
            DrawKpiCard(2, cardW, contentTop, L"\x6700\x9AD8\x9891 Prompt", topTitle, bTopSub, true);

            float listTop = contentTop + 88.0f;
            rankListTopDip = listTop;
            D2D1_RECT_F listViewport = D2D1::RectF(0.0f, listTop, cw, ch - 8.0f);
            lastViewportH = std::max(60.0f, listViewport.bottom - listViewport.top);
            viewportScroll.SetViewport(listViewport);

            float totalContentH = (float)cachedRankedIndices.size() * 65.0f + 16.0f;
            maxScrollY = std::max(0.0f, totalContentH - lastViewportH);
            scrollTargetY = std::clamp(scrollTargetY, 0.0f, maxScrollY);
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);
            rt->PushAxisAlignedClip(listViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            // Gradient brush removed (now uses shared FillRoundedGradient helper)
            float y = listTop - scrollY;
            for (size_t i = 0; i < cachedRankedIndices.size(); ++i) {
                const PromptItem* item = &state->vault[cachedRankedIndices[i]];
                D2D1_RECT_F row = D2D1::RectF(18.0f, y, cw - 20.0f, y + 58.0f);
                if (row.bottom >= listTop && row.top <= ch) {
                    FillRMRimHex(rt.Get(), brush.Get(), row, 8.0f, Theme::CardBg, 0.92f, 0xFFFFFF, 0.09f, 0xFFFFFF, 0.03f);

                    // Rank badge
                    D2D1_RECT_F rkBox = D2D1::RectF(row.left + 10.0f, row.top + 13.0f, row.left + 44.0f, row.bottom - 13.0f);
                    uint32_t rkHex = (i == 0) ? Theme::AccentCyan : (i == 1 ? Theme::AccentBlue : (i == 2 ? Theme::AccentIndigo : Theme::TextMuted));
                    FillRRHex(rt.Get(), brush.Get(), rkBox, 6.0f, rkHex, i < 3 ? 0.16f : 0.07f);
                    wchar_t rkStr[16]{};
                    wsprintfW(rkStr, L"#%u", (unsigned)(i + 1));
                    DrawTextHex(rt.Get(), brush.Get(), fmtRank.Get(), rkBox, rkStr, (UINT32)wcslen(rkStr), rkHex, 0.98f);

                    int slotIdx = state->FindSlotOfId(item->id);
                    float rightZoneLeft = row.right - 220.0f;
                    float tagW = state->IsMultiSceneActive() ? 96.0f : 70.0f;

                    // Title
                    DrawTextHex(rt.Get(), brush.Get(), fmtCardTitle.Get(),
                                D2D1::RectF(rkBox.right + 10.0f, row.top + 7.0f, rightZoneLeft - tagW - 6.0f, row.top + 29.0f),
                                item->title.c_str(), (UINT32)item->title.size(), Theme::TextPrimary, 0.96f);

                    // Slot tag
                    std::wstring sTag;
                    uint32_t sTagCol = Theme::TextMuted;
                    if (!state->IsMultiSceneActive()) {
                        sTag = (slotIdx >= 0) ? (L"\x69FD\x4F4D " + FormatSlotNum(slotIdx)) : L"\x672A\x88C5\x914D";
                        sTagCol = (slotIdx >= 0) ? Theme::AccentCyan : Theme::TextMuted;
                    } else if (slotIdx >= 0) {
                        sTag = state->ActiveScene().name + L" \x00B7 " + FormatSlotNum(slotIdx);
                        sTagCol = Theme::SceneTextAccent(state->EffectiveActiveSceneIndex());
                    } else {
                        int os = -1, osl = -1;
                        if (state->CountOtherEnabledScenesWithId(item->id, &os, &osl) > 0 && os >= 0) {
                            sTag = state->scenes[(size_t)os].name + L" \x00B7 " + FormatSlotNum(osl);
                            sTagCol = Theme::AccentBlue;
                        } else {
                            sTag = L"\x672A\x88C5\x914D";
                        }
                    }
                    DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(),
                                  D2D1::RectF(rightZoneLeft - tagW, row.top + 8.0f, rightZoneLeft - 6.0f, row.top + 28.0f),
                                  sTag.c_str(), sTagCol, 0.92f);

                    // Proportional progress bar (smooth staggered entrance sweep on Tab 1 open)
                    D2D1_RECT_F track = D2D1::RectF(rkBox.right + 10.0f, row.top + 35.0f, rightZoneLeft - 10.0f, row.top + 43.0f);
                    FillRRHex(rt.Get(), brush.Get(), track, 4.0f, 0x0D0F15, 1.0f);
                    if (item->useCount > 0) {
                        float ratio = std::clamp((float)item->useCount / (float)topCount, 0.03f, 1.0f);
                        float rowDelay = std::min((float)i * 0.045f, 0.35f);
                        float barProg = Motion::IsReducedMotion() ? 1.0f
                            : Motion::EaseOutQuart(std::clamp((rankEnterSpring.value - rowDelay) / std::max(0.25f, 1.0f - rowDelay), 0.0f, 1.0f));
                        float fillW = std::max(3.0f, (track.right - track.left) * ratio * barProg);
                        FillRoundedGradient(rt.Get(), D2D1::RectF(track.left, track.top, track.left + fillW, track.bottom),
                                            4.0f, Rgba(Theme::AccentCyan, 0.90f), Rgba(Theme::AccentIndigo, 0.90f), true);
                    }

                    // Count + Copy button + Slot Assign button (honest '放入 XX' in assignTargetSlot mode!)
                    wchar_t cText[48]{};
                    int pct = totalUses > 0 ? (int)((item->useCount * 100ULL + totalUses / 2) / totalUses) : 0;
                    wsprintfW(cText, L"%I64u \x6B21\xFF08%d%%\xFF09", (unsigned long long)item->useCount, pct);
                    DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(),
                                  D2D1::RectF(rightZoneLeft, row.top + 14.0f, row.right - 134.0f, row.bottom - 14.0f),
                                  cText, Theme::TextPrimary, 0.92f);

                    D2D1_RECT_F rCopy   = D2D1::RectF(row.right - 128.0f, row.top + 14.0f, row.right - 78.0f, row.bottom - 14.0f);
                    D2D1_RECT_F rAssign = D2D1::RectF(row.right - 72.0f,  row.top + 14.0f, row.right - 10.0f, row.bottom - 14.0f);
                    int zCopy   = AddClippedZone(10, rCopy, listViewport, item->id);
                    int zAssign = AddClippedZone(11, rAssign, listViewport, item->id);
                    DrawCopyActionButton(rCopy, zCopy, item->id, nowTick);
                    DrawAssignBtn(rAssign, zAssign);
                }
                y += 65.0f;
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        } else {
            // --- Tab 2: Settings (4-Section Task-Oriented Grouped Layout + Custom Dark Scrollbar) ---
            if (!settingsAnimInit) SyncSettingsSprings(true);
            D2D1_RECT_F settingsViewport = D2D1::RectF(0.0f, contentTop, cw, ch - 8.0f);
            lastViewportH = std::max(60.0f, settingsViewport.bottom - settingsViewport.top);
            viewportScroll.SetViewport(settingsViewport);

            UpdateSnapshot upSnap = updater ? updater->GetSnapshot() : UpdateSnapshot{};
            AutoStart::Status autoSt = AutoStart::GetStatus();
            bool hasProgressBar = (upSnap.phase == UpdatePhase::Downloading || upSnap.phase == UpdatePhase::Installing);
            bool hasReleaseNotes = (upSnap.phase == UpdatePhase::Available && !upSnap.releaseNotes.empty());
            float rowAutoStartH = (autoSt == AutoStart::Status::StaleOtherPath) ? 74.0f : 64.0f;
            float rowUpdateH = hasReleaseNotes ? 132.0f
                             : (hasProgressBar ? 84.0f
                                               : ((upSnap.phase == UpdatePhase::Available || upSnap.phase == UpdatePhase::Error) ? 78.0f : 64.0f));

            // Pre-compute exact section heights for accurate scrollbar metrics
            float sceneRowH = 44.0f;
            float rowSceneMgrH = 72.0f + (float)kMaxScenes * sceneRowH + 10.0f; // Header + 4 multi-select scene rows
            float sec1H = 74.0f + 64.0f + 66.0f + rowSceneMgrH; // 贴边面板 (3 rows + 4-scene multi-select & prompt manager)
            float sec2H = rowAutoStartH + 66.0f + rowUpdateH; // 启动与更新 (3 rows)
            float sec3H = 68.0f + 72.0f; // 数据与备份 (2 rows)
            float sec4H = 62.0f + 68.0f + 74.0f; // 帮助与反馈 (3 rows: 快速上手 / 意见反馈 / 内置提示词致谢)
            float totalSettingsH = 6.0f + (24.0f + sec1H + 14.0f) + (24.0f + sec2H + 14.0f)
                                 + (24.0f + sec3H + 14.0f) + (24.0f + sec4H + 18.0f);
            maxScrollY = std::max(0.0f, totalSettingsH - lastViewportH);
            if (pendingScrollToUpdate) {
                pendingScrollToUpdate = false;
                float sec2Top = 6.0f + (24.0f + sec1H + 14.0f);
                scrollTargetY = std::clamp(sec2Top - 8.0f, 0.0f, maxScrollY);
                if (Motion::IsReducedMotion()) scrollY = scrollTargetY;
                else EnsureAnimTimer();
            }
            scrollTargetY = std::clamp(scrollTargetY, 0.0f, maxScrollY);
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(settingsViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float sy = contentTop + 6.0f - scrollY;

            // === Section 1: 贴边面板 ===
            sy = DrawSectionHeader(sy, cw, settingsViewport, L"贴边面板");
            {
                D2D1_RECT_F g = DrawGroupContainer(sy, sec1H, cw, settingsViewport);
                float ry = sy;

                // Row 1.1: 停靠方式 (continuous sliding segmented indicator) + 面板归位
                std::wstring locSummary = L"当前：" + (getDockLocationSummary ? getDockLocationSummary() : std::wstring(L"显示器 1 · 右侧边缘"));
                DrawRowText(g, ry, 74.0f, 306.0f, settingsViewport, L"停靠方式", locSummary.c_str());
                if (ry + 74.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 22.0f, bb = ry + 50.0f;
                    D2D1_RECT_F segTrack = D2D1::RectF(g.right - 296.0f, bt - 2.0f, g.right - 98.0f, bb + 2.0f);
                    FillRMRimHex(rt.Get(), brush.Get(), segTrack, 7.0f, 0x0D0F15, 1.0f, 0xFFFFFF, 0.08f, 0xFFFFFF, 0.03f);

                    D2D1_RECT_F bRight = D2D1::RectF(g.right - 294.0f, bt, g.right - 232.0f, bb);
                    D2D1_RECT_F bLeft  = D2D1::RectF(g.right - 228.0f, bt, g.right - 166.0f, bb);
                    D2D1_RECT_F bFloat = D2D1::RectF(g.right - 162.0f, bt, g.right - 100.0f, bb);
                    int zR = AddClippedZone(33, bRight, settingsViewport);
                    int zL = AddClippedZone(34, bLeft,  settingsViewport);
                    int zF = AddClippedZone(36, bFloat, settingsViewport);

                    float selEX = (state->dockEdge == DockEdge::Right) ? bRight.left
                                : (state->dockEdge == DockEdge::Left ? bLeft.left : bFloat.left);
                    if (!edgePillInit || Motion::IsReducedMotion()) {
                        edgePillXSpring.Snap(selEX);
                        edgePillInit = true;
                    } else if (std::fabs(edgePillXSpring.target - selEX) > 0.2f) {
                        edgePillXSpring.target = selEX;
                        EnsureAnimTimer();
                    }
                    FillRMRimHex(rt.Get(), brush.Get(), D2D1::RectF(edgePillXSpring.value, bt, edgePillXSpring.value + 62.0f, bb),
                                 5.5f, 0x1E3A5F, 1.0f, Theme::AccentCyan, 0.42f, 0xFFFFFF, 0.05f);

                    DrawSegLbl(bRight, L"右侧贴边", state->dockEdge == DockEdge::Right, zR);
                    DrawSegLbl(bLeft,  L"左侧贴边", state->dockEdge == DockEdge::Left,  zL);
                    DrawSegLbl(bFloat, L"自由悬浮", state->dockEdge == DockEdge::None,  zF);
                    DrawClippedPresetBtn(g.right - 90.0f, bt, g.right - 16.0f, bb, 22, L"面板归位", 2, settingsViewport);
                }
                ry += 74.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 1.2: 悬停预览正文 (full-row clickable spring toggle)
                DrawToggleRow(g, ry, 64.0f, 20, swBubbleSpring.value, settingsViewport);
                DrawRowText(g, ry, 64.0f, 82.0f, settingsViewport, L"悬停预览正文",
                            L"鼠标悬停在槽位上时，侧边浮现预览气泡（左键可随时复制全文）");
                ry += 64.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 1.3: 点击托盘图标 (continuous sliding segmented indicator)
                DrawRowText(g, ry, 66.0f, 206.0f, settingsViewport, L"点击托盘图标",
                            L"左键点击右下角托盘图标时的默认操作");
                if (ry + 66.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 19.0f, bb = ry + 47.0f;
                    FillRMRimHex(rt.Get(), brush.Get(), D2D1::RectF(g.right - 198.0f, bt - 2.0f, g.right - 14.0f, bb + 2.0f),
                                 7.0f, 0x0D0F15, 1.0f, 0xFFFFFF, 0.08f, 0xFFFFFF, 0.03f);

                    D2D1_RECT_F bVault = D2D1::RectF(g.right - 196.0f, bt, g.right - 118.0f, bb);
                    D2D1_RECT_F bDock  = D2D1::RectF(g.right - 114.0f, bt, g.right - 16.0f,  bb);
                    int zV = AddClippedZone(37, bVault, settingsViewport);
                    int zD = AddClippedZone(38, bDock,  settingsViewport);

                    float selTX = state->trayClickOpensVault ? bVault.left : bDock.left;
                    float selTW = state->trayClickOpensVault ? (bVault.right - bVault.left) : (bDock.right - bDock.left);
                    if (!trayPillInit || Motion::IsReducedMotion()) {
                        trayPillXSpring.Snap(selTX);
                        trayPillWSpring.Snap(selTW);
                        trayPillInit = true;
                    } else if (std::fabs(trayPillXSpring.target - selTX) > 0.2f || std::fabs(trayPillWSpring.target - selTW) > 0.2f) {
                        trayPillXSpring.target = selTX;
                        trayPillWSpring.target = selTW;
                        EnsureAnimTimer();
                    }
                    FillRMRimHex(rt.Get(), brush.Get(), D2D1::RectF(trayPillXSpring.value, bt, trayPillXSpring.value + trayPillWSpring.value, bb),
                                 5.5f, 0x1E3A5F, 1.0f, Theme::AccentCyan, 0.42f, 0xFFFFFF, 0.05f);

                    DrawSegLbl(bVault, L"打开词舱",    state->trayClickOpensVault,  zV);
                    DrawSegLbl(bDock,  L"展开 / 收起", !state->trayClickOpensVault, zD);
                }
                ry += 66.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 1.4: 场景台面（多选勾选 1 ~ 4 组，点击配置提示词 / 重命名）
                {
                    int enabledCnt = state->EnabledSceneCount();
                    int actIdx = state->EffectiveActiveSceneIndex();
                    DrawRowText(g, ry, 72.0f, 118.0f, settingsViewport,
                                L"场景台面",
                                L"勾选要显示在贴边面板的场景（仅选 1 组时自动隐藏底栏，取消勾选不丢数据）");
                    if (ry + 72.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                        D2D1_RECT_F badgeR = D2D1::RectF(g.right - 108.0f, ry + 11.0f, g.right - 16.0f, ry + 33.0f);
                        FillRRHex(rt.Get(), brush.Get(), badgeR, 6.0f, Theme::AccentCyan, 0.13f);
                        wchar_t eb[32]{};
                        wsprintfW(eb, L"已启用 %d / %d", enabledCnt, kMaxScenes);
                        DrawUiTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), badgeR, eb, Theme::AccentCyan, 0.96f);
                    }
                    ry += 72.0f;

                    for (int s = 0; s < kMaxScenes; ++s) {
                        if (ry + sceneRowH >= settingsViewport.top && ry <= settingsViewport.bottom) {
                            const auto& sc = state->scenes[(size_t)s];
                            bool isEn = sc.enabled;
                            bool isAct = (isEn && s == actIdx);
                            bool isLockedLast = (isEn && enabledCnt <= 1);
                            uint32_t accTop = Theme::AccentCyan, accBot = Theme::AccentIndigo;
                            Theme::SceneAccentColors(s, accTop, accBot);
                            uint32_t txtAcc = Theme::SceneTextAccent(s);

                            D2D1_RECT_F itemCard = D2D1::RectF(g.left + 14.0f, ry + 2.0f, g.right - 14.0f, ry + sceneRowH - 4.0f);
                            D2D1_RECT_F toggleHit = D2D1::RectF(itemCard.left, itemCard.top, itemCard.right - 168.0f, itemCard.bottom);
                            int zTog = AddClippedZone(50 + s, toggleHit, settingsViewport);
                            bool rowHov = (!isLockedLast && zTog == hoverZoneIdx);

                            FillRMRimHex(rt.Get(), brush.Get(), itemCard, 7.0f,
                                         rowHov ? 0x242A3B : (isEn ? 0x1A2030 : 0x131722), 0.96f,
                                         isAct ? txtAcc : 0xFFFFFF, isAct ? 0.44f : (isEn ? 0.12f : 0.06f), 0xFFFFFF, 0.03f);

                            float cbShake = (s == lockedSceneShakeIdx && lockedSceneShakeTick != 0 && !Motion::IsReducedMotion())
                                ? Motion::RejectShakeOffset(lockedSceneShakeTick, nowTick, 260, 3.8f) : 0.0f;
                            D2D1_RECT_F cb = D2D1::RectF(itemCard.left + 10.0f + cbShake, itemCard.top + 11.0f,
                                                         itemCard.left + 26.0f + cbShake, itemCard.top + 27.0f);
                            DrawDarkCheckbox(rt.Get(), brush.Get(), cb, isEn, rowHov, txtAcc, isLockedLast, sceneCheckAnim[(size_t)s]);

                            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(itemCard.left + 38.0f, (itemCard.top + itemCard.bottom) * 0.5f), 3.6f, 3.6f),
                                            Br(Rgba(accTop, isEn ? 0.96f : 0.42f)));

                            DrawTextHex(rt.Get(), brush.Get(), fmtCardTitle.Get(),
                                        D2D1::RectF(itemCard.left + 48.0f, itemCard.top, itemCard.left + 146.0f, itemCard.bottom),
                                        sc.name.c_str(), (UINT32)sc.name.size(),
                                        isAct ? txtAcc : (isEn ? Theme::TextPrimary : Theme::TextSecondary), isEn ? 0.98f : 0.72f);

                            wchar_t mBuf[40]{};
                            int mCnt = state->CountMountedInScene(s);
                            wsprintfW(mBuf, (isAct && enabledCnt > 1) ? L"%d / 10 槽 · 当前" : L"%d / 10 槽", mCnt);
                            DrawUiTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                                          D2D1::RectF(itemCard.left + 150.0f, itemCard.top + 1.0f, itemCard.right - 168.0f, itemCard.bottom),
                                          mBuf, isEn ? Theme::TextSecondary : Theme::TextMuted, 0.88f);

                            DrawClippedPresetBtn(itemCard.right - 160.0f, itemCard.top + 5.0f, itemCard.right - 74.0f, itemCard.bottom - 5.0f, 54 + s, L"编辑台面", 6, settingsViewport);
                            DrawClippedPresetBtn(itemCard.right - 68.0f,  itemCard.top + 5.0f, itemCard.right - 8.0f,  itemCard.bottom - 5.0f, 60 + s, L"重命名",    0, settingsViewport);
                        }
                        ry += sceneRowH;
                    }
                    ry += 10.0f;
                }
                sy += sec1H + 14.0f;
            }

            // === Section 2: 启动与更新 ===
            sy = DrawSectionHeader(sy, cw, settingsViewport, L"启动与更新");
            {
                bool highlightUp = (upSnap.phase == UpdatePhase::Available || hasProgressBar);
                D2D1_RECT_F g = DrawGroupContainer(sy, sec2H, cw, settingsViewport, highlightUp);
                float ry = sy;

                // Row 2.1: 开机自动启动 (handles StaleOtherPath explicitly with both rebind AND disable!)
                bool staleAuto = (autoSt == AutoStart::Status::StaleOtherPath);
                if (staleAuto) {
                    if (ry + rowAutoStartH >= settingsViewport.top && ry <= settingsViewport.bottom) {
                        DrawClippedPresetBtn(g.right - 196.0f, ry + 22.0f, g.right - 120.0f, ry + 52.0f, 16, L"关闭自启", 0, settingsViewport);
                        D2D1_RECT_F btnFix = D2D1::RectF(g.right - 114.0f, ry + 22.0f, g.right - 16.0f, ry + 52.0f);
                        DrawPillButton(btnFix, L"改为当前位置", AddClippedZone(21, btnFix, settingsViewport), 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
                    }
                } else {
                    DrawToggleRow(g, ry, rowAutoStartH, 21, swAutoStartSpring.value, settingsViewport);
                }
                DrawRowText(g, ry, rowAutoStartH, staleAuto ? 208.0f : 82.0f, settingsViewport,
                            L"开机自动启动",
                            staleAuto ? L"开机自启当前指向另一个目录的程序副本，可切换为当前位置或关闭"
                                      : L"开机后自动隐藏在屏幕边缘待命，无需管理员权限",
                            true, staleAuto ? 0xFCD34D : Theme::TextSecondary);
                ry += rowAutoStartH;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 2.2: 启动时自动检查更新 (full-row clickable)
                DrawToggleRow(g, ry, 66.0f, 28, swAutoUpdSpring.value, settingsViewport);
                DrawRowText(g, ry, 66.0f, 82.0f, settingsViewport, L"启动时自动检查更新",
                            L"每天启动时静默检查一次新版本，无弹窗打扰");
                ry += 66.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 2.3: 软件更新 (compact by default; dynamically expands with manifest.notes / progress / error)
                std::wstring upTitle = (upSnap.phase == UpdatePhase::Available && !upSnap.latestVersion.empty())
                    ? (L"发现新版本 v" + upSnap.latestVersion + L"（当前 v" APP_VERSION_W L"）")
                    : std::wstring(L"软件更新 · 当前 v" APP_VERSION_W);
                const wchar_t* upDesc = upSnap.statusText.empty()
                    ? L"从官方 GitHub Releases 自动检查并安全更新，保留全部词库"
                    : upSnap.statusText.c_str();
                float upRightRes = (upSnap.phase == UpdatePhase::Available || upSnap.phase == UpdatePhase::Error) ? 214.0f : 118.0f;
                DrawRowText(g, ry, hasReleaseNotes ? 66.0f : rowUpdateH, upRightRes, settingsViewport, upTitle.c_str(), upDesc,
                            true, upSnap.phase == UpdatePhase::Error ? Theme::DangerRed : Theme::TextSecondary);
                if (ry + rowUpdateH >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 18.0f, bb = ry + 46.0f;
                    if (upSnap.phase == UpdatePhase::Available) {
                        DrawClippedPresetBtn(g.right - 204.0f, bt, g.right - 122.0f, bb, 27, L"打开发布页", 0, settingsViewport);
                        DrawClippedPresetBtn(g.right - 114.0f, bt, g.right - 16.0f,  bb,
                                             upSnap.canAutoInstall ? 26 : 25,
                                             upSnap.canAutoInstall ? L"更新并重启" : L"重新检查",
                                             upSnap.canAutoInstall ? 3 : 2, settingsViewport);
                        if (hasReleaseNotes) {
                            D2D1_RECT_F notesBox = D2D1::RectF(g.left + 16.0f, ry + 68.0f, g.right - 16.0f, ry + rowUpdateH - 12.0f);
                            FillRMRimHex(rt.Get(), brush.Get(), notesBox, 7.0f, 0x131722, 0.96f, Theme::AccentCyan, 0.22f);
                            std::wstring notesClean = CollapseWhitespaceSingleLine(upSnap.releaseNotes, 220);
                            DrawTextHex(rt.Get(), brush.Get(), fmtDescWrap.Get(),
                                        D2D1::RectF(notesBox.left + 10.0f, notesBox.top + 6.0f, notesBox.right - 10.0f, notesBox.bottom - 6.0f),
                                        notesClean.c_str(), (UINT32)notesClean.size(), Theme::TextSecondary, 0.92f);
                        }
                    } else if (upSnap.phase == UpdatePhase::Checking) {
                        D2D1_RECT_F btnBusy = D2D1::RectF(g.right - 108.0f, bt, g.right - 16.0f, bb);
                        DrawPillButton(btnBusy, L"检查中...", -2, 0x1B202E, 0x1B202E, Theme::TextMuted);
                        if (!Motion::IsReducedMotion()) {
                            float bw = (btnBusy.right - btnBusy.left) - 12.0f;
                            float swW = bw * 0.34f;
                            float sx = btnBusy.left + 6.0f + (bw - swW) * (0.5f - 0.5f * std::cos(updateShimmerPhase * 6.283185f));
                            FillRRHex(rt.Get(), brush.Get(), D2D1::RectF(sx, btnBusy.bottom - 3.0f, sx + swW, btnBusy.bottom - 1.0f), 1.0f, Theme::AccentCyan, 0.85f);
                        }
                    } else if (hasProgressBar) {
                        D2D1_RECT_F btnBusy = D2D1::RectF(g.right - 108.0f, bt, g.right - 16.0f, bb);
                        wchar_t pTxt[32]{};
                        if (upSnap.phase == UpdatePhase::Installing) wcscpy_s(pTxt, L"正在安装...");
                        else wsprintfW(pTxt, L"下载 %d%%", upSnap.downloadPercent);
                        DrawPillButton(btnBusy, pTxt, -2, 0x1E3354, 0x1E3354, Theme::AccentCyan, 0.25f);

                        D2D1_RECT_F track = D2D1::RectF(g.left + 16.0f, ry + rowUpdateH - 16.0f, g.right - 16.0f, ry + rowUpdateH - 10.0f);
                        FillRRHex(rt.Get(), brush.Get(), track, 3.0f, 0x0D0F15, 1.0f);
                        float pctRatio = Motion::IsReducedMotion()
                            ? std::clamp((float)upSnap.downloadPercent / 100.0f, 0.04f, 1.0f)
                            : std::clamp(updateProgAnim, 0.04f, 1.0f);
                        FillRRHex(rt.Get(), brush.Get(), D2D1::RectF(track.left, track.top, track.left + (track.right - track.left) * pctRatio, track.bottom),
                                  3.0f, Theme::AccentCyan, 0.92f);
                    } else {
                        if (upSnap.phase == UpdatePhase::Error) {
                            DrawClippedPresetBtn(g.right - 204.0f, bt, g.right - 116.0f, bb, 27, L"打开发布页", 0, settingsViewport);
                        }
                        DrawClippedPresetBtn(g.right - 108.0f, bt, g.right - 16.0f, bb, 25,
                                             upSnap.phase == UpdatePhase::Error ? L"重试检查" : L"检查更新", 2, settingsViewport);
                    }
                }
                sy += sec2H + 14.0f;
            }

            // === Section 3: 数据与备份 ===
            sy = DrawSectionHeader(sy, cw, settingsViewport, L"数据与备份");
            {
                D2D1_RECT_F g = DrawGroupContainer(sy, sec3H, cw, settingsViewport);
                float ry = sy;

                // Row 3.1: 数据备份与恢复 (clean 2-button layout; legacy v2 format tucked into save dialog file type!)
                DrawRowText(g, ry, 68.0f, 190.0f, settingsViewport, L"数据备份与恢复",
                            L"导出或导入全部词舱与场景配置，导入前会自动备份现有数据");
                if (ry + 68.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 20.0f, bb = ry + 48.0f;
                    DrawClippedPresetBtn(g.right - 178.0f, bt, g.right - 100.0f, bb, 5, L"导出备份", 1, settingsViewport);
                    DrawClippedPresetBtn(g.right - 94.0f,  bt, g.right - 16.0f,  bb, 4, L"导入备份", 2, settingsViewport);
                }
                ry += 68.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 3.2: 数据目录、复制路径与运行日志
                DrawRowText(g, ry, 72.0f, 276.0f, settingsViewport, L"本地数据目录与诊断日志",
                            storage ? storage->dataDir.c_str() : L"", false);
                if (ry + 72.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 22.0f, bb = ry + 50.0f;
                    DrawClippedPresetBtn(g.right - 266.0f, bt, g.right - 190.0f, bb, 39, L"复制路径",   0, settingsViewport);
                    DrawClippedPresetBtn(g.right - 184.0f, bt, g.right - 102.0f, bb, 23, L"打开数据目录", 1, settingsViewport);
                    DrawClippedPresetBtn(g.right - 96.0f,  bt, g.right - 16.0f,  bb, 24, L"运行日志",   2, settingsViewport);
                }
                sy += sec3H + 14.0f;
            }

            // === Section 4: 帮助与反馈 ===
            sy = DrawSectionHeader(sy, cw, settingsViewport, L"帮助与反馈");
            {
                D2D1_RECT_F g = DrawGroupContainer(sy, sec4H, cw, settingsViewport);
                float ry = sy;

                // Row 4.1: 快速上手引导
                DrawRowText(g, ry, 62.0f, 118.0f, settingsViewport, L"快速上手引导",
                            L"重新体验 3 步引导，快速熟悉复制、添加与停靠方式");
                if (ry + 62.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    DrawClippedPresetBtn(g.right - 108.0f, ry + 17.0f, g.right - 16.0f, ry + 45.0f, 40, L"重新引导", 2, settingsViewport);
                }
                ry += 62.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 4.2: 意见反馈 & 次级外链
                DrawRowText(g, ry, 68.0f, 294.0f, settingsViewport, L"意见反馈与社区",
                            L"反馈 Bug 或功能建议，或访问 GitHub 主页与作者 X（" APP_X_HANDLE_W L"）");
                if (ry + 68.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 20.0f, bb = ry + 48.0f;
                    DrawClippedPresetBtn(g.right - 284.0f, bt, g.right - 196.0f, bb, 29, L"GitHub 主页", 4, settingsViewport);
                    DrawClippedPresetBtn(g.right - 190.0f, bt, g.right - 110.0f, bb, 30, L"作者 X",      4, settingsViewport);
                    DrawClippedPresetBtn(g.right - 104.0f, bt, g.right - 16.0f,  bb, 31, L"意见反馈",     6, settingsViewport);
                }
                ry += 68.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 4.3: 内置提示词致谢（数字生命卡兹克 X & GitHub 点击跳转）
                DrawRowText(g, ry, 74.0f, 224.0f, settingsViewport, L"内置提示词致谢",
                            L"内置 12 条精选默认 Prompt 均整理自「数字生命卡兹克」的公开分享，特此致谢");
                if (ry + 74.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 23.0f, bb = ry + 51.0f;
                    DrawClippedPresetBtn(g.right - 214.0f, bt, g.right - 130.0f, bb, 64, L"卡兹克 X",      5, settingsViewport);
                    DrawClippedPresetBtn(g.right - 124.0f, bt, g.right - 16.0f,  bb, 65, L"卡兹克 GitHub", 5, settingsViewport);
                }
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        }

        // Bottom inline Toast notification (smooth spring rise/settle + 5s undo countdown hairline!)
        bool undoLive = (toastShowsUndo && undoDelete.active && nowTick < undoDelete.expireTick);
        bool toastVisible = !toastMessage.empty() && (!toastClosing || toastSpring.value > 0.01f);
        if (toastVisible) {
            float tv = std::clamp(toastSpring.value, 0.0f, 1.0f);
            float ta = Motion::EaseOutCubic(tv);
            float dySlide = Motion::IsReducedMotion() ? 0.0f : (10.0f * (1.0f - tv));
            float tw = std::min(cw - 40.0f, undoLive ? 410.0f : 368.0f);
            D2D1_RECT_F tr = D2D1::RectF((cw - tw) * 0.5f, ch - 48.0f + dySlide, (cw + tw) * 0.5f, ch - 14.0f + dySlide);
            uint32_t bgHex = toastIsError ? 0x3B1820 : (undoLive ? 0x1A2233 : 0x162925);
            uint32_t rimHex = toastIsError ? Theme::DangerRed : (undoLive ? Theme::AccentCyan : Theme::CopyGreen);
            float countdown = -1.0f;
            if (!Motion::IsReducedMotion() && undoLive && toastDurationMs > 0) {
                ULONGLONG el = (nowTick >= toastTick) ? (nowTick - toastTick) : 0;
                countdown = std::clamp(1.0f - (float)el / (float)toastDurationMs, 0.0f, 1.0f);
            }
            DrawAnimatedToastSurface(rt.Get(), brush.Get(), tr, 8.0f, bgHex, rimHex, ta, countdown);
            if (undoLive) {
                DrawTextHex(rt.Get(), brush.Get(), fmtCardBody.Get(),
                            D2D1::RectF(tr.left + 12.0f, tr.top, tr.right - 92.0f, tr.bottom),
                            toastMessage.c_str(), (UINT32)toastMessage.size(), Theme::TextPrimary, 0.96f * ta);
                D2D1_RECT_F btnUndo = D2D1::RectF(tr.right - 84.0f, tr.top + 5.0f, tr.right - 8.0f, tr.bottom - 5.0f);
                int zUndo = !toastClosing ? AddZone(35, btnUndo) : -2;
                DrawPillButton(btnUndo, undoDelete.isClearScene ? L"撤销清空" : L"撤销删除", zUndo, 0x264B7A, 0x3366A8, Theme::AccentCyan, 0.35f, ta);
            } else {
                DrawTextHex(rt.Get(), brush.Get(), fmtBadge.Get(), tr,
                            toastMessage.c_str(), (UINT32)toastMessage.size(),
                            toastIsError ? Theme::DangerRed : Theme::CopyGreen, 0.98f * ta);
            }
        }

        HRESULT hrEnd = rt->EndDraw();
        if (hrEnd == D2DERR_RECREATE_TARGET) {
            brush.Reset();
            rt.Reset();
            if (!d2dRecreateScheduled) {
                d2dRecreateScheduled = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else {
            d2dRecreateScheduled = false;
        }
        EndPaint(hwnd, &ps);
    }

    int HitTestZones(float dx, float dy) const {
        for (int i = (int)hitZones.size() - 1; i >= 0; --i) {
            const auto& r = hitZones[(size_t)i].rect;
            if (dx >= r.left && dx <= r.right && dy >= r.top && dy <= r.bottom) return i;
        }
        return -1;
    }

    void ShowSlotAssignPopup(const std::wstring& promptId, POINT screenPt) {
        if (!popupMenu || !state) return;
        int targetScene = TargetSceneForVaultAction();
        std::wstring sceneName = state->scenes[(size_t)targetScene].name;
        std::vector<DarkMenuItem> items;
        std::wstring hdr = state->IsMultiSceneActive()
            ? (L"\x88C5\x914D\x5230\x573A\x666F\xFF1A" + sceneName)
            : L"\x9009\x62E9\x8981\x88C5\x914D\x7684\x53F0\x9762\x69FD\x4F4D";
        items.push_back(DarkMenuItem::Header(hdr));
        int curSlot = state->FindSlotOfIdInScene(targetScene, promptId);
        const auto& curSlots = state->scenes[(size_t)targetScene].slots;
        for (int i = 0; i < 10; ++i) {
            const PromptItem* occ = state->FindById(curSlots[(size_t)i]);
            std::wstring label = L"\x69FD\x4F4D " + FormatSlotNum(i) + L" -- ";
            label += occ ? occ->title : L"\x7A7A\x69FD\x4F4D";
            items.push_back(DarkMenuItem::Action(100 + i, label, occ ? L"\x66FF\x6362" : L"\x7A7A\x95F2", curSlot == i));
        }
        if (curSlot >= 0) {
            items.push_back(DarkMenuItem::Separator());
            items.push_back(DarkMenuItem::Action(
                200,
                state->IsMultiSceneActive()
                    ? (L"移出「" + sceneName + L"」（保留在词舱）")
                    : L"\x79FB\x51FA\x69FD\x4F4D\xFF08\x4FDD\x7559\x5728\x8BCD\x8231\xFF09"));
        }
        lastMenuTriggerKind = 11;
        lastMenuTriggerId = promptId;
        popupMenu->ShowAt(screenPt, items, [this, promptId, targetScene](int cmd) {
            if (cmd >= 100 && cmd < 110) {
                int target = cmd - 100;
                AppState candidate = *state;
                candidate.AssignToSlotInScene(targetScene, target, promptId);
                if (!storage->Save(candidate)) {
                    ShowToast(L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x66F4\x65B0\x69FD\x4F4D", true);
                    return;
                }
                *state = std::move(candidate);
                if (onStateChanged) onStateChanged();
                std::wstring dest = state->IsMultiSceneActive()
                    ? (L"「" + state->scenes[(size_t)targetScene].name + L" · 槽位 " + FormatSlotNum(target) + L"」")
                    : (L"槽位 " + FormatSlotNum(target));
                ShowToast(L"\x5DF2\x653E\x5165" + dest, false);
                RefreshIfOpen();
            } else if (cmd == 200) {
                // Re-query current slot dynamically so stale slot index can never clear another item
                int liveSlot = state->FindSlotOfIdInScene(targetScene, promptId);
                if (liveSlot >= 0) {
                    AppState candidate = *state;
                    candidate.scenes[(size_t)targetScene].slots[(size_t)liveSlot].clear();
                    if (!storage->Save(candidate)) {
                        ShowToast(L"\x4FDD\x5B58\x5931\x8D25\xFF1A\x65E0\x6CD5\x79FB\x51FA\x69FD\x4F4D", true);
                        return;
                    }
                    *state = std::move(candidate);
                    if (onStateChanged) onStateChanged();
                    ShowToast(state->IsMultiSceneActive()
                        ? (L"已移出「" + state->scenes[(size_t)targetScene].name + L"」（词舱已保留）")
                        : L"已移出槽位（词舱已保留）", false);
                    RefreshIfOpen();
                }
            }
        }, 268.0f, MenuOwner::Vault);
    }

    void ShowCardMoreMenu(const std::wstring& promptId, POINT screenPt, bool includePrimaryActions = false) {
        if (!popupMenu || !state) return;
        const PromptItem* item = state->FindById(promptId);
        if (!item) return;
        std::vector<DarkMenuItem> items;
        if (includePrimaryActions) {
            items.push_back(DarkMenuItem::Header(CollapseWhitespaceSingleLine(item->title, 18)));
            items.push_back(DarkMenuItem::Action(1, L"\x590D\x5236\x5168\x6587"));
            items.push_back(DarkMenuItem::Action(2, assignTargetSlot >= 0
                ? (L"\x653E\x5165\x69FD\x4F4D " + FormatSlotNum(assignTargetSlot))
                : L"\x653E\x5165\x6216\x66FF\x6362\x69FD\x4F4D..."));
            items.push_back(DarkMenuItem::Action(3, L"\x7F16\x8F91 Prompt..."));
            items.push_back(DarkMenuItem::Separator());
        }
        items.push_back(DarkMenuItem::Action(4, L"\x521B\x5EFA\x526F\x672C"));
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(5, L"\x5220\x9664\x6B64 Prompt", L"", false, true));

        lastMenuTriggerKind = 15;
        lastMenuTriggerId = promptId;
        popupMenu->ShowAt(screenPt, items, [this, promptId, screenPt](int cmd) {
            if (cmd == 1) CopyPromptById(promptId);
            else if (cmd == 2) {
                if (assignTargetSlot >= 0) AssignPromptToTargetSlot(promptId);
                else ShowSlotAssignPopup(promptId, screenPt);
            } else if (cmd == 3) OpenEditPromptModal(promptId);
            else if (cmd == 4) DuplicatePrompt(promptId);
            else if (cmd == 5) DeletePromptWithUndo(promptId);
        }, 196.0f, MenuOwner::Vault);
    }

    void OnZoneClick(int idx, POINT screenPt) {
        if (idx < 0 || idx >= (int)hitZones.size()) return;
        HitZone z = hitZones[(size_t)idx];
        switch (z.kind) {
            case 1:
                SwitchTab(0);
                break;
            case 2:
                SwitchTab(1);
                break;
            case 7:
                SwitchTab(2);
                break;
            case 70:
            case 71:
            case 72:
            case 73:
            case 74:
            case 75: {
                // Pure view-level Vault filter (All / Scene 0..3 / Unmounted); NEVER mutates dock activeSceneIndex or writes disk!
                int nextFilter = (z.kind == 70) ? -1 : ((z.kind == 75) ? -2 : (z.kind - 71));
                if (nextFilter >= 0 && (!state || nextFilter >= kMaxScenes || !state->scenes[(size_t)nextFilter].enabled)) {
                    nextFilter = -1;
                }
                if (nextFilter != vaultFilter) {
                    vaultFilter = nextFilter;
                    scrollY = 0.0f;
                    scrollTargetY = 0.0f;
                    rowShifts.clear();
                    selectedVaultRow = searchQuery.empty() ? -1 : 0;
                    InvalidateCaches();
                    UpdateSearchCueBanner();
                    if (!Motion::IsReducedMotion()) EnsureAnimTimer();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 20: {
                state->showHoverBubble = !state->showHoverBubble;
                if (!storage->Save(*state)) {
                    ShowToast(L"保存设置失败：当前处于只读保护或文件不可写", true);
                } else {
                    if (onStateChanged) onStateChanged();
                    ShowToast(state->showHoverBubble ? L"已开启悬停预览正文" : L"已关闭悬停预览正文", false);
                }
                SyncSettingsSprings(false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 16: {
                if (AutoStart::SetEnabled(false)) {
                    ShowToast(L"已关闭开机自动启动", false);
                } else {
                    ShowToast(L"修改开机自启失败", true);
                }
                SyncSettingsSprings(false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 21: {
                auto st = AutoStart::GetStatus();
                bool next = (st != AutoStart::Status::EnabledCurrent);
                if (AutoStart::SetEnabled(next)) {
                    if (st == AutoStart::Status::StaleOtherPath) {
                        ShowToast(L"已将开机自动启动切换为当前位置", false);
                    } else {
                        ShowToast(next ? L"已开启开机自动启动" : L"已关闭开机自动启动", false);
                    }
                } else {
                    ShowToast(L"修改开机自启失败", true);
                }
                SyncSettingsSprings(false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 22: {
                if (onResetDockPos) onResetDockPos();
                ShowToast(L"贴边面板已归位", false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 33:
            case 34:
            case 36: {
                DockEdge nextEdge = (z.kind == 33) ? DockEdge::Right
                                  : (z.kind == 34 ? DockEdge::Left : DockEdge::None);
                if (onSetDockEdge) {
                    onSetDockEdge(nextEdge);
                } else {
                    state->dockEdge = nextEdge;
                    storage->Save(*state);
                    if (onStateChanged) onStateChanged();
                }
                ShowToast(nextEdge == DockEdge::Right ? L"已切换至右侧贴边"
                        : (nextEdge == DockEdge::Left ? L"已切换至左侧贴边" : L"已切换至自由悬浮模式"), false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 37:
            case 38: {
                bool wantVault = (z.kind == 37);
                if (state->trayClickOpensVault != wantVault) {
                    state->trayClickOpensVault = wantVault;
                    if (!storage->Save(*state)) {
                        ShowToast(L"保存设置失败：当前无法写入配置文件", true);
                    } else {
                        ShowToast(wantVault ? L"点击托盘图标将打开词舱管理" : L"点击托盘图标将切换贴边面板", false);
                    }
                    EnsureAnimTimer();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 39: {
                if (storage && !storage->dataDir.empty()) {
                    if (CopyTextToClipboard(hwnd, storage->dataDir)) {
                        ShowToast(L"已复制数据目录路径到剪贴板", false);
                    } else {
                        ShowToast(L"复制失败：剪贴板被其他程序占用", true);
                    }
                }
                break;
            }
            case 40: {
                if (onReplayOnboarding) onReplayOnboarding();
                break;
            }
            case 50:
            case 51:
            case 52:
            case 53: {
                int scIdx = z.kind - 50;
                AppState candidate = *state;
                bool wasEnabled = candidate.scenes[(size_t)scIdx].enabled;
                if (!candidate.ToggleSceneEnabled(scIdx)) {
                    lockedSceneShakeIdx = scIdx;
                    lockedSceneShakeTick = GetTickCount64();
                    ShowToast(L"至少需保留 1 个启用场景", false);
                    EnsureAnimTimer();
                    break;
                }
                if (!storage->Save(candidate)) {
                    ShowToast(L"保存设置失败：当前处于只读保护或配置文件不可写", true);
                } else {
                    *state = std::move(candidate);
                    InvalidateCaches();
                    if (onStateChanged) onStateChanged();
                    std::wstring scName = state->scenes[(size_t)scIdx].name;
                    ShowToast(wasEnabled
                        ? (L"已隐藏场景「" + scName + L"」（槽位编排已完整保留）")
                        : (L"已启用场景「" + scName + L"」"), false);
                }
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 54:
            case 55:
            case 56:
            case 57: {
                OpenScenePromptsModal(z.kind - 54);
                break;
            }
            case 60:
            case 61:
            case 62:
            case 63: {
                OpenRenameSceneModal(z.kind - 60);
                break;
            }
            case 64: {
                INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", APP_KHAZIX_X_URL_W, nullptr, nullptr, SW_SHOWNORMAL));
                if (r <= 32) ShowToast(L"打开卡兹克 X 主页失败", true);
                break;
            }
            case 65: {
                INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", APP_KHAZIX_GH_URL_W, nullptr, nullptr, SW_SHOWNORMAL));
                if (r <= 32) ShowToast(L"打开卡兹克 GitHub 主页失败", true);
                break;
            }
            case 17: {
                int sc = (assignTargetSlot < 0 && activeTab == 0 && EffectiveVaultFilter() >= 0) ? EffectiveVaultFilter() : -1;
                OpenNewPromptModal(assignTargetSlot, nullptr, CollapseWhitespaceSingleLine(searchQuery, 24), false, sc);
                break;
            }
            case 8: {
                ClearSearchQueryAndFocus();
                break;
            }
            case 35: {
                UndoLastDelete();
                break;
            }
            case 23: {
                if (storage && !storage->dataDir.empty()) {
                    INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", storage->dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                    if (r <= 32) ShowToast(L"\x6253\x5F00\x6570\x636E\x76EE\x5F55\x5931\x8D25", true);
                }
                break;
            }
            case 24: {
                if (storage && !storage->logFilePath.empty()) {
                    DiagLogger::Instance().Log("INFO", "UI", L"User opened diagnostic log from Settings");
                    INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", storage->logFilePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                    if (r <= 32) ShowToast(L"\x6253\x5F00\x8FD0\x884C\x65E5\x5FD7\x5931\x8D25", true);
                }
                break;
            }
            case 25: {
                if (updater) {
                    if (!updater->StartCheck(false)) {
                        ShowToast(L"正在检查或下载更新，请稍候", false);
                    }
                    EnsureAnimTimer();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 26: {
                if (updater) {
                    if (!updater->StartDownloadAndInstall()) {
                        ShowToast(L"当前无法启动原地更新，请前往发布页下载", true);
                    } else {
                        updateProgAnim = 0.04f;
                        EnsureAnimTimer();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 27: {
                std::wstring url = APP_GITHUB_RELEASES_W;
                if (updater) {
                    auto snap = updater->GetSnapshot();
                    if (!snap.releaseUrl.empty()) url = snap.releaseUrl;
                }
                INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                if (r <= 32) ShowToast(L"打开默认浏览器失败", true);
                break;
            }
            case 28: {
                state->autoCheckUpdates = !state->autoCheckUpdates;
                if (!storage->Save(*state)) {
                    ShowToast(L"保存设置失败：当前无法写入配置文件", true);
                } else {
                    ShowToast(state->autoCheckUpdates ? L"已开启启动时自动检查更新" : L"已关闭启动时自动检查更新", false);
                }
                SyncSettingsSprings(false);
                EnsureAnimTimer();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 29: {
                INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", APP_GITHUB_URL_W, nullptr, nullptr, SW_SHOWNORMAL));
                if (r <= 32) ShowToast(L"打开 GitHub 项目主页失败", true);
                break;
            }
            case 30: {
                INT_PTR r = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", APP_X_URL_W, nullptr, nullptr, SW_SHOWNORMAL));
                if (r <= 32) ShowToast(L"打开 X 主页失败", true);
                break;
            }
            case 31: {
                OpenFeedbackModal();
                break;
            }
            case 32: {
                if (storage && state) {
                    std::wstring restoreErr;
                    if (storage->RestoreFromLastBackup(*state, restoreErr)) {
                        InvalidateCaches();
                        UpdateSearchVisibility();
                        if (onStateChanged) onStateChanged();
                        ShowToast(L"已从上次备份恢复并解除只读保护", false);
                    } else {
                        ShowToast(restoreErr.empty() ? L"恢复上次备份失败" : restoreErr, true);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 3: {
                int sc = (assignTargetSlot < 0 && activeTab == 0 && EffectiveVaultFilter() >= 0) ? EffectiveVaultFilter() : -1;
                OpenNewPromptModal(assignTargetSlot, nullptr, L"", false, sc);
                break;
            }
            case 4:
                ImportBackupDialog();
                break;
            case 5:
                ExportBackupDialog();
                break;
            case 6:
                assignTargetSlot = -1;
                UpdateSearchVisibility();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case 10: { // Copy from vault or leaderboard
                CopyPromptById(z.promptId);
                break;
            }
            case 11: { // Assign to slot
                if (assignTargetSlot >= 0 && assignTargetSlot < 10) {
                    AssignPromptToTargetSlot(z.promptId);
                } else {
                    if (popupMenu && popupMenu->WasJustClosedByOutsideClickAt(MenuOwner::Vault, screenPt) &&
                        lastMenuTriggerKind == 11 && lastMenuTriggerId == z.promptId) {
                        lastMenuTriggerKind = 0;
                        break;
                    }
                    ShowSlotAssignPopup(z.promptId, screenPt);
                }
                break;
            }
            case 15: { // Card '···' more menu (toggle cleanly if just closed by clicking same button)
                if (popupMenu && popupMenu->WasJustClosedByOutsideClickAt(MenuOwner::Vault, screenPt) &&
                    lastMenuTriggerKind == 15 && lastMenuTriggerId == z.promptId) {
                    lastMenuTriggerKind = 0;
                    break;
                }
                ShowCardMoreMenu(z.promptId, screenPt, false);
                break;
            }
            case 12:
                OpenEditPromptModal(z.promptId);
                break;
            case 14:
                DuplicatePrompt(z.promptId);
                break;
            case 13:
                DeletePromptWithUndo(z.promptId);
                break;
        }
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        VaultWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<VaultWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<VaultWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                float sc = GetDpiScaleForWindow(h);
                mmi->ptMinTrackSize.x = (LONG)(640.0f * sc);
                mmi->ptMinTrackSize.y = (LONG)(460.0f * sc);
                return 0;
            }
            case WM_SETTINGCHANGE:
                Motion::RefreshSystemReducedMotion();
                if (Motion::IsReducedMotion()) {
                    self->SnapAllAnims();
                    if (!self->copiedFlashId.empty()) {
                        ULONGLONG el = GetTickCount64() - self->copiedFlashTick;
                        if (el >= 1100) self->copiedFlashId.clear();
                        else SetTimer(h, kTimerCopyClear, (UINT)(1100 - el), nullptr);
                    }
                    self->StopAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_SIZE:
                if (self->rt) {
                    self->rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                }
                self->filterPillInit = false;
                self->edgePillInit = false;
                self->trayPillInit = false;
                self->PositionSearchControl();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset();
                self->rt.Reset();
                auto* suggested = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                self->RebuildSearchFont();
                self->PositionSearchControl();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wp;
                SetTextColor(hdc, RGB(241, 245, 249));
                SetBkColor(hdc, RGB(27, 31, 43));
                return (LRESULT)self->hSearchBrush;
            }
            case WM_KEYDOWN:
                if (self->HandleShortcutKey(wp, false)) return 0;
                break;
            case WM_COMMAND:
                if (LOWORD(wp) == 2001) {
                    WORD code = HIWORD(wp);
                    if (code == EN_SETFOCUS) {
                        self->searchFocused = true;
                        if (Motion::IsReducedMotion()) self->searchFocusAnim = 1.0f;
                        else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        self->searchFocused = false;
                        if (Motion::IsReducedMotion()) self->searchFocusAnim = 0.0f;
                        else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_CHANGE) {
                        int len = GetWindowTextLengthW(self->hSearchEdit);
                        std::wstring q((size_t)len + 1, L'\0');
                        if (len > 0) GetWindowTextW(self->hSearchEdit, q.data(), len + 1);
                        q.resize((size_t)len);
                        self->searchQuery = q;
                        self->scrollY = 0.0f;
                        self->scrollTargetY = 0.0f;
                        self->selectedVaultRow = q.empty() ? -1 : 0;
                        self->InvalidateCaches();
                        if (Motion::IsReducedMotion()) self->clearBtnAnim = q.empty() ? 0.0f : 1.0f;
                        else self->EnsureAnimTimer();
                        InvalidateRect(h, nullptr, FALSE);
                    }
                }
                return 0;
            case WM_MOUSEWHEEL: {
                if (self->viewportScroll.dragging || self->isVaultDragging || self->vaultDragIdx >= 0) return 0;
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                self->scrollTargetY = std::clamp(self->scrollTargetY - (float)delta * 0.45f, 0.0f, self->maxScrollY);
                if (Motion::IsReducedMotion()) {
                    self->scrollY = self->scrollTargetY;
                } else {
                    Motion::ExpApproach(self->scrollY, self->scrollTargetY, 20.0f, 0.016f, 0.25f);
                    self->EnsureAnimTimer();
                }
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    if (self->isVaultDragging) {
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    } else if (self->hoverZoneIdx >= 0) {
                        SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    } else {
                        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                    }
                    return TRUE;
                }
                break;
            case WM_RBUTTONUP: {
                RECT crc{};
                GetClientRect(h, &crc);
                float cw = (float)(crc.right - crc.left) / self->dpiScale;
                float ch = (float)(crc.bottom - crc.top) / self->dpiScale;
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                if (dx < 20.0f || dx > cw - 20.0f || dy > ch - 8.0f) return 0;
                POINT spt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                ClientToScreen(h, &spt);
                if (self->activeTab == 0 && dy >= self->vaultListTopDip) {
                    if (const auto* hit = self->HitTestDrawnCard(dx, dy)) {
                        if (hit->vaultIdx < self->state->vault.size()) {
                            self->selectedVaultRow = hit->rowPos;
                            InvalidateRect(h, nullptr, FALSE);
                            self->ShowCardMoreMenu(self->state->vault[hit->vaultIdx].id, spt, true);
                        }
                    }
                } else if (self->activeTab == 1 && dy >= self->rankListTopDip) {
                    self->EnsureRankCache();
                    float relY = (dy - self->rankListTopDip) + self->scrollY;
                    int idx = (int)std::floor(relY / 65.0f);
                    if (idx >= 0 && idx < (int)self->cachedRankedIndices.size() && (relY - idx * 65.0f) <= 58.0f) {
                        size_t vi = self->cachedRankedIndices[(size_t)idx];
                        if (vi < self->state->vault.size()) {
                            self->ShowCardMoreMenu(self->state->vault[vi].id, spt, true);
                        }
                    }
                }
                return 0;
            }
            case WM_LBUTTONDBLCLK: {
                RECT crc{};
                GetClientRect(h, &crc);
                float cw = (float)(crc.right - crc.left) / self->dpiScale;
                float ch = (float)(crc.bottom - crc.top) / self->dpiScale;
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                if (dx >= 20.0f && dx <= cw - 20.0f && dy <= ch - 8.0f && self->HitTestZones(dx, dy) < 0) {
                    if (self->activeTab == 0 && dy >= self->vaultListTopDip) {
                        if (const auto* hit = self->HitTestDrawnCard(dx, dy)) {
                            if (hit->vaultIdx < self->state->vault.size()) {
                                if (self->assignTargetSlot >= 0) self->AssignPromptToTargetSlot(self->state->vault[hit->vaultIdx].id);
                                else self->OpenEditPromptModal(self->state->vault[hit->vaultIdx].id);
                            }
                        }
                    } else if (self->activeTab == 1 && dy >= self->rankListTopDip) {
                        self->EnsureRankCache();
                        float relY = (dy - self->rankListTopDip) + self->scrollY;
                        int idx = (int)std::floor(relY / 65.0f);
                        if (idx >= 0 && idx < (int)self->cachedRankedIndices.size() && (relY - idx * 65.0f) <= 58.0f) {
                            size_t vi = self->cachedRankedIndices[(size_t)idx];
                            if (vi < self->state->vault.size()) {
                                if (self->assignTargetSlot >= 0) self->AssignPromptToTargetSlot(self->state->vault[vi].id);
                                else self->OpenEditPromptModal(self->state->vault[vi].id);
                            }
                        }
                    }
                }
                return 0;
            }
            case WM_LBUTTONDOWN: {
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedZoneKind = 0;
                self->pressedZonePromptId.clear();

                // Check custom dark viewport scrollbar first (direct 1:1 tracking, zero lag!)
                if (self->viewportScroll.OnLButtonDown(dx, dy, self->scrollY, self->maxScrollY, self->lastViewportH)) {
                    self->scrollTargetY = self->scrollY;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }

                int hit = self->HitTestZones(dx, dy);
                if (hit >= 0) {
                    self->pressedZoneKind = self->hitZones[(size_t)hit].kind;
                    self->pressedZonePromptId = self->hitZones[(size_t)hit].promptId;
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->activeTab == 0) {
                    RECT crc{};
                    GetClientRect(h, &crc);
                    float cw = (float)(crc.right - crc.left) / self->dpiScale;
                    if (dx >= 18.0f && dx <= cw - 226.0f && dy >= self->vaultListTopDip) {
                        if (const auto* hit = self->HitTestDrawnCard(dx, dy)) {
                            self->selectedVaultRow = hit->rowPos;
                            if (self->searchQuery.empty() && self->EffectiveVaultFilter() == -1 && hit->vaultIdx < self->state->vault.size()) {
                                self->scrollTargetY = self->scrollY;
                                self->vaultDragIdx = (int)hit->vaultIdx;
                                self->vaultDragOrigIdx = self->vaultDragIdx;
                                self->isVaultDragging = false;
                                self->vaultDragStartDipY = dy;
                                self->vaultDragLastDipY = dy;
                                self->vaultDragGrabOffsetDipY = dy - hit->cardTop;
                                self->vaultDragCurDipY = hit->cardTop;
                                SetCapture(h);
                            }
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    }
                }
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;

                bool sbRedraw = false;
                if (self->viewportScroll.OnMouseMove(dx, dy, self->scrollY, self->maxScrollY, self->lastViewportH, sbRedraw)) {
                    if (self->viewportScroll.dragging) self->scrollTargetY = self->scrollY;
                    if (self->hoverZoneIdx != -1) {
                        self->hoverZoneIdx = -1;
                        sbRedraw = true;
                    }
                    if (self->viewportScroll.IsAnimActive()) self->EnsureAnimTimer();
                    if (sbRedraw) InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->viewportScroll.IsAnimActive()) self->EnsureAnimTimer();
                if (sbRedraw) InvalidateRect(h, nullptr, FALSE);

                if (self->vaultDragIdx >= 0) {
                    self->vaultDragLastDipY = dy;
                    if (!self->isVaultDragging && std::fabs(dy - self->vaultDragStartDipY) >= 5.0f) {
                        self->isVaultDragging = true;
                        self->vaultDragLiftSpring.Snap(Motion::IsReducedMotion() ? 1.0f : 0.0f);
                        self->vaultDragLiftSpring.target = 1.0f;
                        if (!Motion::IsReducedMotion()) self->EnsureAnimTimer();
                    }
                    if (self->isVaultDragging) {
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                        RECT crc{};
                        GetClientRect(h, &crc);
                        float ch = (float)(crc.bottom - crc.top) / self->dpiScale;
                        self->UpdateDragScrollTimer(dy, ch);
                        self->vaultDragCurDipY = std::clamp(dy - self->vaultDragGrabOffsetDipY, self->vaultListTopDip - 4.0f, ch - 72.0f);
                        self->StepVaultDragReorder();
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    }
                }

                int nh = self->HitTestZones(dx, dy);
                if (nh != self->hoverZoneIdx) {
                    self->hoverZoneIdx = nh;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->trackingLeave = false;
                if (self->viewportScroll.hovered && !self->viewportScroll.dragging) {
                    self->viewportScroll.hovered = false;
                    if (Motion::IsReducedMotion()) self->viewportScroll.hoverAnim = 0.0f;
                    else self->EnsureAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                }
                if (self->hoverZoneIdx != -1) {
                    self->hoverZoneIdx = -1;
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONUP: {
                if (self->viewportScroll.dragging) {
                    self->viewportScroll.dragging = false;
                    self->scrollTargetY = self->scrollY;
                    if (GetCapture() == h) ReleaseCapture();
                    if (self->viewportScroll.IsAnimActive()) self->EnsureAnimTimer();
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->vaultDragIdx >= 0) {
                    bool dragged = self->isVaultDragging;
                    int finIdx = self->vaultDragIdx;
                    self->vaultDragIdx = -1;
                    self->vaultDragOrigIdx = -1;
                    self->isVaultDragging = false;
                    self->vaultDragLiftSpring.target = 0.0f;
                    if (self->dragScrollTimerActive) {
                        KillTimer(h, kTimerDragScroll);
                        self->dragScrollTimerActive = false;
                    }
                    if (GetCapture() == h) ReleaseCapture();
                    if (dragged) {
                        if (!Motion::IsReducedMotion() && finIdx >= 0 && finIdx < (int)self->state->vault.size()) {
                            float landingY = self->vaultListTopDip + (float)finIdx * 76.0f - self->scrollY;
                            self->AddRowShift(self->state->vault[(size_t)finIdx].id, self->vaultDragCurDipY - landingY);
                            self->EnsureAnimTimer();
                        }
                        self->storage->Save(*self->state);
                        self->InvalidateCaches();
                        InvalidateRect(h, nullptr, FALSE);
                        return 0;
                    }
                }
                int downKind = self->pressedZoneKind;
                std::wstring downPid = std::move(self->pressedZonePromptId);
                self->pressedZoneKind = 0;
                self->pressedZonePromptId.clear();
                if (GetCapture() == h) ReleaseCapture();
                if (downKind == 0) return 0;
                InvalidateRect(h, nullptr, FALSE);

                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int hit = self->HitTestZones(dx, dy);
                if (hit >= 0 && self->hitZones[(size_t)hit].kind == downKind &&
                    self->hitZones[(size_t)hit].promptId == downPid) {
                    POINT spt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                    ClientToScreen(h, &spt);
                    self->OnZoneClick(hit, spt);
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
            case WM_CANCELMODE: {
                bool hadPressed = (self->pressedZoneKind != 0);
                self->pressedZoneKind = 0;
                self->pressedZonePromptId.clear();
                self->viewportScroll.dragging = false;
                if (self->vaultDragIdx >= 0 || self->isVaultDragging) {
                    self->CancelVaultDrag(true);
                } else if (hadPressed) {
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_TIMER:
                if (wp == kTimerAnim) {
                    self->OnAnimTick();
                } else if (wp == kTimerCopyClear) {
                    KillTimer(h, kTimerCopyClear);
                    self->copiedFlashId.clear();
                    InvalidateRect(h, nullptr, FALSE);
                } else if (wp == kTimerFeedback) {
                    KillTimer(h, kTimerFeedback);
                    ULONGLONG now = GetTickCount64();
                    if (self->undoDelete.active && now >= self->undoDelete.expireTick) {
                        self->undoDelete.active = false;
                    }
                    if (Motion::IsReducedMotion() || self->toastMessage.empty()) {
                        self->toastMessage.clear();
                        self->toastClosing = false;
                        self->toastShowsUndo = false;
                        self->toastSpring.Snap(0.0f);
                        InvalidateRect(h, nullptr, FALSE);
                    } else {
                        self->toastClosing = true;
                        self->toastSpring.target = 0.0f;
                        self->EnsureAnimTimer();
                    }
                } else if (wp == kTimerDragScroll) {
                    if (!self->isVaultDragging || self->vaultDragIdx < 0) {
                        KillTimer(h, kTimerDragScroll);
                        self->dragScrollTimerActive = false;
                        return 0;
                    }
                    RECT crc{};
                    GetClientRect(h, &crc);
                    float ch = (float)(crc.bottom - crc.top) / self->dpiScale;
                    float scrollDelta = 0.0f;
                    if (self->vaultDragLastDipY < self->vaultListTopDip + 28.0f) {
                        scrollDelta = -12.0f;
                    } else if (self->vaultDragLastDipY > ch - 36.0f) {
                        scrollDelta = 12.0f;
                    }
                    if (scrollDelta != 0.0f && self->maxScrollY > 0.5f) {
                        float nextScroll = std::clamp(self->scrollY + scrollDelta, 0.0f, self->maxScrollY);
                        if (std::fabs(nextScroll - self->scrollY) > 0.1f) {
                            self->scrollY = nextScroll;
                            self->scrollTargetY = nextScroll;
                            self->StepVaultDragReorder();
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    }
                    self->UpdateDragScrollTimer(self->vaultDragLastDipY, ch);
                }
                return 0;
            case WM_PAINT:
                self->OnPaint();
                return 0;
            case WM_CLOSE:
                // Reset transient search query, view filter, drag, toast display & animations when closing VaultWindow
                self->CancelVaultDrag(true);
                self->StopAnimTimer();
                KillTimer(h, kTimerFeedback);
                KillTimer(h, kTimerCopyClear);
                self->toastMessage.clear();
                self->toastClosing = false;
                self->toastShowsUndo = false;
                self->toastSpring.Snap(0.0f);
                self->copiedFlashId.clear();
                self->searchQuery.clear();
                self->searchFocused = false;
                self->searchFocusAnim = 0.0f;
                self->clearBtnAnim = 0.0f;
                self->assignTargetSlot = -1;
                self->vaultFilter = -1;
                self->selectedVaultRow = -1;
                self->scrollY = 0.0f;
                self->scrollTargetY = 0.0f;
                self->rowShifts.clear();
                self->InvalidateCaches();
                if (self->hSearchEdit && IsWindow(self->hSearchEdit)) {
                    SetWindowTextW(self->hSearchEdit, L"");
                }
                ShowWindow(h, SW_HIDE); // Hide to keep Dock fast; free D2D surface to save RAM!
                self->brush.Reset();
                self->rt.Reset();
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

} // namespace pp
