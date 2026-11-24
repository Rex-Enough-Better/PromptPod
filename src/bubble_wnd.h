#pragma once

#include <windows.h>
#include <cmath>
#include <string>
#include <algorithm>
#include "model.h"
#include "theme.h"
#include "d2d_ctx.h"

namespace pp {

// Pure mouse-passthrough (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE) full-text hover preview bubble
class HoverBubbleWindow {
public:
    [[gnu::noinline]] HoverBubbleWindow() = default;
    [[gnu::noinline]] ~HoverBubbleWindow() = default;

    HWND hwnd = nullptr;
    LayeredCanvas canvas;

    ComPtr<IDWriteTextFormat> fmtSlotTag;
    ComPtr<IDWriteTextFormat> fmtTitle;
    ComPtr<IDWriteTextFormat> fmtMeta;
    ComPtr<IDWriteTextFormat> fmtBody;
    ComPtr<IDWriteTextFormat> fmtFootHint;
    ComPtr<IDWriteTextLayout> bodyLayout;

    float dpiScale = 1.0f;
    Spring1D alphaSpring = Spring1D::Snappy(0.0f);
    Spring1D ySpring = Spring1D::Fluid(0.0f);
    Spring1D heightSpring = Spring1D::Fluid(120.0f);
    float contentAlpha = 1.0f;

    int currentSlot = -1;
    std::wstring currentId;
    std::wstring titleText;
    std::wstring bodyText;
    size_t charCount = 0;

    bool placeOnLeft = true;
    bool isOverflow = false;
    float cardWidthDip = 340.0f;
    float cardHeightDip = 120.0f;
    float bodyDrawHeightDip = 60.0f;
    float lastWorkMaxBodyH = 0.0f;

    int anchorScreenX = 0;       // Target X when alpha == 1
    int slotCenterScreenY = 0;   // Hovered slot vertical center in screen px

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastQpc{};
    bool animActive = false;
    bool shown = false;
    int consecutiveRenderFails = 0;

    static constexpr float kPad = 14.0f;
    static constexpr UINT_PTR kTimerAnim = 6101;

