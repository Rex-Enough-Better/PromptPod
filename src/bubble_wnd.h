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
// Features dock-edge-anchored 2D spring morphing (width + height + Y + tether notch) and dual-layer cross-fade.
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
    ComPtr<IDWriteTextLayout> prevBodyLayout;

    float dpiScale = 1.0f;
    Spring1D alphaSpring  = Spring1D::Snappy(0.0f);
    Spring1D ySpring      = Spring1D::Fluid(0.0f);
    Spring1D widthSpring  = Spring1D::Fluid(340.0f);
    Spring1D heightSpring = Spring1D::Fluid(120.0f);
    Spring1D notchYSpring = Spring1D::Fluid(0.0f);
    float contentBlend = 1.0f; // 0 -> 1 cross-fade from prev* snapshot to current*

    int currentSlot = -1;
    std::wstring currentId;
    std::wstring titleText;
    std::wstring bodyText;
    size_t charCount = 0;

    // Lightweight previous-slot snapshot for flicker-free cross-fade during slot transitions
    int prevSlot = -1;
    std::wstring prevTitleText;
    size_t prevCharCount = 0;
    bool prevIsOverflow = false;
    float prevBodyDrawHeightDip = 60.0f;

    bool placeOnLeft = true;
    bool isOverflow = false;
    float cardWidthDip = 340.0f;
    float cardHeightDip = 120.0f;
    float bodyDrawHeightDip = 60.0f;
    float lastWorkMaxBodyH = 0.0f;

    int dockAnchorCardEdgePx = 0; // Right card edge in screen px when placeOnLeft; left card edge when !placeOnLeft
    RECT workAreaScreen{};
    int slotCenterScreenY = 0;    // Hovered slot vertical center in screen px

    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastQpc{};
    bool animActive = false;
    bool shown = false;
    int consecutiveRenderFails = 0;

    static constexpr float kPad = 14.0f;
    static constexpr float kMinCardW = 248.0f;
    static constexpr float kMaxCardW = 360.0f;
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

        alphaSpring  = Spring1D::Snappy(0.0f);
        ySpring      = Spring1D::Fluid(0.0f);
        widthSpring  = Spring1D::Fluid(340.0f);
        heightSpring = Spring1D::Fluid(120.0f);
        notchYSpring = Spring1D::Fluid(0.0f);
        contentBlend = 1.0f;
        return true;
    }

    bool IsShowingOrAppearing() const {
        return shown && alphaSpring.target > 0.5f;
    }

    // True while visible or still noticeably on-screen during early fade-out (so re-hover morphs smoothly instead of re-debouncing)
    bool IsAliveForMorph() const {
        return shown && (alphaSpring.target > 0.5f || alphaSpring.value > 0.08f);
    }

    void ClearPrevSnapshot() {
        prevSlot = -1;
        prevTitleText.clear();
        prevCharCount = 0;
        prevIsOverflow = false;
        prevBodyDrawHeightDip = 60.0f;
        prevBodyLayout.Reset();
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
        ClearPrevSnapshot();
        consecutiveRenderFails = 0;
        alphaSpring.Snap(0.0f);
        contentBlend = 1.0f;
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
        // Keep currentSlot & content intact during smooth fade-out so mid-fade interruption knows what was on screen!
        alphaSpring.target = 0.0f;
        StartTimer();
    }

    void ShowForSlot(int slotIdx, const PromptItem& item, const RECT& dockCardScreenRect, int slotCenterY) {
        if (!hwnd) return;
        POINT midPt{ (dockCardScreenRect.left + dockCardScreenRect.right) / 2, slotCenterY };
        dpiScale = GetDpiScaleForPoint(midPt, hwnd);
        EnsureFormats();

        bool reduced = Motion::IsReducedMotion();
        bool freshOpen = !IsAliveForMorph();
        bool contentChanged = (currentSlot != slotIdx || currentId != item.id || bodyText != item.content || titleText != item.title);

        // Snapshot currently dominant visible content before replacing it for seamless cross-fade
        if (!freshOpen && !reduced && contentChanged && bodyLayout && currentSlot >= 0) {
            if (contentBlend >= 0.35f || !prevBodyLayout) {
                prevSlot = currentSlot;
                prevTitleText = titleText;
                prevCharCount = charCount;
                prevIsOverflow = isOverflow;
                prevBodyDrawHeightDip = bodyDrawHeightDip;
                prevBodyLayout = bodyLayout;
            }
            contentBlend = 0.0f;
        } else if (freshOpen || reduced) {
            ClearPrevSnapshot();
            contentBlend = 1.0f;
        }

        currentSlot = slotIdx;
        currentId = item.id;
        slotCenterScreenY = slotCenterY;

        // Determine left vs right placement relative to dockCardScreenRect (clamped inside work area)
        RECT work = GetMonitorWorkAreaForPoint(midPt);
        workAreaScreen = work;
        float workMaxBodyH = std::max(120.0f, (float)(work.bottom - work.top) / dpiScale - 110.0f);
        float targetMaxBodyH = std::min(Theme::PanelHeight() - 70.0f, workMaxBodyH);

        if (contentChanged || !bodyLayout || std::fabs(targetMaxBodyH - lastWorkMaxBodyH) > 2.0f) {
            titleText = item.title;
            bodyText = item.content;
            charCount = CountCodePoints(bodyText);
            lastWorkMaxBodyH = targetMaxBodyH;
            RebuildLayout(targetMaxBodyH);
        }

        // Pre-reserve high-water DIBSection capacity once for this monitor/DPI so morph never reallocates GDI bitmaps!
        int padPx = (int)std::round(kPad * dpiScale);
        int maxCapW = (int)std::ceil(kMaxCardW * dpiScale) + padPx * 2 + 4;
        int maxCapH = (int)std::ceil((targetMaxBodyH + 78.0f) * dpiScale) + padPx * 2 + 4;
        canvas.ReserveCapacity(maxCapW, maxCapH);

        int gapPx = (int)std::round(8.0f * dpiScale);
        int maxCardWPx = (int)std::round(kMaxCardW * dpiScale);
        int targetCardHPx = (int)std::round(cardHeightDip * dpiScale);

        int leftCandidateEdgeX = dockCardScreenRect.left - gapPx;
        int rightCandidateEdgeX = dockCardScreenRect.right + gapPx;
        int availLeft = leftCandidateEdgeX - work.left;
        int availRight = work.right - rightCandidateEdgeX;

        bool prevSide = placeOnLeft;
        if (leftCandidateEdgeX - maxCardWPx >= work.left + 6) {
            placeOnLeft = true;
            dockAnchorCardEdgePx = leftCandidateEdgeX;
        } else if (rightCandidateEdgeX + maxCardWPx <= work.right - 6) {
            placeOnLeft = false;
            dockAnchorCardEdgePx = rightCandidateEdgeX;
        } else if (availLeft >= availRight) {
            placeOnLeft = true;
            dockAnchorCardEdgePx = std::min(leftCandidateEdgeX, (int)work.right - 4);
        } else {
            placeOnLeft = false;
            dockAnchorCardEdgePx = std::max(rightCandidateEdgeX, (int)work.left + 4);
        }

        int idealCardTopY = slotCenterY - targetCardHPx / 2;
        int clampedCardTopY = std::clamp(idealCardTopY, (int)work.top + 8, std::max((int)work.top + 8, (int)work.bottom - targetCardHPx - 8));
        int targetWinY = clampedCardTopY - padPx;

        if (freshOpen || reduced || (shown && prevSide != placeOnLeft)) {
            ySpring.Snap((float)targetWinY);
            widthSpring.Snap(cardWidthDip);
            heightSpring.Snap(cardHeightDip);
            notchYSpring.Snap((float)slotCenterY);
            if (reduced) {
                alphaSpring.Snap(1.0f);
                contentBlend = 1.0f;
                ClearPrevSnapshot();
            } else {
                // Seed above RenderFrame's 0.004 threshold so the initial frame actually renders and resizes the HWND!
                if (alphaSpring.value <= 0.01f) alphaSpring.value = 0.06f;
                alphaSpring.target = 1.0f;
            }
        } else {
            ySpring.target = (float)targetWinY;
            widthSpring.target = cardWidthDip;
            heightSpring.target = cardHeightDip;
            notchYSpring.target = (float)slotCenterY;
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
        if (!alphaSpring.IsSettled() || !ySpring.IsSettled() || !widthSpring.IsSettled() ||
            !heightSpring.IsSettled() || !notchYSpring.IsSettled() || contentBlend < 0.995f) {
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

        float maxBodyH = std::max(100.0f, maxBodyHCap);
        float maxTextW = kMaxCardW - 28.0f;
        HRESULT hr = eng.dwFactory->CreateTextLayout(
            bodyText.c_str(),
            (UINT32)layoutLen,
            fmtBody.Get(),
            maxTextW,
            4096.0f,
            bodyLayout.GetAddressOf()
        );
        if (FAILED(hr) || !bodyLayout) return;

        DWRITE_TEXT_METRICS tm{};
        bodyLayout->GetMetrics(&tm);

        // Measure title natural width so compact short prompts scale neatly without clipping the header
        float titleNatW = 90.0f;
        if (fmtTitle && !titleText.empty()) {
            ComPtr<IDWriteTextLayout> titleLayout;
            if (SUCCEEDED(eng.dwFactory->CreateTextLayout(titleText.c_str(), (UINT32)titleText.size(),
                                                          fmtTitle.Get(), kMaxCardW, 28.0f, titleLayout.GetAddressOf())) && titleLayout) {
                DWRITE_TEXT_METRICS ttm{};
                if (SUCCEEDED(titleLayout->GetMetrics(&ttm))) {
                    titleNatW = std::min(kMaxCardW - 120.0f, ttm.widthIncludingTrailingWhitespace);
                }
            }
        }
        float headerNeededW = 128.0f + titleNatW;
        float bodyNeededW = tm.widthIncludingTrailingWhitespace + 34.0f;

        bool ovfAtMax = prefixTruncated || (tm.height > maxBodyH + 2.0f);
        if (ovfAtMax) {
            cardWidthDip = kMaxCardW;
        } else {
            float minFloor = (tm.lineCount >= 4) ? 304.0f : kMinCardW;
            cardWidthDip = std::clamp(std::max(headerNeededW, bodyNeededW), minFloor, kMaxCardW);
            if (cardWidthDip < kMaxCardW - 1.5f) {
                bodyLayout->SetMaxWidth(cardWidthDip - 28.0f);
                bodyLayout->GetMetrics(&tm);
                if (tm.height > maxBodyH + 2.0f) {
                    cardWidthDip = kMaxCardW;
                    bodyLayout->SetMaxWidth(maxTextW);
                    bodyLayout->GetMetrics(&tm);
                }
            }
        }

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
            widthSpring.Snap(widthSpring.target);
            heightSpring.Snap(heightSpring.target);
            notchYSpring.Snap(notchYSpring.target);
            contentBlend = 1.0f;
            ClearPrevSnapshot();
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
        bool movingW = widthSpring.Step(dt, 0.25f, 1.5f);
        bool movingH = heightSpring.Step(dt, 0.25f, 1.5f);
        bool movingN = notchYSpring.Step(dt, 0.25f, 1.5f);
        bool movingC = Motion::ExpApproach(contentBlend, 1.0f, 24.0f, dt, 0.008f);
        if (!movingC && prevBodyLayout) {
            ClearPrevSnapshot();
        }

        if (alphaSpring.target == 0.0f && alphaSpring.value <= 0.008f) {
            HideImmediate();
            return;
        }

        if (RenderFrame()) {
            consecutiveRenderFails = 0;
            if (!movingA && !movingY && !movingW && !movingH && !movingN && !movingC) {
                StopTimer();
            }
        } else {
            ++consecutiveRenderFails;
            if (consecutiveRenderFails >= 4) {
                HideImmediate();
            }
        }
    }

    void DrawContentLayer(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const D2D1_RECT_F& box,
                          int sl, const std::wstring& ttl, size_t chars, IDWriteTextLayout* layout,
                          bool ovf, float bodyH, float layerA, float frameA) {
        if (layerA <= 0.01f) return;

        // Clip header strictly inside morphing card width so wider previous text never spills out while shrinking
        D2D1_RECT_F hdrClip = D2D1::RectF(box.left + 6.0f, box.top + 2.0f, box.right - 6.0f, box.top + 34.0f);
        rt->PushAxisAlignedClip(hdrClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        D2D1_RECT_F slotPill = D2D1::RectF(box.left + 12.0f, box.top + 9.0f, box.left + 38.0f, box.top + 27.0f);
        FillRRHex(rt, br, slotPill, 4.5f, Theme::AccentCyan, 0.15f * layerA);
        if (sl >= 0) {
            wchar_t sb[8]{};
            wsprintfW(sb, L"%02d", sl + 1);
            DrawTextHex(rt, br, fmtSlotTag.Get(), slotPill, sb, 2, Theme::AccentCyan, 0.96f * layerA);
        }

        wchar_t metaBuf[32]{};
        wsprintfW(metaBuf, L"%u \u5B57", (unsigned)chars);
        std::wstring metaStr = UiText(metaBuf);
        DrawTextHex(rt, br, fmtMeta.Get(), D2D1::RectF(box.right - 74.0f, box.top + 7.0f, box.right - 13.0f, box.top + 29.0f),
                    metaStr.c_str(), (UINT32)metaStr.size(), Theme::TextMuted, 0.90f * layerA);
        if (box.right - 78.0f > slotPill.right + 12.0f) {
            DrawTextHex(rt, br, fmtTitle.Get(), D2D1::RectF(slotPill.right + 8.0f, box.top + 7.0f, box.right - 78.0f, box.top + 29.0f),
                        ttl.c_str(), (UINT32)ttl.size(), Theme::TextPrimary, 0.98f * layerA);
        }
        rt->PopAxisAlignedClip();

        float divY = box.top + 35.0f;
        float bodyTop = divY + 9.0f;
        float maxClipBot = box.bottom - (ovf ? 30.0f : 12.0f);
        float clipBot = std::max(bodyTop + 4.0f, std::min(bodyTop + bodyH, maxClipBot));
        if (box.right - 14.0f > box.left + 14.0f && clipBot > bodyTop + 2.0f) {
            D2D1_RECT_F bodyClip = D2D1::RectF(box.left + 14.0f, bodyTop, box.right - 14.0f, clipBot);
            rt->PushAxisAlignedClip(bodyClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (layout) {
                rt->DrawTextLayout(D2D1::Point2F(bodyClip.left, bodyClip.top), layout,
                                   canvas.Brush(Rgba(0xE2E8F0, 0.94f * layerA)));
            }
            if (ovf && (clipBot - bodyTop) > 20.0f) {
                D2D1_RECT_F fadeRect = D2D1::RectF(box.left + 2.0f, bodyClip.bottom - 42.0f, box.right - 2.0f, bodyClip.bottom + 1.0f);
                FillRoundedGradient(rt, fadeRect, 0.0f, Rgba(0x10131B, 0.0f), Rgba(0x0E1118, 0.98f * frameA * layerA));
            }
            rt->PopAxisAlignedClip();
        }

        if (ovf && box.bottom - box.top > 72.0f && box.right - box.left > 160.0f) {
            std::wstring fhStr = UiText(L"\u4EC5\u663E\u793A\u524D\u6BB5 \u00B7 \u5DE6\u952E\u590D\u5236\u5168\u6587\xFF0C\x53F3\x952E\x7F16\x8F91");
            DrawTextHex(rt, br, fmtFootHint.Get(), D2D1::RectF(box.left + 12.0f, box.bottom - 26.0f, box.right - 12.0f, box.bottom - 6.0f),
                        fhStr.c_str(), (UINT32)fhStr.size(), Theme::AccentCyan, 0.88f * layerA);
        }
    }

    bool RenderFrame() {
        float aRaw = std::clamp(alphaSpring.value, 0.0f, 1.0f);
        if (aRaw <= 0.004f) return false;
        float a = Motion::EaseOutCubic(aRaw);

        // Snap card width & height to exact device pixels so the dock-facing edge stays 0px jitter-free during 2D morph!
        int padPx = (int)std::round(kPad * dpiScale);
        float padDip = (float)padPx / dpiScale;
        int cardWPx = std::max(80, (int)std::round(std::clamp(widthSpring.value, kMinCardW * 0.85f, kMaxCardW) * dpiScale));
        int cardHPx = std::max(60, (int)std::round(std::max(60.0f, heightSpring.value) * dpiScale));
        float alignedW = (float)cardWPx / dpiScale;
        float alignedH = (float)cardHPx / dpiScale;
        int wPx = cardWPx + padPx * 2;
        int hPx = cardHPx + padPx * 2;

        if (!canvas.Begin(wPx, hPx, dpiScale)) return false;
        auto* rt = canvas.rt.Get();

        D2D1_RECT_F box = D2D1::RectF(padDip, padDip, padDip + alignedW, padDip + alignedH);

        // Directional glowing tether notch smoothly glides with notchYSpring
        int curWinY = (int)std::round(ySpring.value);
        float localSlotY = (notchYSpring.value - (float)curWinY) / dpiScale;
        float notchCenterY = std::clamp(localSlotY, box.top + 22.0f, std::max(box.top + 22.0f, box.bottom - 22.0f));
        float notchX = placeOnLeft ? (box.right - 1.5f) : (box.left - 1.0f);

        // Subtle scale anchored toward the dock tether notch ONLY on emerge/retreat (1.0 during live morph!)
        if (!Motion::IsReducedMotion() && aRaw < 0.996f) {
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

        // Subtle divider (stays solid across slot cross-fades -> zero frame blink!)
        float divY = box.top + 35.0f;
        rt->DrawLine(D2D1::Point2F(box.left + 12.0f, divY), D2D1::Point2F(box.right - 12.0f, divY),
                     canvas.Brush(Rgba(0xFFFFFF, 0.06f * a)), 1.0f);

        // Dual-layer seamless cross-fade: outgoing snapshot fades out while incoming morphs in cleanly
        float blend = std::clamp(contentBlend, 0.0f, 1.0f);
        if (blend < 0.992f && prevSlot >= 0 && prevBodyLayout) {
            float oldA = std::clamp(1.0f - Motion::EaseOutCubic(std::clamp(blend * 1.55f, 0.0f, 1.0f)), 0.0f, 1.0f) * a;
            float newA = Motion::EaseOutCubic(std::clamp((blend - 0.05f) / 0.95f, 0.0f, 1.0f)) * a;
            DrawContentLayer(rt, br, box, prevSlot, prevTitleText, prevCharCount, prevBodyLayout.Get(),
                             prevIsOverflow, prevBodyDrawHeightDip, oldA, a);
            DrawContentLayer(rt, br, box, currentSlot, titleText, charCount, bodyLayout.Get(),
                             isOverflow, bodyDrawHeightDip, newA, a);
        } else {
            DrawContentLayer(rt, br, box, currentSlot, titleText, charCount, bodyLayout.Get(),
                             isOverflow, bodyDrawHeightDip, a, a);
        }

        // Compute dock-edge-locked screen X so width morph expands/contracts purely away from the dock!
        int rawCardLeftPx = placeOnLeft ? (dockAnchorCardEdgePx - cardWPx) : dockAnchorCardEdgePx;
        int minLeftPx = (workAreaScreen.right > workAreaScreen.left) ? ((int)workAreaScreen.left + 4) : rawCardLeftPx;
        int maxLeftPx = (workAreaScreen.right > workAreaScreen.left)
            ? std::max(minLeftPx, (int)workAreaScreen.right - cardWPx - 4)
            : rawCardLeftPx;
        int clampedCardLeftPx = std::clamp(rawCardLeftPx, minLeftPx, maxLeftPx);
        int glideOffsetPx = Motion::IsReducedMotion()
            ? 0
            : (int)std::round((placeOnLeft ? 6.0f : -6.0f) * (1.0f - a) * dpiScale);
        int winX = clampedCardLeftPx - padPx + glideOffsetPx;

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
