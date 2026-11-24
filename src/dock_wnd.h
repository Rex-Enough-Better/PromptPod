#pragma once

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include "version.h"
#include "model.h"
#include "theme.h"
#include "d2d_ctx.h"
#include "dark_menu.h"
#include "vault_wnd.h"
#include "bubble_wnd.h"

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#ifndef WM_DWMCOMPOSITIONCHANGED
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#endif
#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE 0x02B1
#endif
#ifndef WTS_CONSOLE_CONNECT
#define WTS_CONSOLE_CONNECT 0x1
#define WTS_CONSOLE_DISCONNECT 0x2
#define WTS_REMOTE_CONNECT 0x3
#define WTS_REMOTE_DISCONNECT 0x4
#define WTS_SESSION_LOCK 0x7
#define WTS_SESSION_UNLOCK 0x8
#endif
#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION 0
#endif

namespace pp {

enum class OnboardingStep : int {
    Inactive = 0,
    Step1Copy = 1,
    Step2Create = 2,
    Step3Prefs = 3
};

// Lightweight non-focus-stealing D2D floating 3-step edge onboarding card
class OnboardingGuideWindow {
public:
    HWND hwnd = nullptr;
    LayeredCanvas canvas;
    ComPtr<IDWriteTextFormat> fmtStep;
    ComPtr<IDWriteTextFormat> fmtTitle;
    ComPtr<IDWriteTextFormat> fmtBody;
    ComPtr<IDWriteTextFormat> fmtBtn;
    ComPtr<IDWriteTextFormat> fmtLabel;

    OnboardingStep step = OnboardingStep::Inactive;
    int targetSlot = 2; // Default Slot 03 (0-based index 2)
    bool step1Done = false;
    bool step2Done = false;
    bool shown = false;
    float dpiScale = 1.0f;
    int winX = 0;
    int winY = 0;
    bool placeOnLeft = true;
    int hoverZone = 0;   // 1:Skip, 2:Next/SkipStep, 3:CreateBtn, 4:RightEdge, 5:LeftEdge, 6:AutoStart, 7:Finish
    int pressedZone = 0;
    bool trackingLeave = false;

    const AppState* state = nullptr;
    std::function<void()> onSkipOrFinish;
    std::function<void(int)> onRequestCreatePrompt;
    std::function<void(DockEdge)> onSelectEdge;
    std::function<void()> onToggleAutoStart;
    std::function<void()> onStepChanged;

    D2D1_RECT_F rcSkip{};
    D2D1_RECT_F rcNextLink{};
    D2D1_RECT_F rcPrimaryBtn{};
    D2D1_RECT_F rcEdgeRight{};
    D2D1_RECT_F rcEdgeLeft{};
    D2D1_RECT_F rcAutoStartRow{};

    static constexpr float kPad = 14.0f;
    static constexpr float kCardW = 286.0f;
    static constexpr float kCardH = 222.0f;
    static constexpr UINT_PTR kTimerAdvance = 6201;

    bool Init(HINSTANCE hInst, const AppState* st) {
        state = st;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptPod_OnboardingWnd";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName,
            L"",
            WS_POPUP,
            0, 0, 10, 10,
            nullptr, nullptr, hInst, this
        );
        return hwnd != nullptr;
    }

    bool IsActive() const {
        return step != OnboardingStep::Inactive;
    }

    void Start(int preferredEmptySlot) {
        targetSlot = (preferredEmptySlot >= 0 && preferredEmptySlot < 10) ? preferredEmptySlot : 2;
        step = OnboardingStep::Step1Copy;
        step1Done = false;
        step2Done = false;
        hoverZone = 0;
        pressedZone = 0;
        if (hwnd) KillTimer(hwnd, kTimerAdvance);
    }

    void Hide() {
        step = OnboardingStep::Inactive;
        step1Done = false;
        step2Done = false;
        hoverZone = 0;
        pressedZone = 0;
        if (hwnd) {
            KillTimer(hwnd, kTimerAdvance);
            if (shown) {
                ShowWindow(hwnd, SW_HIDE);
                shown = false;
            }
        }
    }

    void NotifyCopied() {
        if (step != OnboardingStep::Step1Copy || step1Done) return;
        step1Done = true;
        if (hwnd) SetTimer(hwnd, kTimerAdvance, 780, nullptr);
        Render();
    }

    void NotifyPromptCreated(int mountedSlot) {
        if (step != OnboardingStep::Step2Create || step2Done) return;
        if (mountedSlot >= 0 && mountedSlot < 10) targetSlot = mountedSlot;
        step2Done = true;
        if (hwnd) SetTimer(hwnd, kTimerAdvance, 820, nullptr);
        Render();
    }

    void SyncPosition(const RECT& dockCardScreenRect, const RECT& work, float scale, bool allowShow = true) {
        if (!hwnd || !IsActive()) return;
        dpiScale = (scale > 0.1f) ? scale : 1.0f;
        EnsureFormats();

        int gapPx = (int)std::round(10.0f * dpiScale);
        int cardWPx = (int)std::ceil(kCardW * dpiScale);
        int cardHPx = (int)std::ceil(kCardH * dpiScale);
        int padPx = (int)std::round(kPad * dpiScale);

        int leftX = dockCardScreenRect.left - gapPx - cardWPx;
        int rightX = dockCardScreenRect.right + gapPx;
        int chosenCardX = leftX;
        if (leftX >= work.left + 6) {
            placeOnLeft = true;
            chosenCardX = leftX;
        } else if (rightX + cardWPx <= work.right - 6) {
            placeOnLeft = false;
            chosenCardX = rightX;
        } else {
            placeOnLeft = (dockCardScreenRect.left - work.left) >= (work.right - dockCardScreenRect.right);
            chosenCardX = std::clamp(placeOnLeft ? leftX : rightX,
                                     (int)work.left + 6,
                                     std::max((int)work.left + 6, (int)work.right - cardWPx - 6));
        }

        int idealTopY = dockCardScreenRect.top + (int)std::round(24.0f * dpiScale);
        int clampedTopY = std::clamp(idealTopY, (int)work.top + 8, std::max((int)work.top + 8, (int)work.bottom - cardHPx - 8));

        winX = chosenCardX - padPx;
        winY = clampedTopY - padPx;

        if (Render() && allowShow) {
            if (!shown) {
                shown = true;
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        }
    }

    bool Render() {
        if (!hwnd || !IsActive()) return false;
        EnsureFormats();
        int wPx = (int)std::ceil((kCardW + kPad * 2.0f) * dpiScale);
        int hPx = (int)std::ceil((kCardH + kPad * 2.0f) * dpiScale);
        if (!canvas.Begin(wPx, hPx, dpiScale)) return false;
        auto* rt = canvas.rt.Get();

        D2D1_RECT_F box = D2D1::RectF(kPad, kPad, kPad + kCardW, kPad + kCardH);
        DrawAmbientShadow(rt, canvas.Brush(D2D1::ColorF(0, 0, 0, 1)), box, 12.0f, 1.0f);

        // Obsidian frosted body
        {
            D2D1_GRADIENT_STOP stops[2] = {
                { 0.0f, Rgba(0x181D29, 0.98f) },
                { 1.0f, Rgba(0x0E1118, 0.98f) }
            };
            ComPtr<ID2D1GradientStopCollection> coll;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) {
                ComPtr<ID2D1LinearGradientBrush> grad;
                rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(box.left, box.top), D2D1::Point2F(box.left, box.bottom)),
                    coll.Get(), grad.GetAddressOf());
                if (grad) rt->FillRoundedRectangle(D2D1::RoundedRect(box, 12.0f, 12.0f), grad.Get());
            }
        }
        DrawRim(rt, box, 12.0f, Rgba(Theme::AccentCyan, 0.42f), Rgba(0xFFFFFF, 0.08f), 1.1f);

        // Directional anchor notch pointing toward DockWindow
        float notchX = placeOnLeft ? (box.right - 1.5f) : (box.left - 1.0f);
        D2D1_RECT_F notch = D2D1::RectF(notchX, box.top + 34.0f, notchX + 2.5f, box.top + 56.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(notch, 1.2f, 1.2f), canvas.Brush(Rgba(Theme::AccentCyan, 0.95f)));

        // Reset interactive rects
        rcSkip = D2D1::RectF(box.right - 56.0f, box.top + 11.0f, box.right - 12.0f, box.top + 33.0f);
        rcNextLink = D2D1::RectF(0, 0, 0, 0);
        rcPrimaryBtn = D2D1::RectF(0, 0, 0, 0);
        rcEdgeRight = D2D1::RectF(0, 0, 0, 0);
        rcEdgeLeft = D2D1::RectF(0, 0, 0, 0);
        rcAutoStartRow = D2D1::RectF(0, 0, 0, 0);

        // Top step progress pills + label
        int stepNum = (int)step;
        for (int s = 1; s <= 3; ++s) {
            float px = box.left + 14.0f + (float)(s - 1) * 24.0f;
            D2D1_RECT_F dot = D2D1::RectF(px, box.top + 19.0f, px + 18.0f, box.top + 23.5f);
            D2D1_COLOR_F col = (s < stepNum) ? Rgba(Theme::CopyGreen, 0.92f)
                             : (s == stepNum ? Rgba(Theme::AccentCyan, 0.98f) : Rgba(0xFFFFFF, 0.14f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(dot, 2.2f, 2.2f), canvas.Brush(col));
        }
        wchar_t sBuf[32]{};
        wsprintfW(sBuf, L"\x6B65\x9AA4 %d / 3", stepNum);
        std::wstring sLbl = UiText(sBuf);
        if (fmtStep) {
            rt->DrawTextW(sLbl.c_str(), (UINT32)sLbl.size(), fmtStep.Get(),
                          D2D1::RectF(box.left + 92.0f, box.top + 11.0f, rcSkip.left - 4.0f, box.top + 33.0f),
                          canvas.Brush(Rgba(Theme::AccentCyan, 0.90f)));
        }

        // Skip button
        if (hoverZone == 1) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcSkip, 5.0f, 5.0f), canvas.Brush(Rgba(0xFFFFFF, 0.08f)));
        }
        std::wstring skipStr = UiText(L"跳过");
        if (fmtBtn) {
            rt->DrawTextW(skipStr.c_str(), (UINT32)skipStr.size(), fmtBtn.Get(), rcSkip,
                          canvas.Brush(Rgba(hoverZone == 1 ? Theme::TextPrimary : Theme::TextMuted, 0.95f)));
        }

        // Divider
        rt->DrawLine(D2D1::Point2F(box.left + 14.0f, box.top + 38.0f), D2D1::Point2F(box.right - 14.0f, box.top + 38.0f),
                     canvas.Brush(Rgba(0xFFFFFF, 0.06f)), 1.0f);

        auto drawPill = [&](const D2D1_RECT_F& r, const std::wstring& txt, bool hov, uint32_t bg, uint32_t bgHov, uint32_t fg, float rim = 0.20f) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, 7.0f, 7.0f), canvas.Brush(Rgba(hov ? bgHov : bg, 1.0f)));
            DrawRim(rt, r, 7.0f, Rgba(0xFFFFFF, hov ? rim + 0.12f : rim), Rgba(0xFFFFFF, 0.05f));
            std::wstring u = UiText(txt);
            if (fmtBtn) {
                rt->DrawTextW(u.c_str(), (UINT32)u.size(), fmtBtn.Get(), r, canvas.Brush(Rgba(fg, 0.98f)));
            }
        };

        wchar_t slotBuf[8]{};
        wsprintfW(slotBuf, L"%02d", targetSlot + 1);

        if (step == OnboardingStep::Step1Copy) {
            std::wstring title = UiText(L"试一次零焦点复制");
            std::wstring body = UiText(L"点击旁侧台面「01」槽位，立即将完整 Prompt 写入剪贴板，原编辑器或浏览器光标绝不丢失。");
            if (fmtTitle) {
                rt->DrawTextW(title.c_str(), (UINT32)title.size(), fmtTitle.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 46.0f, box.right - 14.0f, box.top + 70.0f),
                              canvas.Brush(Rgba(Theme::TextPrimary, 0.98f)));
            }
            if (fmtBody) {
                rt->DrawTextW(body.c_str(), (UINT32)body.size(), fmtBody.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 74.0f, box.right - 14.0f, box.top + 138.0f),
                              canvas.Brush(Rgba(Theme::TextSecondary, 0.94f)));
            }
            if (step1Done) {
                D2D1_RECT_F okBox = D2D1::RectF(box.left + 14.0f, box.bottom - 48.0f, box.right - 14.0f, box.bottom - 14.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(okBox, 7.0f, 7.0f), canvas.Brush(Rgba(0x14533D, 0.95f)));
                DrawRim(rt, okBox, 7.0f, Rgba(Theme::CopyGreen, 0.55f), Rgba(0xFFFFFF, 0.06f));
                std::wstring okTxt = UiText(L"✓ 已完成零焦点复制，进入下一步...");
                if (fmtBtn) {
                    rt->DrawTextW(okTxt.c_str(), (UINT32)okTxt.size(), fmtBtn.Get(), okBox, canvas.Brush(Rgba(Theme::CopyGreen, 1.0f)));
                }
            } else {
                D2D1_RECT_F hintPill = D2D1::RectF(box.left + 14.0f, box.bottom - 46.0f, box.right - 92.0f, box.bottom - 14.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(hintPill, 7.0f, 7.0f), canvas.Brush(Rgba(Theme::AccentCyan, 0.12f)));
                std::wstring ht = UiText(L"请点击台面 01 槽位");
                if (fmtBtn) {
                    rt->DrawTextW(ht.c_str(), (UINT32)ht.size(), fmtBtn.Get(), hintPill, canvas.Brush(Rgba(Theme::AccentCyan, 0.96f)));
                }
                rcNextLink = D2D1::RectF(box.right - 84.0f, box.bottom - 46.0f, box.right - 14.0f, box.bottom - 14.0f);
                drawPill(rcNextLink, L"下一步", hoverZone == 2, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.12f);
            }
        } else if (step == OnboardingStep::Step2Create) {
            std::wstring title = UiText(L"存入你的第一条 Prompt");
            std::wstring body = UiText(L"台面已为你留出 03 ~ 10 空位。点击高亮空槽 " + std::wstring(slotBuf) + L" 或下方按钮，存入你最常用的提示词。");
            if (fmtTitle) {
                rt->DrawTextW(title.c_str(), (UINT32)title.size(), fmtTitle.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 46.0f, box.right - 14.0f, box.top + 70.0f),
                              canvas.Brush(Rgba(Theme::TextPrimary, 0.98f)));
            }
            if (fmtBody) {
                rt->DrawTextW(body.c_str(), (UINT32)body.size(), fmtBody.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 74.0f, box.right - 14.0f, box.top + 138.0f),
                              canvas.Brush(Rgba(Theme::TextSecondary, 0.94f)));
            }
            if (step2Done) {
                D2D1_RECT_F okBox = D2D1::RectF(box.left + 14.0f, box.bottom - 48.0f, box.right - 14.0f, box.bottom - 14.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(okBox, 7.0f, 7.0f), canvas.Brush(Rgba(0x14533D, 0.95f)));
                DrawRim(rt, okBox, 7.0f, Rgba(Theme::CopyGreen, 0.55f), Rgba(0xFFFFFF, 0.06f));
                std::wstring okTxt = UiText(L"✓ 已放入槽位 " + std::wstring(slotBuf) + L"，进入下一步...");
                if (fmtBtn) {
                    rt->DrawTextW(okTxt.c_str(), (UINT32)okTxt.size(), fmtBtn.Get(), okBox, canvas.Brush(Rgba(Theme::CopyGreen, 1.0f)));
                }
            } else {
                rcPrimaryBtn = D2D1::RectF(box.left + 14.0f, box.bottom - 46.0f, box.right - 98.0f, box.bottom - 14.0f);
                drawPill(rcPrimaryBtn, L"+ 存入槽位 " + std::wstring(slotBuf), hoverZone == 3, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.28f);
                rcNextLink = D2D1::RectF(box.right - 90.0f, box.bottom - 46.0f, box.right - 14.0f, box.bottom - 14.0f);
                drawPill(rcNextLink, L"暂不添加", hoverZone == 2, 0x1D2230, 0x282F42, Theme::TextSecondary, 0.12f);
            }
        } else if (step == OnboardingStep::Step3Prefs) {
            std::wstring title = UiText(L"顺手设置习惯");
            if (fmtTitle) {
                rt->DrawTextW(title.c_str(), (UINT32)title.size(), fmtTitle.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 45.0f, box.right - 14.0f, box.top + 68.0f),
                              canvas.Brush(Rgba(Theme::TextPrimary, 0.98f)));
            }

            // Edge selector row
            std::wstring edgeLbl = UiText(L"贴边方向");
            if (fmtLabel) {
                rt->DrawTextW(edgeLbl.c_str(), (UINT32)edgeLbl.size(), fmtLabel.Get(),
                              D2D1::RectF(box.left + 14.0f, box.top + 76.0f, box.left + 92.0f, box.top + 104.0f),
                              canvas.Brush(Rgba(Theme::TextSecondary, 0.94f)));
            }
            bool isRight = (!state || state->dockEdge != DockEdge::Left);
            rcEdgeRight = D2D1::RectF(box.right - 166.0f, box.top + 76.0f, box.right - 92.0f, box.top + 104.0f);
            rcEdgeLeft  = D2D1::RectF(box.right - 88.0f,  box.top + 76.0f, box.right - 14.0f, box.top + 104.0f);
            drawPill(rcEdgeRight, L"右侧贴边", hoverZone == 4, isRight ? 0x1E3A5F : 0x1B1F2B, isRight ? 0x264B7A : 0x262C3D, isRight ? Theme::AccentCyan : Theme::TextSecondary, isRight ? 0.35f : 0.10f);
            drawPill(rcEdgeLeft,  L"左侧贴边", hoverZone == 5, !isRight ? 0x1E3A5F : 0x1B1F2B, !isRight ? 0x264B7A : 0x262C3D, !isRight ? Theme::AccentCyan : Theme::TextSecondary, !isRight ? 0.35f : 0.10f);

            // Auto-start toggle row
            rcAutoStartRow = D2D1::RectF(box.left + 14.0f, box.top + 114.0f, box.right - 14.0f, box.top + 148.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcAutoStartRow, 7.0f, 7.0f),
                                     canvas.Brush(Rgba(hoverZone == 6 ? 0x222838 : 0x181C26, 0.95f)));
            DrawRim(rt, rcAutoStartRow, 7.0f, Rgba(0xFFFFFF, 0.08f), Rgba(0xFFFFFF, 0.03f));
            std::wstring autoLbl = UiText(L"开机自动启动（静默贴边待命）");
            if (fmtLabel) {
                rt->DrawTextW(autoLbl.c_str(), (UINT32)autoLbl.size(), fmtLabel.Get(),
                              D2D1::RectF(rcAutoStartRow.left + 10.0f, rcAutoStartRow.top, rcAutoStartRow.right - 56.0f, rcAutoStartRow.bottom),
                              canvas.Brush(Rgba(Theme::TextPrimary, 0.92f)));
            }
            bool autoOn = AutoStart::IsEnabled();
            D2D1_RECT_F sw = D2D1::RectF(rcAutoStartRow.right - 48.0f, rcAutoStartRow.top + 7.0f, rcAutoStartRow.right - 10.0f, rcAutoStartRow.bottom - 7.0f);
            float rad = (sw.bottom - sw.top) * 0.5f;
            rt->FillRoundedRectangle(D2D1::RoundedRect(sw, rad, rad), canvas.Brush(Rgba(autoOn ? 0x3B7BF6 : 0x131722, 1.0f)));
            DrawRim(rt, sw, rad, Rgba(0xFFFFFF, autoOn ? 0.25f : 0.12f), Rgba(0xFFFFFF, 0.04f));
            float knobR = rad - 2.6f;
            float knobX = autoOn ? (sw.right - rad) : (sw.left + rad);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, (sw.top + sw.bottom) * 0.5f), knobR, knobR),
                            canvas.Brush(Rgba(autoOn ? 0xFFFFFF : 0x94A3B8, 1.0f)));

            // Primary finish button
            rcPrimaryBtn = D2D1::RectF(box.left + 14.0f, box.bottom - 46.0f, box.right - 14.0f, box.bottom - 14.0f);
            drawPill(rcPrimaryBtn, L"开始使用（收起至边缘微光把手）", hoverZone == 7, 0x366EF0, 0x4B82F8, 0xFFFFFF, 0.28f);
        }

        return canvas.EndAndPresent(hwnd, winX, winY);
    }

