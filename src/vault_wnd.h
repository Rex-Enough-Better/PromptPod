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
                                                 float borderAlpha = 0.12f, bool pressed = false) {
    uint32_t fillHex = pressed ? baseBg : (hov ? hoverBg : baseBg);
    br->SetColor(Rgba(fillHex, pressed ? 0.84f : 1.0f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), br);
    float topA = pressed ? (borderAlpha * 0.7f) : (hov ? borderAlpha + 0.10f : borderAlpha);
    DrawRim(rt, r, 6.0f, Rgba(0xFFFFFF, topA), Rgba(0xFFFFFF, 0.04f));
    if (fmt && text && *text) {
        std::wstring s = UiText(text);
        br->SetColor(Rgba(textHex, pressed ? 0.85f : (hov ? 1.0f : 0.92f)));
        rt->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, br);
    }
}

[[gnu::noinline]] inline void DrawDarkCheckbox(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                               const D2D1_RECT_F& cb, bool checked, bool hovered,
                                               uint32_t accentHex = Theme::AccentCyan, bool locked = false) {
    uint32_t fillHex = checked ? (locked ? 0x1E3A5F : 0x2563EB) : (hovered ? 0x252B3B : 0x151924);
    br->SetColor(Rgba(fillHex, locked ? 0.72f : 1.0f));
    rt->FillRoundedRectangle(D2D1::RoundedRect(cb, 4.0f, 4.0f), br);
    DrawRim(rt, cb, 4.0f,
            checked ? Rgba(accentHex, locked ? 0.36f : 0.72f) : Rgba(0xFFFFFF, hovered ? 0.28f : 0.18f),
            Rgba(0xFFFFFF, 0.05f));
    if (checked) {
        br->SetColor(Rgba(0xFFFFFF, locked ? 0.65f : 1.0f));
        rt->DrawLine(D2D1::Point2F(cb.left + 3.5f, cb.top + 8.2f), D2D1::Point2F(cb.left + 6.8f, cb.top + 11.6f), br, 1.8f);
        rt->DrawLine(D2D1::Point2F(cb.left + 6.8f, cb.top + 11.6f), D2D1::Point2F(cb.right - 3.2f, cb.top + 4.4f), br, 1.8f);
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
    float dragGrabOffsetY = 0.0f;

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
        float activeW = (hovered || dragging) ? 5.0f : 3.8f;
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
            outNeedRedraw = true;
        }
        return inHit;
    }

    void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, float scrollY, float maxScrollY, float viewportH) const {
        if (!rt || !br) return;
        D2D1_RECT_F thumb{};
        if (!ComputeThumb(scrollY, maxScrollY, viewportH, thumb)) return;
        if (hovered || dragging) {
            float trRad = (trackVisRect.right - trackVisRect.left) * 0.5f;
            br->SetColor(Rgba(0xFFFFFF, 0.055f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(trackVisRect, trRad, trRad), br);
        }
        float rad = (thumb.right - thumb.left) * 0.5f;
        br->SetColor(dragging ? Rgba(Theme::AccentCyan, 0.85f) : (hovered ? Rgba(0x94A3B8, 0.65f) : Rgba(0x94A3B8, 0.32f)));
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
    bool trackingLeave = false;
    bool dirtyConfirmArmed = false;
    ULONGLONG dirtyConfirmTick = 0;
    bool done = false;
    Result result{};

    D2D1_RECT_F rcCancelBtn{};
    D2D1_RECT_F rcSaveBtn{};

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
            SetFocus(hEditBody);
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
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.lpszClassName = L"PromptPod_EditorDlg";
        RegisterClassExW(&wc);

        POINT anchorPt{ 0, 0 };
        if (parent && IsWindowVisible(parent)) {
            RECT pr{};
            GetWindowRect(parent, &pr);
            anchorPt = { (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
        } else {
            GetCursorPos(&anchorPt);
        }
        RECT work = GetMonitorWorkAreaForPoint(anchorPt);
        float initScale = GetDpiScaleForPoint(anchorPt, parent);

        int wPx = (int)(510.0f * initScale);
        int hPx = (int)(440.0f * initScale);
        int x = 0, y = 0;
        ClampDialogRectToWorkArea(work, wPx, hPx, x, y);

        if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

        hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            wc.lpszClassName,
            heading.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            x, y, wPx, hPx,
            parent, nullptr, hInst, this
        );
        if (!hwnd) {
            if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
            return;
        }

        ApplyWin11DarkWindowAttributes(hwnd);
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

        // Guarantee HWND is destroyed before stack-allocated dialog object unwinds (zero dangling GWLP_USERDATA)
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }

        if (parent && IsWindow(parent)) {
            EnableWindow(parent, TRUE);
            if (!gotQuit) SetForegroundWindow(parent);
        }
        if (gotQuit) {
            PostQuitMessage((int)quitCode);
        }
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
            if (fmtHeading) {
                rt->DrawTextW(heading.c_str(), (UINT32)heading.size(), fmtHeading.Get(),
                              D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f), Br(Rgba(Theme::TextPrimary)));
            }

            // Title input label & live char count
            size_t titleChars = hEditTitle ? CountDisplayChars(GetWndText(hEditTitle)) : 0;
            size_t bodyChars  = hEditBody  ? CountDisplayChars(GetWndText(hEditBody))  : 0;
            std::wstring lbl1 = UiText(L"\x6807\x9898\xFF08\x9009\x586B\xFF0C\x7559\x7A7A\x81EA\x52A8\x53D6\x6B63\x6587\x9996\x884C\xFF09");
            if (fmtLabel) {
                rt->DrawTextW(lbl1.c_str(), (UINT32)lbl1.size(), fmtLabel.Get(),
                              D2D1::RectF(20.0f, 48.0f, cw - 92.0f, 68.0f), Br(Rgba(Theme::TextSecondary)));
            }
            if (fmtCount) {
                wchar_t tcBuf[32]{};
                wsprintfW(tcBuf, L"%u / 80 \u5B57", (unsigned)titleChars);
                std::wstring tc = UiText(tcBuf);
                rt->DrawTextW(tc.c_str(), (UINT32)tc.size(), fmtCount.Get(),
                              D2D1::RectF(cw - 92.0f, 48.0f, cw - 20.0f, 68.0f),
                              Br(Rgba(titleChars > 12 ? 0xFBBF24 : Theme::TextMuted, 0.92f)));
            }
            D2D1_RECT_F titleBox = D2D1::RectF(20.0f, 72.0f, cw - 20.0f, 110.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(titleBox, 7.0f, 7.0f), Br(Rgba(Theme::CardBg)));
            DrawRim(rt.Get(), titleBox, 7.0f,
                             focusedEdit == 1 ? Rgba(Theme::AccentBlue, 0.48f) : Rgba(0xFFFFFF, 0.14f),
                             focusedEdit == 1 ? Rgba(Theme::AccentBlue, 0.14f) : Rgba(0xFFFFFF, 0.06f));

            // Body input label & live char count
            std::wstring lbl2 = UiText(L"Prompt \x6B63\x6587\xFF08\x5DE6\x952E\x70B9\x51FB\x69FD\x4F4D\x65F6\x5B8C\x6574\x590D\x5236\xFF09");
            if (fmtLabel) {
                rt->DrawTextW(lbl2.c_str(), (UINT32)lbl2.size(), fmtLabel.Get(),
                              D2D1::RectF(20.0f, 118.0f, cw - 110.0f, 138.0f), Br(Rgba(Theme::TextSecondary)));
            }
            if (fmtCount) {
                wchar_t bcBuf[48]{};
                wsprintfW(bcBuf, L"\u5171 %u \u5B57", (unsigned)bodyChars);
                std::wstring bc = UiText(bcBuf);
                rt->DrawTextW(bc.c_str(), (UINT32)bc.size(), fmtCount.Get(),
                              D2D1::RectF(cw - 110.0f, 118.0f, cw - 20.0f, 138.0f),
                              Br(Rgba(Theme::TextMuted, 0.92f)));
            }
            D2D1_RECT_F bodyBox = D2D1::RectF(20.0f, 140.0f, cw - 20.0f, ch - 60.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(bodyBox, 7.0f, 7.0f), Br(Rgba(Theme::CardBg)));
            DrawRim(rt.Get(), bodyBox, 7.0f,
                             focusedEdit == 2 ? Rgba(Theme::AccentBlue, 0.48f) : Rgba(0xFFFFFF, 0.14f),
                             focusedEdit == 2 ? Rgba(Theme::AccentBlue, 0.14f) : Rgba(0xFFFFFF, 0.06f));
            bodyScroll.Draw(rt.Get(), brush.Get());

            // Hint or shortcut tip on bottom left
            std::wstring footTip = errorHint.empty()
                ? UiText(L"\u63D0\u793A\uFF1A\u6309 Ctrl+Enter \u5FEB\u901F\u4FDD\u5B58\uFF0CEsc \u53D6\u6D88")
                : errorHint;
            if (fmtHint) {
                D2D1_COLOR_F hc = errorHint.empty() ? Rgba(Theme::TextMuted, 0.9f) : Rgba(Theme::DangerRed, 0.95f);
                rt->DrawTextW(footTip.c_str(), (UINT32)footTip.size(), fmtHint.Get(),
                              D2D1::RectF(20.0f, ch - 46.0f, rcCancelBtn.left - 10.0f, ch - 14.0f), Br(hc));
            }

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
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        if (self->focusedEdit == ((id == 1001) ? 1 : 2)) {
                            self->focusedEdit = 0;
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
    bool trackingLeave = false;
    bool done = false;
    bool accepted = false;

    D2D1_RECT_F rcCancelBtn{};
    D2D1_RECT_F rcConfirmBtn{};

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

            if (fmtHeading) {
                rt->DrawTextW(heading.c_str(), (UINT32)heading.size(), fmtHeading.Get(),
                              D2D1::RectF(20.0f, 16.0f, cw - 20.0f, 44.0f), Br(Rgba(Theme::TextPrimary)));
            }

            D2D1_RECT_F infoBox = D2D1::RectF(20.0f, 52.0f, cw - 20.0f, ch - 64.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(infoBox, 8.0f, 8.0f), Br(Rgba(Theme::CardBg, 1.0f)));
            DrawRim(rt.Get(), infoBox, 8.0f,
                    dangerMode ? Rgba(Theme::DangerRed, 0.42f) : Rgba(Theme::AccentCyan, 0.28f),
                    Rgba(0xFFFFFF, 0.05f));
            if (fmtBody) {
                rt->DrawTextW(bodyText.c_str(), (UINT32)bodyText.size(), fmtBody.Get(),
                              D2D1::RectF(infoBox.left + 14.0f, infoBox.top + 12.0f, infoBox.right - 14.0f, infoBox.bottom - 10.0f),
                              Br(Rgba(Theme::TextSecondary, 0.96f)));
            }

            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcCancelBtn, cancelText.c_str(),
                               hoverBtn == 1, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.14f, pressedBtn == 1);
            DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), rcConfirmBtn, confirmText.c_str(),
                               hoverBtn == 2,
                               dangerMode ? 0xB91C1C : 0x3B7BF6,
                               dangerMode ? 0xDC2626 : 0x4F8EF7,
                               0xFFFFFF, 0.28f, pressedBtn == 2);
            if (focusBtn == 1 || focusBtn == 2) {
                const D2D1_RECT_F& fr = (focusBtn == 1) ? rcCancelBtn : rcConfirmBtn;
                D2D1_RECT_F ring = D2D1::RectF(fr.left - 2.0f, fr.top - 2.0f, fr.right + 2.0f, fr.bottom + 2.0f);
                rt->DrawRoundedRectangle(D2D1::RoundedRect(ring, 7.5f, 7.5f),
                                         Br(Rgba(focusBtn == 2 && dangerMode ? Theme::DangerRed : Theme::AccentCyan, 0.72f)), 1.4f);
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
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.lpszClassName = L"PromptPod_ConfirmDlg";
        RegisterClassExW(&wc);

        int x = 0, y = 0, wPx = 0, hPx = 0;
        dpiScale = ComputeModalPlacement(parent, 456.0f, 244.0f, x, y, wPx, hPx);

        if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

        hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            wc.lpszClassName,
            heading.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            x, y, wPx, hPx,
            parent, nullptr, hInst, this
        );
        if (!hwnd) {
            if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
            return;
        }

        ApplyWin11DarkWindowAttributes(hwnd);
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
                    if (!dangerMode) {
                        Finish(focusBtn == 2);
                    } else if (kbFocusMoved) {
                        Finish(focusBtn == 2);
                    }
                    continue;
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
        if (parent && IsWindow(parent)) {
            EnableWindow(parent, TRUE);
            if (!gotQuit) SetForegroundWindow(parent);
        }
        if (gotQuit) {
            PostQuitMessage((int)quitCode);
        }
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
    bool trackingLeave = false;
    bool done = false;
    bool accepted = false;
    D2D1_RECT_F rcCancel{};
    D2D1_RECT_F rcSave{};

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
            SetFocus(hEdit);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        int units = VisualWidthUnits(trimmed);
        if (units > kMaxSceneNameWidthUnits) {
            errorHint = UiText(L"名称过长（最多 4 个汉字或 8 个英文），请精简");
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

            wchar_t hdBuf[48]{};
            wsprintfW(hdBuf, L"重命名场景 %d", sceneIndex + 1);
            std::wstring hd = UiText(hdBuf);
            brush->SetColor(Rgba(Theme::TextPrimary));
            if (fmtHeading) rt->DrawTextW(hd.c_str(), (UINT32)hd.size(), fmtHeading.Get(), D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f), brush.Get());

            std::wstring lbl = UiText(L"短名称（最多 4 个汉字或 8 个英文字母）");
            brush->SetColor(Rgba(Theme::TextSecondary));
            if (fmtLabel) rt->DrawTextW(lbl.c_str(), (UINT32)lbl.size(), fmtLabel.Get(), D2D1::RectF(20.0f, 48.0f, cw - 92.0f, 68.0f), brush.Get());

            wchar_t cntBuf[32]{};
            wsprintfW(cntBuf, L"已用 %d / %d", units, kMaxSceneNameWidthUnits);
            std::wstring cnt = UiText(cntBuf);
            brush->SetColor(Rgba(overLimit ? Theme::DangerRed : Theme::AccentCyan, 0.96f));
            if (fmtCount) rt->DrawTextW(cnt.c_str(), (UINT32)cnt.size(), fmtCount.Get(), D2D1::RectF(cw - 94.0f, 48.0f, cw - 20.0f, 68.0f), brush.Get());

            D2D1_RECT_F box = D2D1::RectF(20.0f, 73.0f, cw - 20.0f, 111.0f);
            brush->SetColor(Rgba(Theme::CardBg));
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, 7.0f, 7.0f), brush.Get());
            DrawRim(rt.Get(), box, 7.0f,
                             overLimit ? Rgba(Theme::DangerRed, 0.55f) : (focused ? Rgba(Theme::AccentBlue, 0.50f) : Rgba(0xFFFFFF, 0.14f)),
                             Rgba(0xFFFFFF, 0.05f));

            std::wstring hint = errorHint.empty()
                ? UiText(L"示例：通用、产品、开发、写作、Coding（Enter 保存）")
                : errorHint;
            brush->SetColor(errorHint.empty() ? Rgba(Theme::TextMuted, 0.92f) : Rgba(Theme::DangerRed, 0.96f));
            if (fmtLabel) rt->DrawTextW(hint.c_str(), (UINT32)hint.size(), fmtLabel.Get(), D2D1::RectF(20.0f, 118.0f, cw - 20.0f, 142.0f), brush.Get());

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
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.lpszClassName = L"PromptPod_SceneRenameDlg";
        RegisterClassExW(&wc);

        POINT pt{ 0, 0 };
        if (parent && IsWindowVisible(parent)) {
            RECT pr{}; GetWindowRect(parent, &pr);
            pt = { (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
        } else {
            GetCursorPos(&pt);
        }
        RECT work = GetMonitorWorkAreaForPoint(pt);
        dpiScale = GetDpiScaleForPoint(pt, parent);
        int wPx = (int)(416.0f * dpiScale), hPx = (int)(228.0f * dpiScale), x = 0, y = 0;
        ClampDialogRectToWorkArea(work, wPx, hPx, x, y);
        if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, wc.lpszClassName, L"重命名场景",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, wPx, hPx, parent, nullptr, hInst, this);
        if (!hwnd) {
            if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
            return;
        }
        ApplyWin11DarkWindowAttributes(hwnd);
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
        if (hwnd && IsWindow(hwnd)) { DestroyWindow(hwnd); hwnd = nullptr; }
        if (parent && IsWindow(parent)) { EnableWindow(parent, TRUE); if (!gotQuit) SetForegroundWindow(parent); }
        if (gotQuit) PostQuitMessage((int)quitCode);
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
                    if (HIWORD(wp) == EN_SETFOCUS) { self->focused = true; InvalidateRect(h, nullptr, FALSE); }
                    else if (HIWORD(wp) == EN_KILLFOCUS) { self->focused = false; InvalidateRect(h, nullptr, FALSE); }
                    else if (HIWORD(wp) == EN_CHANGE) { self->errorHint.clear(); InvalidateRect(h, nullptr, FALSE); }
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
// Modal Dark Scene Prompts Manager Dialog (Check/Uncheck, Edit & Create Prompt)
// ============================================================================
class ScenePromptsDialog {
public:
    using MutatedFn = void(*)(void*);

    [[gnu::noinline]] static void Show(HINSTANCE hInst, HWND parent, AppState* st, StorageManager* sm, int sceneIdx,
                                       MutatedFn onMutated, void* mutatedCtx) {
        if (!st || !sm || sceneIdx < 0 || sceneIdx >= kMaxScenes) return;
        ScenePromptsDialog dlg(hInst, st, sm, sceneIdx, onMutated, mutatedCtx);
        dlg.RunModal(parent);
    }

private:
    struct Zone {
        int kind = 0; // 1:NewPrompt, 2:Done, 10:ToggleCheck, 11:EditPrompt
        std::wstring pid;
        D2D1_RECT_F rect{};
    };

    HINSTANCE hInst = nullptr;
    AppState* state = nullptr;
    StorageManager* storage = nullptr;
    int sceneIndex = 0;
    MutatedFn onMutated = nullptr;
    void* mutatedCtx = nullptr;
    HWND hwnd = nullptr;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteTextFormat> fmtHeading;
    ComPtr<IDWriteTextFormat> fmtTitle;
    ComPtr<IDWriteTextFormat> fmtBody;
    ComPtr<IDWriteTextFormat> fmtBadge;
    ComPtr<IDWriteTextFormat> fmtBtn;

    std::vector<std::wstring> orderedIds;
    std::vector<Zone> zones;
    DarkViewportScrollbar scrollbar;
    float dpiScale = 1.0f;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    float viewportH = 300.0f;
    int hoverZoneIdx = -1;
    int pressedKind = 0;
    std::wstring pressedPid;
    std::wstring statusText;
    bool statusIsErr = false;
    bool trackingLeave = false;
    bool done = false;

    ScenePromptsDialog(HINSTANCE inst, AppState* st, StorageManager* sm, int sc, MutatedFn cb, void* ctx)
        : hInst(inst), state(st), storage(sm), sceneIndex(sc), onMutated(cb), mutatedCtx(ctx) {
        RebuildStableOrder();
    }

    void NotifyMutated() {
        if (onMutated) onMutated(mutatedCtx);
    }

    [[gnu::noinline]] void RebuildStableOrder() {
        orderedIds.clear();
        if (!state) return;
        const auto& sl = state->scenes[(size_t)sceneIndex].slots;
        for (int i = 0; i < 10; ++i) {
            if (!sl[(size_t)i].empty() && state->FindById(sl[(size_t)i])) {
                orderedIds.push_back(sl[(size_t)i]);
            }
        }
        for (const auto& it : state->vault) {
            if (state->FindSlotOfIdInScene(sceneIndex, it.id) < 0) {
                orderedIds.push_back(it.id);
            }
        }
    }

    ID2D1SolidColorBrush* Br(const D2D1_COLOR_F& c) {
        brush->SetColor(c);
        return brush.Get();
    }

    void EnsureResources() {
        EnsureSoftwareHwndRT(hwnd, dpiScale, rt, brush);
        if (!fmtHeading) {
            auto& eng = D2DEngine::Instance();
            fmtHeading = eng.CreateFormat(15.0f, DWRITE_FONT_WEIGHT_BOLD);
            fmtTitle   = eng.CreateFormat(13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            fmtBody    = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_NORMAL);
            fmtBadge   = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtBtn     = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    [[gnu::noinline]] int AddZone(int kind, const D2D1_RECT_F& r, const std::wstring& pid = L"") {
        int idx = (int)zones.size();
        zones.push_back(Zone{ kind, pid, r });
        return idx;
    }

    [[gnu::noinline]] int AddClippedZone(int kind, const D2D1_RECT_F& r, const D2D1_RECT_F& clip, const std::wstring& pid) {
        D2D1_RECT_F cr = D2D1::RectF(
            std::max(r.left, clip.left), std::max(r.top, clip.top),
            std::min(r.right, clip.right), std::min(r.bottom, clip.bottom));
        if (cr.right <= cr.left || cr.bottom <= cr.top) return -1;
        return AddZone(kind, cr, pid);
    }

    void DrawBtn(const D2D1_RECT_F& r, const wchar_t* txt, int zIdx, uint32_t bg, uint32_t bgHov, uint32_t fg, float rimA = 0.14f) {
        bool isPressed = (zIdx >= 0 && zIdx < (int)zones.size() &&
                          zones[(size_t)zIdx].kind == pressedKind &&
                          zones[(size_t)zIdx].pid == pressedPid);
        DrawDarkPillButton(rt.Get(), brush.Get(), fmtBtn.Get(), r, txt, zIdx == hoverZoneIdx, bg, bgHov, fg, rimA, isPressed);
    }

    [[gnu::noinline]] void OnPaint() {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EnsureResources();
        if (rt && brush) {
            RECT crc{};
            GetClientRect(hwnd, &crc);
            float cw = (float)(crc.right - crc.left) / dpiScale;
            float ch = (float)(crc.bottom - crc.top) / dpiScale;
            zones.clear();

            rt->BeginDraw();
            rt->Clear(Rgba(0x10131B, 1.0f));

            // Top bar
            rt->FillRectangle(D2D1::RectF(0, 0, cw, 56.0f), Br(Rgba(0x141822, 1.0f)));
            rt->DrawLine(D2D1::Point2F(0, 56.0f), D2D1::Point2F(cw, 56.0f), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);

            uint32_t accTop = Theme::AccentCyan, accBot = Theme::AccentIndigo;
            Theme::SceneAccentColors(sceneIndex, accTop, accBot);
            uint32_t txtAcc = Theme::SceneTextAccent(sceneIndex);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(24.0f, 28.0f), 4.0f, 4.0f), Br(Rgba(accTop, 0.96f)));

            std::wstring hd = UiText(L"配置场景「" + state->scenes[(size_t)sceneIndex].name + L"」提示词");
            if (fmtHeading) {
                rt->DrawTextW(hd.c_str(), (UINT32)hd.size(), fmtHeading.Get(),
                              D2D1::RectF(36.0f, 12.0f, cw - 224.0f, 44.0f), Br(Rgba(Theme::TextPrimary, 0.98f)));
            }

            int mountedCnt = state->CountMountedInScene(sceneIndex);
            wchar_t cntBuf[32]{};
            wsprintfW(cntBuf, L"已选 %d / 10", mountedCnt);
            std::wstring cntStr = UiText(cntBuf);
            D2D1_RECT_F cntPill = D2D1::RectF(cw - 216.0f, 16.0f, cw - 134.0f, 40.0f);
            bool isFull = (mountedCnt >= 10);
            rt->FillRoundedRectangle(D2D1::RoundedRect(cntPill, 6.0f, 6.0f),
                                     Br(Rgba(isFull ? 0xF59E0B : accTop, 0.14f)));
            if (fmtBadge) {
                rt->DrawTextW(cntStr.c_str(), (UINT32)cntStr.size(), fmtBadge.Get(), cntPill,
                              Br(Rgba(isFull ? 0xFCD34D : txtAcc, 0.96f)));
            }

            D2D1_RECT_F rcNew = D2D1::RectF(cw - 126.0f, 13.0f, cw - 18.0f, 43.0f);
            int zNew = AddZone(1, rcNew);
            DrawBtn(rcNew, L"+ 新建 Prompt", zNew, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.25f);

            // Inline hint / status bar (plus quick '立即启用' button if this scene is currently hidden on dock)
            bool sceneHidden = !state->scenes[(size_t)sceneIndex].enabled;
            std::wstring tip = statusText.empty()
                ? UiText(sceneHidden
                    ? L"此场景当前未在贴边面板显示，配置后可点击右侧「立即启用」"
                    : L"勾选加入此场景，取消勾选仅移出场景、不删词舱内容")
                : statusText;
            uint32_t tipCol = statusText.empty() ? (sceneHidden ? 0xFBBF24 : Theme::TextMuted) : (statusIsErr ? Theme::DangerRed : Theme::CopyGreen);
            float tipRight = sceneHidden ? (cw - 104.0f) : (cw - 18.0f);
            if (fmtBody) {
                rt->DrawTextW(tip.c_str(), (UINT32)tip.size(), fmtBody.Get(),
                              D2D1::RectF(18.0f, 62.0f, tipRight, 84.0f), Br(Rgba(tipCol, 0.95f)));
            }
            if (sceneHidden) {
                D2D1_RECT_F rcEn = D2D1::RectF(cw - 96.0f, 61.0f, cw - 18.0f, 83.0f);
                int zEn = AddZone(3, rcEn);
                DrawBtn(rcEn, L"立即启用", zEn, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.30f);
            }

            // Scrollable checklist viewport
            float listTop = 88.0f;
            float listBot = ch - 52.0f;
            D2D1_RECT_F vp = D2D1::RectF(0.0f, listTop, cw, listBot);
            viewportH = std::max(60.0f, vp.bottom - vp.top);
            scrollbar.SetViewport(vp);

            float stride = 58.0f;
            float totalH = (float)orderedIds.size() * stride + 8.0f;
            maxScrollY = std::max(0.0f, totalH - viewportH);
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float y = listTop - scrollY;
            for (const auto& pid : orderedIds) {
                const PromptItem* item = state->FindById(pid);
                if (!item) continue;
                D2D1_RECT_F card = D2D1::RectF(18.0f, y, cw - 20.0f, y + 52.0f);
                if (card.bottom >= listTop && card.top <= listBot) {
                    int slotIdx = state->FindSlotOfIdInScene(sceneIndex, item->id);
                    bool checked = (slotIdx >= 0);

                    D2D1_RECT_F chkZoneR = D2D1::RectF(card.left, card.top, card.right - 68.0f, card.bottom);
                    D2D1_RECT_F editBtnR = D2D1::RectF(card.right - 60.0f, card.top + 13.0f, card.right - 8.0f, card.bottom - 13.0f);
                    int zChk = AddClippedZone(10, chkZoneR, vp, item->id);
                    int zEd  = AddClippedZone(11, editBtnR, vp, item->id);
                    bool rowHov = (zChk == hoverZoneIdx);

                    rt->FillRoundedRectangle(D2D1::RoundedRect(card, 8.0f, 8.0f),
                                             Br(Rgba(rowHov ? 0x222838 : (checked ? 0x1E2434 : Theme::CardBg), 0.96f)));
                    DrawRim(rt.Get(), card, 8.0f,
                            checked ? Rgba(txtAcc, 0.34f) : Rgba(0xFFFFFF, rowHov ? 0.16f : 0.08f),
                            Rgba(0xFFFFFF, 0.03f));

                    D2D1_RECT_F cb = D2D1::RectF(card.left + 12.0f, card.top + 18.0f, card.left + 28.0f, card.top + 34.0f);
                    DrawDarkCheckbox(rt.Get(), brush.Get(), cb, checked, rowHov, txtAcc);

                    float textLeft = cb.right + 10.0f;
                    if (checked) {
                        D2D1_RECT_F sBadge = D2D1::RectF(textLeft, card.top + 8.0f, textLeft + 30.0f, card.top + 25.0f);
                        rt->FillRoundedRectangle(D2D1::RoundedRect(sBadge, 4.0f, 4.0f), Br(Rgba(accTop, 0.16f)));
                        wchar_t sb[8]{};
                        wsprintfW(sb, L"%02d", slotIdx + 1);
                        if (fmtBadge) rt->DrawTextW(sb, 2, fmtBadge.Get(), sBadge, Br(Rgba(txtAcc, 0.96f)));
                        textLeft = sBadge.right + 7.0f;
                    }

                    if (fmtTitle) {
                        float titleA = checked ? 0.98f : (isFull ? 0.56f : 0.88f);
                        rt->DrawTextW(item->title.c_str(), (UINT32)item->title.size(), fmtTitle.Get(),
                                      D2D1::RectF(textLeft, card.top + 6.0f, editBtnR.left - 10.0f, card.top + 27.0f),
                                      Br(Rgba(Theme::TextPrimary, titleA)));
                    }
                    std::wstring prev = CollapseWhitespaceSingleLine(item->content, 78);
                    if (fmtBody) {
                        float bodyA = checked ? 0.78f : (isFull ? 0.48f : 0.78f);
                        rt->DrawTextW(prev.c_str(), (UINT32)prev.size(), fmtBody.Get(),
                                      D2D1::RectF(cb.right + 10.0f, card.top + 29.0f, editBtnR.left - 10.0f, card.bottom - 5.0f),
                                      Br(Rgba(Theme::TextSecondary, bodyA)));
                    }

                    DrawBtn(editBtnR, L"编辑", zEd, 0x222736, 0x2D3448, Theme::TextSecondary, 0.10f);
                }
                y += stride;
            }
            if (orderedIds.empty() && fmtBody) {
                std::wstring emp = UiText(L"词舱暂无条目，点击右上角「+ 新建 Prompt」立即添加");
                rt->DrawTextW(emp.c_str(), (UINT32)emp.size(), fmtBody.Get(),
                              D2D1::RectF(24.0f, listTop + 28.0f, cw - 24.0f, listTop + 58.0f),
                              Br(Rgba(Theme::TextMuted, 0.92f)));
            }
            rt->PopAxisAlignedClip();
            scrollbar.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, viewportH);

            // Bottom footer bar
            rt->DrawLine(D2D1::Point2F(0, listBot), D2D1::Point2F(cw, listBot), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);
            std::wstring footTip = UiText(L"勾选与编辑实时保存，贴边面板同步生效");
            if (fmtBody) {
                rt->DrawTextW(footTip.c_str(), (UINT32)footTip.size(), fmtBody.Get(),
                              D2D1::RectF(18.0f, listBot + 14.0f, cw - 120.0f, ch - 10.0f),
                              Br(Rgba(Theme::TextMuted, 0.88f)));
            }
            D2D1_RECT_F rcDone = D2D1::RectF(cw - 102.0f, listBot + 10.0f, cw - 18.0f, ch - 10.0f);
            int zDone = AddZone(2, rcDone);
            DrawBtn(rcDone, L"完成", zDone, 0x222736, 0x2D3448, Theme::TextPrimary, 0.16f);

            if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
                brush.Reset();
                rt.Reset();
            }
        }
        EndPaint(hwnd, &ps);
    }

    int HitZone(float x, float y) const {
        for (int i = (int)zones.size() - 1; i >= 0; --i) {
            const auto& r = zones[(size_t)i].rect;
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
        }
        return -1;
    }

    [[gnu::noinline]] void HandleTogglePrompt(const std::wstring& pid) {
        if (!state || !storage) return;
        int curSlot = state->FindSlotOfIdInScene(sceneIndex, pid);
        AppState candidate = *state;
        int assignedSlot = -1;
        if (curSlot >= 0) {
            candidate.RemoveFromScene(sceneIndex, pid);
        } else {
            assignedSlot = candidate.AssignToSceneFirstEmpty(sceneIndex, pid);
            if (assignedSlot < 0) {
                statusText = UiText(L"该场景已满 10 格，请先取消勾选 1 条后再添加");
                statusIsErr = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        if (!storage->Save(candidate)) {
            statusText = UiText(storage->readOnlyRecoveryMode
                ? L"只读保护模式：无法保存，请先恢复备份"
                : L"保存失败：无法写入数据文件");
            statusIsErr = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        *state = std::move(candidate);
        wchar_t msg[128]{};
        if (curSlot >= 0) {
            _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已从「%ls」移出（词舱数据已保留）", state->scenes[(size_t)sceneIndex].name.c_str());
        } else {
            _snwprintf_s(msg, _countof(msg), _TRUNCATE, L"已装配到「%ls · 槽位 %02d」", state->scenes[(size_t)sceneIndex].name.c_str(), assignedSlot + 1);
        }
        statusText = UiText(msg);
        statusIsErr = false;
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void HandleEnableSceneNow() {
        if (!state || !storage || state->scenes[(size_t)sceneIndex].enabled) return;
        AppState candidate = *state;
        candidate.SetSceneEnabled(sceneIndex, true);
        if (!storage->Save(candidate)) {
            statusText = UiText(storage->readOnlyRecoveryMode
                ? L"只读保护模式：无法保存，请先恢复备份"
                : L"保存失败：无法写入数据文件");
            statusIsErr = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        *state = std::move(candidate);
        statusText = UiText(L"已启用场景「" + state->scenes[(size_t)sceneIndex].name + L"」，贴边面板已同步显示");
        statusIsErr = false;
        NotifyMutated();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    [[gnu::noinline]] void HandleNewPrompt() {
        if (!state || !storage) return;
        bool wasFull = (state->CountMountedInScene(sceneIndex) >= 10);
        wchar_t hdBuf[96]{};
        if (wasFull) {
            wcscpy_s(hdBuf, L"新建 Prompt 到词舱（当前场景已满）");
        } else {
            _snwprintf_s(hdBuf, _countof(hdBuf), _TRUNCATE, L"新建 Prompt 并加入「%ls」", state->scenes[(size_t)sceneIndex].name.c_str());
        }
        int mountedSlot = -1;
        std::wstring createdId;
        auto res = PromptEditorDialog::Show(hInst, hwnd, hdBuf, L"", L"",
            [this, &mountedSlot, &createdId](const std::wstring& title, const std::wstring& content, std::wstring& outErr) -> bool {
                AppState candidate = *state;
                PromptItem item{};
                item.id = GeneratePromptId(&candidate);
                item.title = title;
                item.content = content;
                item.createdAt = NowUnixSeconds();
                candidate.vault.insert(candidate.vault.begin(), item);
                mountedSlot = candidate.AssignToSceneFirstEmpty(sceneIndex, item.id);
                if (!storage->Save(candidate)) {
                    outErr = storage->readOnlyRecoveryMode
                        ? L"只读保护模式：请先恢复备份或导入 data.json"
                        : L"保存失败：无法写入数据文件";
                    return false;
                }
                createdId = item.id;
                *state = std::move(candidate);
                return true;
            });
        if (!res.accepted) return;
        orderedIds.insert(orderedIds.begin(), createdId);
        scrollY = 0.0f;
        if (mountedSlot >= 0) {
            wchar_t mb[64]{};
            wsprintfW(mb, L"已新建并放入槽位 %02d", mountedSlot + 1);
            statusText = UiText(mb);
            statusIsErr = false;
        } else {
            statusText = UiText(L"已存入词舱，取消勾选任意条目后即可加入此场景");
            statusIsErr = false;
        }
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

    [[gnu::noinline]] void RunModal(HWND parent) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.lpszClassName = L"PromptPod_ScenePromptsDlg";
        RegisterClassExW(&wc);

        int x = 0, y = 0, wPx = 0, hPx = 0;
        dpiScale = ComputeModalPlacement(parent, 560.0f, 492.0f, x, y, wPx, hPx);

        if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
        std::wstring cap = UiText(L"配置场景「" + state->scenes[(size_t)sceneIndex].name + L"」提示词");
        hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            wc.lpszClassName, cap.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            x, y, wPx, hPx, parent, nullptr, hInst, this);
        if (!hwnd) {
            if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
            return;
        }
        ApplyWin11DarkWindowAttributes(hwnd);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        MSG msg{};
        bool gotQuit = false;
        WPARAM quitCode = 0;
        while (!done) {
            BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
            if (gm == 0) { gotQuit = true; quitCode = msg.wParam; break; }
            if (gm < 0) break;
            if ((msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                done = true;
                DestroyWindow(hwnd);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (hwnd && IsWindow(hwnd)) { DestroyWindow(hwnd); hwnd = nullptr; }
        if (parent && IsWindow(parent)) {
            EnableWindow(parent, TRUE);
            if (!gotQuit) SetForegroundWindow(parent);
        }
        if (gotQuit) PostQuitMessage((int)quitCode);
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
            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                float sc = GetDpiScaleForWindow(h);
                mmi->ptMinTrackSize.x = (LONG)(460.0f * sc);
                mmi->ptMinTrackSize.y = (LONG)(360.0f * sc);
                return 0;
            }
            case WM_SIZE:
                if (self->rt) self->rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                self->brush.Reset(); self->rt.Reset();
                auto* sug = reinterpret_cast<RECT*>(lp);
                SetWindowPos(h, nullptr, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_PAINT: self->OnPaint(); return 0;
            case WM_MOUSEWHEEL: {
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                self->scrollY = std::clamp(self->scrollY - (float)delta * 0.45f, 0.0f, self->maxScrollY);
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(LoadCursorW(nullptr, self->hoverZoneIdx >= 0 ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_LBUTTONDOWN: {
                float x = (float)(short)LOWORD(lp) / self->dpiScale;
                float y = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedKind = 0; self->pressedPid.clear();
                if (self->scrollbar.OnLButtonDown(x, y, self->scrollY, self->maxScrollY, self->viewportH)) {
                    SetCapture(h);
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                int hz = self->HitZone(x, y);
                if (hz >= 0) {
                    self->pressedKind = self->zones[(size_t)hz].kind;
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
                bool sbRedraw = false;
                if (self->scrollbar.OnMouseMove(x, y, self->scrollY, self->maxScrollY, self->viewportH, sbRedraw)) {
                    if (self->hoverZoneIdx != -1) { self->hoverZoneIdx = -1; sbRedraw = true; }
                    if (sbRedraw) InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
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
                    else if (downK == 3) self->HandleEnableSceneNow();
                    else if (downK == 10) self->HandleTogglePrompt(downPid);
                    else if (downK == 11) self->HandleEditPrompt(downPid);
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (self->pressedKind != 0) { self->pressedKind = 0; self->pressedPid.clear(); InvalidateRect(h, nullptr, FALSE); }
                self->scrollbar.dragging = false;
                return 0;
            case WM_CLOSE: self->done = true; DestroyWindow(h); return 0;
            case WM_DESTROY: self->done = true; return 0;
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
    bool trackingLeave = false;
    bool dirtyConfirmArmed = false;
    ULONGLONG dirtyConfirmTick = 0;
    bool done = false;
    Result result{};
    std::wstring inlineStatus;
    bool inlineStatusOk = true;

    D2D1_RECT_F rcBugTab{};
    D2D1_RECT_F rcFeatTab{};
    D2D1_RECT_F rcDiagRow{};
    D2D1_RECT_F rcXLink{};
    D2D1_RECT_F rcCopyBtn{};
    D2D1_RECT_F rcSubmitBtn{};

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

            std::wstring hd = UiText(L"提交问题反馈或功能建议");
            if (fmtHeading) {
                rt->DrawTextW(hd.c_str(), (UINT32)hd.size(), fmtHeading.Get(),
                              D2D1::RectF(20.0f, 14.0f, cw - 20.0f, 42.0f), Br(Rgba(Theme::TextPrimary)));
            }

            // Category pills
            auto drawCatPill = [&](const D2D1_RECT_F& r, const std::wstring& txt, bool selected, bool hov) {
                uint32_t bg = selected ? 0x1E3354 : (hov ? 0x222736 : 0x181C26);
                rt->FillRoundedRectangle(D2D1::RoundedRect(r, 7.0f, 7.0f), Br(Rgba(bg, 1.0f)));
                DrawRim(rt.Get(), r, 7.0f,
                                 selected ? Rgba(Theme::AccentCyan, 0.55f) : Rgba(0xFFFFFF, hov ? 0.16f : 0.08f),
                                 Rgba(0xFFFFFF, 0.04f));
                std::wstring t = UiText(txt);
                if (fmtBtn) {
                    rt->DrawTextW(t.c_str(), (UINT32)t.size(), fmtBtn.Get(), r,
                                  Br(Rgba(selected ? Theme::AccentCyan : Theme::TextSecondary, 0.98f)));
                }
            };
            drawCatPill(rcBugTab,  L"Bug 问题反馈", isBug, hoverZone == 1);
            drawCatPill(rcFeatTab, L"功能建议",    !isBug, hoverZone == 2);

            std::wstring lbl1 = UiText(L"简短标题（选填，将自动预填到 GitHub Issue 表单）");
            if (fmtLabel) {
                rt->DrawTextW(lbl1.c_str(), (UINT32)lbl1.size(), fmtLabel.Get(),
                              D2D1::RectF(20.0f, 90.0f, cw - 20.0f, 108.0f), Br(Rgba(Theme::TextSecondary)));
            }
            D2D1_RECT_F titleBox = D2D1::RectF(20.0f, 110.0f, cw - 20.0f, 148.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(titleBox, 7.0f, 7.0f), Br(Rgba(Theme::CardBg)));
            DrawRim(rt.Get(), titleBox, 7.0f,
                             focusedEdit == 1 ? Rgba(Theme::AccentBlue, 0.48f) : Rgba(0xFFFFFF, 0.14f),
                             focusedEdit == 1 ? Rgba(Theme::AccentBlue, 0.14f) : Rgba(0xFFFFFF, 0.06f));

            std::wstring lbl2 = UiText(isBug
                ? L"问题现象与复现步骤（选填，也可直接在浏览器中补充截图）"
                : L"期望功能与使用场景（选填）");
            if (fmtLabel) {
                rt->DrawTextW(lbl2.c_str(), (UINT32)lbl2.size(), fmtLabel.Get(),
                              D2D1::RectF(20.0f, 156.0f, cw - 20.0f, 174.0f), Br(Rgba(Theme::TextSecondary)));
            }
            float bodyBotReserve = isBug ? 110.0f : 80.0f;
            D2D1_RECT_F bodyBox = D2D1::RectF(20.0f, 176.0f, cw - 20.0f, ch - bodyBotReserve);
            rt->FillRoundedRectangle(D2D1::RoundedRect(bodyBox, 7.0f, 7.0f), Br(Rgba(Theme::CardBg)));
            DrawRim(rt.Get(), bodyBox, 7.0f,
                             focusedEdit == 2 ? Rgba(Theme::AccentBlue, 0.48f) : Rgba(0xFFFFFF, 0.14f),
                             focusedEdit == 2 ? Rgba(Theme::AccentBlue, 0.14f) : Rgba(0xFFFFFF, 0.06f));
            bodyScroll.Draw(rt.Get(), brush.Get());

            // Diagnostics checkbox row (ONLY shown in Bug report mode -- not needed for feature requests!)
            if (isBug) {
                D2D1_RECT_F cb = D2D1::RectF(rcDiagRow.left, rcDiagRow.top + 4.0f, rcDiagRow.left + 16.0f, rcDiagRow.top + 20.0f);
                DrawDarkCheckbox(rt.Get(), brush.Get(), cb, includeDiagnostics, hoverZone == 3);
                std::wstring diagTxt = UiText(L"附带脱敏环境与诊断摘要（绝不含任何 Prompt 内容）");
                if (fmtHint) {
                    rt->DrawTextW(diagTxt.c_str(), (UINT32)diagTxt.size(), fmtHint.Get(),
                                  D2D1::RectF(cb.right + 8.0f, rcDiagRow.top + 2.0f, rcDiagRow.right, rcDiagRow.bottom),
                                  Br(Rgba(hoverZone == 3 ? Theme::TextPrimary : Theme::TextSecondary, 0.94f)));
                }
            }

            // Inline status line
            if (!inlineStatus.empty() && fmtHint) {
                rt->DrawTextW(inlineStatus.c_str(), (UINT32)inlineStatus.size(), fmtHint.Get(),
                              D2D1::RectF(20.0f, ch - 72.0f, cw - 20.0f, ch - 52.0f),
                              Br(Rgba(inlineStatusOk ? Theme::CopyGreen : Theme::DangerRed, 0.95f)));
            } else if (fmtHint) {
                std::wstring tip = UiText(L"反馈统一沉淀在 GitHub Issues，提交后可在页面直接粘贴截图或追踪进度");
                rt->DrawTextW(tip.c_str(), (UINT32)tip.size(), fmtHint.Get(),
                              D2D1::RectF(20.0f, ch - 72.0f, cw - 20.0f, ch - 52.0f),
                              Br(Rgba(Theme::TextMuted, 0.92f)));
            }

            // Bottom left X fallback link
            std::wstring xTxt = UiText(L"无 GitHub 账号？前往 X（" APP_X_HANDLE_W L"）");
            if (fmtHint) {
                rt->DrawTextW(xTxt.c_str(), (UINT32)xTxt.size(), fmtHint.Get(),
                              D2D1::RectF(rcXLink.left, rcXLink.top + 8.0f, rcXLink.right, rcXLink.bottom),
                              Br(Rgba(hoverZone == 4 ? Theme::AccentCyan : Theme::AccentBlue, hoverZone == 4 ? 1.0f : 0.88f)));
            }

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
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DlgWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
        wc.lpszClassName = L"PromptPod_FeedbackDlg";
        RegisterClassExW(&wc);

        int x = 0, y = 0, wPx = 0, hPx = 0;
        ComputeModalPlacement(parent, 548.0f, 472.0f, x, y, wPx, hPx);

        if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);

        std::wstring cap = UiText(L"词舱 PromptPod · 意见反馈");
        hwnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            wc.lpszClassName,
            cap.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
            x, y, wPx, hPx,
            parent, nullptr, hInst, this
        );
        if (!hwnd) {
            if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
            return;
        }

        ApplyWin11DarkWindowAttributes(hwnd);
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

        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
        if (parent && IsWindow(parent)) {
            EnableWindow(parent, TRUE);
            if (!gotQuit) SetForegroundWindow(parent);
        }
        if (gotQuit) {
            PostQuitMessage((int)quitCode);
        }
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
                        InvalidateRect(h, nullptr, FALSE);
                    } else if (code == EN_KILLFOCUS) {
                        if (self->focusedEdit == ((id == 3001) ? 1 : 2)) {
                            self->focusedEdit = 0;
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
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hz == 2) {
                    self->isBug = false;
                    self->includeDiagnostics = false;
                    self->LayoutChildControls();
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hz == 3) {
                    self->includeDiagnostics = !self->includeDiagnostics;
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
    float maxScrollY = 0.0f;
    float lastViewportH = 300.0f;
    float dpiScale = 1.0f;
    std::wstring searchQuery;
    WNDPROC origSearchProc = nullptr;

    // Cached filtered/ranked lists (invalidated on state/query mutation, never re-computed per frame!)
    std::vector<size_t> cachedFilteredIndices;
    std::vector<size_t> cachedRankedIndices;
    bool vaultCacheDirty = true;
    bool rankCacheDirty = true;
    int selectedVaultRow = -1; // Index into cachedFilteredIndices

    // Dark capsule viewport scrollbar shared across all 3 tabs
    DarkViewportScrollbar viewportScroll;

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
    int hoverZoneIdx = -1;
    int pressedZoneKind = 0;
    std::wstring pressedZonePromptId;

    // Vault card drag-to-reorder state (Tab 0 when searchQuery is empty)
    int vaultDragIdx = -1;
    bool isVaultDragging = false;
    float vaultDragStartDipY = 0.0f;
    float vaultDragLastDipY = 0.0f;
    float vaultListTopDip = 110.0f;
    float rankListTopDip = 154.0f;
    bool trackingLeave = false;
    bool editorModalOpen = false;
    bool d2dRecreateScheduled = false;

    static constexpr UINT_PTR kTimerFeedback   = 7701;
    static constexpr UINT_PTR kTimerDragScroll = 7702;
    static constexpr const wchar_t* kSearchProp = L"PP_VaultSearchOwner";

    bool IsEditorOpen() const { return editorModalOpen; }

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

    ~VaultWindow() {
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
        if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd)) {
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
        ScenePromptsDialog::Show(hInst, parent, state, storage, sceneIdx, [](void* ctx) {
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

    void ShowToast(const std::wstring& msg, bool isErr, ULONGLONG durationMs = 2600, bool keepUndo = false) {
        toastShowsUndo = (keepUndo && undoDelete.active);
        toastMessage = UiText(msg);
        toastIsError = isErr;
        toastTick = GetTickCount64();
        toastDurationMs = durationMs;
        if (hwnd && IsWindow(hwnd)) {
            SetTimer(hwnd, kTimerFeedback, 120, nullptr);
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
        if (rowTop < scrollY) {
            scrollY = rowTop;
        } else if (rowBot > scrollY + lastViewportH) {
            scrollY = std::max(0.0f, rowBot - lastViewportH + 8.0f);
        }
        scrollY = std::clamp(scrollY, 0.0f, maxScrollY);
    }

    void ClearSearchQueryAndFocus() {
        searchQuery.clear();
        selectedVaultRow = -1;
        scrollY = 0.0f;
        InvalidateCaches();
        if (hSearchEdit && IsWindow(hSearchEdit)) {
            SetWindowTextW(hSearchEdit, L"");
            SetFocus(hSearchEdit);
        }
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    void CopyPromptById(const std::wstring& pid) {
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
            if (hwnd) SetTimer(hwnd, kTimerFeedback, 120, nullptr);
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
                activeTab = 0;
                scrollY = 0.0f;
                UpdateSearchVisibility();
                InvalidateRect(hwnd, nullptr, FALSE);
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
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 5.0f, 5.0f), Br(Rgba(bgHex, bgAlpha)));
        if (fmtBadge && !txt.empty()) {
            std::wstring s = UiText(txt);
            rt->DrawTextW(s.c_str(), (UINT32)s.size(), fmtBadge.Get(), r, Br(Rgba(fgHex, fgAlpha)));
        }
    }

    [[gnu::noinline]] void DrawPillButton(const D2D1_RECT_F& r, const wchar_t* text, int zoneIdx,
                                          uint32_t baseBg, uint32_t hoverBg, uint32_t textHex, float borderAlpha = 0.12f) {
        bool isPressed = (zoneIdx >= 0 && zoneIdx < (int)hitZones.size() &&
                          hitZones[(size_t)zoneIdx].kind == pressedZoneKind &&
                          hitZones[(size_t)zoneIdx].promptId == pressedZonePromptId);
        DrawDarkPillButton(rt.Get(), brush.Get(), fmtBadge.Get(), r, text, zoneIdx == hoverZoneIdx, baseBg, hoverBg, textHex, borderAlpha, isPressed);
    }

    void DrawPillButton(const D2D1_RECT_F& r, const std::wstring& text, int zoneIdx,
                        uint32_t baseBg, uint32_t hoverBg, uint32_t textHex, float borderAlpha = 0.12f) {
        DrawPillButton(r, text.c_str(), zoneIdx, baseBg, hoverBg, textHex, borderAlpha);
    }

    [[gnu::noinline]] void DrawSegPill(const D2D1_RECT_F& r, const wchar_t* txt, bool selected, int zoneIdx) {
        if (selected) {
            DrawPillButton(r, txt, zoneIdx, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.34f);
        } else {
            DrawPillButton(r, txt, zoneIdx, 0x151923, 0x222838, Theme::TextSecondary, 0.08f);
        }
    }

    [[gnu::noinline]] float DrawSectionHeader(float yPos, float cw, const D2D1_RECT_F& vp, const wchar_t* title) {
        if (yPos + 24.0f >= vp.top && yPos <= vp.bottom) {
            DrawUiText(fmtSection.Get(), D2D1::RectF(22.0f, yPos + 2.0f, cw - 24.0f, yPos + 22.0f), title, Rgba(Theme::AccentCyan, 0.88f));
        }
        return yPos + 24.0f;
    }

    [[gnu::noinline]] D2D1_RECT_F DrawGroupContainer(float yPos, float h, float cw, const D2D1_RECT_F& vp, bool accent = false) {
        D2D1_RECT_F g = D2D1::RectF(18.0f, yPos, cw - 20.0f, yPos + h);
        if (g.bottom >= vp.top && g.top <= vp.bottom) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(g, 10.0f, 10.0f), Br(Rgba(Theme::CardBg, 0.95f)));
            DrawRim(rt.Get(), g, 10.0f,
                             accent ? Rgba(Theme::AccentCyan, 0.38f) : Rgba(0xFFFFFF, 0.11f),
                             Rgba(0xFFFFFF, 0.04f));
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
        DrawUiText(fmtCardTitle.Get(), D2D1::RectF(grp.left + 16.0f, rowTop + 10.0f, grp.right - rightReserve, rowTop + 31.0f),
                   title, Rgba(Theme::TextPrimary, 0.98f));
        DrawUiText(wrapDesc ? fmtDescWrap.Get() : fmtCardBody.Get(),
                   D2D1::RectF(grp.left + 16.0f, rowTop + 32.0f, grp.right - rightReserve, rowTop + rowH - 6.0f),
                   desc, Rgba(descHex, 0.88f));
    }

    [[gnu::noinline]] void DrawKpiCard(int idx, float cardW, float contentTop, const wchar_t* label,
                                       const wchar_t* val, const wchar_t* sub, bool isTextVal) {
        float lx = 18.0f + (float)idx * (cardW + 10.0f);
        D2D1_RECT_F r = D2D1::RectF(lx, contentTop, lx + cardW, contentTop + 76.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 9.0f, 9.0f), Br(Rgba(Theme::CardBg, 0.95f)));
        DrawRim(rt.Get(), r, 9.0f, Rgba(0xFFFFFF, 0.12f), Rgba(0xFFFFFF, 0.04f));
        DrawUiText(fmtCardBody.Get(), D2D1::RectF(r.left + 14.0f, r.top + 8.0f, r.right - 12.0f, r.top + 28.0f),
                   label, Rgba(Theme::TextSecondary, 0.88f));
        IDWriteTextFormat* vFmt = isTextVal ? fmtKpiTitle.Get() : fmtBigNum.Get();
        if (vFmt && val && *val) {
            rt->DrawTextW(val, (UINT32)wcslen(val), vFmt,
                          D2D1::RectF(r.left + 14.0f, isTextVal ? (r.top + 30.0f) : (r.top + 27.0f), r.right - 12.0f, r.top + 56.0f),
                          Br(Rgba(Theme::TextPrimary, 1.0f)));
        }
        if (sub && *sub) {
            DrawUiText(fmtBadge.Get(), D2D1::RectF(r.left + 14.0f, r.top + 54.0f, r.right - 12.0f, r.bottom - 4.0f),
                       sub, Rgba(Theme::AccentCyan, 0.85f));
        }
    }

    [[gnu::noinline]] void DrawToggleSwitch(const D2D1_RECT_F& r, bool isOn, int zoneIdx) {
        bool hov = (zoneIdx == hoverZoneIdx);
        float radius = (r.bottom - r.top) * 0.5f;
        if (isOn) {
            D2D1_GRADIENT_STOP stops[2] = {
                { 0.0f, Rgba(hov ? 0x4F8EF7 : 0x3B7BF6, 1.0f) },
                { 1.0f, Rgba(Theme::AccentCyan, hov ? 0.98f : 0.90f) }
            };
            ComPtr<ID2D1GradientStopCollection> coll;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) {
                ComPtr<ID2D1LinearGradientBrush> grad;
                rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, 0), D2D1::Point2F(r.right, 0)),
                    coll.Get(), grad.GetAddressOf());
                if (grad) rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), grad.Get());
            }
            DrawRim(rt.Get(), r, radius, Rgba(0xFFFFFF, 0.28f), Rgba(0xFFFFFF, 0.08f));
        } else {
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius),
                                     Br(Rgba(hov ? 0x222838 : 0x131722, 1.0f)));
            DrawRim(rt.Get(), r, radius, Rgba(0xFFFFFF, 0.14f), Rgba(0xFFFFFF, 0.05f));
        }
        float knobR = radius - 3.0f;
        float knobX = isOn ? (r.right - radius) : (r.left + radius);
        float knobY = (r.top + r.bottom) * 0.5f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, knobY), knobR, knobR),
                        Br(Rgba(isOn ? 0xFFFFFF : 0x94A3B8, 1.0f)));
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
        ULONGLONG nowTick = GetTickCount64();

        rt->BeginDraw();
        rt->Clear(Rgba(0x0F1219, 1.0f));

        // --- Top Bar ---
        rt->FillRectangle(D2D1::RectF(0, 0, cw, 56.0f), Br(Rgba(0x141822, 1.0f)));
        rt->DrawLine(D2D1::Point2F(0, 56.0f), D2D1::Point2F(cw, 56.0f), Br(Rgba(0xFFFFFF, 0.07f)), 1.0f);

        // Segmented Tabs (0: Vault, 1: Leaderboard, 2: Settings)
        D2D1_RECT_F segBg = D2D1::RectF(18.0f, 12.0f, 312.0f, 44.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(segBg, 8.0f, 8.0f), Br(Rgba(0x0D0F15, 1.0f)));
        DrawRim(rt.Get(), segBg, 8.0f, Rgba(0xFFFFFF, 0.08f), Rgba(0xFFFFFF, 0.04f));

        D2D1_RECT_F tab0R = D2D1::RectF(21.0f, 15.0f, 138.0f, 41.0f);
        D2D1_RECT_F tab1R = D2D1::RectF(140.0f, 15.0f, 232.0f, 41.0f);
        D2D1_RECT_F tab2R = D2D1::RectF(234.0f, 15.0f, 309.0f, 41.0f);
        int zTab0 = AddZone(1, tab0R);
        int zTab1 = AddZone(2, tab1R);
        int zTab2 = AddZone(7, tab2R);

        if (activeTab == 0) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(tab0R, 6.0f, 6.0f), Br(Rgba(0x23293A, 1.0f)));
            DrawRim(rt.Get(), tab0R, 6.0f, Rgba(Theme::AccentBlue, 0.35f), Rgba(0xFFFFFF, 0.06f));
        } else if (activeTab == 1) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(tab1R, 6.0f, 6.0f), Br(Rgba(0x23293A, 1.0f)));
            DrawRim(rt.Get(), tab1R, 6.0f, Rgba(Theme::AccentCyan, 0.35f), Rgba(0xFFFFFF, 0.06f));
        } else {
            rt->FillRoundedRectangle(D2D1::RoundedRect(tab2R, 6.0f, 6.0f), Br(Rgba(0x23293A, 1.0f)));
            DrawRim(rt.Get(), tab2R, 6.0f, Rgba(Theme::AccentIndigo, 0.40f), Rgba(0xFFFFFF, 0.06f));
        }

        wchar_t t0Buf[64]{};
        wsprintfW(t0Buf, L"\x8BCD\x8231\x6536\x85CF %u", (unsigned)state->vault.size());
        std::wstring sTab0 = UiText(t0Buf);
        std::wstring sTab1 = UiText(L"\x4F7F\x7528\x6392\x884C");
        std::wstring sTab2 = UiText(L"\x8BBE\x7F6E");
        if (fmtTab) {
            rt->DrawTextW(sTab0.c_str(), (UINT32)sTab0.size(), fmtTab.Get(), tab0R,
                          Br(Rgba(activeTab == 0 ? Theme::TextPrimary : Theme::TextSecondary, zTab0 == hoverZoneIdx ? 1.0f : 0.9f)));
            rt->DrawTextW(sTab1.c_str(), (UINT32)sTab1.size(), fmtTab.Get(), tab1R,
                          Br(Rgba(activeTab == 1 ? Theme::TextPrimary : Theme::TextSecondary, zTab1 == hoverZoneIdx ? 1.0f : 0.9f)));
            rt->DrawTextW(sTab2.c_str(), (UINT32)sTab2.size(), fmtTab.Get(), tab2R,
                          Br(Rgba(activeTab == 2 ? Theme::TextPrimary : Theme::TextSecondary, zTab2 == hoverZoneIdx ? 1.0f : 0.9f)));
        }
        if (updater && updater->HasAvailableUpdate()) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tab2R.right - 10.0f, tab2R.top + 8.0f), 3.4f, 3.4f),
                            Br(Rgba(Theme::AccentCyan, 0.98f)));
        }

        // Top-right action bar scoped strictly by Tab: only Tab 0 shows '+ New Prompt'; Tab 1 & 2 stay clean
        if (activeTab == 0) {
            D2D1_RECT_F rcNew = D2D1::RectF(cw - 126.0f, 13.0f, cw - 18.0f, 43.0f);
            int zNew = AddZone(3, rcNew);
            DrawPillButton(rcNew, L"+ \x65B0\x5EFA Prompt", zNew, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.25f);
        }

        float contentTop = 66.0f;

        // Persistent Read-Only Recovery Banner (visible across all tabs until user restores/imports)
        if (storage && storage->readOnlyRecoveryMode) {
            D2D1_RECT_F rban = D2D1::RectF(18.0f, contentTop - 2.0f, cw - 18.0f, contentTop + 38.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rban, 7.0f, 7.0f), Br(Rgba(0x2D1B18, 0.96f)));
            DrawRim(rt.Get(), rban, 7.0f, Rgba(0xF59E0B, 0.55f), Rgba(0xF59E0B, 0.16f));

            bool canRestore = storage->recoveredFromBackup;
            float rightReserve = canRestore ? 220.0f : 196.0f;
            std::wstring rtxt = UiText(canRestore
                ? L"数据文件异常，已进入只读保护（原文件已保留，当前展示上次备份）"
                : (!storage->loadFailureHint.empty()
                    ? storage->loadFailureHint
                    : L"数据文件异常，已进入只读保护（原文件已保留，可导入备份恢复）"));
            if (fmtCardBody) {
                rt->DrawTextW(rtxt.c_str(), (UINT32)rtxt.size(), fmtCardBody.Get(),
                              D2D1::RectF(rban.left + 12.0f, rban.top, rban.right - rightReserve, rban.bottom),
                              Br(Rgba(0xFCD34D, 0.98f)));
            }
            if (canRestore) {
                D2D1_RECT_F rcRestore = D2D1::RectF(rban.right - 210.0f, rban.top + 6.0f, rban.right - 106.0f, rban.bottom - 6.0f);
                int zRest = AddZone(32, rcRestore);
                DrawPillButton(rcRestore, L"一键恢复备份", zRest, 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
            } else {
                D2D1_RECT_F rcImp = D2D1::RectF(rban.right - 186.0f, rban.top + 6.0f, rban.right - 106.0f, rban.bottom - 6.0f);
                int zImp = AddZone(4, rcImp);
                DrawPillButton(rcImp, L"导入备份", zImp, 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
            }
            D2D1_RECT_F rcOpenDir = D2D1::RectF(rban.right - 98.0f, rban.top + 6.0f, rban.right - 8.0f, rban.bottom - 6.0f);
            int zDir = AddZone(23, rcOpenDir);
            DrawPillButton(rcOpenDir, L"打开数据目录", zDir, 0x2A2024, 0x3A2C31, 0xFDE68A, 0.18f);
            contentTop += 48.0f;
        }

        // Optional slot assignment mode banner (ONLY shown on Vault / Leaderboard, hidden on Settings!)
        if ((activeTab == 0 || activeTab == 1) && assignTargetSlot >= 0 && assignTargetSlot < 10) {
            D2D1_RECT_F ban = D2D1::RectF(18.0f, contentTop - 2.0f, cw - 18.0f, contentTop + 30.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(ban, 7.0f, 7.0f), Br(Rgba(Theme::AccentBlue, 0.14f)));
            DrawRim(rt.Get(), ban, 7.0f, Rgba(Theme::AccentCyan, 0.40f), Rgba(Theme::AccentBlue, 0.18f));

            std::wstring targetLabel = state->IsMultiSceneActive()
                ? (state->ActiveScene().name + L" \x00B7 \x69FD\x4F4D " + FormatSlotNum(assignTargetSlot))
                : (L"\x69FD\x4F4D " + FormatSlotNum(assignTargetSlot));
            std::wstring btxt = UiText(L"正在为「" + targetLabel +
                                       (activeTab == 0
                                           ? (L"」挑选 Prompt -- 点「放入 " + FormatSlotNum(assignTargetSlot) + L"」、双击条目或按 Enter 即可完成")
                                           : (L"」挑选 Prompt -- 点「放入 " + FormatSlotNum(assignTargetSlot) + L"」或双击条目即可完成（Esc 取消）")));
            if (fmtCardBody) {
                rt->DrawTextW(btxt.c_str(), (UINT32)btxt.size(), fmtCardBody.Get(),
                              D2D1::RectF(ban.left + 12.0f, ban.top, ban.right - 88.0f, ban.bottom),
                              Br(Rgba(Theme::AccentCyan, 0.96f)));
            }
            D2D1_RECT_F rcCancelAssign = D2D1::RectF(ban.right - 80.0f, ban.top + 5.0f, ban.right - 8.0f, ban.bottom - 5.0f);
            int zCA = AddZone(6, rcCancelAssign);
            DrawPillButton(rcCancelAssign, L"\x53D6\x6D88\x6307\x5B9A", zCA, 0x1E2536, 0x2B354D, Theme::TextSecondary);
            contentTop += 40.0f;
        }

        if (activeTab == 0) {
            // Search box surround + right-side clear '×' button when query is non-empty
            D2D1_RECT_F sBox = D2D1::RectF(18.0f, contentTop, cw - 20.0f, contentTop + 34.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(sBox, 7.0f, 7.0f), Br(Rgba(Theme::CardBg, 1.0f)));
            DrawRim(rt.Get(), sBox, 7.0f, Rgba(0xFFFFFF, 0.12f), Rgba(0xFFFFFF, 0.05f));
            if (!searchQuery.empty()) {
                D2D1_RECT_F rcClear = D2D1::RectF(sBox.right - 28.0f, sBox.top + 5.0f, sBox.right - 4.0f, sBox.bottom - 5.0f);
                int zClr = AddZone(8, rcClear);
                bool clrHov = (zClr == hoverZoneIdx);
                if (clrHov) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(rcClear, 5.0f, 5.0f), Br(Rgba(0xFFFFFF, 0.09f)));
                }
                std::wstring xStr = L"\u00D7";
                if (fmtTab) {
                    rt->DrawTextW(xStr.c_str(), 1, fmtTab.Get(), rcClear,
                                  Br(Rgba(clrHov ? Theme::TextPrimary : Theme::TextMuted, 0.95f)));
                }
            }
            contentTop += 42.0f;

            // Vault scene/status filter bar (hidden during assignTargetSlot pick mode so all prompts stay direct)
            int effFilter = EffectiveVaultFilter();
            if (assignTargetSlot < 0) {
                struct FilterPill {
                    int kind;
                    bool selected;
                    uint32_t accent;
                    std::wstring label;
                    float width;
                };
                FilterPill pills[kMaxScenes + 2]{};
                int pillCnt = 0;
                auto pushPill = [&](int kind, bool sel, uint32_t acc, const std::wstring& txt) {
                    std::wstring u = UiText(txt);
                    float w = std::clamp((float)VisualWidthUnits(u) * 6.2f + 18.0f, 50.0f, 114.0f);
                    pills[pillCnt++] = FilterPill{ kind, sel, acc, std::move(u), w };
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
                for (int i = 0; i < pillCnt; ++i) {
                    D2D1_RECT_F pr = D2D1::RectF(px, contentTop, px + pills[i].width, contentTop + 26.0f);
                    int zF = AddZone(pills[i].kind, pr);
                    DrawPillButton(pr, pills[i].label, zF,
                                   pills[i].selected ? 0x202A40 : 0x151923,
                                   pills[i].selected ? 0x283552 : 0x222838,
                                   pills[i].selected ? pills[i].accent : Theme::TextSecondary,
                                   pills[i].selected ? 0.34f : 0.08f);
                    px += pills[i].width + gap;
                }

                if (effFilter >= 0) {
                    D2D1_RECT_F rcCfg = D2D1::RectF(cw - 104.0f, contentTop, cw - 20.0f, contentTop + 26.0f);
                    int zCfg = AddZone(54 + effFilter, rcCfg);
                    DrawPillButton(rcCfg, L"\x914D\x7F6E\x63D0\x793A\x8BCD", zCfg, 0x1B263B, 0x253554, Theme::AccentCyan, 0.22f);
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
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(listViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            int badgeScene = (effFilter >= 0) ? effFilter : state->EffectiveActiveSceneIndex();
            bool canDragVaultOrder = (searchQuery.empty() && effFilter == -1);
            float y = contentTop - scrollY;
            int visibleCount = (int)cachedFilteredIndices.size();
            for (int rowPos = 0; rowPos < visibleCount; ++rowPos) {
                size_t vi = cachedFilteredIndices[(size_t)rowPos];
                const auto& item = state->vault[vi];
                bool isBeingDragged = (isVaultDragging && (int)vi == vaultDragIdx);
                bool isSelectedRow = (rowPos == selectedVaultRow);
                D2D1_RECT_F card = D2D1::RectF(18.0f, y, cw - 20.0f, y + 68.0f);
                if (card.bottom >= contentTop && card.top <= ch) {
                    int slotIdx = state->FindSlotOfIdInScene(badgeScene, item.id);
                    uint32_t cardBgHex = isBeingDragged ? 0x263046 : (isSelectedRow ? 0x21283A : Theme::CardBg);
                    rt->FillRoundedRectangle(D2D1::RoundedRect(card, 9.0f, 9.0f),
                                             Br(Rgba(cardBgHex, isBeingDragged ? 0.99f : 0.96f)));
                    D2D1_COLOR_F cardRim = isBeingDragged ? Rgba(Theme::AccentCyan, 0.78f)
                                         : (isSelectedRow ? Rgba(Theme::AccentCyan, 0.56f)
                                                          : (slotIdx >= 0 ? Rgba(Theme::AccentBlue, 0.26f) : Rgba(0xFFFFFF, 0.10f)));
                    DrawRim(rt.Get(), card, 9.0f, cardRim, Rgba(0xFFFFFF, 0.04f),
                                     (isBeingDragged || isSelectedRow) ? 1.25f : 1.0f);

                    // Subtle 6-dot drag grip on far left ONLY in unfiltered All-vault view
                    float leftContentOffset = 12.0f;
                    if (canDragVaultOrder) {
                        leftContentOffset = 24.0f;
                        auto* gripBr = Br(Rgba(isBeingDragged ? Theme::AccentCyan : Theme::TextMuted, isBeingDragged ? 0.9f : 0.45f));
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
                    if (fmtCardTitle) {
                        rt->DrawTextW(item.title.c_str(), (UINT32)item.title.size(), fmtCardTitle.Get(),
                                      D2D1::RectF(pillRight + 8.0f, card.top + 9.0f, actionLeft - countReserve, card.top + 33.0f),
                                      Br(Rgba(Theme::TextPrimary, 0.98f)));
                    }

                    if (item.useCount > 0 && fmtBadge) {
                        wchar_t cntBuf[48]{};
                        wsprintfW(cntBuf, L"%I64u \x6B21", (unsigned long long)item.useCount);
                        std::wstring cntStr = UiText(cntBuf);
                        rt->DrawTextW(cntStr.c_str(), (UINT32)cntStr.size(), fmtBadge.Get(),
                                      D2D1::RectF(actionLeft - 60.0f, card.top + 11.0f, actionLeft - 4.0f, card.top + 31.0f),
                                      Br(Rgba(Theme::TextSecondary, 0.85f)));
                    }

                    // Body preview (context snippet centered on match when searching!)
                    std::wstring preview = BuildSearchPreviewSnippet(item.content, searchQuery, 108);
                    if (fmtCardBody) {
                        rt->DrawTextW(preview.c_str(), (UINT32)preview.size(), fmtCardBody.Get(),
                                      D2D1::RectF(card.left + leftContentOffset, card.top + 37.0f, actionLeft - 8.0f, card.bottom - 8.0f),
                                      Br(Rgba(Theme::TextSecondary, 0.80f)));
                    }

                    // 4 clean actions: Copy (48) | Slot (60) | Edit (44) | More '···' (32) -> destructive Delete tucked safely inside More/Right-Click!
                    float btnTop = card.top + 19.0f;
                    float btnBot = card.top + 49.0f;
                    D2D1_RECT_F rCopy   = D2D1::RectF(card.right - 202.0f, btnTop, card.right - 154.0f, btnBot);
                    D2D1_RECT_F rAssign = D2D1::RectF(card.right - 149.0f, btnTop, card.right - 89.0f,  btnBot);
                    D2D1_RECT_F rEdit   = D2D1::RectF(card.right - 84.0f,  btnTop, card.right - 40.0f,  btnBot);
                    D2D1_RECT_F rMore   = D2D1::RectF(card.right - 36.0f,  btnTop, card.right - 8.0f,   btnBot);

                    int zCopy   = AddClippedZone(10, rCopy, listViewport, item.id);
                    int zAssign = AddClippedZone(11, rAssign, listViewport, item.id);
                    int zEdit   = AddClippedZone(12, rEdit, listViewport, item.id);
                    int zMore   = AddClippedZone(15, rMore, listViewport, item.id);

                    bool isCopiedFlash = (copiedFlashId == item.id && nowTick - copiedFlashTick < 1100);
                    if (isCopiedFlash) {
                        DrawPillButton(rCopy, L"\x5DF2\x590D\x5236", zCopy, 0x14533D, 0x18664B, Theme::CopyGreen, 0.30f);
                    } else {
                        DrawPillButton(rCopy, L"\x590D\x5236", zCopy, 0x222736, 0x2D3448, Theme::TextPrimary);
                    }

                    std::wstring assignLabel = (assignTargetSlot >= 0)
                        ? (L"\x653E\x5165 " + FormatSlotNum(assignTargetSlot))
                        : L"\x69FD\x4F4D \x25BE";
                    DrawPillButton(rAssign, assignLabel, zAssign,
                                   assignTargetSlot >= 0 ? 0x1E3A5F : 0x222736,
                                   assignTargetSlot >= 0 ? 0x284D7E : 0x2D3448,
                                   assignTargetSlot >= 0 ? Theme::AccentCyan : Theme::AccentBlue);

                    DrawPillButton(rEdit, L"\x7F16\x8F91", zEdit, 0x222736, 0x2D3448, Theme::TextSecondary);
                    DrawPillButton(rMore, L"\x00B7\x00B7\x00B7", zMore, 0x1D2230, 0x2B3348, Theme::TextSecondary, 0.08f);
                }
                y += 76.0f;
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
                rt->DrawTextW(emptyMsg.c_str(), (UINT32)emptyMsg.size(), fmtCardBody.Get(),
                              D2D1::RectF(24.0f, contentTop + 26.0f, cw - 24.0f, contentTop + 54.0f),
                              Br(Rgba(Theme::TextMuted, 0.95f)));
                float bx = 24.0f;
                if (!searchQuery.empty()) {
                    D2D1_RECT_F rcClrEmpty = D2D1::RectF(bx, contentTop + 60.0f, bx + 84.0f, contentTop + 88.0f);
                    int zCE = AddClippedZone(8, rcClrEmpty, listViewport);
                    DrawPillButton(rcClrEmpty, L"清空搜索", zCE, 0x222736, 0x2D3448, Theme::TextSecondary);
                    bx += 94.0f;
                    if (effFilter != -1) {
                        D2D1_RECT_F rcAll = D2D1::RectF(bx, contentTop + 60.0f, bx + 98.0f, contentTop + 88.0f);
                        int zAll = AddClippedZone(70, rcAll, listViewport);
                        DrawPillButton(rcAll, L"搜全部词舱", zAll, 0x222736, 0x2D3448, Theme::TextPrimary);
                        bx += 108.0f;
                    }
                    D2D1_RECT_F rcNewEmpty = D2D1::RectF(bx, contentTop + 60.0f, bx + 128.0f, contentTop + 88.0f);
                    int zNE = AddClippedZone(17, rcNewEmpty, listViewport);
                    DrawPillButton(rcNewEmpty, L"+ 新建此 Prompt", zNE, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.25f);
                } else if (effFilter >= 0) {
                    D2D1_RECT_F rcCfgEmpty = D2D1::RectF(bx, contentTop + 60.0f, bx + 132.0f, contentTop + 88.0f);
                    int zCfgE = AddClippedZone(54 + effFilter, rcCfgEmpty, listViewport);
                    DrawPillButton(rcCfgEmpty, L"配置此场景提示词", zCfgE, 0x1E3A5F, 0x284D7E, Theme::AccentCyan, 0.25f);
                    bx += 142.0f;
                    D2D1_RECT_F rcNewSc = D2D1::RectF(bx, contentTop + 60.0f, bx + 114.0f, contentTop + 88.0f);
                    int zNewSc = AddClippedZone(3, rcNewSc, listViewport);
                    DrawPillButton(rcNewSc, L"+ 新建并加入", zNewSc, 0x222736, 0x2D3448, Theme::TextPrimary);
                } else if (effFilter == -2) {
                    D2D1_RECT_F rcAll = D2D1::RectF(bx, contentTop + 60.0f, bx + 108.0f, contentTop + 88.0f);
                    int zAll = AddClippedZone(70, rcAll, listViewport);
                    DrawPillButton(rcAll, L"查看全部词舱", zAll, 0x222736, 0x2D3448, Theme::AccentCyan);
                }
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        } else if (activeTab == 1) {
            // --- Tab 1: Usage Analytics & Leaderboard ---
            float cardW = (cw - 38.0f - 20.0f) / 3.0f;
            int mountedCount = state->CountMountedInScene(state->EffectiveActiveSceneIndex());

            EnsureRankCache();
            uint64_t totalUses = state->TotalCopies();
            uint64_t topCount = (!cachedRankedIndices.empty() && state->vault[cachedRankedIndices[0]].useCount > 0)
                ? state->vault[cachedRankedIndices[0]].useCount : 1;

            // 3 Summary KPI cards (KPI 3 uses crisp fmtKpiTitle so titles never truncate awkwardly)
            wchar_t bTotal[32]{}, bVault[32]{}, bMounted[64]{};
            wsprintfW(bTotal, L"%I64u", (unsigned long long)totalUses);
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
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);
            rt->PushAxisAlignedClip(listViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            // Gradient brush for leaderboard bars
            D2D1_GRADIENT_STOP barStops[2] = {
                { 0.0f, Rgba(Theme::AccentCyan, 0.90f) },
                { 1.0f, Rgba(Theme::AccentIndigo, 0.90f) }
            };
            ComPtr<ID2D1GradientStopCollection> barColl;
            rt->CreateGradientStopCollection(barStops, 2, barColl.GetAddressOf());

            float y = listTop - scrollY;
            for (size_t i = 0; i < cachedRankedIndices.size(); ++i) {
                const PromptItem* item = &state->vault[cachedRankedIndices[i]];
                D2D1_RECT_F row = D2D1::RectF(18.0f, y, cw - 20.0f, y + 58.0f);
                if (row.bottom >= listTop && row.top <= ch) {
                    rt->FillRoundedRectangle(D2D1::RoundedRect(row, 8.0f, 8.0f), Br(Rgba(Theme::CardBg, 0.92f)));
                    DrawRim(rt.Get(), row, 8.0f, Rgba(0xFFFFFF, 0.09f), Rgba(0xFFFFFF, 0.03f));

                    // Rank badge
                    D2D1_RECT_F rkBox = D2D1::RectF(row.left + 10.0f, row.top + 13.0f, row.left + 44.0f, row.bottom - 13.0f);
                    uint32_t rkHex = (i == 0) ? Theme::AccentCyan : (i == 1 ? Theme::AccentBlue : (i == 2 ? Theme::AccentIndigo : Theme::TextMuted));
                    rt->FillRoundedRectangle(D2D1::RoundedRect(rkBox, 6.0f, 6.0f), Br(Rgba(rkHex, i < 3 ? 0.16f : 0.07f)));
                    wchar_t rkStr[16]{};
                    wsprintfW(rkStr, L"#%u", (unsigned)(i + 1));
                    if (fmtRank) rt->DrawTextW(rkStr, (UINT32)wcslen(rkStr), fmtRank.Get(), rkBox, Br(Rgba(rkHex, 0.98f)));

                    int slotIdx = state->FindSlotOfId(item->id);
                    float rightZoneLeft = row.right - 220.0f;
                    float tagW = state->IsMultiSceneActive() ? 96.0f : 70.0f;

                    // Title
                    if (fmtCardTitle) {
                        rt->DrawTextW(item->title.c_str(), (UINT32)item->title.size(), fmtCardTitle.Get(),
                                      D2D1::RectF(rkBox.right + 10.0f, row.top + 7.0f, rightZoneLeft - tagW - 6.0f, row.top + 29.0f),
                                      Br(Rgba(Theme::TextPrimary, 0.96f)));
                    }

                    // Slot tag
                    std::wstring sTag;
                    uint32_t sTagCol = Theme::TextMuted;
                    if (!state->IsMultiSceneActive()) {
                        sTag = (slotIdx >= 0) ? UiText(L"\x69FD\x4F4D " + FormatSlotNum(slotIdx)) : UiText(L"\x672A\x88C5\x914D");
                        sTagCol = (slotIdx >= 0) ? Theme::AccentCyan : Theme::TextMuted;
                    } else if (slotIdx >= 0) {
                        sTag = UiText(state->ActiveScene().name + L" \x00B7 " + FormatSlotNum(slotIdx));
                        sTagCol = Theme::SceneTextAccent(state->EffectiveActiveSceneIndex());
                    } else {
                        int os = -1, osl = -1;
                        if (state->CountOtherEnabledScenesWithId(item->id, &os, &osl) > 0 && os >= 0) {
                            sTag = UiText(state->scenes[(size_t)os].name + L" \x00B7 " + FormatSlotNum(osl));
                            sTagCol = Theme::AccentBlue;
                        } else {
                            sTag = UiText(L"\x672A\x88C5\x914D");
                        }
                    }
                    if (fmtBadge) {
                        rt->DrawTextW(sTag.c_str(), (UINT32)sTag.size(), fmtBadge.Get(),
                                      D2D1::RectF(rightZoneLeft - tagW, row.top + 8.0f, rightZoneLeft - 6.0f, row.top + 28.0f),
                                      Br(Rgba(sTagCol, 0.92f)));
                    }

                    // Proportional progress bar
                    D2D1_RECT_F track = D2D1::RectF(rkBox.right + 10.0f, row.top + 35.0f, rightZoneLeft - 10.0f, row.top + 43.0f);
                    rt->FillRoundedRectangle(D2D1::RoundedRect(track, 4.0f, 4.0f), Br(Rgba(0x0D0F15, 1.0f)));
                    if (item->useCount > 0 && barColl) {
                        float ratio = std::clamp((float)item->useCount / (float)topCount, 0.03f, 1.0f);
                        float fillW = (track.right - track.left) * ratio;
                        D2D1_RECT_F fillR = D2D1::RectF(track.left, track.top, track.left + fillW, track.bottom);
                        ComPtr<ID2D1LinearGradientBrush> barGrad;
                        rt->CreateLinearGradientBrush(
                            D2D1::LinearGradientBrushProperties(D2D1::Point2F(track.left, 0), D2D1::Point2F(track.right, 0)),
                            barColl.Get(), barGrad.GetAddressOf());
                        if (barGrad) rt->FillRoundedRectangle(D2D1::RoundedRect(fillR, 4.0f, 4.0f), barGrad.Get());
                    }

                    // Count + Copy button + Slot Assign button (honest '放入 XX' in assignTargetSlot mode!)
                    wchar_t cText[48]{};
                    int pct = totalUses > 0 ? (int)((item->useCount * 100ULL + totalUses / 2) / totalUses) : 0;
                    wsprintfW(cText, L"%I64u \x6B21\xFF08%d%%\xFF09", (unsigned long long)item->useCount, pct);
                    std::wstring cs = UiText(cText);
                    if (fmtBadge) {
                        rt->DrawTextW(cs.c_str(), (UINT32)cs.size(), fmtBadge.Get(),
                                      D2D1::RectF(rightZoneLeft, row.top + 14.0f, row.right - 134.0f, row.bottom - 14.0f),
                                      Br(Rgba(Theme::TextPrimary, 0.92f)));
                    }

                    D2D1_RECT_F rCopy   = D2D1::RectF(row.right - 128.0f, row.top + 14.0f, row.right - 78.0f, row.bottom - 14.0f);
                    D2D1_RECT_F rAssign = D2D1::RectF(row.right - 72.0f,  row.top + 14.0f, row.right - 10.0f, row.bottom - 14.0f);
                    int zCopy   = AddClippedZone(10, rCopy, listViewport, item->id);
                    int zAssign = AddClippedZone(11, rAssign, listViewport, item->id);
                    bool isCopiedFlash = (copiedFlashId == item->id && nowTick - copiedFlashTick < 1100);
                    if (isCopiedFlash) {
                        DrawPillButton(rCopy, L"\x5DF2\x590D\x5236", zCopy, 0x14533D, 0x18664B, Theme::CopyGreen, 0.30f);
                    } else {
                        DrawPillButton(rCopy, L"\x590D\x5236", zCopy, 0x222736, 0x2D3448, Theme::TextPrimary);
                    }
                    std::wstring rkAssignLbl = (assignTargetSlot >= 0)
                        ? (L"\x653E\x5165 " + FormatSlotNum(assignTargetSlot))
                        : L"\x69FD\x4F4D \x25BE";
                    DrawPillButton(rAssign, rkAssignLbl, zAssign,
                                   assignTargetSlot >= 0 ? 0x1E3A5F : 0x222736,
                                   assignTargetSlot >= 0 ? 0x284D7E : 0x2D3448,
                                   assignTargetSlot >= 0 ? Theme::AccentCyan : Theme::AccentBlue);
                }
                y += 65.0f;
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        } else {
            // --- Tab 2: Settings (4-Section Task-Oriented Grouped Layout + Custom Dark Scrollbar) ---
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
                scrollY = std::clamp(sec2Top - 8.0f, 0.0f, maxScrollY);
            }
            scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

            rt->PushAxisAlignedClip(settingsViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float sy = contentTop + 6.0f - scrollY;

            // === Section 1: 贴边面板 ===
            sy = DrawSectionHeader(sy, cw, settingsViewport, L"贴边面板");
            {
                D2D1_RECT_F g = DrawGroupContainer(sy, sec1H, cw, settingsViewport);
                float ry = sy;

                // Row 1.1: 停靠方式 + 面板归位
                std::wstring locSummary = L"当前：" + (getDockLocationSummary ? getDockLocationSummary() : std::wstring(L"显示器 1 · 右侧边缘"));
                DrawRowText(g, ry, 74.0f, 306.0f, settingsViewport, L"停靠方式", locSummary.c_str());
                if (ry + 74.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 22.0f, bb = ry + 50.0f;
                    D2D1_RECT_F bRight = D2D1::RectF(g.right - 294.0f, bt, g.right - 232.0f, bb);
                    D2D1_RECT_F bLeft  = D2D1::RectF(g.right - 228.0f, bt, g.right - 166.0f, bb);
                    D2D1_RECT_F bFloat = D2D1::RectF(g.right - 162.0f, bt, g.right - 100.0f, bb);
                    D2D1_RECT_F bReset = D2D1::RectF(g.right - 90.0f,  bt, g.right - 16.0f,  bb);
                    int zR = AddClippedZone(33, bRight, settingsViewport);
                    int zL = AddClippedZone(34, bLeft,  settingsViewport);
                    int zF = AddClippedZone(36, bFloat, settingsViewport);
                    int zG = AddClippedZone(22, bReset, settingsViewport);
                    DrawSegPill(bRight, L"右侧贴边", state->dockEdge == DockEdge::Right, zR);
                    DrawSegPill(bLeft,  L"左侧贴边", state->dockEdge == DockEdge::Left,  zL);
                    DrawSegPill(bFloat, L"自由悬浮", state->dockEdge == DockEdge::None,  zF);
                    DrawPillButton(bReset, L"面板归位", zG, 0x222736, 0x2D3448, Theme::AccentCyan);
                }
                ry += 74.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 1.2: 悬停预览正文 (full-row clickable toggle)
                if (ry + 64.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    D2D1_RECT_F rowHit = D2D1::RectF(g.left + 4.0f, ry + 2.0f, g.right - 4.0f, ry + 62.0f);
                    int zRow = AddClippedZone(20, rowHit, settingsViewport);
                    if (zRow == hoverZoneIdx) {
                        rt->FillRoundedRectangle(D2D1::RoundedRect(rowHit, 7.0f, 7.0f), Br(Rgba(0xFFFFFF, 0.03f)));
                    }
                    D2D1_RECT_F sw = D2D1::RectF(g.right - 62.0f, ry + 20.0f, g.right - 16.0f, ry + 44.0f);
                    DrawToggleSwitch(sw, state->showHoverBubble, zRow);
                }
                DrawRowText(g, ry, 64.0f, 82.0f, settingsViewport, L"悬停预览正文",
                            L"鼠标悬停在槽位上时，侧边浮现预览气泡（左键可随时复制全文）");
                ry += 64.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 1.3: 点击托盘图标 (Option A default: 打开词舱)
                DrawRowText(g, ry, 66.0f, 206.0f, settingsViewport, L"点击托盘图标",
                            L"左键点击右下角托盘图标时的默认操作");
                if (ry + 66.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 19.0f, bb = ry + 47.0f;
                    D2D1_RECT_F bVault = D2D1::RectF(g.right - 196.0f, bt, g.right - 118.0f, bb);
                    D2D1_RECT_F bDock  = D2D1::RectF(g.right - 114.0f, bt, g.right - 16.0f,  bb);
                    int zV = AddClippedZone(37, bVault, settingsViewport);
                    int zD = AddClippedZone(38, bDock,  settingsViewport);
                    DrawSegPill(bVault, L"打开词舱",  state->trayClickOpensVault,  zV);
                    DrawSegPill(bDock,  L"展开 / 收起", !state->trayClickOpensVault, zD);
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
                        rt->FillRoundedRectangle(D2D1::RoundedRect(badgeR, 6.0f, 6.0f),
                                                 Br(Rgba(Theme::AccentCyan, 0.13f)));
                        wchar_t eb[32]{};
                        wsprintfW(eb, L"已启用 %d / %d", enabledCnt, kMaxScenes);
                        DrawUiText(fmtBadge.Get(), badgeR, eb, Rgba(Theme::AccentCyan, 0.96f));
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
                            D2D1_RECT_F btnCfg = D2D1::RectF(itemCard.right - 160.0f, itemCard.top + 5.0f, itemCard.right - 74.0f, itemCard.bottom - 5.0f);
                            D2D1_RECT_F btnRen = D2D1::RectF(itemCard.right - 68.0f,  itemCard.top + 5.0f, itemCard.right - 8.0f,  itemCard.bottom - 5.0f);

                            int zTog = AddClippedZone(50 + s, toggleHit, settingsViewport);
                            int zCfg = AddClippedZone(54 + s, btnCfg, settingsViewport);
                            int zRen = AddClippedZone(60 + s, btnRen, settingsViewport);
                            bool rowHov = (!isLockedLast && zTog == hoverZoneIdx);

                            rt->FillRoundedRectangle(D2D1::RoundedRect(itemCard, 7.0f, 7.0f),
                                                     Br(Rgba(rowHov ? 0x242A3B : (isEn ? 0x1A2030 : 0x131722), 0.96f)));
                            DrawRim(rt.Get(), itemCard, 7.0f,
                                    isAct ? Rgba(txtAcc, 0.44f) : (isEn ? Rgba(0xFFFFFF, 0.12f) : Rgba(0xFFFFFF, 0.06f)),
                                    Rgba(0xFFFFFF, 0.03f));

                            D2D1_RECT_F cb = D2D1::RectF(itemCard.left + 10.0f, itemCard.top + 11.0f, itemCard.left + 26.0f, itemCard.top + 27.0f);
                            DrawDarkCheckbox(rt.Get(), brush.Get(), cb, isEn, rowHov, txtAcc, isLockedLast);

                            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cb.right + 12.0f, (itemCard.top + itemCard.bottom) * 0.5f), 3.6f, 3.6f),
                                            Br(Rgba(accTop, isEn ? 0.96f : 0.42f)));

                            if (fmtCardTitle) {
                                rt->DrawTextW(sc.name.c_str(), (UINT32)sc.name.size(), fmtCardTitle.Get(),
                                              D2D1::RectF(cb.right + 22.0f, itemCard.top, cb.right + 120.0f, itemCard.bottom),
                                              Br(Rgba(isAct ? txtAcc : (isEn ? Theme::TextPrimary : Theme::TextSecondary), isEn ? 0.98f : 0.72f)));
                            }

                            wchar_t mBuf[40]{};
                            int mCnt = state->CountMountedInScene(s);
                            if (isAct && enabledCnt > 1) {
                                wsprintfW(mBuf, L"%d / 10 槽 · 当前", mCnt);
                            } else {
                                wsprintfW(mBuf, L"%d / 10 槽", mCnt);
                            }
                            DrawUiText(fmtCardBody.Get(),
                                       D2D1::RectF(cb.right + 124.0f, itemCard.top + 1.0f, btnCfg.left - 8.0f, itemCard.bottom),
                                       mBuf, Rgba(isEn ? Theme::TextSecondary : Theme::TextMuted, 0.88f));

                            DrawPillButton(btnCfg, L"配置提示词", zCfg, 0x1E3354, 0x284570, Theme::AccentCyan, 0.22f);
                            DrawPillButton(btnRen, L"重命名",    zRen, 0x222736, 0x2D3448, Theme::TextSecondary, 0.10f);
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
                bool autoOn = (autoSt == AutoStart::Status::EnabledCurrent);
                if (ry + rowAutoStartH >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    if (staleAuto) {
                        D2D1_RECT_F btnOff = D2D1::RectF(g.right - 196.0f, ry + 22.0f, g.right - 120.0f, ry + 52.0f);
                        D2D1_RECT_F btnFix = D2D1::RectF(g.right - 114.0f, ry + 22.0f, g.right - 16.0f,  ry + 52.0f);
                        int zOff = AddClippedZone(16, btnOff, settingsViewport);
                        int zFix = AddClippedZone(21, btnFix, settingsViewport);
                        DrawPillButton(btnOff, L"关闭自启",   zOff, 0x222736, 0x2D3448, Theme::TextSecondary);
                        DrawPillButton(btnFix, L"改为当前位置", zFix, 0xB45309, 0xD97706, 0xFFFFFF, 0.25f);
                    } else {
                        D2D1_RECT_F rowHit = D2D1::RectF(g.left + 4.0f, ry + 2.0f, g.right - 4.0f, ry + rowAutoStartH - 2.0f);
                        int zRow = AddClippedZone(21, rowHit, settingsViewport);
                        if (zRow == hoverZoneIdx) {
                            rt->FillRoundedRectangle(D2D1::RoundedRect(rowHit, 7.0f, 7.0f), Br(Rgba(0xFFFFFF, 0.03f)));
                        }
                        D2D1_RECT_F sw = D2D1::RectF(g.right - 62.0f, ry + 20.0f, g.right - 16.0f, ry + 44.0f);
                        DrawToggleSwitch(sw, autoOn, zRow);
                    }
                }
                DrawRowText(g, ry, rowAutoStartH, staleAuto ? 208.0f : 82.0f, settingsViewport,
                            L"开机自动启动",
                            staleAuto ? L"开机自启当前指向另一个目录的程序副本，可切换为当前位置或关闭"
                                      : L"开机后自动隐藏在屏幕边缘待命，无需管理员权限",
                            true, staleAuto ? 0xFCD34D : Theme::TextSecondary);
                ry += rowAutoStartH;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 2.2: 启动时自动检查更新 (full-row clickable)
                if (ry + 66.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    D2D1_RECT_F rowHit = D2D1::RectF(g.left + 4.0f, ry + 2.0f, g.right - 4.0f, ry + 64.0f);
                    int zRow = AddClippedZone(28, rowHit, settingsViewport);
                    if (zRow == hoverZoneIdx) {
                        rt->FillRoundedRectangle(D2D1::RoundedRect(rowHit, 7.0f, 7.0f), Br(Rgba(0xFFFFFF, 0.03f)));
                    }
                    D2D1_RECT_F sw = D2D1::RectF(g.right - 62.0f, ry + 21.0f, g.right - 16.0f, ry + 45.0f);
                    DrawToggleSwitch(sw, state->autoCheckUpdates, zRow);
                }
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
                        D2D1_RECT_F btnRel = D2D1::RectF(g.right - 204.0f, bt, g.right - 122.0f, bb);
                        D2D1_RECT_F btnAct = D2D1::RectF(g.right - 114.0f, bt, g.right - 16.0f,  bb);
                        int zRel = AddClippedZone(27, btnRel, settingsViewport);
                        DrawPillButton(btnRel, L"打开发布页", zRel, 0x222736, 0x2D3448, Theme::TextSecondary);
                        if (upSnap.canAutoInstall) {
                            int zInst = AddClippedZone(26, btnAct, settingsViewport);
                            DrawPillButton(btnAct, L"更新并重启", zInst, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.28f);
                        } else {
                            int zChk = AddClippedZone(25, btnAct, settingsViewport);
                            DrawPillButton(btnAct, L"重新检查", zChk, 0x222736, 0x2D3448, Theme::AccentCyan);
                        }
                        if (hasReleaseNotes) {
                            D2D1_RECT_F notesBox = D2D1::RectF(g.left + 16.0f, ry + 68.0f, g.right - 16.0f, ry + rowUpdateH - 12.0f);
                            rt->FillRoundedRectangle(D2D1::RoundedRect(notesBox, 7.0f, 7.0f), Br(Rgba(0x131722, 0.96f)));
                            DrawRim(rt.Get(), notesBox, 7.0f, Rgba(Theme::AccentCyan, 0.22f), Rgba(0xFFFFFF, 0.04f));
                            std::wstring notesClean = CollapseWhitespaceSingleLine(upSnap.releaseNotes, 220);
                            if (fmtDescWrap) {
                                rt->DrawTextW(notesClean.c_str(), (UINT32)notesClean.size(), fmtDescWrap.Get(),
                                              D2D1::RectF(notesBox.left + 10.0f, notesBox.top + 6.0f, notesBox.right - 10.0f, notesBox.bottom - 6.0f),
                                              Br(Rgba(Theme::TextSecondary, 0.92f)));
                            }
                        }
                    } else if (upSnap.phase == UpdatePhase::Checking) {
                        D2D1_RECT_F btnBusy = D2D1::RectF(g.right - 108.0f, bt, g.right - 16.0f, bb);
                        DrawPillButton(btnBusy, L"检查中...", -2, 0x1B202E, 0x1B202E, Theme::TextMuted);
                    } else if (hasProgressBar) {
                        D2D1_RECT_F btnBusy = D2D1::RectF(g.right - 108.0f, bt, g.right - 16.0f, bb);
                        wchar_t pTxt[32]{};
                        if (upSnap.phase == UpdatePhase::Installing) wcscpy_s(pTxt, L"正在安装...");
                        else wsprintfW(pTxt, L"下载 %d%%", upSnap.downloadPercent);
                        DrawPillButton(btnBusy, pTxt, -2, 0x1E3354, 0x1E3354, Theme::AccentCyan, 0.25f);

                        D2D1_RECT_F track = D2D1::RectF(g.left + 16.0f, ry + rowUpdateH - 16.0f, g.right - 16.0f, ry + rowUpdateH - 10.0f);
                        rt->FillRoundedRectangle(D2D1::RoundedRect(track, 3.0f, 3.0f), Br(Rgba(0x0D0F15, 1.0f)));
                        float pctRatio = std::clamp((float)upSnap.downloadPercent / 100.0f, 0.04f, 1.0f);
                        D2D1_RECT_F fill = D2D1::RectF(track.left, track.top, track.left + (track.right - track.left) * pctRatio, track.bottom);
                        rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 3.0f, 3.0f), Br(Rgba(Theme::AccentCyan, 0.92f)));
                    } else {
                        if (upSnap.phase == UpdatePhase::Error) {
                            D2D1_RECT_F btnRel = D2D1::RectF(g.right - 204.0f, bt, g.right - 116.0f, bb);
                            int zRel = AddClippedZone(27, btnRel, settingsViewport);
                            DrawPillButton(btnRel, L"打开发布页", zRel, 0x222736, 0x2D3448, Theme::TextSecondary);
                        }
                        D2D1_RECT_F btnChk = D2D1::RectF(g.right - 108.0f, bt, g.right - 16.0f, bb);
                        int zChk = AddClippedZone(25, btnChk, settingsViewport);
                        DrawPillButton(btnChk, upSnap.phase == UpdatePhase::Error ? L"重试检查" : L"检查更新",
                                       zChk, 0x222736, 0x2D3448, Theme::AccentCyan);
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
                    D2D1_RECT_F bExp = D2D1::RectF(g.right - 178.0f, bt, g.right - 100.0f, bb);
                    D2D1_RECT_F bImp = D2D1::RectF(g.right - 94.0f,  bt, g.right - 16.0f,  bb);
                    int zExp = AddClippedZone(5, bExp, settingsViewport);
                    int zImp = AddClippedZone(4, bImp, settingsViewport);
                    DrawPillButton(bExp, L"导出备份", zExp, 0x222736, 0x2D3448, Theme::TextPrimary);
                    DrawPillButton(bImp, L"导入备份", zImp, 0x222736, 0x2D3448, Theme::AccentCyan);
                }
                ry += 68.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 3.2: 数据目录、复制路径与运行日志
                DrawRowText(g, ry, 72.0f, 276.0f, settingsViewport, L"本地数据目录与诊断日志",
                            storage ? storage->dataDir.c_str() : L"", false);
                if (ry + 72.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 22.0f, bb = ry + 50.0f;
                    D2D1_RECT_F bCopyP = D2D1::RectF(g.right - 266.0f, bt, g.right - 190.0f, bb);
                    D2D1_RECT_F bDir   = D2D1::RectF(g.right - 184.0f, bt, g.right - 102.0f, bb);
                    D2D1_RECT_F bLog   = D2D1::RectF(g.right - 96.0f,  bt, g.right - 16.0f,  bb);
                    int zCp  = AddClippedZone(39, bCopyP, settingsViewport);
                    int zDir = AddClippedZone(23, bDir,   settingsViewport);
                    int zLog = AddClippedZone(24, bLog,   settingsViewport);
                    DrawPillButton(bCopyP, L"复制路径",   zCp,  0x222736, 0x2D3448, Theme::TextSecondary);
                    DrawPillButton(bDir,   L"打开数据目录", zDir, 0x222736, 0x2D3448, Theme::TextPrimary);
                    DrawPillButton(bLog,   L"运行日志",   zLog, 0x222736, 0x2D3448, Theme::AccentCyan);
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
                    D2D1_RECT_F bGuide = D2D1::RectF(g.right - 108.0f, ry + 17.0f, g.right - 16.0f, ry + 45.0f);
                    int zG = AddClippedZone(40, bGuide, settingsViewport);
                    DrawPillButton(bGuide, L"重新引导", zG, 0x222736, 0x2D3448, Theme::AccentCyan);
                }
                ry += 62.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 4.2: 意见反馈 & 次级外链
                DrawRowText(g, ry, 68.0f, 294.0f, settingsViewport, L"意见反馈与社区",
                            L"反馈 Bug 或功能建议，或访问 GitHub 主页与作者 X（" APP_X_HANDLE_W L"）");
                if (ry + 68.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 20.0f, bb = ry + 48.0f;
                    D2D1_RECT_F btnGh = D2D1::RectF(g.right - 284.0f, bt, g.right - 196.0f, bb);
                    D2D1_RECT_F btnX  = D2D1::RectF(g.right - 190.0f, bt, g.right - 110.0f, bb);
                    D2D1_RECT_F btnFb = D2D1::RectF(g.right - 104.0f, bt, g.right - 16.0f,  bb);
                    int zGh = AddClippedZone(29, btnGh, settingsViewport);
                    int zX  = AddClippedZone(30, btnX,  settingsViewport);
                    int zFb = AddClippedZone(31, btnFb, settingsViewport);
                    DrawPillButton(btnGh, L"GitHub 主页", zGh, 0x1B1F2B, 0x262C3D, Theme::TextSecondary);
                    DrawPillButton(btnX,  L"作者 X",      zX,  0x1B1F2B, 0x262C3D, Theme::TextSecondary);
                    DrawPillButton(btnFb, L"意见反馈",     zFb, 0x1E3354, 0x284570, Theme::AccentCyan, 0.24f);
                }
                ry += 68.0f;
                DrawRowDivider(g, ry, settingsViewport);

                // Row 4.3: 内置提示词致谢（数字生命卡兹克 X & GitHub 点击跳转）
                DrawRowText(g, ry, 74.0f, 224.0f, settingsViewport, L"内置提示词致谢",
                            L"内置 12 条精选默认 Prompt 均整理自「数字生命卡兹克」的公开分享，特此致谢");
                if (ry + 74.0f >= settingsViewport.top && ry <= settingsViewport.bottom) {
                    float bt = ry + 23.0f, bb = ry + 51.0f;
                    D2D1_RECT_F btnKX  = D2D1::RectF(g.right - 214.0f, bt, g.right - 130.0f, bb);
                    D2D1_RECT_F btnKGh = D2D1::RectF(g.right - 124.0f, bt, g.right - 16.0f,  bb);
                    int zKX  = AddClippedZone(64, btnKX,  settingsViewport);
                    int zKGh = AddClippedZone(65, btnKGh, settingsViewport);
                    DrawPillButton(btnKX,  L"卡兹克 X",      zKX,  0x1B1F2B, 0x262C3D, Theme::AccentCyan, 0.16f);
                    DrawPillButton(btnKGh, L"卡兹克 GitHub", zKGh, 0x1B1F2B, 0x262C3D, Theme::AccentCyan, 0.16f);
                }
            }

            rt->PopAxisAlignedClip();
            viewportScroll.Draw(rt.Get(), brush.Get(), scrollY, maxScrollY, lastViewportH);
        }

        // Bottom inline Toast notification (with inline '撤销' button only when toastShowsUndo is true!)
        bool undoLive = (toastShowsUndo && undoDelete.active && nowTick < undoDelete.expireTick);
        if (!toastMessage.empty() && nowTick - toastTick < toastDurationMs) {
            float tw = std::min(cw - 40.0f, undoLive ? 410.0f : 368.0f);
            D2D1_RECT_F tr = D2D1::RectF((cw - tw) * 0.5f, ch - 48.0f, (cw + tw) * 0.5f, ch - 14.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(tr, 8.0f, 8.0f),
                                     Br(Rgba(toastIsError ? 0x3B1820 : (undoLive ? 0x1A2233 : 0x162925), 0.98f)));
            DrawRim(rt.Get(), tr, 8.0f,
                             Rgba(toastIsError ? Theme::DangerRed : (undoLive ? Theme::AccentCyan : Theme::CopyGreen), 0.48f),
                             Rgba(0xFFFFFF, 0.06f));
            if (undoLive) {
                D2D1_RECT_F textR = D2D1::RectF(tr.left + 12.0f, tr.top, tr.right - 92.0f, tr.bottom);
                if (fmtCardBody) {
                    rt->DrawTextW(toastMessage.c_str(), (UINT32)toastMessage.size(), fmtCardBody.Get(), textR,
                                  Br(Rgba(Theme::TextPrimary, 0.96f)));
                }
                D2D1_RECT_F btnUndo = D2D1::RectF(tr.right - 84.0f, tr.top + 5.0f, tr.right - 8.0f, tr.bottom - 5.0f);
                int zUndo = AddZone(35, btnUndo);
                DrawPillButton(btnUndo, undoDelete.isClearScene ? L"撤销清空" : L"撤销删除", zUndo, 0x264B7A, 0x3366A8, Theme::AccentCyan, 0.35f);
            } else if (fmtBadge) {
                rt->DrawTextW(toastMessage.c_str(), (UINT32)toastMessage.size(), fmtBadge.Get(), tr,
                              Br(Rgba(toastIsError ? Theme::DangerRed : Theme::CopyGreen, 0.98f)));
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
                activeTab = 0;
                scrollY = 0.0f;
                UpdateSearchVisibility();
                if (hSearchEdit && IsWindow(hSearchEdit)) SetFocus(hSearchEdit);
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case 2:
                activeTab = 1;
                scrollY = 0.0f;
                UpdateSearchVisibility();
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case 7:
                activeTab = 2;
                scrollY = 0.0f;
                UpdateSearchVisibility();
                InvalidateRect(hwnd, nullptr, FALSE);
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
                    selectedVaultRow = searchQuery.empty() ? -1 : 0;
                    InvalidateCaches();
                    UpdateSearchCueBanner();
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
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 16: {
                if (AutoStart::SetEnabled(false)) {
                    ShowToast(L"已关闭开机自动启动", false);
                } else {
                    ShowToast(L"修改开机自启失败", true);
                }
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
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            case 22: {
                if (onResetDockPos) onResetDockPos();
                ShowToast(L"贴边面板已归位", false);
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
                    ShowToast(L"至少需保留 1 个启用场景", false);
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
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case 26: {
                if (updater) {
                    if (!updater->StartDownloadAndInstall()) {
                        ShowToast(L"当前无法启动原地更新，请前往发布页下载", true);
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
            case WM_SIZE:
                if (self->rt) {
                    self->rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                }
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
                if (LOWORD(wp) == 2001 && HIWORD(wp) == EN_CHANGE) {
                    int len = GetWindowTextLengthW(self->hSearchEdit);
                    std::wstring q((size_t)len + 1, L'\0');
                    if (len > 0) GetWindowTextW(self->hSearchEdit, q.data(), len + 1);
                    q.resize((size_t)len);
                    self->searchQuery = q;
                    self->scrollY = 0.0f;
                    self->selectedVaultRow = q.empty() ? -1 : 0;
                    self->InvalidateCaches();
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            case WM_MOUSEWHEEL: {
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                self->scrollY = std::clamp(self->scrollY - (float)delta * 0.45f, 0.0f, self->maxScrollY);
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
                    self->EnsureVaultCache();
                    float relY = (dy - self->vaultListTopDip) + self->scrollY;
                    int idx = (int)std::floor(relY / 76.0f);
                    if (idx >= 0 && idx < (int)self->cachedFilteredIndices.size() && (relY - idx * 76.0f) <= 68.0f) {
                        size_t vi = self->cachedFilteredIndices[(size_t)idx];
                        if (vi < self->state->vault.size()) {
                            self->selectedVaultRow = idx;
                            InvalidateRect(h, nullptr, FALSE);
                            self->ShowCardMoreMenu(self->state->vault[vi].id, spt, true);
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
                        self->EnsureVaultCache();
                        float relY = (dy - self->vaultListTopDip) + self->scrollY;
                        int idx = (int)std::floor(relY / 76.0f);
                        if (idx >= 0 && idx < (int)self->cachedFilteredIndices.size() && (relY - idx * 76.0f) <= 68.0f) {
                            size_t vi = self->cachedFilteredIndices[(size_t)idx];
                            if (vi < self->state->vault.size()) {
                                if (self->assignTargetSlot >= 0) self->AssignPromptToTargetSlot(self->state->vault[vi].id);
                                else self->OpenEditPromptModal(self->state->vault[vi].id);
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

                // Check custom dark viewport scrollbar first
                if (self->viewportScroll.OnLButtonDown(dx, dy, self->scrollY, self->maxScrollY, self->lastViewportH)) {
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
                        self->EnsureVaultCache();
                        float relY = (dy - self->vaultListTopDip) + self->scrollY;
                        int rowIdx = (int)std::floor(relY / 76.0f);
                        if (rowIdx >= 0 && rowIdx < (int)self->cachedFilteredIndices.size() && (relY - rowIdx * 76.0f) <= 68.0f) {
                            self->selectedVaultRow = rowIdx;
                            if (self->searchQuery.empty() && self->EffectiveVaultFilter() == -1) {
                                self->vaultDragIdx = (int)self->cachedFilteredIndices[(size_t)rowIdx];
                                self->isVaultDragging = false;
                                self->vaultDragStartDipY = dy;
                                self->vaultDragLastDipY = dy;
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
                    if (self->hoverZoneIdx != -1) {
                        self->hoverZoneIdx = -1;
                        sbRedraw = true;
                    }
                    if (sbRedraw) InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (sbRedraw) InvalidateRect(h, nullptr, FALSE);

                if (self->vaultDragIdx >= 0) {
                    self->vaultDragLastDipY = dy;
                    if (!self->isVaultDragging && std::fabs(dy - self->vaultDragStartDipY) >= 5.0f) {
                        self->isVaultDragging = true;
                        SetTimer(h, kTimerDragScroll, 35, nullptr);
                    }
                    if (self->isVaultDragging) {
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                        float relY = (dy - self->vaultListTopDip) + self->scrollY;
                        int target = std::clamp((int)std::floor(relY / 76.0f), 0, (int)self->state->vault.size() - 1);
                        if (target != self->vaultDragIdx) {
                            // Decoupled from dock slots: dragging inside Vault ONLY reorders vault display order!
                            if (ReorderVaultItemOnly(*self->state, (size_t)self->vaultDragIdx, (size_t)target)) {
                                self->vaultDragIdx = target;
                                self->selectedVaultRow = target;
                                self->InvalidateCaches();
                                InvalidateRect(h, nullptr, FALSE);
                            }
                        }
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
                    if (GetCapture() == h) ReleaseCapture();
                    InvalidateRect(h, nullptr, FALSE);
                    return 0;
                }
                if (self->vaultDragIdx >= 0) {
                    bool dragged = self->isVaultDragging;
                    self->vaultDragIdx = -1;
                    self->isVaultDragging = false;
                    KillTimer(h, kTimerDragScroll);
                    if (GetCapture() == h) ReleaseCapture();
                    if (dragged) {
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
                KillTimer(h, kTimerDragScroll);
                if (self->vaultDragIdx >= 0) {
                    bool dragged = self->isVaultDragging;
                    self->vaultDragIdx = -1;
                    self->isVaultDragging = false;
                    if (dragged) {
                        self->storage->Save(*self->state);
                        self->InvalidateCaches();
                    }
                    InvalidateRect(h, nullptr, FALSE);
                } else if (hadPressed) {
                    InvalidateRect(h, nullptr, FALSE);
                }
                return 0;
            }
            case WM_TIMER:
                if (wp == kTimerFeedback) {
                    ULONGLONG now = GetTickCount64();
                    bool active = false;
                    if (self->undoDelete.active && now < self->undoDelete.expireTick) active = true;
                    else self->undoDelete.active = false;
                    if (!self->copiedFlashId.empty() && now - self->copiedFlashTick < 1200) active = true;
                    else self->copiedFlashId.clear();
                    if (!self->toastMessage.empty() && now - self->toastTick < self->toastDurationMs + 100) active = true;
                    else self->toastMessage.clear();
                    if (!active) KillTimer(h, kTimerFeedback);
                    InvalidateRect(h, nullptr, FALSE);
                } else if (wp == kTimerDragScroll) {
                    if (!self->isVaultDragging || self->vaultDragIdx < 0) {
                        KillTimer(h, kTimerDragScroll);
                        return 0;
                    }
                    RECT crc{};
                    GetClientRect(h, &crc);
                    float ch = (float)(crc.bottom - crc.top) / self->dpiScale;
                    float scrollDelta = 0.0f;
                    if (self->vaultDragLastDipY < self->vaultListTopDip + 28.0f) {
                        scrollDelta = -14.0f;
                    } else if (self->vaultDragLastDipY > ch - 36.0f) {
                        scrollDelta = 14.0f;
                    }
                    if (scrollDelta != 0.0f && self->maxScrollY > 0.5f) {
                        float nextScroll = std::clamp(self->scrollY + scrollDelta, 0.0f, self->maxScrollY);
                        if (std::fabs(nextScroll - self->scrollY) > 0.1f) {
                            self->scrollY = nextScroll;
                            float relY = (self->vaultDragLastDipY - self->vaultListTopDip) + self->scrollY;
                            int target = std::clamp((int)std::floor(relY / 76.0f), 0, (int)self->state->vault.size() - 1);
                            if (target != self->vaultDragIdx &&
                                ReorderVaultItemOnly(*self->state, (size_t)self->vaultDragIdx, (size_t)target)) {
                                self->vaultDragIdx = target;
                                self->selectedVaultRow = target;
                                self->InvalidateCaches();
                            }
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    }
                }
                return 0;
            case WM_PAINT:
                self->OnPaint();
                return 0;
            case WM_CLOSE:
                // Reset transient search query, view filter & slot assignment target when closing VaultWindow
                self->searchQuery.clear();
                self->assignTargetSlot = -1;
                self->vaultFilter = -1;
                self->selectedVaultRow = -1;
                self->scrollY = 0.0f;
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