    static size_t CountCodePoints(const std::wstring& s) {
        size_t n = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            wchar_t c = s[i];
            if (c == L'\r') continue;
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                ++i;
            }
            ++n;
        }
        return n;
    }

    bool Init(HINSTANCE hInst) {
        QueryPerformanceFrequency(&qpcFreq);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptPod_HoverBubbleWnd";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            wc.lpszClassName,
            L"",
            WS_POPUP,
            0, 0, 10, 10,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd) return false;

        alphaSpring = Spring1D::Snappy(0.0f);
        ySpring = Spring1D::Fluid(0.0f);
        heightSpring = Spring1D::Fluid(120.0f);
        contentAlpha = 1.0f;
        return true;
    }

    bool IsShowingOrAppearing() const {
        return shown && alphaSpring.target > 0.5f;
    }

    void HideImmediate() {
        if (!hwnd) return;
        currentSlot = -1;
        currentId.clear();
        titleText.clear();
        if (bodyText.capacity() > 2048) {
            std::wstring().swap(bodyText);
        } else {
            bodyText.clear();
        }
        consecutiveRenderFails = 0;
        alphaSpring.Snap(0.0f);
        contentAlpha = 1.0f;
        StopTimer();
        if (shown) {
            ShowWindow(hwnd, SW_HIDE);
            shown = false;
        }
        bodyLayout.Reset();
    }

    void HideSmooth() {
        if (!hwnd || !shown || alphaSpring.target == 0.0f) return;
        if (Motion::IsReducedMotion()) {
            HideImmediate();
            return;
        }
        currentSlot = -1;
        alphaSpring.target = 0.0f;
        StartTimer();
    }

    void ShowForSlot(int slotIdx, const PromptItem& item, const RECT& dockCardScreenRect, int slotCenterY) {
        if (!hwnd) return;
        POINT midPt{ (dockCardScreenRect.left + dockCardScreenRect.right) / 2, slotCenterY };
        dpiScale = GetDpiScaleForPoint(midPt, hwnd);
        EnsureFormats();

        bool contentChanged = (currentSlot != slotIdx || currentId != item.id || bodyText != item.content || titleText != item.title);
        currentSlot = slotIdx;
        currentId = item.id;
        slotCenterScreenY = slotCenterY;

        // Determine left vs right placement relative to dockCardScreenRect (clamped inside work area)
        RECT work = GetMonitorWorkAreaForPoint(midPt);
        float workMaxBodyH = std::max(120.0f, (float)(work.bottom - work.top) / dpiScale - 110.0f);
        float targetMaxBodyH = std::min(Theme::PanelHeight() - 70.0f, workMaxBodyH);

        if (contentChanged || !bodyLayout || std::fabs(targetMaxBodyH - lastWorkMaxBodyH) > 2.0f) {
            titleText = item.title;
            bodyText = item.content;
            charCount = CountCodePoints(bodyText);
            lastWorkMaxBodyH = targetMaxBodyH;
            RebuildLayout(targetMaxBodyH);
        }

        int gapPx = (int)std::round(8.0f * dpiScale);
        int cardWPx = (int)std::ceil(cardWidthDip * dpiScale);
        int cardHPx = (int)std::ceil(cardHeightDip * dpiScale);
        int padPx = (int)std::round(kPad * dpiScale);

        int leftCandidateCardX = dockCardScreenRect.left - gapPx - cardWPx;
        int rightCandidateCardX = dockCardScreenRect.right + gapPx;
        int availLeft = dockCardScreenRect.left - gapPx - work.left;
        int availRight = work.right - (dockCardScreenRect.right + gapPx);

        int chosenCardX = leftCandidateCardX;
        if (leftCandidateCardX >= work.left + 6) {
            placeOnLeft = true;
            chosenCardX = leftCandidateCardX;
        } else if (rightCandidateCardX + cardWPx <= work.right - 6) {
            placeOnLeft = false;
            chosenCardX = rightCandidateCardX;
        } else if (availLeft >= availRight) {
            placeOnLeft = true;
            chosenCardX = std::clamp(leftCandidateCardX, (int)work.left + 4, std::max((int)work.left + 4, (int)work.right - cardWPx - 4));
        } else {
            placeOnLeft = false;
            chosenCardX = std::clamp(rightCandidateCardX, (int)work.left + 4, std::max((int)work.left + 4, (int)work.right - cardWPx - 4));
        }
        anchorScreenX = chosenCardX - padPx;

        int idealCardTopY = slotCenterY - cardHPx / 2;
        int clampedCardTopY = std::clamp(idealCardTopY, (int)work.top + 8, std::max((int)work.top + 8, (int)work.bottom - cardHPx - 8));
        int targetWinY = clampedCardTopY - padPx;

        bool reduced = Motion::IsReducedMotion();
        bool freshOpen = (!shown || alphaSpring.value <= 0.02f);
        if (freshOpen || reduced) {
            ySpring.Snap((float)targetWinY);
            heightSpring.Snap(cardHeightDip);
            contentAlpha = 1.0f;
            if (reduced) {
                alphaSpring.Snap(1.0f);
            } else {
                // Seed above RenderFrame's 0.004 threshold so the initial frame actually renders and resizes the HWND!
                if (alphaSpring.value <= 0.01f) alphaSpring.value = 0.05f;
                alphaSpring.target = 1.0f;
            }
        } else {
            ySpring.target = (float)targetWinY;
            heightSpring.target = cardHeightDip;
            if (contentChanged) contentAlpha = 0.14f;
            alphaSpring.target = 1.0f;
        }

        consecutiveRenderFails = 0;
        if (!shown) {
            if (!RenderFrame()) {
                currentSlot = -1;
                alphaSpring.Snap(0.0f);
                return;
            }
            shown = true;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        } else {
            if (RenderFrame()) {
                if (!IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                }
            }
        }
        if (!alphaSpring.IsSettled() || !ySpring.IsSettled() || !heightSpring.IsSettled() || contentAlpha < 0.995f) {
            StartTimer();
        } else {
            StopTimer();
        }
    }