private:
    void EnsureFormats() {
        if (fmtTitle) return;
        auto& eng = D2DEngine::Instance();
        fmtStep  = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        fmtTitle = eng.CreateFormat(14.5f, DWRITE_FONT_WEIGHT_BOLD);
        fmtBody  = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL, false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (fmtBody) fmtBody->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 18.5f, 14.5f);
        fmtBtn   = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtLabel = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_MEDIUM);
    }

    static bool PtInDip(const D2D1_RECT_F& r, float x, float y) {
        return r.right > r.left && r.bottom > r.top && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }

    int HitZone(float x, float y) const {
        if (PtInDip(rcSkip, x, y)) return 1;
        if (step == OnboardingStep::Step1Copy) {
            if (PtInDip(rcNextLink, x, y)) return 2;
        } else if (step == OnboardingStep::Step2Create) {
            if (PtInDip(rcPrimaryBtn, x, y)) return 3;
            if (PtInDip(rcNextLink, x, y)) return 2;
        } else if (step == OnboardingStep::Step3Prefs) {
            if (PtInDip(rcEdgeRight, x, y)) return 4;
            if (PtInDip(rcEdgeLeft, x, y)) return 5;
            if (PtInDip(rcAutoStartRow, x, y)) return 6;
            if (PtInDip(rcPrimaryBtn, x, y)) return 7;
        }
        return 0;
    }

    void AdvanceStep() {
        if (step == OnboardingStep::Step1Copy) {
            step = OnboardingStep::Step2Create;
            step2Done = false;
        } else if (step == OnboardingStep::Step2Create) {
            step = OnboardingStep::Step3Prefs;
        }
        hoverZone = 0;
        pressedZone = 0;
        Render();
        if (onStepChanged) onStepChanged();
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        OnboardingGuideWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<OnboardingGuideWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<OnboardingGuideWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_TIMER:
                if (wp == kTimerAdvance) {
                    KillTimer(h, kTimerAdvance);
                    self->AdvanceStep();
                }
                return 0;
            case WM_MOUSEMOVE: {
                if (!self->trackingLeave) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme)) self->trackingLeave = true;
                }
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int nh = self->HitZone(dx, dy);
                if (nh != self->hoverZone) {
                    self->hoverZone = nh;
                    self->Render();
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                self->trackingLeave = false;
                if (self->hoverZone != 0) {
                    self->hoverZone = 0;
                    self->Render();
                }
                return 0;
            case WM_LBUTTONDOWN: {
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                self->pressedZone = self->HitZone(dx, dy);
                if (self->pressedZone != 0) SetCapture(h);
                return 0;
            }
            case WM_LBUTTONUP: {
                int downZ = self->pressedZone;
                self->pressedZone = 0;
                if (GetCapture() == h) ReleaseCapture();
                if (downZ == 0) return 0;
                float dx = (float)(short)LOWORD(lp) / self->dpiScale;
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int hz = self->HitZone(dx, dy);
                if (hz != downZ) return 0;
                if (hz == 1 || hz == 7) {
                    if (self->onSkipOrFinish) self->onSkipOrFinish();
                } else if (hz == 2) {
                    KillTimer(h, kTimerAdvance);
                    self->AdvanceStep();
                } else if (hz == 3) {
                    if (self->onRequestCreatePrompt) self->onRequestCreatePrompt(self->targetSlot);
                } else if (hz == 4) {
                    if (self->onSelectEdge) self->onSelectEdge(DockEdge::Right);
                } else if (hz == 5) {
                    if (self->onSelectEdge) self->onSelectEdge(DockEdge::Left);
                } else if (hz == 6) {
                    if (self->onToggleAutoStart) self->onToggleAutoStart();
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
            case WM_CANCELMODE:
                self->pressedZone = 0;
                return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

class DockWindow {
public:
    struct DockGeometry {
        bool collapsed = false;
        int winX = 0;
        int winY = 0;
        int winW = 0;
        int winH = 0;
        float dpiScale = 1.0f;
        float panelHeightDip = Theme::PanelHeight();
        RECT monFull{};
        RECT work{};
        POINT solidProbePts[3]{};
        int centerLocalX = 0;
        int centerLocalY = 0;
    };

    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    AppState* state = nullptr;
    StorageManager* storage = nullptr;
    DarkPopupMenu* popupMenu = nullptr;
    VaultWindow* vaultWnd = nullptr;
    HoverBubbleWindow hoverBubble;
    OnboardingGuideWindow onboardingWnd;
    std::function<void(POINT)> onRequestAppMenu;
    std::function<bool(const std::wstring&)> clipboardWriter;

    LayeredCanvas canvas;
    ComPtr<IDWriteTextFormat> fmtHeader;
    ComPtr<IDWriteTextFormat> fmtHeaderSub;
    ComPtr<IDWriteTextFormat> fmtSlotIdx;
    ComPtr<IDWriteTextFormat> fmtSlotTitle;
    ComPtr<IDWriteTextFormat> fmtSlotEmpty;
    ComPtr<IDWriteTextFormat> fmtSlotBadge;
    ComPtr<IDWriteTextFormat> fmtFooterBtn;
    ComPtr<IDWriteTextFormat> fmtFooterBtnCompact;

    float dpiScale = 1.0f;
    float curPanelHeightDip = Theme::PanelHeight();
    float slotScrollDip = 0.0f;
    float maxSlotScrollDip = 0.0f;
    Spring1D expandSpring{}; // 0.0 = collapsed capsule, 1.0 = expanded panel
    std::array<float, 10> slotHoverAnim{};
    std::array<float, 10> slotShiftOffsetDip{};

    int hoverSlot = -1;       // 0..9
    int hoverSceneBtn = -1;   // Actual scene index 0..3 when ShowSceneBar()
    bool hoverRedockBtn = false;
    bool handleHovered = false;
    bool mouseTracking = false;
    int bubblePendingSlot = -1;
    ULONGLONG bubbleHoverDeadline = 0;

    // Slot drag-to-reorder & press-release ownership state
    int pressedSlot = -1;
    int pressedEmptySlot = -1;
    int pressedSceneBtn = -1;
    bool pressedRedockBtn = false;
    bool isSlotDragging = false;
    POINT slotDragStartScreen{};
    float slotDragGrabOffsetDipY = 0.0f;
    float slotDragCurDipY = 0.0f;

    // Post-onboarding 2-cycle edge capsule breathing glow
    ULONGLONG capsuleBreathStartTick = 0;
    static constexpr ULONGLONG kCapsuleBreathDurationMs = 1900;

    // Copy flash feedback state (green success / red failure)
    int copiedSlot = -1;
    ULONGLONG copiedTick = 0;
    int copyFailedSlot = -1;
    ULONGLONG copyFailedTick = 0;
    bool pendingAutoRetract = false;
    uint64_t retractGen = 0;
    uint64_t pendingAutoRetractGen = 0;

    // Transactional dragging state (preview during WM_MOUSEMOVE -> commit on WM_LBUTTONUP / rollback on cancel)
    bool isDragging = false;
    bool dragMoved = false;
    bool dragStartedCollapsed = false;
    bool dragPreviewFloating = false;
    DockEdge dragOrigEdge = DockEdge::Right;
    double dragOrigRatioY = 0.32;
    int dragOrigFloatX = 240;
    int dragOrigFloatY = 180;
    int dragPreviewFloatX = 0;
    int dragPreviewFloatY = 0;
    double dragPreviewRatioY = 0.32;
    POINT dragStartScreen{};
    int dragStartFloatX = 0;
    int dragStartFloatY = 0;
    double dragStartRatioY = 0.32;

    // Animation & debounce timing
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    bool animRunning = false;
    bool leaveWatchRunning = false;
    ULONGLONG mouseOutsideSince = 0;
    ULONGLONG holdOpenUntilTick = 0;

    // Last successfully presented window screen placement
    int curWinX = 0;
    int curWinY = 0;
    int curWinW = 0;
    int curWinH = 0;
    RECT curMonFull{};
    bool hasCurMonFull = false;
    bool hasPresentedOnce = false;
    bool showInitialFlag = true;
    int selfPresentDepth = 0;
    bool lastPresentOk = false;
    int consecutiveRenderFails = 0;
    int lastLoggedMode = -1; // 0 = collapsed, 1 = expanded/floating
    int displaySyncStage = 0;
    bool displaySyncQueued = false;
    std::wstring queuedSyncReason;
    bool wtsRegistered = false;

    // First-principles Z-order & reachability self-heal state (anti-fight circuit breaker)
    HWND lastHealedOccluder = nullptr;
    HWND lastHealedFg = nullptr;
    ULONGLONG lastZHealTick = 0;
    int lastWatchdogStateCode = 0; // 0=ok, 1=cloaked, 2=fullscreen, 3=own-or-shell, 4=occluded, 5=surface-loss
    int watchdogZHealCount = 0;
    int watchdogSurfaceRecommitCount = 0;
    BYTE lastCollapsedCenterAlpha = 0;

    // Injectable providers for deterministic headless/isolated self-tests
    std::function<RECT(POINT, RECT*)> monitorProvider;
    std::function<HWND(POINT)> hitProbeProvider;
    std::function<bool(HWND, HWND)> aboveInZOrderProvider;
    std::function<bool(HWND)> swpTopmostProvider;
    std::function<BOOL(POINT*)> cursorPosProvider;

    struct ScopedSelfPresent {
        int& depth;
        explicit ScopedSelfPresent(int& d) : depth(d) { ++depth; }
        ~ScopedSelfPresent() { if (depth > 0) --depth; }
    };

    static constexpr UINT WM_APP_SYNC_DISPLAY   = WM_APP + 40;
    static constexpr UINT_PTR kTimerAnim        = 5001;
    static constexpr UINT_PTR kTimerHoverExpand = 5002;
    static constexpr UINT_PTR kTimerLeaveWatch  = 5003;
    static constexpr UINT_PTR kTimerAutoRetract = 5004;
    static constexpr UINT_PTR kTimerBubbleHover = 5005;
    static constexpr UINT_PTR kTimerDisplaySync = 5006;
    static constexpr UINT_PTR kTimerWatchdog    = 5007;
    static constexpr UINT_PTR kTimerRenderRetry = 5008;
    static constexpr UINT_PTR kTimerQuickVerify = 5009;

    bool Init(HINSTANCE inst, AppState* st, StorageManager* stor, DarkPopupMenu* menu, VaultWindow* vault,
              std::function<void(POINT)> appMenuCb, bool showInitial = true) {
        hInst = inst;
        state = st;
        storage = stor;
        popupMenu = menu;
        vaultWnd = vault;
        onRequestAppMenu = std::move(appMenuCb);
        showInitialFlag = showInitial;
        QueryPerformanceFrequency(&qpcFreq);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PromptPod_DockWnd";
        RegisterClassExW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName,
            APP_DISPLAY_TITLE_W,
            WS_POPUP,
            0, 0, 10, 10,
            nullptr, nullptr, hInst, this
        );
        if (!hwnd) {
            DiagLogger::Instance().LogFmt("ERROR", "INIT", L"CreateWindowExW for DockWnd failed err=%lu", GetLastError());
            return false;
        }

        if (popupMenu) {
            popupMenu->onClosed = [this](MenuOwner closedOwner) {
                if (hwnd && IsWindow(hwnd)) {
                    if (closedOwner == MenuOwner::Dock) {
                        EvaluateCollapseState("menu-closed");
                    }
                    SetTimer(hwnd, kTimerQuickVerify, 120, nullptr);
                }
            };
        }

        // Dynamically register session lock/unlock & RDP notifications (zero static extra DLL dependency)
        RegisterWtsSessionNotifications();

        dpiScale = GetDpiScaleForWindow(hwnd);
        InitTextFormats();
        hoverBubble.Init(hInst);
        onboardingWnd.Init(hInst, state);
        onboardingWnd.onSkipOrFinish = [this]() {
            FinishOnboarding(true);
        };
        onboardingWnd.onRequestCreatePrompt = [this](int targetSlot) {
            TriggerOnboardingCreatePrompt(targetSlot);
        };
        onboardingWnd.onSelectEdge = [this](DockEdge edge) {
            SetDockEdgeAndPersist(edge, false);
            SyncOnboardingPosition();
        };
        onboardingWnd.onToggleAutoStart = [this]() {
            AutoStart::SetEnabled(!AutoStart::IsEnabled());
            onboardingWnd.Render();
            if (vaultWnd) vaultWnd->RefreshIfOpen();
        };
        onboardingWnd.onStepChanged = [this]() {
            RenderAndPresent("onboarding-step");
        };

        if (state->dockEdge == DockEdge::None) {
            expandSpring.Snap(1.0f);
        } else {
            expandSpring.Snap(0.0f);
        }

        DiagLogger::Instance().LogMonitorTopology(L"DockWindow::Init");
        RenderAndPresent("init");
        if (showInitial) {
            ScopedSelfPresent guard(selfPresentDepth);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        }
        if (SetTimer(hwnd, kTimerWatchdog, 3000, nullptr) == 0) {
            DiagLogger::Instance().LogFmt("ERROR", "TIMER", L"SetTimer(kTimerWatchdog) failed err=%lu", (unsigned long)GetLastError());
        }
        return true;
    }

    void Refresh() {
        if (!hwnd) return;
        if (!state->showHoverBubble) {
            KillTimer(hwnd, kTimerBubbleHover);
            bubblePendingSlot = -1;
            bubbleHoverDeadline = 0;
            hoverBubble.HideImmediate();
        }
        RenderAndPresent("refresh");
    }

    // Explicit Z-order + visibility restoration (NEVER mutates WS_EX_TOPMOST via SetWindowLongPtrW!)
    bool EnsureTopmostAndVisible(const char* reason, bool forceReassertTopmost = true) {
        if (!hwnd || !showInitialFlag) return true;
        BOOL vis = IsWindowVisible(hwnd);
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        bool hadTopmost = (ex & WS_EX_TOPMOST) != 0;

        ScopedSelfPresent guard(selfPresentDepth);
        if (!vis) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
        if ((ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED)) !=
            (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED)) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                              (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED) & ~WS_EX_TRANSPARENT);
        }

        bool swpOk = true;
        if (forceReassertTopmost || !hadTopmost || !vis) {
            UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            if (!vis) flags |= SWP_SHOWWINDOW;
            HWND insertAfter = (!hadTopmost || forceReassertTopmost) ? HWND_TOPMOST : HWND_TOP;
            if (swpTopmostProvider) {
                swpOk = swpTopmostProvider(insertAfter);
            } else {
                swpOk = SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags) != FALSE;
            }
        }

        if (!vis || !hadTopmost || !swpOk) {
            DiagLogger::Instance().LogFmt(swpOk ? "WARN" : "ERROR", "ZORDER",
                L"EnsureTopmostAndVisible(%hs): wasVis=%d hadTopmost=%d exStyle=0x%08lX swpOk=%d err=%lu",
                reason ? reason : "check", (int)vis, (int)hadTopmost, (unsigned long)ex,
                (int)swpOk, swpOk ? 0UL : GetLastError());
        }
        return swpOk;
    }

    bool IsOnboardingActive() const {
        return onboardingWnd.IsActive();
    }

    bool IsExpandedOrExpanding() const {
        if (!state) return false;
        if (state->dockEdge == DockEdge::None) return true;
        return expandSpring.target > 0.5f || expandSpring.value > 0.25f;
    }

    void CollapseNow() {
        if (!hwnd || !state || state->dockEdge == DockEdge::None) return;
        if (IsOnboardingActive()) FinishOnboarding(false);
        KillTimer(hwnd, kTimerHoverExpand);
        KillTimer(hwnd, kTimerAutoRetract);
        pendingAutoRetract = false;
        ++retractGen;
        holdOpenUntilTick = 0;
        hoverSlot = -1;
        hoverSceneBtn = -1;
        hoverRedockBtn = false;
        handleHovered = false;
        StartSpringTo(0.0f);
    }

    bool ShowSceneBar() const {
        return state && state->IsMultiSceneActive();
    }

    void SwitchActiveScene(int nextIdx) {
        if (!state || nextIdx < 0 || nextIdx >= kMaxScenes || !state->scenes[(size_t)nextIdx].enabled) return;
        if (nextIdx == state->EffectiveActiveSceneIndex()) return;
        state->activeSceneIndex = nextIdx;

        // Clear all transient slot state & cancel pending auto-retract on scene switch
        if (hwnd) {
            KillTimer(hwnd, kTimerBubbleHover);
            KillTimer(hwnd, kTimerAutoRetract);
        }
        pendingAutoRetract = false;
        ++retractGen;
        bubblePendingSlot = -1;
        bubbleHoverDeadline = 0;
        hoverBubble.HideImmediate();
        hoverSlot = -1;
        copiedSlot = -1;
        copiedTick = 0;
        copyFailedSlot = -1;
        copyFailedTick = 0;
        slotScrollDip = 0.0f;
        slotHoverAnim.fill(0.0f);
        slotShiftOffsetDip.fill(0.0f);

        if (storage) storage->Save(*state);
        if (vaultWnd) vaultWnd->RefreshIfOpen();
        if (hwnd) RenderAndPresent("scene-switch");
    }

    void StartOnboarding(bool isReplay = false) {
        if (!hwnd || !state) return;
        if (state->dockEdge == DockEdge::None) {
            state->dockEdge = DockEdge::Right;
            state->dockRatioY = 0.32;
        }
        int targetEmpty = state->FindFirstEmptySlot();
        if (targetEmpty < 0) targetEmpty = 2; // Fallback to slot 03 on full-deck replay
        onboardingWnd.Start(targetEmpty);
        KillTimer(hwnd, kTimerHoverExpand);
        KillTimer(hwnd, kTimerAutoRetract);
        pendingAutoRetract = false;
        ++retractGen;
        holdOpenUntilTick = 0;
        EnsureTopmostAndVisible("onboarding-start", true);
        expandSpring.Snap(1.0f);
        RenderAndPresent(isReplay ? "onboarding-replay" : "onboarding-start");
        SyncOnboardingPosition();
        DiagLogger::Instance().LogFmt("INFO", "ONBOARD", L"Started 3-step onboarding (replay=%d, targetSlot=%02d)",
                                      isReplay ? 1 : 0, targetEmpty + 1);
    }

    void FinishOnboarding(bool animateCapsuleGlow = true) {
        if (!state) return;
        bool wasActive = onboardingWnd.IsActive();
        onboardingWnd.Hide();
        if (!state->onboardingCompleted) {
            state->onboardingCompleted = true;
            if (storage) storage->Save(*state);
        }
        if (wasActive && hwnd && state->dockEdge != DockEdge::None) {
            hoverSlot = -1;
            hoverSceneBtn = -1;
            handleHovered = false;
            if (animateCapsuleGlow) {
                capsuleBreathStartTick = GetTickCount64();
            }
            StartSpringTo(0.0f);
            EnsureAnimTimer();
        }
        if (vaultWnd) vaultWnd->RefreshIfOpen();
    }

    void TriggerOnboardingCreatePrompt(int targetSlot) {
        if (!vaultWnd) return;
        int mountedSlot = -1;
        bool created = vaultWnd->OpenNewPromptModal(targetSlot, &mountedSlot);
        if (created && onboardingWnd.IsActive()) {
            onboardingWnd.NotifyPromptCreated(mountedSlot >= 0 ? mountedSlot : targetSlot);
            RenderAndPresent("onboarding-created");
        }
    }

    void SyncOnboardingPosition() {
        if (!onboardingWnd.IsActive() || !hwnd) return;
        DockGeometry geom = ComputeExpectedGeometry(false);
        D2D1_RECT_F pb = PanelBoxDip();
        RECT cardScreen{
            curWinX + (int)std::round(pb.left * dpiScale),
            curWinY + (int)std::round(pb.top * dpiScale),
            curWinX + (int)std::round(pb.right * dpiScale),
            curWinY + (int)std::round(pb.bottom * dpiScale)
        };
        onboardingWnd.SyncPosition(cardScreen, geom.work, dpiScale, showInitialFlag);
    }

    void PersistDockAnchorFromMonitor(const RECT& monFull) {
        if (!state) return;
        state->hasDockAnchor = true;
        state->dockAnchorX = (monFull.left + monFull.right) / 2;
        state->dockAnchorY = (monFull.top + monFull.bottom) / 2;
    }

    void SetDockEdgeAndPersist(DockEdge edge, bool revealHold = true) {
        if (!state) return;
        RECT mon{};
        GetTargetMonitorWorkArea(&mon);
        state->dockEdge = edge;
        if (edge != DockEdge::None) {
            PersistDockAnchorFromMonitor(mon);
        } else {
            // Switch to floating near the center-right of current monitor
            RECT work = GetTargetMonitorWorkArea(&mon);
            int wPx = (int)std::ceil((Theme::PanelWidth + Theme::ShadowPad * 2.0f) * dpiScale);
            int hPx = (int)std::ceil((curPanelHeightDip + Theme::ShadowPad * 2.0f) * dpiScale);
            state->floatX = std::clamp((int)work.right - wPx - 48, (int)work.left + 20, std::max((int)work.left + 20, (int)work.right - wPx - 20));
            state->floatY = std::clamp((int)work.top + ((int)(work.bottom - work.top) - hPx) / 2, (int)work.top + 16, std::max((int)work.top + 16, (int)work.bottom - hPx - 16));
            PersistDockAnchorFromMonitor(mon);
        }
        if (storage) storage->Save(*state);
        if (edge == DockEdge::None) {
            StopLeaveWatchTimer();
            expandSpring.Snap(1.0f);
            EnsureTopmostAndVisible("set-edge-float", true);
            RenderAndPresent("set-edge-float");
        } else if (IsOnboardingActive()) {
            expandSpring.Snap(1.0f);
            RenderAndPresent("set-edge-onboard");
            SyncOnboardingPosition();
        } else if (revealHold) {
            ExpandAndReveal(2000);
        } else {
            RenderAndPresent("set-edge");
        }
    }

    void ResetToEdgeDefault() {
        if (!state) return;
        if (state->dockEdge == DockEdge::None) {
            state->dockEdge = DockEdge::Right;
        }
        state->dockRatioY = 0.32;
        slotScrollDip = 0.0f;
        // Preserve current anchor monitor if still connected; otherwise fall back to primary monitor
        if (state->hasDockAnchor) {
            POINT apt{ state->dockAnchorX, state->dockAnchorY };
            if (!MonitorFromPoint(apt, MONITOR_DEFAULTTONULL)) {
                state->hasDockAnchor = false;
            }
        }
        hasPresentedOnce = false; // Force GetTargetMonitorWorkArea to re-resolve cleanly from anchor
        DockGeometry g = ComputeExpectedGeometry(false);
        PersistDockAnchorFromMonitor(g.monFull);
        if (storage) storage->Save(*state);
        DiagLogger::Instance().LogFmt("INFO", "DOCK", L"ResetToEdgeDefault triggered by user (edge=%d)", (int)state->dockEdge);
        ExpandAndReveal(2000);
    }

    void ResetToRightEdgeDefault() {
        if (state) state->dockEdge = DockEdge::Right;
        ResetToEdgeDefault();
    }

    std::wstring DescribeDockLocation() const {
        if (!state) return L"";
        RECT mon{};
        GetTargetMonitorWorkArea(&mon);
        int monIndex = 1;
        struct FindMonCtx { RECT target; int idx; int found; } ctx{ mon, 0, 1 };
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<FindMonCtx*>(lp);
            ++c->idx;
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hMon, &mi) &&
                mi.rcMonitor.left == c->target.left && mi.rcMonitor.top == c->target.top &&
                mi.rcMonitor.right == c->target.right && mi.rcMonitor.bottom == c->target.bottom) {
                c->found = c->idx;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        monIndex = ctx.found;

        wchar_t buf[96]{};
        if (state->dockEdge == DockEdge::Right) {
            wsprintfW(buf, L"显示器 %d · 右侧边缘", monIndex);
        } else if (state->dockEdge == DockEdge::Left) {
            wsprintfW(buf, L"显示器 %d · 左侧边缘", monIndex);
        } else {
            wsprintfW(buf, L"显示器 %d · 自由悬浮", monIndex);
        }
        return UiText(buf);
    }

    static std::vector<const PromptItem*> SelectQuickSwapCandidates(const AppState& st, size_t maxCount = 6) {
        struct Entry {
            const PromptItem* item;
            size_t order;
        };
        std::vector<Entry> bench;
        bench.reserve(st.vault.size());
        for (size_t i = 0; i < st.vault.size(); ++i) {
            if (st.FindSlotOfId(st.vault[i].id) < 0) {
                bench.push_back(Entry{ &st.vault[i], i });
            }
        }
        std::sort(bench.begin(), bench.end(), [](const Entry& a, const Entry& b) {
            if (a.item->lastUsedAt != b.item->lastUsedAt) return a.item->lastUsedAt > b.item->lastUsedAt;
            if (a.item->useCount != b.item->useCount) return a.item->useCount > b.item->useCount;
            return a.order < b.order;
        });
        std::vector<const PromptItem*> out;
        size_t limit = std::min(bench.size(), maxCount);
        out.reserve(limit);
        for (size_t i = 0; i < limit; ++i) out.push_back(bench[i].item);
        return out;
    }

    [[gnu::noinline]] void ExpandAndReveal(ULONGLONG minHoldMs = 0) {
        if (!hwnd) return;
        if (showInitialFlag && state && !state->onboardingCompleted && !IsOnboardingActive() &&
            (!storage || !storage->readOnlyRecoveryMode)) {
            StartOnboarding(false);
            return;
        }
        KillTimer(hwnd, kTimerHoverExpand);
        KillTimer(hwnd, kTimerAutoRetract);
        pendingAutoRetract = false;
        ++retractGen;
        handleHovered = false;
        capsuleBreathStartTick = 0;
        lastHealedOccluder = nullptr; // Explicit user reveal resets occluder backoff
        ULONGLONG now = GetTickCount64();
        mouseOutsideSince = now;
        holdOpenUntilTick = (minHoldMs > 0) ? (now + minHoldMs) : 0;
        EnsureTopmostAndVisible("expand-reveal", true);
        DiagLogger::Instance().LogFmt("INFO", "REVEAL",
            L"ExpandAndReveal(holdMs=%llu) edge=%d cur=(%d,%d %dx%d) spring=%.3f",
            (unsigned long long)minHoldMs, (int)state->dockEdge,
            curWinX, curWinY, curWinW, curWinH, expandSpring.value);
        StartSpringTo(1.0f);
        RenderAndPresent("expand-reveal");
        EnsureLeaveWatchTimer();
    }

    // Coalesced async display/session sync (never re-enters SetWindowPos synchronously inside Win32 notifications)
    void ScheduleDisplaySync(const wchar_t* triggerReason, bool immediateSync = false) {
        if (!hwnd) return;
        lastHealedOccluder = nullptr;
        if (immediateSync) {
            ExecuteDisplaySync(triggerReason ? triggerReason : L"sync");
            return;
        }
        queuedSyncReason = triggerReason ? triggerReason : L"sync";
        if (!displaySyncQueued) {
            displaySyncQueued = true;
            PostMessageW(hwnd, WM_APP_SYNC_DISPLAY, 0, 0);
        }
    }

    friend int RunHeadlessSelfTest(const std::wstring& testDir);

