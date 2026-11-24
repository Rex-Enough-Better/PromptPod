#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "theme.h"

namespace pp {

using Microsoft::WRL::ComPtr;

// Shared Direct2D & DirectWrite factory singleton
class D2DEngine {
public:
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> dwFactory;

    bool Init() {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwFactory.GetAddressOf()));
        return SUCCEEDED(hr);
    }

    ComPtr<IDWriteTextFormat> CreateFormat(float sizeDip, DWRITE_FONT_WEIGHT weight,
                                           bool noWrap = true,
                                           DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
                                           DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        ComPtr<IDWriteTextFormat> fmt;
        if (!dwFactory) return fmt;
        // Segoe UI Variable / Microsoft YaHei UI fallback stack
        HRESULT hr = dwFactory->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            sizeDip,
            L"zh-CN",
            fmt.GetAddressOf()
        );
        if (FAILED(hr)) return fmt;
        fmt->SetTextAlignment(hAlign);
        fmt->SetParagraphAlignment(vAlign);
        if (noWrap) {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(dwFactory->CreateEllipsisTrimmingSign(fmt.Get(), ellipsis.GetAddressOf()))) {
                fmt->SetTrimming(&trim, ellipsis.Get());
            }
        } else {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        return fmt;
    }

    static D2DEngine& Instance() {
        static D2DEngine inst;
        return inst;
    }
};

// Offscreen 32-bit premultiplied ARGB DIBSection + ID2D1DCRenderTarget for atomic UpdateLayeredWindow
class LayeredCanvas {
public:
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;
    int widthPx = 0;
    int heightPx = 0;
    int capWidthPx = 0;
    int capHeightPx = 0;
    const char* lastFailStep = "";
    HRESULT lastHr = S_OK;
    DWORD lastWin32Err = 0;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> solidBrush;

    ~LayeredCanvas() {
        ReleaseGdi();
    }

    void ReleaseGdi() {
        if (memDC) {
            if (oldBmp) SelectObject(memDC, oldBmp);
            oldBmp = nullptr;
        }
        if (dib) {
            DeleteObject(dib);
            dib = nullptr;
        }
        if (memDC) {
            DeleteDC(memDC);
            memDC = nullptr;
        }
        bits = nullptr;
        widthPx = 0;
        heightPx = 0;
        capWidthPx = 0;
        capHeightPx = 0;
    }

    bool EnsureTarget() {
        if (rt && solidBrush) return true;
        solidBrush.Reset();
        rt.Reset();
        auto& eng = D2DEngine::Instance();
        if (!eng.d2dFactory) {
            lastFailStep = "NoD2DFactory";
            return false;
        }
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f
        );
        HRESULT hr = eng.d2dFactory->CreateDCRenderTarget(&props, rt.GetAddressOf());
        if (FAILED(hr)) {
            lastFailStep = "CreateDCRenderTarget";
            lastHr = hr;
            return false;
        }
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        hr = rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), solidBrush.GetAddressOf());
        if (FAILED(hr)) {
            lastFailStep = "CreateSolidColorBrush";
            lastHr = hr;
            return false;
        }
        return true;
    }

    bool EnsureSize(int wPx, int hPx) {
        if (wPx <= 0 || hPx <= 0) {
            lastFailStep = "InvalidSize";
            return false;
        }
        if (!EnsureTarget()) return false;

        // Reuse existing DIBSection when capacity is sufficient (avoids GDI reallocation on collapse/expand)
        if (memDC && dib && capWidthPx >= wPx && capHeightPx >= hPx) {
            widthPx = wPx;
            heightPx = hPx;
            return true;
        }

        int newCapW = std::max(capWidthPx, wPx);
        int newCapH = std::max(capHeightPx, hPx);

        HDC screenDC = GetDC(nullptr);
        HDC newMemDC = CreateCompatibleDC(screenDC);
        if (screenDC) ReleaseDC(nullptr, screenDC);
        if (!newMemDC) {
            lastFailStep = "CreateCompatibleDC";
            lastWin32Err = GetLastError();
            return false;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = newCapW;
        bmi.bmiHeader.biHeight = -newCapH; // top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* newBits = nullptr;
        HBITMAP newDib = CreateDIBSection(newMemDC, &bmi, DIB_RGB_COLORS, &newBits, nullptr, 0);
        if (!newDib || !newBits) {
            lastFailStep = "CreateDIBSection";
            lastWin32Err = GetLastError();
            if (newDib) DeleteObject(newDib);
            DeleteDC(newMemDC);
            return false;
        }

        HGDIOBJ newOldBmp = SelectObject(newMemDC, newDib);
        if (!newOldBmp || newOldBmp == HGDI_ERROR) {
            lastFailStep = "SelectObject";
            lastWin32Err = GetLastError();
            DeleteObject(newDib);
            DeleteDC(newMemDC);
            return false;
        }

        ReleaseGdi();
        memDC = newMemDC;
        dib = newDib;
        bits = newBits;
        oldBmp = newOldBmp;
        capWidthPx = newCapW;
        capHeightPx = newCapH;
        widthPx = wPx;
        heightPx = hPx;
        return true;
    }

    // Sample premultiplied BGRA alpha byte at (x, y) in the top-down DIB after EndDraw()+GdiFlush()
    BYTE SampleAlphaAt(int x, int y) const {
        if (!bits || x < 0 || y < 0 || x >= capWidthPx || y >= capHeightPx) return 0;
        const BYTE* px = static_cast<const BYTE*>(bits) + ((size_t)y * (size_t)capWidthPx + (size_t)x) * 4u;
        return px[3];
    }

    ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& c) {
        if (solidBrush) solidBrush->SetColor(c);
        return solidBrush.Get();
    }

    bool Begin(int wPx, int hPx, float dpiScale) {
        lastFailStep = "";
        lastHr = S_OK;
        lastWin32Err = 0;
        if (!EnsureSize(wPx, hPx)) return false;
        RECT rc{ 0, 0, wPx, hPx };
        HRESULT hr = rt->BindDC(memDC, &rc);
        if (FAILED(hr)) {
            // Recreate target once inline if D2D target became invalid
            solidBrush.Reset();
            rt.Reset();
            if (!EnsureTarget()) return false;
            hr = rt->BindDC(memDC, &rc);
            if (FAILED(hr)) {
                lastFailStep = "BindDC";
                lastHr = hr;
                return false;
            }
        }
        rt->BeginDraw();
        rt->SetTransform(D2D1::Matrix3x2F::Scale(dpiScale, dpiScale));
        rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        return true;
    }

    bool EndAndPresent(HWND hwnd, int screenX, int screenY, BYTE masterAlpha = 255) {
        HRESULT hr = rt->EndDraw();
        if (FAILED(hr)) {
            lastFailStep = "EndDraw";
            lastHr = hr;
            solidBrush.Reset();
            rt.Reset();
            return false;
        }
        POINT ptDst{ screenX, screenY };
        SIZE szWnd{ widthPx, heightPx };
        POINT ptSrc{ 0, 0 };
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = masterAlpha;
        blend.AlphaFormat = AC_SRC_ALPHA;
        SetLastError(0);
        if (!UpdateLayeredWindow(hwnd, nullptr, &ptDst, &szWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA)) {
            lastFailStep = "UpdateLayeredWindow";
            lastWin32Err = GetLastError();
            return false;
        }
        return true;
    }
};