private:
    void EnsureFormats() {
        if (fmtBody) return;
        auto& eng = D2DEngine::Instance();
        fmtSlotTag  = eng.CreateFormat(10.5f, DWRITE_FONT_WEIGHT_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtTitle    = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtMeta     = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtBody     = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL, false,
                                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (fmtBody) {
            fmtBody->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 19.5f, 15.2f);
        }
        fmtFootHint = eng.CreateFormat(10.5f, DWRITE_FONT_WEIGHT_MEDIUM, true, DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    void RebuildLayout(float maxBodyHCap = Theme::PanelHeight() - 70.0f) {
        bodyLayout.Reset();
        auto& eng = D2DEngine::Instance();
        if (!eng.dwFactory || !fmtBody) return;

        // Cap layout prefix for giant prompts (overflow indicator still shows exact full charCount)
        constexpr size_t kMaxLayoutUnits = 2400;
        size_t layoutLen = std::min(bodyText.size(), kMaxLayoutUnits);
        if (layoutLen < bodyText.size() && layoutLen > 0 &&
            bodyText[layoutLen - 1] >= 0xD800 && bodyText[layoutLen - 1] <= 0xDBFF) {
            --layoutLen;
        }
        bool prefixTruncated = (layoutLen < bodyText.size());

        float textMaxW = cardWidthDip - 28.0f;
        HRESULT hr = eng.dwFactory->CreateTextLayout(
            bodyText.c_str(),
            (UINT32)layoutLen,
            fmtBody.Get(),
            textMaxW,
            4096.0f,
            bodyLayout.GetAddressOf()
        );
        if (FAILED(hr) || !bodyLayout) return;

        DWRITE_TEXT_METRICS tm{};
        bodyLayout->GetMetrics(&tm);

        float maxBodyH = std::max(100.0f, maxBodyHCap);
        isOverflow = prefixTruncated || (tm.height > maxBodyH + 2.0f);
        bodyDrawHeightDip = isOverflow ? maxBodyH : std::max(22.0f, std::ceil(tm.height) + 2.0f);

        float headerH = 34.0f;
        float bottomPad = isOverflow ? 30.0f : 14.0f;
        cardHeightDip = headerH + 10.0f + bodyDrawHeightDip + bottomPad;
    }

    [[gnu::noinline]] void SnapOrHide() {
        StopTimer();
        if (alphaSpring.target == 0.0f) {
            HideImmediate();
        } else {
            alphaSpring.Snap(alphaSpring.target);
            ySpring.Snap(ySpring.target);
            heightSpring.Snap(heightSpring.target);
            contentAlpha = 1.0f;
            RenderFrame();
        }
    }

    void StartTimer() {
        if (!animActive) {
            QueryPerformanceCounter(&lastQpc);
            if (SetTimer(hwnd, kTimerAnim, 10, nullptr) != 0) {
                animActive = true;
            } else {
                SnapOrHide();
            }
        }
    }

    void StopTimer() {
        if (animActive) {
            KillTimer(hwnd, kTimerAnim);
            animActive = false;
        }
    }

    void OnTick() {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - lastQpc.QuadPart) / (float)qpcFreq.QuadPart;
        lastQpc = now;

        bool movingA = alphaSpring.Step(dt);
        bool movingY = ySpring.Step(dt, 0.25f, 1.5f);
        bool movingH = heightSpring.Step(dt, 0.25f, 1.5f);
        bool movingC = Motion::ExpApproach(contentAlpha, 1.0f, 22.0f, dt);

        if (alphaSpring.target == 0.0f && alphaSpring.value <= 0.008f) {
            HideImmediate();
            return;
        }

        if (RenderFrame()) {
            consecutiveRenderFails = 0;
            if (!movingA && !movingY && !movingH && !movingC) {
                StopTimer();
            }
        } else {
            ++consecutiveRenderFails;
            if (consecutiveRenderFails >= 4) {
                HideImmediate();
            }
        }
    }

    bool RenderFrame() {
        float aRaw = std::clamp(alphaSpring.value, 0.0f, 1.0f);
        if (aRaw <= 0.004f) return false;
        float a = Motion::EaseOutCubic(aRaw);
        float ca = a * std::clamp(contentAlpha, 0.0f, 1.0f);

        float drawCardH = std::max(60.0f, heightSpring.value);
        float totalWDip = cardWidthDip + kPad * 2.0f;
        float totalHDip = drawCardH + kPad * 2.0f;
        int wPx = (int)std::ceil(totalWDip * dpiScale);
        int hPx = (int)std::ceil(totalHDip * dpiScale);

        if (!canvas.Begin(wPx, hPx, dpiScale)) return false;
        auto* rt = canvas.rt.Get();

        D2D1_RECT_F box = D2D1::RectF(kPad, kPad, kPad + cardWidthDip, kPad + drawCardH);

        // Directional glowing tether notch aligned with hovered slot Y
        int curWinY = (int)std::round(ySpring.value);
        float localSlotY = (float)(slotCenterScreenY - curWinY) / dpiScale;
        float notchCenterY = std::clamp(localSlotY, box.top + 22.0f, std::max(box.top + 22.0f, box.bottom - 22.0f));
        float notchX = placeOnLeft ? (box.right - 1.5f) : (box.left - 1.0f);

        // Subtle scale anchored toward the dock tether notch for organic emerge/retreat
        if (!Motion::IsReducedMotion() && aRaw < 0.998f) {
            float sc = 0.968f + 0.032f * a;
            float anchorX = placeOnLeft ? box.right : box.left;
            rt->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc, D2D1::Point2F(anchorX, notchCenterY)) *
                             D2D1::Matrix3x2F::Scale(dpiScale, dpiScale));
        }

        auto* br = canvas.solidBrush.Get();
        // Ambient floating shadow
        DrawAmbientShadow(rt, br, box, 11.0f, a);

        // Deep obsidian frosted body (shared gradient helper)
        FillRoundedGradient(rt, box, 11.0f, Rgba(0x171B26, 0.97f * a), Rgba(0x0E1118, 0.98f * a));
        DrawTopLitBorder(rt, box, 11.0f, Rgba(0x93C5FD, 0.25f * a), Rgba(0xFFFFFF, 0.07f * a), 1.0f);

        FillRRHex(rt, br, D2D1::RectF(notchX, notchCenterY - 8.5f, notchX + 2.5f, notchCenterY + 8.5f), 1.2f, Theme::AccentCyan, 0.92f * a);

        // Header bar: Slot index pill + Full Title + Char count
        D2D1_RECT_F slotPill = D2D1::RectF(box.left + 12.0f, box.top + 9.0f, box.left + 38.0f, box.top + 27.0f);
        FillRRHex(rt, br, slotPill, 4.5f, Theme::AccentCyan, 0.15f * ca);
        if (currentSlot >= 0) {
            wchar_t sb[8]{};
            wsprintfW(sb, L"%02d", currentSlot + 1);
            DrawTextHex(rt, br, fmtSlotTag.Get(), slotPill, sb, 2, Theme::AccentCyan, 0.96f * ca);
        }

        wchar_t metaBuf[32]{};
        wsprintfW(metaBuf, L"%u \u5B57", (unsigned)charCount);
        std::wstring metaStr = UiText(metaBuf);
        DrawTextHex(rt, br, fmtMeta.Get(), D2D1::RectF(box.right - 74.0f, box.top + 7.0f, box.right - 13.0f, box.top + 29.0f),
                    metaStr.c_str(), (UINT32)metaStr.size(), Theme::TextMuted, 0.90f * ca);
        DrawTextHex(rt, br, fmtTitle.Get(), D2D1::RectF(slotPill.right + 8.0f, box.top + 7.0f, box.right - 78.0f, box.top + 29.0f),
                    titleText.c_str(), (UINT32)titleText.size(), Theme::TextPrimary, 0.98f * ca);

        // Subtle divider
        float divY = box.top + 35.0f;
        rt->DrawLine(D2D1::Point2F(box.left + 12.0f, divY), D2D1::Point2F(box.right - 12.0f, divY),
                     canvas.Brush(Rgba(0xFFFFFF, 0.06f * a)), 1.0f);

        // Full-text body layout (clipped cleanly to the live morphing box height)
        float bodyTop = divY + 9.0f;
        float maxClipBot = box.bottom - (isOverflow ? 30.0f : 12.0f);
        float clipBot = std::max(bodyTop + 4.0f, std::min(bodyTop + bodyDrawHeightDip, maxClipBot));
        D2D1_RECT_F bodyClip = D2D1::RectF(box.left + 14.0f, bodyTop, box.right - 14.0f, clipBot);
        rt->PushAxisAlignedClip(bodyClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (bodyLayout) {
            rt->DrawTextLayout(D2D1::Point2F(bodyClip.left, bodyClip.top), bodyLayout.Get(),
                               canvas.Brush(Rgba(0xE2E8F0, 0.94f * ca)));
        }
        if (isOverflow && (clipBot - bodyTop) > 20.0f) {
            D2D1_RECT_F fadeRect = D2D1::RectF(box.left + 2.0f, bodyClip.bottom - 42.0f, box.right - 2.0f, bodyClip.bottom + 1.0f);
            FillRoundedGradient(rt, fadeRect, 0.0f, Rgba(0x10131B, 0.0f), Rgba(0x0E1118, 0.98f * a));
        }
        rt->PopAxisAlignedClip();

        if (isOverflow && box.bottom - box.top > 72.0f) {
            std::wstring fhStr = UiText(L"\u4EC5\u663E\u793A\u524D\u6BB5 \u00B7 \u5DE6\u952E\u590D\u5236\u5168\u6587\xFF0C\x53F3\x952E\x7F16\x8F91");
            DrawTextHex(rt, br, fmtFootHint.Get(), D2D1::RectF(box.left + 12.0f, box.bottom - 26.0f, box.right - 12.0f, box.bottom - 6.0f),
                        fhStr.c_str(), (UINT32)fhStr.size(), Theme::AccentCyan, 0.88f * ca);
        }

        // Subtle horizontal micro-glide toward the dock as it fades in/out (zero shift in Reduced Motion)
        int glideOffsetPx = Motion::IsReducedMotion()
            ? 0
            : (int)std::round((placeOnLeft ? 6.0f : -6.0f) * (1.0f - a) * dpiScale);
        int winX = anchorScreenX + glideOffsetPx;
        if (!canvas.EndAndPresent(hwnd, winX, curWinY)) {
            DiagLogger::Instance().LogFmt("ERROR", "BUBBLE",
                L"HoverBubble RenderFrame failed: step=%hs hr=0x%08lX win32Err=%lu slot=%d pos=(%d,%d %dx%d) alpha=%.3f",
                canvas.lastFailStep ? canvas.lastFailStep : "unknown",
                (unsigned long)canvas.lastHr,
                canvas.lastWin32Err,
                currentSlot, winX, curWinY, wPx, hPx, a);
            return false;
        }
        return true;
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        HoverBubbleWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<HoverBubbleWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<HoverBubbleWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_NCHITTEST:
                return HTTRANSPARENT;
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_SETTINGCHANGE:
                Motion::RefreshSystemReducedMotion();
                if (Motion::IsReducedMotion() && self->shown) self->SnapOrHide();
                return 0;
            case WM_TIMER:
                if (wp == kTimerAnim) self->OnTick();
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

} // namespace pp