private:
    void RegisterWtsSessionNotifications() {
        if (!hwnd || wtsRegistered) return;
        HMODULE hWts = LoadLibraryW(L"wtsapi32.dll");
        if (!hWts) return;
        using WtsRegFn = BOOL(WINAPI*)(HWND, DWORD);
        auto fn = reinterpret_cast<WtsRegFn>(reinterpret_cast<void*>(GetProcAddress(hWts, "WTSRegisterSessionNotification")));
        if (fn && fn(hwnd, NOTIFY_FOR_THIS_SESSION)) {
            wtsRegistered = true;
        }
    }

    void UnregisterWtsSessionNotifications() {
        if (!hwnd || !wtsRegistered) return;
        HMODULE hWts = GetModuleHandleW(L"wtsapi32.dll");
        if (hWts) {
            using WtsUnregFn = BOOL(WINAPI*)(HWND);
            auto fn = reinterpret_cast<WtsUnregFn>(reinterpret_cast<void*>(GetProcAddress(hWts, "WTSUnRegisterSessionNotification")));
            if (fn) fn(hwnd);
        }
        wtsRegistered = false;
    }

    void ExecuteDisplaySync(const wchar_t* triggerReason) {
        if (!hwnd) return;
        dpiScale = GetDpiScaleForWindow(hwnd);
        DiagLogger::Instance().LogMonitorTopology(triggerReason);
        // Display sync updates geometry & surface first without blindly fighting own menus/editor Z-order
        if (showInitialFlag && !IsWindowVisible(hwnd)) {
            EnsureTopmostAndVisible("display-sync-hidden", true);
        }
        RenderAndPresent("display-sync");
        displaySyncStage = 0;
        SetTimer(hwnd, kTimerDisplaySync, 150, nullptr);
    }

    bool IsDockMenuOpen() const {
        return popupMenu && popupMenu->IsOpenFor(MenuOwner::Dock);
    }

    bool EnsureLeaveWatchTimer() {
        if (!hwnd || !state || state->dockEdge == DockEdge::None) return false;
        if (SetTimer(hwnd, kTimerLeaveWatch, 60, nullptr) != 0) {
            leaveWatchRunning = true;
            return true;
        }
        leaveWatchRunning = false;
        DiagLogger::Instance().LogFmt("ERROR", "TIMER", L"SetTimer(kTimerLeaveWatch) failed err=%lu", (unsigned long)GetLastError());
        return false;
    }

    void StopLeaveWatchTimer() {
        if (hwnd) KillTimer(hwnd, kTimerLeaveWatch);
        leaveWatchRunning = false;
    }

    bool IsCursorInsideInteractivePanel(POINT* outPt = nullptr, float* outDx = nullptr, float* outDy = nullptr) const {
        POINT pt{};
        BOOL gotPt = cursorPosProvider ? cursorPosProvider(&pt) : GetCursorPos(&pt);
        if (!gotPt) return false; // Fail-closed: unable to read cursor -> do not pin panel open
        if (outPt) *outPt = pt;
        float staticDx = (float)(pt.x - curWinX) / dpiScale;
        float staticDy = (float)(pt.y - curWinY) / dpiScale;
        if (outDx) *outDx = staticDx - CurrentSlideOffsetDip();
        if (outDy) *outDy = staticDy;

        D2D1_RECT_F pb = PanelBoxDip();
        // Keep vertical bounds strictly aligned with the card body.
        if (staticDy < pb.top || staticDy > pb.bottom) return false;

        // Horizontal keep-open zone: keep the inner side strictly at PanelBoxDip,
        // while bridging the docked-side 6 DIP floating gap to the physical monitor edge
        // [monFull.left, monFull.right) so slamming the cursor to the screen edge to open
        // the dock never self-retracts, yet crossing onto an adjacent monitor immediately leaves.
        if (state && state->dockEdge == DockEdge::Right) {
            if (staticDx < pb.left) return false;
            if (staticDx <= pb.right) return true;
            RECT mon = hasCurMonFull ? curMonFull : ComputeExpectedGeometry(false).monFull;
            return (pt.x < mon.right) && (staticDx <= pb.right + 12.0f);
        }
        if (state && state->dockEdge == DockEdge::Left) {
            if (staticDx > pb.right) return false;
            if (staticDx >= pb.left) return true;
            RECT mon = hasCurMonFull ? curMonFull : ComputeExpectedGeometry(false).monFull;
            return (pt.x >= mon.left) && (staticDx >= pb.left - 12.0f);
        }
        return (staticDx >= pb.left && staticDx <= pb.right);
    }

    void RollbackPanelDrag(const char* reason) {
        if (!isDragging) return;
        bool wasCollapsedStart = dragStartedCollapsed;
        isDragging = false;
        dragMoved = false;
        dragStartedCollapsed = false;
        dragPreviewFloating = false;
        state->dockEdge = dragOrigEdge;
        state->dockRatioY = dragOrigRatioY;
        state->floatX = dragOrigFloatX;
        state->floatY = dragOrigFloatY;
        DiagLogger::Instance().LogFmt("INFO", "DOCK",
            L"Panel drag rolled back (%hs) -> edge=%d ratioMilli=%d",
            reason ? reason : "cancel", (int)state->dockEdge, (int)(state->dockRatioY * 10000.0 + 0.5));
        if (wasCollapsedStart && state->dockEdge != DockEdge::None) {
            expandSpring.Snap(0.0f);
        }
        RenderAndPresent(reason ? reason : "drag-rollback");
        if (!wasCollapsedStart && state->dockEdge != DockEdge::None) {
            mouseOutsideSince = GetTickCount64();
            EvaluateCollapseState(reason ? reason : "drag-rollback");
        }
    }

    void CancelActiveInteractions() {
        KillTimer(hwnd, kTimerHoverExpand);
        KillTimer(hwnd, kTimerBubbleHover);
        KillTimer(hwnd, kTimerAutoRetract);
        pendingAutoRetract = false;
        ++retractGen;
        handleHovered = false;
        bubblePendingSlot = -1;
        bubbleHoverDeadline = 0;
        hoverBubble.HideImmediate();
        if (popupMenu && popupMenu->IsOpenFor(MenuOwner::Dock)) popupMenu->Close();
        bool hadDrag = isDragging;
        bool hadSlotDrag = isSlotDragging;
        isSlotDragging = false;
        pressedSlot = -1;
        pressedEmptySlot = -1;
        pressedSceneBtn = -1;
        pressedRedockBtn = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (hadSlotDrag) {
            storage->Save(*state);
            if (vaultWnd) vaultWnd->RefreshIfOpen();
        }
        if (hadDrag) {
            RollbackPanelDrag("cancel-interaction");
        } else {
            EvaluateCollapseState("cancel-interaction");
        }
    }

    void InitTextFormats() {
        auto& eng = D2DEngine::Instance();
        fmtHeader    = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_BOLD);
        fmtHeaderSub = eng.CreateFormat(10.0f, DWRITE_FONT_WEIGHT_NORMAL, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtSlotIdx   = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtSlotTitle = eng.CreateFormat(12.5f, DWRITE_FONT_WEIGHT_MEDIUM);
        fmtSlotEmpty = eng.CreateFormat(12.0f, DWRITE_FONT_WEIGHT_NORMAL);
        fmtSlotBadge = eng.CreateFormat(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtFooterBtn = eng.CreateFormat(11.5f, DWRITE_FONT_WEIGHT_MEDIUM, true, DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtFooterBtnCompact = eng.CreateFormat(11.0f, DWRITE_FONT_WEIGHT_MEDIUM, true, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void EnsureAnimTimer() {
        if (!animRunning) {
            QueryPerformanceCounter(&lastAnimQpc);
            if (SetTimer(hwnd, kTimerAnim, 10, nullptr) != 0) {
                animRunning = true;
            } else {
                DiagLogger::Instance().LogFmt("ERROR", "TIMER",
                    L"SetTimer(kTimerAnim) failed err=%lu; snapping spring to target=%.2f",
                    (unsigned long)GetLastError(), expandSpring.target);
                expandSpring.Snap(expandSpring.target);
                for (int i = 0; i < 10; ++i) {
                    slotHoverAnim[(size_t)i] = (hoverSlot == i) ? 1.0f : 0.0f;
                    slotShiftOffsetDip[(size_t)i] = 0.0f;
                }
                RenderAndPresent("anim-timer-fail");
            }
        }
    }

    void ScheduleRenderRetry() {
        if (!hwnd) return;
        UINT delayMs = (consecutiveRenderFails <= 1) ? 500u : (consecutiveRenderFails <= 3 ? 1500u : 3000u);
        SetTimer(hwnd, kTimerRenderRetry, delayMs, nullptr);
    }

    void StopAnimTimerIfIdle() {
        bool anyHoverAnim = false;
        for (int i = 0; i < 10; ++i) {
            float target = (hoverSlot == i) ? 1.0f : 0.0f;
            if (std::fabs(slotHoverAnim[(size_t)i] - target) > 0.01f ||
                std::fabs(slotShiftOffsetDip[(size_t)i]) > 0.15f) {
                anyHoverAnim = true;
                break;
            }
        }
        ULONGLONG now = GetTickCount64();
        bool copyActive = (copiedSlot >= 0 && (now - copiedTick) < 650);
        bool failActive = (copyFailedSlot >= 0 && (now - copyFailedTick) < 1250);
        bool breathActive = (capsuleBreathStartTick != 0 && (now - capsuleBreathStartTick) < kCapsuleBreathDurationMs);
        if (capsuleBreathStartTick != 0 && !breathActive) {
            capsuleBreathStartTick = 0;
        }
        if (expandSpring.IsSettled() && !anyHoverAnim && !copyActive && !failActive && !breathActive && !isSlotDragging) {
            if (copiedSlot >= 0 && (now - copiedTick) >= 650) {
                copiedSlot = -1;
            }
            if (copyFailedSlot >= 0 && (now - copyFailedTick) >= 1250) {
                copyFailedSlot = -1;
            }
            KillTimer(hwnd, kTimerAnim);
            animRunning = false;
        }
    }

    void StartSpringTo(float target) {
        if (target < 0.5f) {
            KillTimer(hwnd, kTimerBubbleHover);
            bubblePendingSlot = -1;
            bubbleHoverDeadline = 0;
            hoverBubble.HideSmooth();
            StopLeaveWatchTimer();
        }
        expandSpring.target = target;
        EnsureAnimTimer();
    }

    void SyncHoverBubble(bool immediate) {
        if (!hwnd || !state || !state->showHoverBubble || hoverSlot < 0 || hoverSlot >= 10 ||
            isDragging || isSlotDragging || IsDockMenuOpen() ||
            IsCompactCollapsedMode() || expandSpring.target < 0.5f) {
            KillTimer(hwnd, kTimerBubbleHover);
            bubblePendingSlot = -1;
            bubbleHoverDeadline = 0;
            hoverBubble.HideSmooth();
            return;
        }
        const PromptItem* item = state->FindById(state->ActiveSlots()[(size_t)hoverSlot]);
        if (!item) {
            KillTimer(hwnd, kTimerBubbleHover);
            bubblePendingSlot = -1;
            bubbleHoverDeadline = 0;
            hoverBubble.HideSmooth();
            return;
        }
        ULONGLONG now = GetTickCount64();
        bool deadlineReached = (bubblePendingSlot == hoverSlot && bubbleHoverDeadline != 0 && now >= bubbleHoverDeadline);
        if (expandSpring.value >= 0.72f && (immediate || deadlineReached || hoverBubble.IsShowingOrAppearing())) {
            KillTimer(hwnd, kTimerBubbleHover);
            bubblePendingSlot = -1;
            bubbleHoverDeadline = 0;
            D2D1_RECT_F pb = PanelBoxDip();
            RECT cardScreen{
                curWinX + (int)std::round(pb.left * dpiScale),
                curWinY + (int)std::round(pb.top * dpiScale),
                curWinX + (int)std::round(pb.right * dpiScale),
                curWinY + (int)std::round(pb.bottom * dpiScale)
            };
            D2D1_RECT_F sr = SlotRectDip(hoverSlot);
            int slotMidY = curWinY + (int)std::round(((sr.top + sr.bottom) * 0.5f) * dpiScale);
            hoverBubble.ShowForSlot(hoverSlot, *item, cardScreen, slotMidY);
        } else if (bubblePendingSlot != hoverSlot || bubbleHoverDeadline == 0) {
            // Schedule 180ms hover debounce ONCE per hovered slot (never reset on every animation frame!)
            if (SetTimer(hwnd, kTimerBubbleHover, 180, nullptr) != 0) {
                bubblePendingSlot = hoverSlot;
                bubbleHoverDeadline = now + 180;
            } else {
                bubblePendingSlot = -1;
                bubbleHoverDeadline = 0;
            }
        }
    }

    // Geometry helpers in DIPs inside the full expanded canvas (supports short-screen height clamp)
    D2D1_RECT_F PanelBoxDip() const {
        return D2D1::RectF(
            Theme::ShadowPad,
            Theme::ShadowPad,
            Theme::ShadowPad + Theme::PanelWidth,
            Theme::ShadowPad + curPanelHeightDip
        );
    }

    D2D1_RECT_F SlotViewportDip() const {
        D2D1_RECT_F pb = PanelBoxDip();
        float bottomReserve = ShowSceneBar() ? (Theme::FooterHeight + 4.0f) : 8.0f;
        return D2D1::RectF(pb.left, pb.top + Theme::HeaderHeight, pb.right, pb.bottom - bottomReserve);
    }

    D2D1_RECT_F SlotRectDip(int idx) const {
        D2D1_RECT_F pb = PanelBoxDip();
        float top = pb.top + Theme::HeaderHeight + (float)idx * (Theme::SlotHeight + Theme::SlotGap) - slotScrollDip;
        float rightInset = (maxSlotScrollDip > 0.5f) ? (Theme::PaddingX + 5.0f) : Theme::PaddingX;
        return D2D1::RectF(pb.left + Theme::PaddingX, top, pb.right - rightInset, top + Theme::SlotHeight);
    }

    D2D1_RECT_F HeaderRedockBtnRectDip() const {
        D2D1_RECT_F pb = PanelBoxDip();
        return D2D1::RectF(pb.right - 56.0f, pb.top + 8.0f, pb.right - 10.0f, pb.top + 28.0f);
    }

    // Scene buttons occupy the exact original footer button bar geometry (no extra outer trough)
    D2D1_RECT_F SceneBarBoxDip() const {
        D2D1_RECT_F pb = PanelBoxDip();
        return D2D1::RectF(
            pb.left + Theme::PaddingX,
            pb.bottom - Theme::FooterHeight + 4.0f,
            pb.right - Theme::PaddingX,
            pb.bottom - 8.0f
        );
    }

    D2D1_RECT_F SceneBtnRectDip(int visPos, int visCount) const {
        D2D1_RECT_F box = SceneBarBoxDip();
        float gap = (visCount >= 4) ? 5.0f : 6.0f;
        int safeCount = std::max(1, visCount);
        float avail = (box.right - box.left) - (float)(safeCount - 1) * gap;
        float btnW = avail / (float)safeCount;
        float l = box.left + (float)visPos * (btnW + gap);
        float r = (visPos == safeCount - 1) ? box.right : (l + btnW);
        return D2D1::RectF(l, box.top, r, box.bottom);
    }

    // Returns actual scene index (0..kMaxScenes-1) or -1
    int HitTestSceneBtnDip(float dx, float dy) const {
        if (!ShowSceneBar()) return -1;
        int vis[kMaxScenes]{};
        int cnt = state->GetEnabledScenes(vis);
        for (int p = 0; p < cnt; ++p) {
            D2D1_RECT_F r = SceneBtnRectDip(p, cnt);
            if (dx >= r.left && dx <= r.right && dy >= r.top && dy <= r.bottom) return vis[p];
        }
        return -1;
    }

    bool IsPointInSceneBarDip(float dx, float dy) const {
        if (!ShowSceneBar()) return false;
        D2D1_RECT_F b = SceneBarBoxDip();
        return dx >= b.left && dx <= b.right && dy >= b.top && dy <= b.bottom;
    }

    bool IsCompactCollapsedMode() const {
        if (isDragging && dragPreviewFloating) return false;
        return (state->dockEdge != DockEdge::None) && (expandSpring.value <= 0.002f) && (expandSpring.target == 0.0f);
    }

    float CurrentSlideOffsetDip() const {
        if (!state || state->dockEdge == DockEdge::None || (isDragging && dragPreviewFloating) || IsCompactCollapsedMode()) return 0.0f;
        float t = std::clamp(expandSpring.value, 0.0f, 1.0f);
        if (state->dockEdge == DockEdge::Left) return -(1.0f - t) * (Theme::PanelWidth + 8.0f);
        return (1.0f - t) * (Theme::PanelWidth + 8.0f);
    }

    static POINT MsgScreenPoint(HWND h, LPARAM lp) {
        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen(h, &pt);
        return pt;
    }

    RECT GetTargetMonitorWorkArea(RECT* outMon = nullptr, float* outDpi = nullptr) const {
        POINT refPt{ state->floatX + 80, state->floatY + 80 };
        if (hasPresentedOnce && curWinW > 0 && curWinH > 0) {
            refPt = { curWinX + curWinW / 2, curWinY + curWinH / 2 };
        } else if (state && state->hasDockAnchor) {
            refPt = { state->dockAnchorX, state->dockAnchorY };
        } else if (state->dockEdge != DockEdge::None) {
            refPt = { 0, 0 };
        }
        RECT work{};
        if (monitorProvider) {
            work = monitorProvider(refPt, outMon);
            if (outDpi) *outDpi = GetDpiScaleForWindow(hwnd);
            return work;
        }
        work = GetMonitorWorkAreaForPoint(refPt, outMon);
        if (outDpi) *outDpi = GetDpiScaleForPoint(refPt, hwnd);
        return work;
    }

    DockGeometry ComputeExpectedGeometry(bool collapsedMode) const {
        DockGeometry g{};
        g.collapsed = collapsedMode;
        g.work = GetTargetMonitorWorkArea(&g.monFull, &g.dpiScale);
        if (g.dpiScale <= 0.1f) g.dpiScale = 1.0f;

        float workHDip = std::max(240.0f, (float)(g.work.bottom - g.work.top) / g.dpiScale);
        float maxPanelHDip = std::max(240.0f, workHDip - 16.0f);
        g.panelHeightDip = std::min(Theme::PanelHeight(ShowSceneBar()), maxPanelHDip);

        if (collapsedMode) {
            float cwDip = Theme::HandleHitWidth + 4.0f;
            float chDip = Theme::HandleHeight + 12.0f;
            g.winW = (int)std::ceil(cwDip * g.dpiScale);
            g.winH = (int)std::ceil(chDip * g.dpiScale);

            int fullPanelHPx = (int)std::ceil((g.panelHeightDip + Theme::ShadowPad * 2.0f) * g.dpiScale);
            int minCenterY = g.work.top + fullPanelHPx / 2;
            int maxCenterY = g.work.bottom - fullPanelHPx / 2;
            if (maxCenterY < minCenterY) minCenterY = maxCenterY = (g.work.top + g.work.bottom) / 2;
            double activeRatioY = (isDragging && dragStartedCollapsed && !dragPreviewFloating) ? dragPreviewRatioY : state->dockRatioY;
            int centerY = std::clamp((int)(g.work.top + activeRatioY * (g.work.bottom - g.work.top)), minCenterY, maxCenterY);

            g.winX = (state->dockEdge == DockEdge::Left) ? g.monFull.left : (g.monFull.right - g.winW);
            g.winY = centerY - g.winH / 2;

            // Compute 3 interior solid capsule points strictly inside the opaque 6x118 DIP pill (away from transparent area & rounded corners)
            float actualCwDip = (float)g.winW / g.dpiScale;
            float hxDip = (state->dockEdge == DockEdge::Left) ? 0.0f : (actualCwDip - Theme::HandleWidth);
            float solidCenterXDip = hxDip + Theme::HandleWidth * 0.5f;
            int localX = std::clamp((int)std::round(solidCenterXDip * g.dpiScale), 0, std::max(0, g.winW - 1));
            int yTop = std::clamp((int)std::round((6.0f + Theme::HandleHeight * 0.28f) * g.dpiScale), 0, std::max(0, g.winH - 1));
            int yMid = std::clamp((int)std::round((6.0f + Theme::HandleHeight * 0.50f) * g.dpiScale), 0, std::max(0, g.winH - 1));
            int yBot = std::clamp((int)std::round((6.0f + Theme::HandleHeight * 0.72f) * g.dpiScale), 0, std::max(0, g.winH - 1));
            g.centerLocalX = localX;
            g.centerLocalY = yMid;
            g.solidProbePts[0] = { g.winX + localX, g.winY + yMid };
            g.solidProbePts[1] = { g.winX + localX, g.winY + yTop };
            g.solidProbePts[2] = { g.winX + localX, g.winY + yBot };
            return g;
        }

        float canvasWDip = Theme::PanelWidth + Theme::ShadowPad * 2.0f;
        float canvasHDip = g.panelHeightDip + Theme::ShadowPad * 2.0f;
        g.winW = (int)std::ceil(canvasWDip * g.dpiScale);
        g.winH = (int)std::ceil(canvasHDip * g.dpiScale);

        if (isDragging && dragPreviewFloating) {
            g.winX = dragPreviewFloatX;
            g.winY = dragPreviewFloatY;
        } else if (state->dockEdge != DockEdge::None) {
            int minCenterY = g.work.top + g.winH / 2;
            int maxCenterY = g.work.bottom - g.winH / 2;
            if (maxCenterY < minCenterY) minCenterY = maxCenterY = (g.work.top + g.work.bottom) / 2;
            int centerY = std::clamp((int)(g.work.top + state->dockRatioY * (g.work.bottom - g.work.top)), minCenterY, maxCenterY);
            g.winY = centerY - g.winH / 2;

            int edgeGapPx = (int)std::round(6.0f * g.dpiScale);
            if (state->dockEdge == DockEdge::Left) {
                g.winX = g.monFull.left - (int)std::round(Theme::ShadowPad * g.dpiScale) + edgeGapPx;
            } else {
                g.winX = g.monFull.right - g.winW + (int)std::round(Theme::ShadowPad * g.dpiScale) - edgeGapPx;
            }
        } else {
            g.winX = state->floatX;
            g.winY = state->floatY;
            if (!isDragging) {
                int minX = g.work.left - g.winW + (int)std::round(80.0f * g.dpiScale);
                int maxX = g.work.right - (int)std::round(80.0f * g.dpiScale);
                int minY = g.work.top;
                int maxY = g.work.bottom - (int)std::round(60.0f * g.dpiScale);
                if (maxX >= minX) g.winX = std::clamp(g.winX, minX, maxX);
                if (maxY >= minY) g.winY = std::clamp(g.winY, minY, maxY);
            }
        }
        return g;
    }

    void DrawCapsulePill(ID2D1RenderTarget* rt, const D2D1_RECT_F& pill, float alphaScale) {
        if (alphaScale <= 0.01f) return;
        float breath = 0.0f;
        if (capsuleBreathStartTick != 0) {
            ULONGLONG elapsed = GetTickCount64() - capsuleBreathStartTick;
            if (elapsed < kCapsuleBreathDurationMs) {
                float p = (float)elapsed / (float)kCapsuleBreathDurationMs; // 0..1 -> 2 full breathing cycles
                breath = 0.5f - 0.5f * std::cos(p * 4.0f * 3.14159265f);
            }
        }
        float glow = std::max(handleHovered ? 1.0f : 0.0f, breath);
        uint32_t accTop = Theme::AccentCyan, accBot = Theme::AccentIndigo;
        Theme::SceneAccentColors(state ? state->EffectiveActiveSceneIndex() : 0, accTop, accBot);

        // Outer dark glass capsule
        rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 3.0f, 3.0f),
                                 canvas.Brush(Rgba(0x141822, (0.86f + 0.10f * glow) * alphaScale)));
        DrawRim(rt, pill, 3.0f,
                         Rgba(accTop, (0.22f + 0.68f * glow) * alphaScale),
                         Rgba(accBot, (0.16f + 0.52f * glow) * alphaScale), 1.0f + 0.4f * breath);

        // Inner glowing scene-tinted light core
        float corePadX = 1.8f - 0.4f * breath;
        float corePadY = 22.0f - 12.0f * glow;
        D2D1_RECT_F core = D2D1::RectF(pill.left + corePadX, pill.top + corePadY,
                                       pill.right - corePadX, pill.bottom - corePadY);
        D2D1_GRADIENT_STOP stops[2] = {
            { 0.0f, Rgba(accTop, (0.65f + 0.35f * glow) * alphaScale) },
            { 1.0f, Rgba(accBot, (0.65f + 0.35f * glow) * alphaScale) }
        };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) {
            ComPtr<ID2D1LinearGradientBrush> grad;
            rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, core.top), D2D1::Point2F(0, core.bottom)),
                coll.Get(), grad.GetAddressOf());
            if (grad) rt->FillRoundedRectangle(D2D1::RoundedRect(core, 1.5f, 1.5f), grad.Get());
        }
    }

    [[gnu::noinline]] bool RenderAndPresent(const char* reason = nullptr) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (RenderFrameAttempt(reason)) {
                lastPresentOk = true;
                consecutiveRenderFails = 0;
                KillTimer(hwnd, kTimerRenderRetry);
                return true;
            }
        }
        lastPresentOk = false;
        ++consecutiveRenderFails;
        if (consecutiveRenderFails >= 2) {
            canvas.solidBrush.Reset();
            canvas.rt.Reset();
        }
        DiagLogger::Instance().LogFmt("ERROR", "RENDER",
            L"RenderAndPresent failed (%hs, fail#%d): step=%hs hr=0x%08lX win32Err=%lu cur=(%d,%d %dx%d) spring=%.3f",
            reason ? reason : "tick",
            consecutiveRenderFails,
            canvas.lastFailStep ? canvas.lastFailStep : "unknown",
            (unsigned long)canvas.lastHr,
            canvas.lastWin32Err,
            curWinX, curWinY, curWinW, curWinH,
            expandSpring.value);
        ScheduleRenderRetry();
        return false;
    }

    bool RenderFrameAttempt(const char* reason) {
        bool collapsedMode = IsCompactCollapsedMode();
        DockGeometry geom = ComputeExpectedGeometry(collapsedMode);
        dpiScale = geom.dpiScale;
        curPanelHeightDip = geom.panelHeightDip;
        float fullSlotsH = 10.0f * Theme::SlotHeight + 9.0f * Theme::SlotGap;
        float visibleSlotsH = std::max(60.0f, curPanelHeightDip - Theme::HeaderHeight - (ShowSceneBar() ? (Theme::FooterHeight + 8.0f) : 10.0f));
        maxSlotScrollDip = std::max(0.0f, fullSlotsH - visibleSlotsH);
        slotScrollDip = std::clamp(slotScrollDip, 0.0f, maxSlotScrollDip);

        // 1) Pure Collapsed Capsule Mode: ultra-tiny HWND hugging only the 14x130 DIP handle!
        if (collapsedMode) {
            float cwDip = (float)geom.winW / dpiScale;
            if (!canvas.Begin(geom.winW, geom.winH, dpiScale)) return false;
            auto* rt = canvas.rt.Get();

            float hx = (state->dockEdge == DockEdge::Left) ? 0.0f : (cwDip - Theme::HandleWidth);
            D2D1_RECT_F pill = D2D1::RectF(hx, 6.0f, hx + Theme::HandleWidth, 6.0f + Theme::HandleHeight);
            DrawCapsulePill(rt, pill, 1.0f);

            int prevX = curWinX, prevY = curWinY;
            bool ok = false;
            {
                ScopedSelfPresent guard(selfPresentDepth);
                ok = canvas.EndAndPresent(hwnd, geom.winX, geom.winY);
            }
            if (ok) {
                GdiFlush();
                lastCollapsedCenterAlpha = canvas.SampleAlphaAt(geom.centerLocalX, geom.centerLocalY);
                curWinX = geom.winX; curWinY = geom.winY; curWinW = geom.winW; curWinH = geom.winH;
                curMonFull = geom.monFull; hasCurMonFull = true;
                hasPresentedOnce = true;
                if (lastLoggedMode != 0 || reason != nullptr || std::abs(prevX - geom.winX) > 4 || std::abs(prevY - geom.winY) > 4) {
                    lastLoggedMode = 0;
                    DiagLogger::Instance().LogFmt("INFO", "GEOM",
                        L"Mode=Collapsed(%hs) edge=%d pos=(%d,%d %dx%d) mon=(%ld,%ld)-(%ld,%ld) dpi=%.2f centerAlpha=%u",
                        reason ? reason : "transition", (int)state->dockEdge,
                        geom.winX, geom.winY, geom.winW, geom.winH,
                        geom.monFull.left, geom.monFull.top, geom.monFull.right, geom.monFull.bottom,
                        dpiScale, (unsigned)lastCollapsedCenterAlpha);
                }
            }
            return ok;
        }

        // 2) Expanded or Spring-Animating Panel Mode
        float canvasWDip = Theme::PanelWidth + Theme::ShadowPad * 2.0f;
        float canvasHDip = curPanelHeightDip + Theme::ShadowPad * 2.0f;
        int wPx = geom.winW;
        int hPx = geom.winH;
        int winX = geom.winX;
        int winY = geom.winY;
        const RECT& monFull = geom.monFull;
        bool renderAsFloating = (state->dockEdge == DockEdge::None) || (isDragging && dragPreviewFloating);
        float t = std::clamp(expandSpring.value, 0.0f, 1.0f);
        float slideX = 0.0f;
        if (!renderAsFloating) {
            if (state->dockEdge == DockEdge::Left) {
                slideX = -(1.0f - t) * (Theme::PanelWidth + 8.0f);
            } else if (state->dockEdge == DockEdge::Right) {
                slideX = (1.0f - t) * (Theme::PanelWidth + 8.0f);
            }
        }

        if (!canvas.Begin(wPx, hPx, dpiScale)) return false;
        auto* rt = canvas.rt.Get();

        // Clip strictly to the monitor side so dual-monitor setups never leak sliding pixels across screens
        D2D1_RECT_F clipDip = D2D1::RectF(0.0f, 0.0f, canvasWDip, canvasHDip);
        if (!renderAsFloating) {
            if (state->dockEdge == DockEdge::Left) {
                float monEdgeDip = (float)(monFull.left - winX) / dpiScale;
                clipDip.left = std::max(0.0f, monEdgeDip);
            } else if (state->dockEdge == DockEdge::Right) {
                float monEdgeDip = (float)(monFull.right - winX) / dpiScale;
                clipDip.right = std::min(canvasWDip, monEdgeDip);
            }
        }
        rt->PushAxisAlignedClip(clipDip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Cross-fade edge capsule handle when panel is retracting (t < 0.35) so edge is NEVER 0-alpha transparent!
        if (!renderAsFloating && t < 0.35f) {
            float capAlpha = 1.0f - std::clamp(t / 0.35f, 0.0f, 1.0f);
            float pillTop = (canvasHDip - Theme::HandleHeight) * 0.5f;
            D2D1_RECT_F edgePill = (state->dockEdge == DockEdge::Left)
                ? D2D1::RectF(clipDip.left, pillTop, clipDip.left + Theme::HandleWidth, pillTop + Theme::HandleHeight)
                : D2D1::RectF(clipDip.right - Theme::HandleWidth, pillTop, clipDip.right, pillTop + Theme::HandleHeight);
            DrawCapsulePill(rt, edgePill, capAlpha);
        }

        rt->SetTransform(D2D1::Matrix3x2F::Translation(slideX, 0.0f) * D2D1::Matrix3x2F::Scale(dpiScale, dpiScale));

        D2D1_RECT_F pb = PanelBoxDip();
        float panelAlpha = renderAsFloating ? 1.0f : std::clamp(t * 1.25f, 0.0f, 1.0f);

        // Ambient floating drop shadow
        DrawAmbientShadow(rt, canvas.Brush(D2D1::ColorF(0, 0, 0, 1)), pb, Theme::PanelRadius, panelAlpha);

        // Deep Linear/Raycast obsidian frosted gradient body
        {
            D2D1_GRADIENT_STOP bgStops[2] = {
                { 0.0f, Rgba(Theme::BgTop, 0.96f * panelAlpha) },
                { 1.0f, Rgba(Theme::BgBottom, 0.97f * panelAlpha) }
            };
            ComPtr<ID2D1GradientStopCollection> coll;
            if (SUCCEEDED(rt->CreateGradientStopCollection(bgStops, 2, coll.GetAddressOf()))) {
                ComPtr<ID2D1LinearGradientBrush> grad;
                rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(pb.left, pb.top), D2D1::Point2F(pb.left, pb.bottom)),
                    coll.Get(), grad.GetAddressOf());
                if (grad) rt->FillRoundedRectangle(D2D1::RoundedRect(pb, Theme::PanelRadius, Theme::PanelRadius), grad.Get());
            }
        }

        // 1px Top-lit specular rim
        DrawRim(rt, pb, Theme::PanelRadius,
                         Rgba(0x93C5FD, 0.24f * panelAlpha),
                         Rgba(0xFFFFFF, 0.06f * panelAlpha), 1.0f);

        // --- Header Bar (with Tilted Agent Star Capsule Brand Emblem) ---
        {
            float cx = pb.left + 17.5f;
            float cy = pb.top + 18.2f;
            D2D1_MATRIX_3X2_F prevTf;
            rt->GetTransform(&prevTf);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(-36.0f, D2D1::Point2F(cx, cy)) * prevTf);

            D2D1_RECT_F outerCap = D2D1::RectF(cx - 7.2f, cy - 3.8f, cx + 7.2f, cy + 3.8f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(outerCap, 3.8f, 3.8f), canvas.Brush(Rgba(0x161B29, 0.98f * panelAlpha)));

            D2D1_RECT_F innerCap = D2D1::RectF(cx - 4.6f, cy - 2.4f, cx + 5.2f, cy + 2.4f);
            D2D1_GRADIENT_STOP cStops[2] = {
                { 0.0f, Rgba(Theme::AccentIndigo, 0.96f * panelAlpha) },
                { 1.0f, Rgba(Theme::AccentCyan, 0.98f * panelAlpha) }
            };
            ComPtr<ID2D1GradientStopCollection> cColl;
            if (SUCCEEDED(rt->CreateGradientStopCollection(cStops, 2, cColl.GetAddressOf()))) {
                ComPtr<ID2D1LinearGradientBrush> cGrad;
                rt->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(innerCap.left, cy), D2D1::Point2F(innerCap.right, cy)),
                    cColl.Get(), cGrad.GetAddressOf());
                if (cGrad) rt->FillRoundedRectangle(D2D1::RoundedRect(innerCap, 2.4f, 2.4f), cGrad.Get());
            }
            rt->DrawRoundedRectangle(D2D1::RoundedRect(outerCap, 3.8f, 3.8f), canvas.Brush(Rgba(Theme::AccentCyan, 0.65f * panelAlpha)), 0.9f);

            // Upright 4-point AI star center gleam
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(45.0f, D2D1::Point2F(cx + 0.4f, cy)) * prevTf);
            D2D1_RECT_F starSq = D2D1::RectF(cx + 0.4f - 1.5f, cy - 1.5f, cx + 0.4f + 1.5f, cy + 1.5f);
            rt->FillRectangle(starSq, canvas.Brush(Rgba(0xFFFFFF, 0.98f * panelAlpha)));
            rt->SetTransform(prevTf);

            std::wstring hTitle = UiText(APP_DISPLAY_TITLE_W);
            if (fmtHeader) {
                rt->DrawTextW(hTitle.c_str(), (UINT32)hTitle.size(), fmtHeader.Get(),
                              D2D1::RectF(pb.left + 28.0f, pb.top + 4.0f, pb.left + 134.0f, pb.top + 32.0f),
                              canvas.Brush(Rgba(Theme::TextPrimary, 0.95f * panelAlpha)));
            }
            bool isReadOnlyWarn = (storage && storage->readOnlyRecoveryMode);
            bool showRedockBtn = (state->dockEdge == DockEdge::None && !isDragging && !isReadOnlyWarn);
            if (showRedockBtn) {
                D2D1_RECT_F rb = HeaderRedockBtnRectDip();
                rt->FillRoundedRectangle(D2D1::RoundedRect(rb, 5.5f, 5.5f),
                                         canvas.Brush(Rgba(hoverRedockBtn ? 0x264B7A : 0x1E3354, 0.96f * panelAlpha)));
                DrawRim(rt, rb, 5.5f,
                                 Rgba(Theme::AccentCyan, (hoverRedockBtn ? 0.65f : 0.38f) * panelAlpha),
                                 Rgba(0xFFFFFF, 0.06f * panelAlpha), 1.0f);
                std::wstring rdTxt = UiText(L"贴边");
                if (fmtFooterBtn) {
                    rt->DrawTextW(rdTxt.c_str(), (UINT32)rdTxt.size(), fmtFooterBtn.Get(), rb,
                                  canvas.Brush(Rgba(Theme::AccentCyan, 0.98f * panelAlpha)));
                }
            } else {
                std::wstring hSub = UiText(isReadOnlyWarn
                    ? L"只读保护 · 点此恢复"
                    : (renderAsFloating ? L"拖至边缘贴靠" : L"拖拽排序 · 右键管理"));
                if (fmtHeaderSub) {
                    D2D1_COLOR_F subCol = isReadOnlyWarn
                        ? Rgba(0xFBBF24, 0.96f * panelAlpha)
                        : Rgba(Theme::TextMuted, 0.88f * panelAlpha);
                    rt->DrawTextW(hSub.c_str(), (UINT32)hSub.size(), fmtHeaderSub.Get(),
                                  D2D1::RectF(pb.left + 126.0f, pb.top + 4.0f, pb.right - 10.0f, pb.top + 32.0f),
                                  canvas.Brush(subCol));
                }
            }
        }

        // --- 10 Fixed Muscle-Memory Slots (with Live Drag-to-Reorder & Short-Screen Scroll Clip) ---
        D2D1_RECT_F slotVp = SlotViewportDip();
        rt->PushAxisAlignedClip(slotVp, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        ULONGLONG nowTick = GetTickCount64();
        int onboardHighlightSlot = (onboardingWnd.step == OnboardingStep::Step2Create && !onboardingWnd.step2Done)
            ? onboardingWnd.targetSlot
            : ((onboardingWnd.step == OnboardingStep::Step1Copy && !onboardingWnd.step1Done) ? 0 : -1);
        for (int i = 0; i < 10; ++i) {
            D2D1_RECT_F sr = SlotRectDip(i);
            if (sr.bottom < slotVp.top - 4.0f || sr.top > slotVp.bottom + 4.0f) continue;
            wchar_t idxBuf[8]{};
            wsprintfW(idxBuf, L"%02d", i + 1);

            if (isSlotDragging && i == pressedSlot) {
                // Recessed glowing drop-target well for the currently dragged slot
                rt->FillRoundedRectangle(D2D1::RoundedRect(sr, Theme::SlotRadius, Theme::SlotRadius),
                                         canvas.Brush(Rgba(0x0D1017, 0.72f * panelAlpha)));
                DrawRim(rt, sr, Theme::SlotRadius,
                                 Rgba(Theme::AccentCyan, 0.34f * panelAlpha),
                                 Rgba(Theme::AccentIndigo, 0.18f * panelAlpha), 1.0f);
                D2D1_RECT_F idxBox = D2D1::RectF(sr.left + 7.0f, sr.top + 8.0f, sr.left + 31.0f, sr.bottom - 8.0f);
                if (fmtSlotIdx) {
                    rt->DrawTextW(idxBuf, 2, fmtSlotIdx.Get(), idxBox,
                                  canvas.Brush(Rgba(Theme::AccentCyan, 0.65f * panelAlpha)));
                }
                continue;
            }

            float hovT = isSlotDragging ? 0.0f : slotHoverAnim[(size_t)i];
            float yShift = slotShiftOffsetDip[(size_t)i];
            const PromptItem* item = state->FindById(state->ActiveSlots()[(size_t)i]);

            bool isCopyFlash = (copiedSlot == i) && (nowTick - copiedTick < 620);
            float flashT = isCopyFlash ? (1.0f - (float)(nowTick - copiedTick) / 620.0f) : 0.0f;
            bool isFailFlash = (copyFailedSlot == i) && (nowTick - copyFailedTick < 1200);
            float failT = isFailFlash ? (1.0f - (float)(nowTick - copyFailedTick) / 1200.0f) : 0.0f;

            float nudge = hovT * (state->dockEdge == DockEdge::Right ? -1.5f : 1.5f);
            D2D1_RECT_F card = D2D1::RectF(sr.left + nudge, sr.top + yShift, sr.right + nudge, sr.bottom + yShift);

            if (item) {
                D2D1_COLOR_F baseFill = LerpColor(Rgba(Theme::CardBg, 0.86f * panelAlpha),
                                                  Rgba(Theme::CardHoverBg, 0.98f * panelAlpha), hovT);
                if (failT > 0.0f) {
                    baseFill = LerpColor(baseFill, Rgba(0x4A1822, 0.98f * panelAlpha), std::min(1.0f, failT * 1.4f));
                } else if (flashT > 0.0f) {
                    baseFill = LerpColor(baseFill, Rgba(0x114232, 0.98f * panelAlpha), std::min(1.0f, flashT * 1.4f));
                }
                rt->FillRoundedRectangle(D2D1::RoundedRect(card, Theme::SlotRadius, Theme::SlotRadius), canvas.Brush(baseFill));

                D2D1_COLOR_F rimTop = LerpColor(Rgba(0xFFFFFF, 0.07f * panelAlpha), Rgba(Theme::AccentBlue, 0.45f * panelAlpha), hovT);
                if (i == onboardHighlightSlot) {
                    rimTop = Rgba(Theme::AccentCyan, 0.88f * panelAlpha);
                }
                if (failT > 0.0f) {
                    rimTop = LerpColor(rimTop, Rgba(Theme::DangerRed, 0.84f * panelAlpha), std::min(1.0f, failT * 1.5f));
                } else if (flashT > 0.0f) {
                    rimTop = LerpColor(rimTop, Rgba(Theme::CopyGreen, 0.78f * panelAlpha), std::min(1.0f, flashT * 1.5f));
                }
                DrawRim(rt, card, Theme::SlotRadius, rimTop, Rgba(0xFFFFFF, 0.03f * panelAlpha), (i == onboardHighlightSlot) ? 1.4f : 1.0f);

                // Slot index pill
                D2D1_RECT_F idxBox = D2D1::RectF(card.left + 7.0f, card.top + 8.0f, card.left + 31.0f, card.bottom - 8.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(idxBox, 4.5f, 4.5f),
                                         canvas.Brush(Rgba(failT > 0.0f ? Theme::DangerRed : (hovT > 0.2f ? Theme::AccentCyan : 0xFFFFFF),
                                                           (failT > 0.0f ? 0.16f : (0.05f + 0.08f * hovT)) * panelAlpha)));
                if (fmtSlotIdx) {
                    D2D1_COLOR_F idxCol = LerpColor(Rgba(Theme::TextSecondary, 0.78f * panelAlpha),
                                                    Rgba(Theme::AccentCyan, 1.0f * panelAlpha), hovT);
                    if (failT > 0.0f) idxCol = Rgba(Theme::DangerRed, 1.0f * panelAlpha);
                    else if (flashT > 0.0f) idxCol = Rgba(Theme::CopyGreen, 1.0f * panelAlpha);
                    rt->DrawTextW(idxBuf, 2, fmtSlotIdx.Get(), idxBox, canvas.Brush(idxCol));
                }

                // Right status / usage micro-badge
                float rightReserve = 10.0f;
                if (failT > 0.0f && fmtSlotBadge) {
                    rightReserve = 104.0f;
                    std::wstring failTxt = UiText(L"复制失败，请重试");
                    rt->DrawTextW(failTxt.c_str(), (UINT32)failTxt.size(), fmtSlotBadge.Get(),
                                  D2D1::RectF(card.right - 108.0f, card.top, card.right - 9.0f, card.bottom),
                                  canvas.Brush(Rgba(Theme::DangerRed, 0.98f * panelAlpha)));
                } else if (flashT > 0.0f && fmtSlotBadge) {
                    rightReserve = 62.0f;
                    std::wstring copiedTxt = UiText(L"\x2713 \x5DF2\x590D\x5236");
                    rt->DrawTextW(copiedTxt.c_str(), (UINT32)copiedTxt.size(), fmtSlotBadge.Get(),
                                  D2D1::RectF(card.right - 64.0f, card.top, card.right - 9.0f, card.bottom),
                                  canvas.Brush(Rgba(Theme::CopyGreen, 0.98f * panelAlpha)));
                } else if ((hovT > 0.15f || item->useCount > 0) && fmtSlotBadge) {
                    if (item->useCount > 0) {
                        rightReserve = 48.0f;
                        wchar_t uBuf[32]{};
                        wsprintfW(uBuf, L"%I64u \x6B21", (unsigned long long)item->useCount);
                        std::wstring uStr = UiText(uBuf);
                        rt->DrawTextW(uStr.c_str(), (UINT32)uStr.size(), fmtSlotBadge.Get(),
                                      D2D1::RectF(card.right - 52.0f, card.top, card.right - 9.0f, card.bottom),
                                      canvas.Brush(Rgba(Theme::TextSecondary, (0.42f + 0.45f * hovT) * panelAlpha)));
                    }
                }

                // Prompt Title
                if (fmtSlotTitle) {
                    rt->DrawTextW(item->title.c_str(), (UINT32)item->title.size(), fmtSlotTitle.Get(),
                                  D2D1::RectF(idxBox.right + 8.0f, card.top, card.right - rightReserve, card.bottom),
                                  canvas.Brush(Rgba(hovT > 0.3f ? 0xFFFFFF : Theme::TextPrimary, 0.96f * panelAlpha)));
                }
            } else {
                // Empty '+' slot (highlighted during Onboarding Step 2)
                bool isOnboardTarget = (i == onboardHighlightSlot);
                D2D1_COLOR_F emptyFill = isOnboardTarget
                    ? Rgba(0x16263A, 0.92f * panelAlpha)
                    : LerpColor(Rgba(Theme::CardEmptyBg, 0.52f * panelAlpha),
                                Rgba(Theme::CardBg, 0.85f * panelAlpha), hovT);
                rt->FillRoundedRectangle(D2D1::RoundedRect(card, Theme::SlotRadius, Theme::SlotRadius), canvas.Brush(emptyFill));
                DrawRim(rt, card, Theme::SlotRadius,
                                 isOnboardTarget ? Rgba(Theme::AccentCyan, 0.85f * panelAlpha)
                                                 : LerpColor(Rgba(0xFFFFFF, 0.04f * panelAlpha), Rgba(Theme::AccentBlue, 0.28f * panelAlpha), hovT),
                                 Rgba(0xFFFFFF, 0.02f * panelAlpha), isOnboardTarget ? 1.4f : 1.0f);

                D2D1_RECT_F idxBox = D2D1::RectF(card.left + 7.0f, card.top + 8.0f, card.left + 31.0f, card.bottom - 8.0f);
                if (fmtSlotIdx) {
                    rt->DrawTextW(idxBuf, 2, fmtSlotIdx.Get(), idxBox,
                                  canvas.Brush(isOnboardTarget ? Rgba(Theme::AccentCyan, 0.98f * panelAlpha)
                                                               : Rgba(Theme::TextMuted, (0.55f + 0.35f * hovT) * panelAlpha)));
                }
                std::wstring emptyLbl = UiText(isOnboardTarget
                    ? L"+ 添加我的第一条 Prompt"
                    : (hovT > 0.35f ? L"+ 添加或从词舱选入" : L"+ 添加我的 Prompt"));
                if (fmtSlotEmpty) {
                    rt->DrawTextW(emptyLbl.c_str(), (UINT32)emptyLbl.size(), fmtSlotEmpty.Get(),
                                  D2D1::RectF(idxBox.right + 8.0f, card.top, card.right - 10.0f, card.bottom),
                                  canvas.Brush(isOnboardTarget
                                               ? Rgba(Theme::AccentCyan, 0.98f * panelAlpha)
                                               : LerpColor(Rgba(Theme::TextMuted, 0.68f * panelAlpha),
                                                           Rgba(Theme::TextSecondary, 0.95f * panelAlpha), hovT)));
                }
            }
        }

        // Draw subtle scrollbar indicator when short-screen slot scrolling is active
        if (maxSlotScrollDip > 0.5f) {
            float vpH = slotVp.bottom - slotVp.top;
            float fullH = 10.0f * Theme::SlotHeight + 9.0f * Theme::SlotGap;
            float thumbH = std::clamp(vpH * (vpH / std::max(1.0f, fullH)), 26.0f, vpH - 8.0f);
            float progress = std::clamp(slotScrollDip / maxSlotScrollDip, 0.0f, 1.0f);
            float thumbTop = slotVp.top + 4.0f + progress * std::max(0.0f, vpH - 8.0f - thumbH);
            D2D1_RECT_F sbThumb = D2D1::RectF(pb.right - 7.5f, thumbTop, pb.right - 4.0f, thumbTop + thumbH);
            rt->FillRoundedRectangle(D2D1::RoundedRect(sbThumb, 1.75f, 1.75f),
                                     canvas.Brush(Rgba(Theme::AccentCyan, 0.45f * panelAlpha)));
        }

        // Draw floating elevated card on top while dragging a slot!
        if (isSlotDragging && pressedSlot >= 0 && pressedSlot < 10) {
            const PromptItem* dragItem = state->FindById(state->ActiveSlots()[(size_t)pressedSlot]);
            if (dragItem) {
                D2D1_RECT_F dragCard = D2D1::RectF(
                    pb.left + Theme::PaddingX - 2.0f,
                    slotDragCurDipY,
                    pb.right - Theme::PaddingX + 2.0f,
                    slotDragCurDipY + Theme::SlotHeight
                );
                DrawAmbientShadow(rt, canvas.Brush(D2D1::ColorF(0, 0, 0, 1)), dragCard, Theme::SlotRadius, 0.90f * panelAlpha);
                rt->FillRoundedRectangle(D2D1::RoundedRect(dragCard, Theme::SlotRadius, Theme::SlotRadius),
                                         canvas.Brush(Rgba(0x283147, 0.99f * panelAlpha)));
                DrawRim(rt, dragCard, Theme::SlotRadius,
                                 Rgba(Theme::AccentCyan, 0.72f * panelAlpha),
                                 Rgba(Theme::AccentBlue, 0.35f * panelAlpha), 1.2f);

                D2D1_RECT_F idxBox = D2D1::RectF(dragCard.left + 7.0f, dragCard.top + 8.0f, dragCard.left + 31.0f, dragCard.bottom - 8.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(idxBox, 4.5f, 4.5f),
                                         canvas.Brush(Rgba(Theme::AccentCyan, 0.18f * panelAlpha)));
                wchar_t idxBuf[8]{};
                wsprintfW(idxBuf, L"%02d", pressedSlot + 1);
                if (fmtSlotIdx) {
                    rt->DrawTextW(idxBuf, 2, fmtSlotIdx.Get(), idxBox,
                                  canvas.Brush(Rgba(Theme::AccentCyan, 1.0f * panelAlpha)));
                }
                if (fmtSlotTitle) {
                    rt->DrawTextW(dragItem->title.c_str(), (UINT32)dragItem->title.size(), fmtSlotTitle.Get(),
                                  D2D1::RectF(idxBox.right + 8.0f, dragCard.top, dragCard.right - 28.0f, dragCard.bottom),
                                  canvas.Brush(Rgba(0xFFFFFF, 1.0f * panelAlpha)));
                }
                if (fmtSlotBadge) {
                    std::wstring grip = L"\u21C5";
                    rt->DrawTextW(grip.c_str(), (UINT32)grip.size(), fmtSlotBadge.Get(),
                                  D2D1::RectF(dragCard.right - 30.0f, dragCard.top, dragCard.right - 9.0f, dragCard.bottom),
                                  canvas.Brush(Rgba(Theme::AccentCyan, 0.90f * panelAlpha)));
                }
            }
        }
        rt->PopAxisAlignedClip(); // End slot viewport clip

        // --- Optional Footer Multi-Scene Buttons (2..4 checked scenes in-place using unified card button UI) ---
        if (ShowSceneBar()) {
            int vis[kMaxScenes]{};
            int cnt = state->GetEnabledScenes(vis);
            int actIdx = state->EffectiveActiveSceneIndex();
            IDWriteTextFormat* btnFmt = (cnt >= 4 && fmtFooterBtnCompact) ? fmtFooterBtnCompact.Get() : fmtFooterBtn.Get();
            for (int p = 0; p < cnt; ++p) {
                int s = vis[p];
                D2D1_RECT_F sr = SceneBtnRectDip(p, cnt);
                bool active = (s == actIdx);
                bool hov = (s == hoverSceneBtn);
                uint32_t sceneTextHex = Theme::SceneTextAccent(s);

                D2D1_COLOR_F fillCol = active
                    ? Rgba(hov ? 0x26324B : 0x202A40, 0.96f * panelAlpha)
                    : Rgba(hov ? 0x242A3B : 0x171B26, (hov ? 0.95f : 0.72f) * panelAlpha);
                D2D1_COLOR_F rimTop = active
                    ? Rgba(sceneTextHex, (hov ? 0.58f : 0.44f) * panelAlpha)
                    : Rgba(hov ? Theme::AccentBlue : 0xFFFFFF, (hov ? 0.32f : 0.06f) * panelAlpha);
                D2D1_COLOR_F rimBot = active
                    ? Rgba(Theme::AccentBlue, 0.14f * panelAlpha)
                    : Rgba(0xFFFFFF, 0.02f * panelAlpha);

                rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 6.5f, 6.5f), canvas.Brush(fillCol));
                DrawRim(rt, sr, 6.5f, rimTop, rimBot, 1.0f);

                std::wstring sLabel = UiText(state->scenes[(size_t)s].name);
                if (btnFmt) {
                    D2D1_RECT_F txtR = D2D1::RectF(sr.left + 2.0f, sr.top, sr.right - 2.0f, sr.bottom);
                    D2D1_COLOR_F txtCol = active
                        ? Rgba(sceneTextHex, 0.98f * panelAlpha)
                        : Rgba(hov ? Theme::TextPrimary : Theme::TextSecondary, (hov ? 0.98f : 0.80f) * panelAlpha);
                    rt->DrawTextW(sLabel.c_str(), (UINT32)sLabel.size(), btnFmt, txtR, canvas.Brush(txtCol));
                }
            }
        }

        rt->PopAxisAlignedClip();

        int prevX = curWinX, prevY = curWinY;
        bool ok = false;
        {
            ScopedSelfPresent guard(selfPresentDepth);
            ok = canvas.EndAndPresent(hwnd, winX, winY);
        }
        if (ok) {
            curWinX = winX; curWinY = winY; curWinW = wPx; curWinH = hPx;
            curMonFull = geom.monFull; hasCurMonFull = true;
            hasPresentedOnce = true;
            if (onboardingWnd.IsActive() && t >= 0.95f) {
                SyncOnboardingPosition();
            }
            if (lastLoggedMode != 1 || reason != nullptr || std::abs(prevX - winX) > 32 || std::abs(prevY - winY) > 32) {
                lastLoggedMode = 1;
                DiagLogger::Instance().LogFmt("INFO", "GEOM",
                    L"Mode=Expanded(%hs) edge=%d pos=(%d,%d %dx%d) mon=(%ld,%ld)-(%ld,%ld) spring=%.3f dpi=%.2f",
                    reason ? reason : "transition", (int)state->dockEdge,
                    winX, winY, wPx, hPx,
                    monFull.left, monFull.top, monFull.right, monFull.bottom,
                    t, dpiScale);
            }
        }
        return ok;
    }

    bool HitTestRedockBtnDip(float dx, float dy) const {
        if (!state || state->dockEdge != DockEdge::None || isDragging) return false;
        if (storage && storage->readOnlyRecoveryMode) return false;
        D2D1_RECT_F rb = HeaderRedockBtnRectDip();
        return dx >= rb.left && dx <= rb.right && dy >= rb.top && dy <= rb.bottom;
    }

    void HitTestExpandedDip(float dx, float dy, int& outSlot, bool& outHeader) const {
        outSlot = -1;
        outHeader = false;
        D2D1_RECT_F pb = PanelBoxDip();
        if (dx < pb.left || dx > pb.right || dy < pb.top || dy > pb.bottom) return;
        if (dy <= pb.top + Theme::HeaderHeight) {
            outHeader = true;
            return;
        }
        D2D1_RECT_F slotVp = SlotViewportDip();
        if (dy >= slotVp.top && dy <= slotVp.bottom) {
            for (int i = 0; i < 10; ++i) {
                D2D1_RECT_F sr = SlotRectDip(i);
                if (dx >= sr.left && dx <= sr.right && dy >= sr.top && dy <= sr.bottom) {
                    outSlot = i;
                    return;
                }
            }
        }
    }

    [[gnu::noinline]] void TriggerSlotCopy(int slotIdx) {
        if (slotIdx < 0 || slotIdx >= 10) return;
        KillTimer(hwnd, kTimerBubbleHover);
        hoverBubble.HideSmooth();
        PromptItem* item = state->FindById(state->ActiveSlots()[(size_t)slotIdx]);
        if (!item) return;

        bool copied = clipboardWriter ? clipboardWriter(item->content)
                                      : CopyTextToClipboard(hwnd, item->content);
        if (copied) {
            item->useCount += 1;
            item->lastUsedAt = NowUnixSeconds();
            if (!storage->Save(*state)) {
                DiagLogger::Instance().Log("WARN", "COPY", L"Copied to clipboard, but persisting updated useCount failed");
            }
            if (vaultWnd) vaultWnd->RefreshIfOpen();

            copyFailedSlot = -1;
            copyFailedTick = 0;
            copiedSlot = slotIdx;
            copiedTick = GetTickCount64();
            EnsureAnimTimer();

            if (onboardingWnd.IsActive()) {
                onboardingWnd.NotifyCopied();
            } else if (state->dockEdge != DockEdge::None) {
                uint64_t nextGen = ++retractGen;
                if (SetTimer(hwnd, kTimerAutoRetract, 340, nullptr) != 0) {
                    pendingAutoRetract = true;
                    pendingAutoRetractGen = nextGen;
                } else {
                    pendingAutoRetract = false;
                    DiagLogger::Instance().LogFmt("ERROR", "TIMER", L"SetTimer(kTimerAutoRetract) failed err=%lu", (unsigned long)GetLastError());
                    EvaluateCollapseState("auto-retract-timer-fail");
                }
            }
            std::wstring safeId = SanitizeLogField(item->id, 64);
            DiagLogger::Instance().LogFmt("INFO", "COPY",
                L"Copied slot=%02d id=%ls uses=%llu",
                slotIdx + 1, safeId.c_str(), (unsigned long long)item->useCount);
        } else {
            // Clipboard write failed: inline red flash (1200ms), NEVER increment useCount, NEVER auto-retract!
            KillTimer(hwnd, kTimerAutoRetract);
            pendingAutoRetract = false;
            ++retractGen;
            copiedSlot = -1;
            copiedTick = 0;
            copyFailedSlot = slotIdx;
            copyFailedTick = GetTickCount64();
            EnsureAnimTimer();
            RenderAndPresent("copy-failed");
            DiagLogger::Instance().LogFmt("WARN", "COPY",
                L"Clipboard copy failed on slot=%02d err=%lu",
                slotIdx + 1, GetLastError());
        }
    }

    void ShowSlotMenu(int slotIdx, POINT screenPt, bool emptySlotMode) {
        if (!popupMenu || slotIdx < 0 || slotIdx >= 10) return;
        std::vector<DarkMenuItem> items;

        wchar_t slotNum[8]{};
        wsprintfW(slotNum, L"%02d", slotIdx + 1);
        PromptItem* current = state->FindById(state->ActiveSlots()[(size_t)slotIdx]);
        std::wstring scenePrefix = ShowSceneBar() ? (state->ActiveScene().name + L" \x00B7 ") : L"";

        if (!emptySlotMode && current) {
            items.push_back(DarkMenuItem::Header(scenePrefix + L"\x69FD\x4F4D " + std::wstring(slotNum) + L" \x00B7 " + current->title));
            items.push_back(DarkMenuItem::Action(1, L"\x7F16\x8F91\x6B64 Prompt..."));
            items.push_back(DarkMenuItem::Action(2, L"\x65B0\x5EFA\x5E76\x66FF\x6362\x6B64\x683C..."));
        } else {
            items.push_back(DarkMenuItem::Header(scenePrefix + L"\x88C5\x914D\x7A7A\x69FD\x4F4D " + std::wstring(slotNum)));
            items.push_back(DarkMenuItem::Action(2, L"+ \x65B0\x5EFA Prompt \x653E\x5165\x6B64\x683C..."));
        }

        // Gather unmounted vault items sorted by lastUsedAt desc, then useCount desc
        std::vector<const PromptItem*> bench = SelectQuickSwapCandidates(*state, 6);

        items.push_back(DarkMenuItem::Separator());
        if (!bench.empty()) {
            items.push_back(DarkMenuItem::Header(current ? L"\x4ECE\x8BCD\x8231\x5FEB\x901F\x66FF\x6362" : L"\x4ECE\x8BCD\x8231\x5FEB\x901F\x9009\x5165"));
            for (size_t k = 0; k < bench.size(); ++k) {
                wchar_t hint[32]{};
                wsprintfW(hint, L"%I64u \x6B21", (unsigned long long)bench[k]->useCount);
                items.push_back(DarkMenuItem::Action(1000 + (int)k, bench[k]->title, hint));
            }
        }
        items.push_back(DarkMenuItem::Action(3, L"\x6253\x5F00\x8BCD\x8231\x6311\x9009\x4E0E\x641C\x7D22..."));

        if (!emptySlotMode && current) {
            items.push_back(DarkMenuItem::Separator());
            items.push_back(DarkMenuItem::Action(4, L"\x4E0A\x79FB\x4E00\x683C", L"", false, false, slotIdx == 0));
            items.push_back(DarkMenuItem::Action(5, L"\x4E0B\x79FB\x4E00\x683C", L"", false, false, slotIdx == 9));
            items.push_back(DarkMenuItem::Separator());
            items.push_back(DarkMenuItem::Action(6, L"\x79FB\x56DE\x8BCD\x8231\xFF08\x817E\x7A7A\x69FD\x4F4D\xFF09", L"", false, true));
        }
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(7, L"\x6253\x5F00\x8BBE\x7F6E..."));

        // Capture bench IDs by value safely
        std::vector<std::wstring> benchIds;
        for (size_t k = 0; k < std::min<size_t>(bench.size(), 6); ++k) benchIds.push_back(bench[k]->id);
        std::wstring curId = current ? current->id : L"";

        popupMenu->ShowAt(screenPt, items, [this, slotIdx, curId, benchIds](int cmd) {
            if (cmd == 1 && !curId.empty() && vaultWnd) {
                vaultWnd->OpenEditPromptModal(curId);
            } else if (cmd == 2 && vaultWnd) {
                if (onboardingWnd.step == OnboardingStep::Step2Create && !onboardingWnd.step2Done) {
                    TriggerOnboardingCreatePrompt(slotIdx);
                } else {
                    vaultWnd->OpenNewPromptModal(slotIdx);
                }
            } else if (cmd == 3 && vaultWnd) {
                vaultWnd->Open(0, slotIdx);
            } else if (cmd == 4 && slotIdx > 0) {
                auto& sl = state->ActiveSlots();
                std::swap(sl[(size_t)slotIdx], sl[(size_t)(slotIdx - 1)]);
                storage->Save(*state);
                if (vaultWnd) vaultWnd->RefreshIfOpen();
                RenderAndPresent();
            } else if (cmd == 5 && slotIdx < 9) {
                auto& sl = state->ActiveSlots();
                std::swap(sl[(size_t)slotIdx], sl[(size_t)(slotIdx + 1)]);
                storage->Save(*state);
                if (vaultWnd) vaultWnd->RefreshIfOpen();
                RenderAndPresent();
            } else if (cmd == 6) {
                state->ActiveSlots()[(size_t)slotIdx].clear();
                storage->Save(*state);
                if (vaultWnd) vaultWnd->RefreshIfOpen();
                RenderAndPresent();
            } else if (cmd == 7 && vaultWnd) {
                vaultWnd->Open(2, -1);
            } else if (cmd >= 1000 && (size_t)(cmd - 1000) < benchIds.size()) {
                state->AssignToSlot(slotIdx, benchIds[(size_t)(cmd - 1000)]);
                storage->Save(*state);
                if (vaultWnd) vaultWnd->RefreshIfOpen();
                RenderAndPresent();
            }
        }, 250.0f, MenuOwner::Dock);
    }

    void ShowSceneMenu(int sceneIdx, POINT screenPt) {
        if (!popupMenu || !state || sceneIdx < 0 || sceneIdx >= kMaxScenes || !state->scenes[(size_t)sceneIdx].enabled) return;
        int mounted = state->CountMountedInScene(sceneIdx);
        std::vector<DarkMenuItem> items;
        items.push_back(DarkMenuItem::Header(L"\x573A\x666F \x00B7 " + state->scenes[(size_t)sceneIdx].name));
        items.push_back(DarkMenuItem::Action(6, L"\x914D\x7F6E\x6B64\x573A\x666F\x63D0\x793A\x8BCD..."));
        items.push_back(DarkMenuItem::Action(1, L"\x91CD\x547D\x540D\x6B64\x573A\x666F..."));
        items.push_back(DarkMenuItem::Action(2, L"\x6E05\x7A7A\x6B64\x573A\x666F\x69FD\x4F4D\xFF08\x4FDD\x7559\x8BCD\x8231\xFF09", L"", false, true, mounted == 0));
        items.push_back(DarkMenuItem::Separator());
        items.push_back(DarkMenuItem::Action(4, L"\x6253\x5F00\x8BCD\x8231\x7BA1\x7406..."));
        items.push_back(DarkMenuItem::Action(5, L"\x4F7F\x7528\x6392\x884C..."));
        items.push_back(DarkMenuItem::Action(3, L"\x6253\x5F00\x8BBE\x7F6E..."));

        popupMenu->ShowAt(screenPt, items, [this, sceneIdx](int cmd) {
            if (!vaultWnd) return;
            if (cmd == 6) {
                vaultWnd->OpenScenePromptsModal(sceneIdx);
            } else if (cmd == 1) {
                vaultWnd->OpenRenameSceneModal(sceneIdx);
            } else if (cmd == 2) {
                vaultWnd->ConfirmClearSceneSlots(sceneIdx);
            } else if (cmd == 4) {
                vaultWnd->Open(0, -1);
            } else if (cmd == 5) {
                vaultWnd->Open(1, -1);
            } else if (cmd == 3) {
                vaultWnd->Open(2, -1);
            }
        }, 240.0f, MenuOwner::Dock);
    }

    void OnAnimTick() {
        LARGE_INTEGER nowQpc{};
        QueryPerformanceCounter(&nowQpc);
        float dt = (float)(nowQpc.QuadPart - lastAnimQpc.QuadPart) / (float)qpcFreq.QuadPart;
        lastAnimQpc = nowQpc;

        expandSpring.Step(dt);

        float alphaStep = std::min(1.0f, dt * 14.0f);
        float shiftDecay = std::min(1.0f, dt * 19.0f);
        for (int i = 0; i < 10; ++i) {
            float target = (hoverSlot == i) ? 1.0f : 0.0f;
            slotHoverAnim[(size_t)i] += (target - slotHoverAnim[(size_t)i]) * alphaStep;
            if (std::fabs(slotHoverAnim[(size_t)i] - target) < 0.008f) {
                slotHoverAnim[(size_t)i] = target;
            }
            if (std::fabs(slotShiftOffsetDip[(size_t)i]) > 0.0f) {
                slotShiftOffsetDip[(size_t)i] *= (1.0f - shiftDecay);
                if (std::fabs(slotShiftOffsetDip[(size_t)i]) < 0.12f) {
                    slotShiftOffsetDip[(size_t)i] = 0.0f;
                }
            }
        }

        RenderAndPresent();
        // If user moved onto a slot while the expand spring was still < 0.72 and then stopped moving the mouse,
        // WM_MOUSEMOVE will NOT fire again once the spring crosses 0.72. Re-sync the hover bubble here!
        if (hoverSlot >= 0 && state && state->showHoverBubble &&
            expandSpring.target > 0.5f && expandSpring.value >= 0.72f &&
            !hoverBubble.IsShowingOrAppearing()) {
            SyncHoverBubble(false);
        }
        StopAnimTimerIfIdle();
    }

    // Single source of truth for deciding whether a docked expanded panel should stay open or collapse
    [[gnu::noinline]] void EvaluateCollapseState(const char* trigger) {
        if (!hwnd || !state) return;
        if (state->dockEdge == DockEdge::None) {
            StopLeaveWatchTimer();
            KillTimer(hwnd, kTimerAutoRetract);
            pendingAutoRetract = false;
            return;
        }
        if (IsCompactCollapsedMode() || expandSpring.target < 0.5f) {
            StopLeaveWatchTimer();
            return;
        }
        ULONGLONG now = GetTickCount64();
        if (IsOnboardingActive() || isDragging || isSlotDragging || pressedSlot >= 0 || pressedEmptySlot >= 0 ||
            pressedSceneBtn >= 0 || pressedRedockBtn || IsDockMenuOpen() || pendingAutoRetract) {
            mouseOutsideSince = now;
            if (!leaveWatchRunning) EnsureLeaveWatchTimer();
            return;
        }

        POINT pt{};
        float dx = 0.0f, dy = 0.0f;
        bool inside = IsCursorInsideInteractivePanel(&pt, &dx, &dy);

        if (inside) {
            mouseOutsideSince = now;
            holdOpenUntilTick = 0; // Once cursor enters card body, normal leave behavior takes over
            // Self-heal hover state if cursor is stationary inside a slot (e.g. panel slid out under cursor)
            int ns = -1;
            bool nh = false;
            HitTestExpandedDip(dx, dy, ns, nh);
            int nsc = HitTestSceneBtnDip(dx, dy);
            if (ns != hoverSlot || nsc != hoverSceneBtn) {
                hoverSlot = ns;
                hoverSceneBtn = nsc;
                SyncHoverBubble(false);
                EnsureAnimTimer();
                RenderAndPresent();
            }
            if (!leaveWatchRunning) EnsureLeaveWatchTimer();
        } else {
            if (hoverSlot != -1 || hoverSceneBtn != -1) {
                hoverSlot = -1;
                hoverSceneBtn = -1;
                SyncHoverBubble(false);
                EnsureAnimTimer();
            }
            if (mouseOutsideSince == 0) mouseOutsideSince = now;
            bool holdExpired = (now >= holdOpenUntilTick);
            bool leaveDebounced = (now >= mouseOutsideSince) && ((now - mouseOutsideSince) >= 170);
            if (holdExpired && leaveDebounced) {
                StopLeaveWatchTimer();
                handleHovered = false;
                if (trigger && (strcmp(trigger, "watchdog-expanded-stall") == 0 || strcmp(trigger, "watchdog-stale-retract") == 0)) {
                    DiagLogger::Instance().LogFmt("WARN", "SELF_HEAL",
                        L"EvaluateCollapseState(%hs) collapsing stalled expanded panel: edge=%d spring=%.3f outsideMs=%llu",
                        trigger, (int)state->dockEdge, expandSpring.value, (unsigned long long)(now - mouseOutsideSince));
                }
                StartSpringTo(0.0f);
            } else if (!EnsureLeaveWatchTimer() && holdExpired) {
                // Fail-closed: if OS refuses leave-watch timer after hold expired, collapse now instead of sticking open
                handleHovered = false;
                StartSpringTo(0.0f);
            }
        }
    }

    void OnLeaveWatchTimer() {
        EvaluateCollapseState("leave-watch");
    }

    void OnDisplaySyncTimer() {
        KillTimer(hwnd, kTimerDisplaySync);
        dpiScale = GetDpiScaleForWindow(hwnd);
        // Geometry & pixel sync only; avoid fighting own menus/editor Z-order during staged sync
        if (showInitialFlag && !IsWindowVisible(hwnd)) {
            EnsureTopmostAndVisible("display-sync-stage-hidden", true);
        }
        RenderAndPresent("display-sync-stage");
        ++displaySyncStage;
        if (displaySyncStage == 1) {
            SetTimer(hwnd, kTimerDisplaySync, 450, nullptr);
        } else if (displaySyncStage == 2) {
            SetTimer(hwnd, kTimerDisplaySync, 1100, nullptr);
        }
    }

    static DWORD GetDwmCloakedState(HWND h) {
        DWORD cloaked = 0;
        if (h) DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        return cloaked;
    }

    static bool IsOwnProcessWindow(HWND h) {
        if (!h) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        return pid == GetCurrentProcessId();
    }

    static std::wstring GetWindowClassStr(HWND h) {
        if (!h) return L"";
        wchar_t buf[128]{};
        int n = GetClassNameW(h, buf, _countof(buf));
        return n > 0 ? std::wstring(buf, (size_t)n) : L"";
    }

    static bool IsSystemShellOrTransientWindow(HWND h) {
        if (!h) return false;
        std::wstring cls = GetWindowClassStr(h);
        if (cls == L"Shell_TrayWnd" ||
            cls == L"Shell_SecondaryTrayWnd" ||
            cls == L"NotifyIconOverflowWindow" ||
            cls == L"Windows.UI.Core.CoreWindow" ||
            cls == L"XamlExplorerHostIslandWindow" ||
            cls == L"ForegroundStaging" ||
            cls == L"TaskListThumbnailWnd" ||
            cls == L"MultitaskingViewFrame" ||
            cls == L"MSCTFIME UI" ||
            cls == L"IME") {
            return true;
        }
        if (cls.rfind(L"Weasel", 0) == 0 || cls.rfind(L"IME", 0) == 0) return true;
        return false;
    }

    bool IsFullscreenOrPresentationActive(const RECT& dockMon) const {
        QUERY_USER_NOTIFICATION_STATE quns = QUNS_ACCEPTS_NOTIFICATIONS;
        if (SUCCEEDED(SHQueryUserNotificationState(&quns))) {
            if (quns == QUNS_RUNNING_D3D_FULL_SCREEN || quns == QUNS_PRESENTATION_MODE) {
                return true;
            }
        }
        HWND fg = GetForegroundWindow();
        if (!fg || fg == hwnd || IsOwnProcessWindow(fg) || !IsWindowVisible(fg) || IsIconic(fg)) {
            return false;
        }
        std::wstring cls = GetWindowClassStr(fg);
        if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd") {
            return false;
        }
        RECT fgRc{};
        if (!GetWindowRect(fg, &fgRc)) return false;
        bool coversFullMon = (fgRc.left <= dockMon.left && fgRc.top <= dockMon.top &&
                              fgRc.right >= dockMon.right && fgRc.bottom >= dockMon.bottom);
        if (!coversFullMon) return false;
        // Normal maximized windows stay within work area (leaving taskbar exposed) and keep WS_CAPTION
        LONG_PTR fgStyle = GetWindowLongPtrW(fg, GWL_STYLE);
        return (fgStyle & WS_CAPTION) == 0 || (fgRc.bottom >= dockMon.bottom);
    }

    bool CheckIsAboveInZOrder(HWND candidateAbove) const {
        if (!candidateAbove || candidateAbove == hwnd) return false;
        if (aboveInZOrderProvider) {
            return aboveInZOrderProvider(candidateAbove, hwnd);
        }
        HWND cur = hwnd;
        for (int hops = 0; hops < 512; ++hops) {
            cur = GetWindow(cur, GW_HWNDPREV);
            if (!cur) break;
            if (cur == candidateAbove) return true;
        }
        return false;
    }

    HWND FindExternalOccluderAbove(const POINT& probePt, bool* outIsTopmost = nullptr) const {
        if (outIsTopmost) *outIsTopmost = false;
        HWND cur = hwnd;
        for (int hops = 0; hops < 512; ++hops) {
            cur = GetWindow(cur, GW_HWNDPREV);
            if (!cur) break;
            if (IsOwnProcessWindow(cur)) continue;
            if (!IsWindowVisible(cur) || IsIconic(cur)) continue;
            if (GetDwmCloakedState(cur) != 0) continue;
            LONG_PTR ex = GetWindowLongPtrW(cur, GWL_EXSTYLE);
            if (ex & WS_EX_TRANSPARENT) continue;
            if (ex & WS_EX_LAYERED) {
                BYTE alpha = 255;
                DWORD flags = 0;
                if (GetLayeredWindowAttributes(cur, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) {
                    continue;
                }
            }
            RECT rc{};
            if (!GetWindowRect(cur, &rc)) continue;
            if (probePt.x >= rc.left && probePt.x < rc.right &&
                probePt.y >= rc.top && probePt.y < rc.bottom) {
                if (outIsTopmost) *outIsTopmost = ((ex & WS_EX_TOPMOST) != 0);
                return cur;
            }
        }
        return nullptr;
    }

    HWND ProbeTopWindowAt(POINT pt) const {
        if (hitProbeProvider) return hitProbeProvider(pt);
        HWND raw = WindowFromPoint(pt);
        if (!raw) return nullptr;
        HWND root = GetAncestor(raw, GA_ROOT);
        return root ? root : raw;
    }

    // First-principles 3-second watchdog: verifies visibility, DWM cloak, 4-edge geometry,
    // physical solid-capsule hit reachability, relative Z-order occlusion, and layered surface integrity.
    void OnWatchdogTimer() {
        if (!hwnd) return;

        // Recover from stuck background capture if LBUTTON was released outside our non-activating window
        if ((isDragging || isSlotDragging || pressedSlot >= 0 || pressedEmptySlot >= 0 || pressedSceneBtn >= 0) &&
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
            DiagLogger::Instance().Log("WARN", "SELF_HEAL", L"Watchdog cleared stuck drag/capture state after LBUTTON release");
            CancelActiveInteractions();
            RenderAndPresent("watchdog-capture-reset");
            return;
        }

        // Recover stale pendingAutoRetract if timer was dropped or delayed abnormally (> 1500 ms)
        if (pendingAutoRetract && copiedTick != 0 && (GetTickCount64() - copiedTick) > 1500) {
            KillTimer(hwnd, kTimerAutoRetract);
            pendingAutoRetract = false;
            ++retractGen;
            EvaluateCollapseState("watchdog-stale-retract");
            return;
        }

        if (isDragging || isSlotDragging || animRunning) return;

        // Expanded-state stall self-heal: if docked panel is expanded with no active blockers and cursor is outside past hold, retract!
        if (state->dockEdge != DockEdge::None && !IsCompactCollapsedMode() && expandSpring.target > 0.5f &&
            !IsOnboardingActive() && !IsDockMenuOpen() && !pendingAutoRetract &&
            pressedSlot < 0 && pressedEmptySlot < 0 && pressedSceneBtn < 0 && !pressedRedockBtn) {
            ULONGLONG now = GetTickCount64();
            if (!IsCursorInsideInteractivePanel()) {
                if (now >= holdOpenUntilTick) {
                    if (mouseOutsideSince == 0) mouseOutsideSince = now;
                    if (!leaveWatchRunning || (now - mouseOutsideSince) >= 170) {
                        if ((now - mouseOutsideSince) < 170) {
                            mouseOutsideSince = (now >= 170) ? (now - 170) : 0;
                        }
                        EvaluateCollapseState("watchdog-expanded-stall");
                        return;
                    }
                } else if (!leaveWatchRunning) {
                    EnsureLeaveWatchTimer();
                }
            } else if (!leaveWatchRunning) {
                EnsureLeaveWatchTimer();
            }
        }

        if (!lastPresentOk) {
            DiagLogger::Instance().Log("WARN", "SELF_HEAL", L"Watchdog scheduling recovery for failed RenderAndPresent");
            RenderAndPresent("watchdog-retry");
            return;
        }

        if (showInitialFlag && (!IsWindowVisible(hwnd) || IsIconic(hwnd))) {
            DiagLogger::Instance().Log("WARN", "SELF_HEAL", L"Watchdog detected hidden/minimized DockWnd, restoring visibility");
            EnsureTopmostAndVisible("watchdog-hidden", true);
            RenderAndPresent("watchdog-hidden");
            return;
        }

        DWORD cloaked = showInitialFlag ? GetDwmCloakedState(hwnd) : 0;
        if (cloaked != 0) {
            if (lastWatchdogStateCode != 1) {
                lastWatchdogStateCode = 1;
                DiagLogger::Instance().LogFmt("INFO", "SELF_HEAL",
                    L"DockWnd is DWM-cloaked (0x%08lX, e.g. inactive virtual desktop); yielding cleanly",
                    (unsigned long)cloaked);
            }
            return;
        }

        bool collapsedMode = IsCompactCollapsedMode();
        DockGeometry geom = ComputeExpectedGeometry(collapsedMode);

        // 1. Check all 4 edges + DPI against expected geometry (independent of Z-order)
        if (state->dockEdge != DockEdge::None) {
            RECT actualRc{};
            BOOL gotRc = GetWindowRect(hwnd, &actualRc);
            int actualW = gotRc ? (int)(actualRc.right - actualRc.left) : -1;
            int actualH = gotRc ? (int)(actualRc.bottom - actualRc.top) : -1;
            if (!gotRc ||
                std::abs(actualRc.left - geom.winX) > 2 ||
                std::abs(actualRc.top - geom.winY) > 2 ||
                std::abs(actualW - geom.winW) > 2 ||
                std::abs(actualH - geom.winH) > 2 ||
                std::fabs(geom.dpiScale - dpiScale) > 0.01f) {
                DiagLogger::Instance().LogFmt("WARN", "SELF_HEAL",
                    L"Watchdog geometry drift corrected: gotRc=%d osRect=(%ld,%ld %dx%d) expected=(%d,%d %dx%d) mon=(%ld,%ld)-(%ld,%ld) dpi=%.2f->%.2f",
                    (int)gotRc, actualRc.left, actualRc.top, actualW, actualH,
                    geom.winX, geom.winY, geom.winW, geom.winH,
                    geom.monFull.left, geom.monFull.top, geom.monFull.right, geom.monFull.bottom,
                    dpiScale, geom.dpiScale);
                RenderAndPresent("watchdog-drift");
                return;
            }
        }

        // 2. Check stripped WS_EX_TOPMOST bit (even when expanded)
        bool hasTopmostBit = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        bool ownPopupActive = (popupMenu && popupMenu->IsOpen()) || (vaultWnd && vaultWnd->IsEditorOpen());
        if (showInitialFlag && !hasTopmostBit && !ownPopupActive) {
            if (!IsFullscreenOrPresentationActive(geom.monFull)) {
                DiagLogger::Instance().Log("WARN", "SELF_HEAL", L"Watchdog detected stripped WS_EX_TOPMOST, restoring");
                EnsureTopmostAndVisible("watchdog-topmost-bit", true);
                ++watchdogZHealCount;
            }
            return;
        }

        // 3. Collapsed capsule physical reachability & Z-order/surface self-heal!
        if (!collapsedMode || (!showInitialFlag && !hitProbeProvider) || ownPopupActive) {
            lastWatchdogStateCode = 0;
            return;
        }

        if (!hitProbeProvider && IsFullscreenOrPresentationActive(geom.monFull)) {
            if (lastWatchdogStateCode != 2) {
                lastWatchdogStateCode = 2;
                DiagLogger::Instance().Log("INFO", "SELF_HEAL", L"Fullscreen/presentation app active on dock monitor; yielding Z-order");
            }
            lastHealedOccluder = nullptr;
            return;
        }

        // Probe the 3 interior solid capsule points (middle, upper, lower -- strictly inside opaque 6x118 pill)
        HWND hitMid = ProbeTopWindowAt(geom.solidProbePts[0]);
        HWND hitTop = ProbeTopWindowAt(geom.solidProbePts[1]);
        HWND hitBot = ProbeTopWindowAt(geom.solidProbePts[2]);
        if (hitMid == hwnd && hitTop == hwnd && hitBot == hwnd) {
            // Capsule is 100% physically visible and hover-reachable!
            if (lastWatchdogStateCode != 0) {
                lastWatchdogStateCode = 0;
                DiagLogger::Instance().Log("INFO", "SELF_HEAL", L"Capsule reachability healthy again");
            }
            lastHealedOccluder = nullptr;
            lastHealedFg = nullptr;
            return;
        }

        // Choose the primary obstructing hit window from the failing probe point
        POINT failPt = geom.solidProbePts[0];
        HWND hitWnd = hitMid;
        if (hitWnd == hwnd) { failPt = geom.solidProbePts[1]; hitWnd = hitTop; }
        if (hitWnd == hwnd) { failPt = geom.solidProbePts[2]; hitWnd = hitBot; }

        // Yield politely to our own windows or transient system Shell/IME overlays
        if (hitWnd && (IsOwnProcessWindow(hitWnd) || IsSystemShellOrTransientWindow(hitWnd))) {
            lastWatchdogStateCode = 3;
            return;
        }

        // Determine whether an external visible window is actually ABOVE DockWnd in Z-order
        bool hitIsAbove = (hitWnd != nullptr) && CheckIsAboveInZOrder(hitWnd);
        bool occIsTopmost = false;
        HWND zOccluder = hitIsAbove ? hitWnd : (hitProbeProvider ? nullptr : FindExternalOccluderAbove(failPt, &occIsTopmost));
        if (zOccluder && (IsOwnProcessWindow(zOccluder) || IsSystemShellOrTransientWindow(zOccluder))) {
            lastWatchdogStateCode = 3;
            return;
        }

        if (zOccluder != nullptr) {
            // Branch A: Confirmed Z-order occlusion by an external window above DockWnd!
            HWND fg = GetForegroundWindow();
            ULONGLONG now = GetTickCount64();
            if (zOccluder == lastHealedOccluder && fg == lastHealedFg) {
                // Circuit breaker: already attempted one raise against this persistent occluder; do NOT ping-pong fight!
                return;
            }
            if (lastZHealTick != 0 && (now - lastZHealTick) < 2000) {
                return;
            }

            DWORD occPid = 0;
            GetWindowThreadProcessId(zOccluder, &occPid);
            LONG_PTR occEx = GetWindowLongPtrW(zOccluder, GWL_EXSTYLE);
            RECT occRc{}, dockRc{};
            GetWindowRect(zOccluder, &occRc);
            GetWindowRect(hwnd, &dockRc);
            std::wstring occCls = SanitizeLogField(GetWindowClassStr(zOccluder), 64);

            lastWatchdogStateCode = 4;
            DiagLogger::Instance().LogFmt("WARN", "SELF_HEAL",
                L"Capsule occluded at (%ld,%ld): occHwnd=0x%p pid=%lu cls=%ls occEx=0x%08lX occRc=(%ld,%ld)-(%ld,%ld) dockRc=(%ld,%ld)-(%ld,%ld) dockTopmost=%d fg=0x%p",
                failPt.x, failPt.y, (void*)zOccluder, (unsigned long)occPid, occCls.c_str(),
                (unsigned long)occEx, occRc.left, occRc.top, occRc.right, occRc.bottom,
                dockRc.left, dockRc.top, dockRc.right, dockRc.bottom,
                (int)hasTopmostBit, (void*)fg);

            // Raise ONCE: HWND_TOP preserves topmost band membership; fallback to HWND_TOPMOST if band was broken
            bool swpOk = false;
            {
                ScopedSelfPresent guard(selfPresentDepth);
                HWND insertAfter = hasTopmostBit ? HWND_TOP : HWND_TOPMOST;
                UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
                swpOk = swpTopmostProvider
                    ? swpTopmostProvider(insertAfter)
                    : (SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags) != FALSE);
                if (hasTopmostBit && (occEx & WS_EX_TOPMOST) == 0 && CheckIsAboveInZOrder(zOccluder)) {
                    // Win11 broken topmost band fallback: explicit HWND_TOPMOST re-attach
                    swpOk = swpTopmostProvider
                        ? swpTopmostProvider(HWND_TOPMOST)
                        : (SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags) != FALSE);
                }
            }
            lastHealedOccluder = zOccluder;
            lastHealedFg = fg;
            lastZHealTick = now;
            ++watchdogZHealCount;

            HWND afterHit = ProbeTopWindowAt(failPt);
            DiagLogger::Instance().LogFmt("INFO", "SELF_HEAL",
                L"Capsule Z-order heal complete: swpOk=%d nowHitsDock=%d afterHit=0x%p",
                (int)swpOk, (int)(afterHit == hwnd), (void*)afterHit);
            return;
        }

        // Branch B: NO external window is above DockWnd in Z-order, yet WindowFromPoint fell through!
        // This indicates layered surface loss / zero-alpha click-through: recommit ONLY the collapsed frame.
        DWORD hitPid = 0;
        if (hitWnd) GetWindowThreadProcessId(hitWnd, &hitPid);
        std::wstring hitCls = SanitizeLogField(GetWindowClassStr(hitWnd), 64);
        lastWatchdogStateCode = 5;
        DiagLogger::Instance().LogFmt("WARN", "SELF_HEAL",
            L"Capsule layered-surface click-through at (%ld,%ld) (no occluder above): hitHwnd=0x%p pid=%lu cls=%ls prevCenterAlpha=%u; recommitting frame",
            failPt.x, failPt.y, (void*)hitWnd, (unsigned long)hitPid, hitCls.c_str(), (unsigned)lastCollapsedCenterAlpha);

        bool recommitOk = RenderAndPresent("watchdog-recommit");
        ++watchdogSurfaceRecommitCount;
        HWND afterHit = ProbeTopWindowAt(failPt);
        DiagLogger::Instance().LogFmt("INFO", "SELF_HEAL",
            L"Capsule surface recommit complete: ok=%d centerAlpha=%u nowHitsDock=%d",
            (int)recommitOk, (unsigned)lastCollapsedCenterAlpha, (int)(afterHit == hwnd));
    }

    static LRESULT CALLBACK WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        DockWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<DockWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (!self) return DefWindowProcW(h, msg, wp, lp);

        switch (msg) {
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE; // Zero focus stealing iron law!

            case WM_APP_SYNC_DISPLAY: {
                self->displaySyncQueued = false;
                std::wstring reason = self->queuedSyncReason.empty() ? L"async-sync" : self->queuedSyncReason;
                self->queuedSyncReason.clear();
                self->ExecuteDisplaySync(reason.c_str());
                return 0;
            }

            case WM_DISPLAYCHANGE: {
                DiagLogger::Instance().LogFmt("INFO", "DISPLAY",
                    L"WM_DISPLAYCHANGE bpp=%u res=%ux%u",
                    (unsigned)wp, (unsigned)LOWORD(lp), (unsigned)HIWORD(lp));
                self->ScheduleDisplaySync(L"WM_DISPLAYCHANGE", true);
                return 0;
            }

            case WM_SETTINGCHANGE: {
                if (wp == SPI_SETWORKAREA || wp == 0) {
                    DiagLogger::Instance().LogFmt("INFO", "DISPLAY", L"WM_SETTINGCHANGE wp=0x%04X", (unsigned)wp);
                    self->ScheduleDisplaySync(L"WM_SETTINGCHANGE");
                }
                return 0;
            }

            case WM_DPICHANGED: {
                self->dpiScale = (float)HIWORD(wp) / 96.0f;
                DiagLogger::Instance().LogFmt("INFO", "DISPLAY", L"WM_DPICHANGED newDpi=%u scale=%.2f",
                    (unsigned)HIWORD(wp), self->dpiScale);
                self->ScheduleDisplaySync(L"WM_DPICHANGED");
                return 0;
            }

            case WM_DWMCOMPOSITIONCHANGED: {
                DiagLogger::Instance().Log("WARN", "DISPLAY", L"WM_DWMCOMPOSITIONCHANGED received, scheduling surface sync");
                self->ScheduleDisplaySync(L"WM_DWMCOMPOSITIONCHANGED");
                return 0;
            }

            case WM_POWERBROADCAST: {
                if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND || wp == PBT_POWERSETTINGCHANGE) {
                    DiagLogger::Instance().LogFmt("INFO", "POWER", L"WM_POWERBROADCAST wp=0x%04X, scheduling sync", (unsigned)wp);
                    self->ScheduleDisplaySync(L"WM_POWERBROADCAST");
                }
                return TRUE;
            }

            case WM_WTSSESSION_CHANGE: {
                if (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT) {
                    DiagLogger::Instance().LogFmt("INFO", "SESSION", L"Session lock/disconnect wp=%u, cancelling transient UI", (unsigned)wp);
                    self->CancelActiveInteractions();
                } else if (wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT) {
                    DiagLogger::Instance().LogFmt("INFO", "SESSION", L"Session unlock/connect wp=%u, scheduling recovery sync", (unsigned)wp);
                    self->ScheduleDisplaySync(L"WM_WTSSESSION_CHANGE");
                }
                return 0;
            }

            case WM_WINDOWPOSCHANGED: {
                if (self->selfPresentDepth == 0 && !self->isDragging && self->hasPresentedOnce) {
                    auto* wpos = reinterpret_cast<const WINDOWPOS*>(lp);
                    if (wpos) {
                        bool moved = !(wpos->flags & SWP_NOMOVE) &&
                                     (std::abs(wpos->x - self->curWinX) > 2 || std::abs(wpos->y - self->curWinY) > 2);
                        bool resized = !(wpos->flags & SWP_NOSIZE) &&
                                       (std::abs(wpos->cx - self->curWinW) > 2 || std::abs(wpos->cy - self->curWinH) > 2);
                        bool hidden = (wpos->flags & SWP_HIDEWINDOW) != 0;
                        bool zChanged = !(wpos->flags & SWP_NOZORDER) && (wpos->hwndInsertAfter != HWND_TOPMOST);

                        if (moved || resized || hidden) {
                            DiagLogger::Instance().LogFmt("WARN", "EXT_MOVE",
                                L"External WM_WINDOWPOSCHANGED: osPos=(%d,%d %dx%d) expected=(%d,%d %dx%d) flags=0x%08X",
                                wpos->x, wpos->y, wpos->cx, wpos->cy,
                                self->curWinX, self->curWinY, self->curWinW, self->curWinH,
                                (unsigned)wpos->flags);
                            if (self->state->dockEdge != DockEdge::None || hidden) {
                                self->ScheduleDisplaySync(L"EXT_MOVE");
                            }
                        } else if (zChanged && self->IsCompactCollapsedMode() && self->showInitialFlag) {
                            // External Z-order demotion detected (even with SWP_NOMOVE!): schedule quick verification
                            SetTimer(h, kTimerQuickVerify, 180, nullptr);
                        }
                    }
                }
                break;
            }

            case WM_SHOWWINDOW: {
                if (self->selfPresentDepth == 0 && wp == FALSE && self->showInitialFlag) {
                    DiagLogger::Instance().LogFmt("WARN", "VISIBILITY",
                        L"External WM_SHOWWINDOW(FALSE) status=%ld, scheduling restore", (long)lp);
                    SetTimer(h, kTimerDisplaySync, 100, nullptr);
                }
                break;
            }

            case WM_CANCELMODE: {
                self->CancelActiveInteractions();
                self->RenderAndPresent("cancel-mode");
                return 0;
            }

            case WM_DESTROY: {
                if (self->onboardingWnd.hwnd) {
                    DestroyWindow(self->onboardingWnd.hwnd);
                    self->onboardingWnd.hwnd = nullptr;
                }
                self->UnregisterWtsSessionNotifications();
                return 0;
            }

            case WM_MOUSEWHEEL: {
                if (!self->IsCompactCollapsedMode()) {
                    short delta = GET_WHEEL_DELTA_WPARAM(wp);
                    if (self->ShowSceneBar() && delta != 0) {
                        POINT pt{ (short)LOWORD(lp), (short)HIWORD(lp) };
                        float dx = (float)(pt.x - self->curWinX) / self->dpiScale - self->CurrentSlideOffsetDip();
                        float dy = (float)(pt.y - self->curWinY) / self->dpiScale;
                        if (self->IsPointInSceneBarDip(dx, dy)) {
                            int vis[kMaxScenes]{};
                            int cnt = self->state->GetEnabledScenes(vis);
                            int curAct = self->state->EffectiveActiveSceneIndex();
                            int curPos = 0;
                            for (int p = 0; p < cnt; ++p) {
                                if (vis[p] == curAct) { curPos = p; break; }
                            }
                            int step = (delta < 0) ? 1 : -1;
                            int nextPos = (curPos + step + cnt) % cnt;
                            self->SwitchActiveScene(vis[nextPos]);
                            return 0;
                        }
                    }
                    if (self->maxSlotScrollDip > 0.5f) {
                        float nextScroll = std::clamp(self->slotScrollDip - (float)delta * 0.35f, 0.0f, self->maxSlotScrollDip);
                        if (std::fabs(nextScroll - self->slotScrollDip) > 0.1f) {
                            self->slotScrollDip = nextScroll;
                            self->hoverBubble.HideImmediate();
                            self->RenderAndPresent("slot-wheel");
                        }
                    }
                }
                return 0;
            }

            case WM_TIMER: {
                if (wp == kTimerAnim) {
                    self->OnAnimTick();
                } else if (wp == kTimerHoverExpand) {
                    KillTimer(h, kTimerHoverExpand);
                    if (!self->isDragging && self->handleHovered && self->state->dockEdge != DockEdge::None) {
                        self->ExpandAndReveal();
                    }
                } else if (wp == kTimerLeaveWatch) {
                    self->OnLeaveWatchTimer();
                } else if (wp == kTimerAutoRetract) {
                    KillTimer(h, kTimerAutoRetract);
                    if (!self->pendingAutoRetract || self->pendingAutoRetractGen != self->retractGen) {
                        return 0; // Stale auto-retract timer invalidated by newer user interaction
                    }
                    self->pendingAutoRetract = false;
                    if (self->state->dockEdge != DockEdge::None && !self->IsDockMenuOpen() &&
                        !self->isDragging && !self->isSlotDragging &&
                        self->pressedSlot < 0 && self->pressedEmptySlot < 0 &&
                        self->pressedSceneBtn < 0) {
                        self->hoverSlot = -1;
                        self->handleHovered = false;
                        self->StartSpringTo(0.0f);
                    } else {
                        self->EvaluateCollapseState("auto-retract-deferred");
                    }
                } else if (wp == kTimerBubbleHover) {
                    KillTimer(h, kTimerBubbleHover);
                    self->SyncHoverBubble(true);
                } else if (wp == kTimerDisplaySync) {
                    self->OnDisplaySyncTimer();
                } else if (wp == kTimerWatchdog) {
                    self->OnWatchdogTimer();
                } else if (wp == kTimerRenderRetry) {
                    KillTimer(h, kTimerRenderRetry);
                    if (!self->lastPresentOk) {
                        self->RenderAndPresent("render-retry");
                    }
                } else if (wp == kTimerQuickVerify) {
                    KillTimer(h, kTimerQuickVerify);
                    self->OnWatchdogTimer();
                }
                return 0;
            }

            case WM_MOUSEMOVE: {
                if (!self->mouseTracking) {
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
                    if (TrackMouseEvent(&tme) != FALSE) {
                        self->mouseTracking = true;
                    }
                }

                if (self->pressedSlot >= 0) {
                    POINT cur = MsgScreenPoint(h, lp);
                    float curDipY = (float)(cur.y - self->curWinY) / self->dpiScale;
                    if (!self->isSlotDragging) {
                        int distY = std::abs(cur.y - self->slotDragStartScreen.y);
                        if (distY >= (int)std::round(5.0f * self->dpiScale)) {
                            self->isSlotDragging = true;
                            self->hoverSlot = -1;
                            KillTimer(h, kTimerAutoRetract);
                            self->pendingAutoRetract = false;
                            ++self->retractGen;
                            KillTimer(h, kTimerBubbleHover);
                            self->hoverBubble.HideImmediate();
                        }
                    }
                    if (self->isSlotDragging) {
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                        D2D1_RECT_F pb = self->PanelBoxDip();
                        float firstTop = pb.top + Theme::HeaderHeight - self->slotScrollDip;
                        float step = Theme::SlotHeight + Theme::SlotGap;
                        float lastTop = firstTop + 9.0f * step;
                        self->slotDragCurDipY = std::clamp(curDipY - self->slotDragGrabOffsetDipY, firstTop - 4.0f, lastTop + 4.0f);
                        float dragMidY = self->slotDragCurDipY + Theme::SlotHeight * 0.5f;

                        while (self->pressedSlot < 9) {
                            float nextMidY = firstTop + (float)(self->pressedSlot + 1) * step + Theme::SlotHeight * 0.5f;
                            if (dragMidY > nextMidY - 5.0f) {
                                auto& sl = self->state->ActiveSlots();
                                std::swap(sl[(size_t)self->pressedSlot], sl[(size_t)(self->pressedSlot + 1)]);
                                self->slotShiftOffsetDip[(size_t)self->pressedSlot] += step;
                                self->pressedSlot += 1;
                            } else {
                                break;
                            }
                        }
                        while (self->pressedSlot > 0) {
                            float prevMidY = firstTop + (float)(self->pressedSlot - 1) * step + Theme::SlotHeight * 0.5f;
                            if (dragMidY < prevMidY + 5.0f) {
                                auto& sl = self->state->ActiveSlots();
                                std::swap(sl[(size_t)self->pressedSlot], sl[(size_t)(self->pressedSlot - 1)]);
                                self->slotShiftOffsetDip[(size_t)self->pressedSlot] -= step;
                                self->pressedSlot -= 1;
                            } else {
                                break;
                            }
                        }
                        self->EnsureAnimTimer();
                        self->RenderAndPresent();
                        return 0;
                    }
                }

                if (self->isDragging) {
                    POINT cur = MsgScreenPoint(h, lp);
                    int dxPx = cur.x - self->dragStartScreen.x;
                    int dyPx = cur.y - self->dragStartScreen.y;
                    if (std::abs(dxPx) > 4 || std::abs(dyPx) > 4) self->dragMoved = true;
                    if (!self->dragMoved) return 0;

                    if (self->dragStartedCollapsed && !self->dragPreviewFloating && self->dragOrigEdge != DockEdge::None) {
                        RECT mon{};
                        RECT work = self->monitorProvider ? self->monitorProvider(cur, &mon)
                                                          : GetMonitorWorkAreaForPoint(cur, &mon);
                        float pullFromEdgeDip = (self->dragOrigEdge == DockEdge::Left)
                            ? (float)(cur.x - mon.left) / self->dpiScale
                            : (float)(mon.right - cur.x) / self->dpiScale;
                        float pullFromStartDip = (self->dragOrigEdge == DockEdge::Left)
                            ? (float)(cur.x - self->dragStartScreen.x) / self->dpiScale
                            : (float)(self->dragStartScreen.x - cur.x) / self->dpiScale;

                        if (pullFromEdgeDip > 34.0f && pullFromStartDip > 24.0f) {
                            // Preview floating expanded panel (DO NOT mutate state->dockEdge until WM_LBUTTONUP commit!)
                            self->dragPreviewFloating = true;
                            self->expandSpring.Snap(1.0f);
                            int panelWPx = (int)((Theme::PanelWidth + Theme::ShadowPad * 2.0f) * self->dpiScale);
                            self->dragPreviewFloatX = cur.x - panelWPx / 2;
                            self->dragPreviewFloatY = cur.y - (int)(28.0f * self->dpiScale);
                            self->dragStartScreen = cur;
                            self->dragStartFloatX = self->dragPreviewFloatX;
                            self->dragStartFloatY = self->dragPreviewFloatY;
                        } else {
                            double hWork = std::max(100.0, (double)(work.bottom - work.top));
                            self->dragPreviewRatioY = std::clamp(self->dragStartRatioY + (double)dyPx / hWork, 0.08, 0.92);
                        }
                        self->RenderAndPresent("drag-collapsed-preview");
                        return 0;
                    }

                    // Preview dragging expanded panel (state->dockEdge stays untouched until commit!)
                    self->dragPreviewFloating = true;
                    self->expandSpring.Snap(1.0f);
                    self->dragPreviewFloatX = self->dragStartFloatX + dxPx;
                    self->dragPreviewFloatY = self->dragStartFloatY + dyPx;
                    self->RenderAndPresent("drag-expanded-preview");
                    return 0;
                }

                if (self->IsCompactCollapsedMode()) {
                    if (!self->handleHovered) {
                        self->handleHovered = true;
                        self->RenderAndPresent();
                        // 100 ms anti-accidental-touch debounce!
                        if (SetTimer(h, kTimerHoverExpand, 100, nullptr) == 0) {
                            self->ExpandAndReveal();
                        }
                    }
                    return 0;
                }

                float dx = (float)(short)LOWORD(lp) / self->dpiScale - self->CurrentSlideOffsetDip();
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int ns = -1;
                bool nh = false;
                self->HitTestExpandedDip(dx, dy, ns, nh);
                int nsc = self->HitTestSceneBtnDip(dx, dy);
                bool nRedock = self->HitTestRedockBtnDip(dx, dy);
                if (ns != self->hoverSlot || nsc != self->hoverSceneBtn || nRedock != self->hoverRedockBtn) {
                    self->hoverSlot = ns;
                    self->hoverSceneBtn = nsc;
                    self->hoverRedockBtn = nRedock;
                    self->SyncHoverBubble(false);
                    self->EnsureAnimTimer();
                    self->RenderAndPresent();
                }
                return 0;
            }

            case WM_MOUSELEAVE: {
                self->mouseTracking = false;
                if (self->handleHovered) {
                    self->handleHovered = false;
                    KillTimer(h, kTimerHoverExpand);
                    if (self->IsCompactCollapsedMode()) self->RenderAndPresent();
                }
                if (self->hoverSlot != -1 || self->hoverSceneBtn != -1 || self->hoverRedockBtn) {
                    self->hoverSlot = -1;
                    self->hoverSceneBtn = -1;
                    self->hoverRedockBtn = false;
                    self->SyncHoverBubble(false);
                    self->EnsureAnimTimer();
                    self->RenderAndPresent();
                }
                if (!self->IsCompactCollapsedMode()) {
                    self->EvaluateCollapseState("mouse-leave");
                }
                return 0;
            }

            case WM_LBUTTONDOWN: {
                if (self->popupMenu && self->popupMenu->IsOpen()) self->popupMenu->Close();
                self->pressedEmptySlot = -1;
                self->pressedSceneBtn = -1;
                self->pressedRedockBtn = false;
                POINT cur = MsgScreenPoint(h, lp);

                if (self->IsCompactCollapsedMode()) {
                    KillTimer(h, kTimerHoverExpand);
                    KillTimer(h, kTimerAutoRetract);
                    self->pendingAutoRetract = false;
                    ++self->retractGen;
                    self->isDragging = true;
                    self->dragMoved = false;
                    self->dragStartedCollapsed = true;
                    self->dragPreviewFloating = false;
                    self->dragOrigEdge = self->state->dockEdge;
                    self->dragOrigRatioY = self->state->dockRatioY;
                    self->dragOrigFloatX = self->state->floatX;
                    self->dragOrigFloatY = self->state->floatY;
                    self->dragPreviewRatioY = self->state->dockRatioY;
                    self->dragStartScreen = cur;
                    self->dragStartRatioY = self->state->dockRatioY;
                    SetCapture(h);
                    return 0;
                }

                float dx = (float)(short)LOWORD(lp) / self->dpiScale - self->CurrentSlideOffsetDip();
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                if (self->HitTestRedockBtnDip(dx, dy)) {
                    self->pressedRedockBtn = true;
                    SetCapture(h);
                    return 0;
                }
                int hsc = self->HitTestSceneBtnDip(dx, dy);
                if (hsc >= 0) {
                    self->pressedSceneBtn = hsc;
                    SetCapture(h);
                    return 0;
                }
                int hs = -1;
                bool onHeader = false;
                self->HitTestExpandedDip(dx, dy, hs, onHeader);

                if (onHeader) {
                    KillTimer(h, kTimerBubbleHover);
                    KillTimer(h, kTimerAutoRetract);
                    self->pendingAutoRetract = false;
                    ++self->retractGen;
                    self->hoverBubble.HideImmediate();
                    self->isDragging = true;
                    self->dragMoved = false;
                    self->dragStartedCollapsed = false;
                    self->dragPreviewFloating = (self->state->dockEdge == DockEdge::None);
                    self->dragOrigEdge = self->state->dockEdge;
                    self->dragOrigRatioY = self->state->dockRatioY;
                    self->dragOrigFloatX = self->state->floatX;
                    self->dragOrigFloatY = self->state->floatY;
                    self->dragPreviewFloatX = self->curWinX;
                    self->dragPreviewFloatY = self->curWinY;
                    self->dragStartScreen = cur;
                    self->dragStartFloatX = self->curWinX;
                    self->dragStartFloatY = self->curWinY;
                    SetCapture(h);
                } else if (hs >= 0 && hs < 10) {
                    if (self->state->FindById(self->state->ActiveSlots()[(size_t)hs]) != nullptr) {
                        self->pressedSlot = hs;
                        self->isSlotDragging = false;
                        self->slotDragStartScreen = cur;
                        D2D1_RECT_F sr = self->SlotRectDip(hs);
                        self->slotDragGrabOffsetDipY = dy - sr.top;
                        self->slotDragCurDipY = sr.top;
                        SetCapture(h);
                    } else {
                        self->pressedEmptySlot = hs;
                        SetCapture(h);
                    }
                }
                return 0;
            }

            case WM_LBUTTONUP: {
                POINT cur = MsgScreenPoint(h, lp);

                if (self->pressedSlot >= 0) {
                    // Snapshot & clear state BEFORE ReleaseCapture(), which synchronously fires WM_CAPTURECHANGED!
                    int finSlot = self->pressedSlot;
                    bool wasDragging = self->isSlotDragging;
                    POINT startPt = self->slotDragStartScreen;
                    self->pressedSlot = -1;
                    self->isSlotDragging = false;
                    if (GetCapture() == h) ReleaseCapture();

                    float dx = (float)(short)LOWORD(lp) / self->dpiScale - self->CurrentSlideOffsetDip();
                    float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                    int ns = -1;
                    bool nh = false;
                    self->HitTestExpandedDip(dx, dy, ns, nh);

                    if (wasDragging) {
                        self->storage->Save(*self->state);
                        if (self->vaultWnd) self->vaultWnd->RefreshIfOpen();
                        self->hoverSlot = ns;
                        self->EnsureAnimTimer();
                        self->RenderAndPresent();
                        self->EvaluateCollapseState("slot-drag-end");
                    } else {
                        int jitterPx = (int)std::round(5.0f * self->dpiScale);
                        D2D1_RECT_F pb = self->PanelBoxDip();
                        bool insidePanel = (dx >= pb.left && dx <= pb.right && dy >= pb.top && dy <= pb.bottom);
                        if (ns == finSlot ||
                            (insidePanel && std::abs(cur.x - startPt.x) < jitterPx && std::abs(cur.y - startPt.y) < jitterPx)) {
                            self->TriggerSlotCopy(finSlot);
                        } else {
                            self->hoverSlot = ns;
                            self->EnsureAnimTimer();
                            self->RenderAndPresent();
                            self->EvaluateCollapseState("slot-click-cancel");
                        }
                    }
                    return 0;
                }

                if (self->isDragging) {
                    bool moved = self->dragMoved;
                    bool startedCollapsed = self->dragStartedCollapsed;
                    bool wasPreviewFloating = self->dragPreviewFloating;
                    int finFloatX = self->dragPreviewFloatX;
                    int finFloatY = self->dragPreviewFloatY;
                    double finRatioY = self->dragPreviewRatioY;
                    self->isDragging = false;
                    self->dragMoved = false;
                    self->dragStartedCollapsed = false;
                    self->dragPreviewFloating = false;
                    if (GetCapture() == h) ReleaseCapture();

                    if (!moved && startedCollapsed) {
                        // Quick click on the collapsed handle expands it immediately
                        self->ExpandAndReveal();
                        return 0;
                    }

                    if (!moved && !startedCollapsed) {
                        // In read-only recovery mode, clicking header opens Vault recovery banner directly
                        if (self->storage && self->storage->readOnlyRecoveryMode && self->vaultWnd) {
                            self->vaultWnd->Open(0, -1);
                        }
                        self->EvaluateCollapseState("header-click");
                        return 0;
                    }

                    if (startedCollapsed && !wasPreviewFloating) {
                        // Committed vertical edge slide
                        RECT mon{};
                        self->GetTargetMonitorWorkArea(&mon);
                        self->state->dockRatioY = finRatioY;
                        self->PersistDockAnchorFromMonitor(mon);
                        self->storage->Save(*self->state);
                        self->RenderAndPresent("drag-commit-edge-y");
                        return 0;
                    }

                    // Transactional commit for floating preview: only switch to DockEdge::None when > 36 DIP from both edges!
                    RECT mon{};
                    RECT work = self->monitorProvider ? self->monitorProvider(cur, &mon)
                                                      : GetMonitorWorkAreaForPoint(cur, &mon);
                    float snapDistPx = 36.0f * self->dpiScale;
                    int shadowPx = (int)std::round(Theme::ShadowPad * self->dpiScale);
                    int panelLeftPx = finFloatX + shadowPx;
                    int panelRightPx = finFloatX + self->curWinW - shadowPx;

                    bool nearLeft = ((cur.x - mon.left) <= snapDistPx) || ((panelLeftPx - mon.left) <= snapDistPx);
                    bool nearRight = ((mon.right - cur.x) <= snapDistPx) || ((mon.right - panelRightPx) <= snapDistPx);

                    if (nearLeft && !nearRight) {
                        self->state->dockEdge = DockEdge::Left;
                        double centerY = (double)(finFloatY + self->curWinH / 2 - work.top);
                        self->state->dockRatioY = std::clamp(centerY / std::max(100.0, (double)(work.bottom - work.top)), 0.08, 0.92);
                        self->mouseOutsideSince = GetTickCount64();
                        self->EnsureLeaveWatchTimer();
                    } else if (nearRight) {
                        self->state->dockEdge = DockEdge::Right;
                        double centerY = (double)(finFloatY + self->curWinH / 2 - work.top);
                        self->state->dockRatioY = std::clamp(centerY / std::max(100.0, (double)(work.bottom - work.top)), 0.08, 0.92);
                        self->mouseOutsideSince = GetTickCount64();
                        self->EnsureLeaveWatchTimer();
                    } else {
                        self->state->dockEdge = DockEdge::None;
                        self->state->floatX = finFloatX;
                        self->state->floatY = finFloatY;
                        self->StopLeaveWatchTimer();
                        DiagLogger::Instance().LogFmt("INFO", "DOCK",
                            L"Committed floating mode at (%d,%d)", finFloatX, finFloatY);
                    }

                    self->PersistDockAnchorFromMonitor(mon);
                    self->storage->Save(*self->state);
                    self->RenderAndPresent("drag-commit");
                    if (self->vaultWnd) self->vaultWnd->RefreshIfOpen();
                    return 0;
                }

                // Press-release ownership for redock button, scene buttons & empty slots
                bool downRedock = self->pressedRedockBtn;
                int downScene = self->pressedSceneBtn;
                int downEmpty = self->pressedEmptySlot;
                self->pressedRedockBtn = false;
                self->pressedSceneBtn = -1;
                self->pressedEmptySlot = -1;
                if (GetCapture() == h) ReleaseCapture();

                if (!downRedock && downScene < 0 && downEmpty < 0) {
                    return 0;
                }

                float dx = (float)(short)LOWORD(lp) / self->dpiScale - self->CurrentSlideOffsetDip();
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                if (downRedock && self->HitTestRedockBtnDip(dx, dy)) {
                    RECT mon{};
                    RECT work = self->GetTargetMonitorWorkArea(&mon);
                    int winMidX = self->curWinX + self->curWinW / 2;
                    int monMidX = (mon.left + mon.right) / 2;
                    self->state->dockEdge = (winMidX < monMidX) ? DockEdge::Left : DockEdge::Right;
                    double centerY = (double)(self->curWinY + self->curWinH / 2 - work.top);
                    self->state->dockRatioY = std::clamp(centerY / std::max(100.0, (double)(work.bottom - work.top)), 0.08, 0.92);
                    self->PersistDockAnchorFromMonitor(mon);
                    self->storage->Save(*self->state);
                    self->hoverRedockBtn = false;
                    self->ExpandAndReveal(1400);
                    if (self->vaultWnd) self->vaultWnd->RefreshIfOpen();
                    return 0;
                }

                if (downScene >= 0) {
                    if (self->HitTestSceneBtnDip(dx, dy) == downScene) {
                        self->SwitchActiveScene(downScene);
                    }
                    return 0;
                }

                int hs = -1;
                bool onHeader = false;
                self->HitTestExpandedDip(dx, dy, hs, onHeader);

                if (downEmpty >= 0 && hs == downEmpty && hs < 10 &&
                    self->state->FindById(self->state->ActiveSlots()[(size_t)hs]) == nullptr) {
                    KillTimer(h, kTimerBubbleHover);
                    self->hoverBubble.HideImmediate();
                    if (self->onboardingWnd.step == OnboardingStep::Step2Create && !self->onboardingWnd.step2Done) {
                        self->TriggerOnboardingCreatePrompt(hs);
                    } else {
                        self->ShowSlotMenu(hs, cur, true);
                    }
                }
                return 0;
            }

            case WM_CAPTURECHANGED: {
                self->pressedRedockBtn = false;
                self->pressedSceneBtn = -1;
                self->pressedEmptySlot = -1;
                if (self->isSlotDragging) {
                    self->isSlotDragging = false;
                    self->pressedSlot = -1;
                    self->storage->Save(*self->state);
                    if (self->vaultWnd) self->vaultWnd->RefreshIfOpen();
                    self->EnsureAnimTimer();
                    self->RenderAndPresent();
                    self->EvaluateCollapseState("slot-drag-capture-lost");
                } else if (self->pressedSlot >= 0) {
                    self->pressedSlot = -1;
                    self->EvaluateCollapseState("slot-press-capture-lost");
                }
                if (self->isDragging) {
                    // Transactional rollback: NEVER commit floating mode on interrupted capture!
                    self->RollbackPanelDrag("capture-changed");
                }
                return 0;
            }

            case WM_RBUTTONUP: {
                KillTimer(h, kTimerBubbleHover);
                self->hoverBubble.HideImmediate();
                POINT cur = MsgScreenPoint(h, lp);
                if (self->IsCompactCollapsedMode()) {
                    if (self->onRequestAppMenu) self->onRequestAppMenu(cur);
                    return 0;
                }
                float dx = (float)(short)LOWORD(lp) / self->dpiScale - self->CurrentSlideOffsetDip();
                float dy = (float)(short)HIWORD(lp) / self->dpiScale;
                int hsc = self->HitTestSceneBtnDip(dx, dy);
                if (hsc >= 0) {
                    self->ShowSceneMenu(hsc, cur);
                    return 0;
                }
                int hs = -1;
                bool onHeader = false;
                self->HitTestExpandedDip(dx, dy, hs, onHeader);

                if (hs >= 0 && hs < 10) {
                    bool isEmpty = (self->state->FindById(self->state->ActiveSlots()[(size_t)hs]) == nullptr);
                    self->ShowSlotMenu(hs, cur, isEmpty);
                } else {
                    D2D1_RECT_F pb = self->PanelBoxDip();
                    bool inPanel = (dx >= pb.left && dx <= pb.right && dy >= pb.top && dy <= pb.bottom);
                    if ((onHeader || inPanel) && self->onRequestAppMenu) {
                        self->onRequestAppMenu(cur);
                    }
                }
                return 0;
            }
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

} // namespace pp