// Helper: draw soft layered ambient shadow around a rounded rectangle in DIPs
inline void DrawAmbientShadow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                              const D2D1_RECT_F& box, float radius, float opacityScale = 1.0f) {
    if (opacityScale <= 0.01f) return;
    constexpr int kLayers = 6;
    for (int i = kLayers; i >= 1; --i) {
        float spread = (float)i * 2.1f;
        float yShift = (float)i * 0.65f;
        float a = (0.045f * (float)(kLayers - i + 1) / (float)kLayers) * opacityScale;
        D2D1_ROUNDED_RECT rr{
            D2D1::RectF(box.left - spread, box.top - spread + yShift,
                        box.right + spread, box.bottom + spread + yShift),
            radius + spread * 0.65f,
            radius + spread * 0.65f
        };
        br->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, a));
        rt->FillRoundedRectangle(rr, br);
    }
}

// Helper: draw 1px top-lit Linear-style specular gradient border
inline void DrawTopLitBorder(ID2D1RenderTarget* rt, const D2D1_RECT_F& box, float radius,
                             const D2D1_COLOR_F& topColor, const D2D1_COLOR_F& bottomColor, float strokeWidth = 1.0f) {
    D2D1_GRADIENT_STOP stops[2] = {
        { 0.0f, topColor },
        { 1.0f, bottomColor }
    };
    ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(rt->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return;
    ComPtr<ID2D1LinearGradientBrush> grad;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = D2D1::LinearGradientBrushProperties(
        D2D1::Point2F(box.left, box.top),
        D2D1::Point2F(box.left, box.bottom)
    );
    if (FAILED(rt->CreateLinearGradientBrush(props, coll.Get(), grad.GetAddressOf()))) return;
    float half = strokeWidth * 0.5f;
    D2D1_ROUNDED_RECT rr{
        D2D1::RectF(box.left + half, box.top + half, box.right - half, box.bottom - half),
        radius, radius
    };
    rt->DrawRoundedRectangle(rr, grad.Get(), strokeWidth);
}

inline float GetDpiScaleForWindow(HWND hwnd) {
    UINT dpi = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
        if (fn && hwnd) dpi = fn(hwnd);
    }
    return dpi > 0 ? (float)dpi / 96.0f : 1.0f;
}

inline float GetDpiScaleForMonitor(HMONITOR mon, HWND fallbackHwnd = nullptr) {
    if (mon) {
        static HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static auto fn = reinterpret_cast<GetDpiForMonitorFn>(
                reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor")));
            if (fn) {
                UINT dpiX = 96, dpiY = 96;
                if (SUCCEEDED(fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dpiX, &dpiY)) && dpiX > 0) {
                    return (float)dpiX / 96.0f;
                }
            }
        }
    }
    if (fallbackHwnd) return GetDpiScaleForWindow(fallbackHwnd);
    HDC sdc = GetDC(nullptr);
    float sc = sdc ? (float)GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f : 1.0f;
    if (sdc) ReleaseDC(nullptr, sdc);
    return sc > 0.1f ? sc : 1.0f;
}

inline float GetDpiScaleForPoint(POINT pt, HWND fallbackHwnd = nullptr) {
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    return GetDpiScaleForMonitor(mon, fallbackHwnd);
}

inline RECT GetMonitorWorkAreaForPoint(POINT pt, RECT* outFullMonitor = nullptr) {
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        if (outFullMonitor) *outFullMonitor = mi.rcMonitor;
        return mi.rcWork;
    }
    RECT rc{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    if (outFullMonitor) *outFullMonitor = rc;
    return rc;
}

} // namespace pp
