#pragma once

#include <windows.h>
#include <d2d1.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace pp {

[[gnu::noinline]] inline D2D1_COLOR_F Rgba(uint32_t hexRgb, float alpha = 1.0f) {
    float r = ((hexRgb >> 16) & 0xFF) / 255.0f;
    float g = ((hexRgb >> 8) & 0xFF) / 255.0f;
    float b = (hexRgb & 0xFF) / 255.0f;
    return D2D1::ColorF(r, g, b, alpha);
}

[[gnu::noinline]] inline D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    );
}

namespace Theme {
    // Raycast / Linear Obsidian dark design tokens
    static constexpr uint32_t BgTop          = 0x161922;
    static constexpr uint32_t BgBottom       = 0x0E1016;
    static constexpr uint32_t CardBg         = 0x1B1F2B;
    static constexpr uint32_t CardHoverBg    = 0x252B3B;
    static constexpr uint32_t CardEmptyBg    = 0x141720;
    static constexpr uint32_t AccentCyan     = 0x5EEAD4;
    static constexpr uint32_t AccentBlue     = 0x6EA8FE;
    static constexpr uint32_t AccentIndigo   = 0x818CF8;
    static constexpr uint32_t CopyGreen      = 0x34D399;
    static constexpr uint32_t DangerRed      = 0xF87171;
    static constexpr uint32_t TextPrimary    = 0xF1F5F9;
    static constexpr uint32_t TextSecondary  = 0xA6B5C9;
    static constexpr uint32_t TextMuted      = 0x7E8DA4;

    // Dock geometry in DIPs (96 DPI logical pixels)
    static constexpr float ShadowPad         = 16.0f;
    static constexpr float PanelWidth        = 252.0f;
    static constexpr float HeaderHeight      = 36.0f;
    static constexpr float SlotHeight        = 38.0f;
    static constexpr float SlotGap           = 5.0f;
    static constexpr float FooterHeight      = 38.0f;
    static constexpr float PaddingX          = 10.0f;
    static constexpr float PanelRadius       = 13.0f;
    static constexpr float SlotRadius        = 8.0f;

    // Collapsed edge capsule handle in DIPs
    static constexpr float HandleWidth       = 6.0f;
    static constexpr float HandleHeight      = 118.0f;
    static constexpr float HandleHitWidth    = 10.0f;

    static constexpr float PanelHeight(bool showSceneBar = false) {
        return HeaderHeight + 10.0f * SlotHeight + 9.0f * SlotGap + (showSceneBar ? (FooterHeight + 8.0f) : 10.0f);
    }

    [[gnu::noinline]] static inline void SceneAccentColors(int sceneIdx, uint32_t& topHex, uint32_t& botHex) {
        if (sceneIdx == 1) {
            topHex = 0xA78BFA;
            botHex = 0x60A5FA;
        } else if (sceneIdx == 2) {
            topHex = 0x34D399;
            botHex = 0x38BDF8;
        } else if (sceneIdx == 3) {
            topHex = 0xFBBF24;
            botHex = 0xFB7185;
        } else {
            topHex = AccentCyan;   // 0x5EEAD4
            botHex = AccentIndigo; // 0x818CF8
        }
    }

    [[gnu::noinline]] static inline uint32_t SceneTextAccent(int sceneIdx) {
        if (sceneIdx == 1) return 0xC4B5FD;
        if (sceneIdx == 2) return 0x6EE7B7;
        if (sceneIdx == 3) return 0xFDE68A;
        return AccentCyan;
    }
}

// ============================================================================
// Unified Motion Grammar (Apple Core Animation / SwiftUI inspired spring grammar
// with OS Reduced-Motion respect & framerate-independent integration)
// ============================================================================
namespace Motion {
    struct State {
        bool initialized = false;
        bool systemReduced = false;
        int testOverride = -1; // -1 = follow Windows SPI_GETCLIENTAREAANIMATION, 0 = normal, 1 = reduced
    };

    inline State& GlobalState() {
        static State s{};
        return s;
    }

    [[gnu::noinline]] inline void RefreshSystemReducedMotion() {
        auto& st = GlobalState();
        BOOL animEnabled = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animEnabled, 0)) {
            st.systemReduced = (animEnabled == FALSE);
        } else {
            st.systemReduced = false;
        }
        st.initialized = true;
    }

    [[gnu::noinline]] inline bool IsReducedMotion() {
        auto& st = GlobalState();
        if (st.testOverride >= 0) return st.testOverride != 0;
        if (!st.initialized) RefreshSystemReducedMotion();
        return st.systemReduced;
    }

    inline void SetReducedMotionOverride(int mode) {
        GlobalState().testOverride = mode;
    }

    [[gnu::noinline]] inline float SmoothStep(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    [[gnu::noinline]] inline float EaseOutCubic(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }

    [[gnu::noinline]] inline float EaseOutQuart(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        float inv = 1.0f - t;
        float inv2 = inv * inv;
        return 1.0f - inv2 * inv2;
    }

    // Framerate-independent exponential approach: returns true while still moving.
    [[gnu::noinline]] inline bool ExpApproach(float& cur, float target, float rate, float dt,
                                              float eps = 0.005f, bool respectReduced = true) {
        if (std::fabs(target - cur) <= eps) {
            cur = target;
            return false;
        }
        if ((respectReduced && IsReducedMotion()) || dt >= 0.35f) {
            cur = target;
            return false;
        }
        if (dt <= 0.0f) return true;
        float factor = 1.0f - std::exp(-rate * std::min(dt, 0.20f));
        cur += (target - cur) * factor;
        if (std::fabs(target - cur) <= eps) {
            cur = target;
            return false;
        }
        return true;
    }

    [[gnu::noinline]] inline bool ApproachColorRgb(D2D1_COLOR_F& c, const D2D1_COLOR_F& d, float rate, float dt) {
        bool m = ExpApproach(c.r, d.r, rate, dt, 0.004f);
        m |= ExpApproach(c.g, d.g, rate, dt, 0.004f);
        m |= ExpApproach(c.b, d.b, rate, dt, 0.004f);
        c.a = 1.0f;
        return m;
    }

    // Damped horizontal micro-shake for validation rejection / locked-item / copy failure.
    // Returns 0 in Reduced Motion or after duration expires.
    [[gnu::noinline]] inline float RejectShakeOffset(ULONGLONG startTick, ULONGLONG nowTick,
                                                     ULONGLONG durationMs = 260, float ampDip = 4.2f) {
        if (startTick == 0 || nowTick < startTick || IsReducedMotion()) return 0.0f;
        ULONGLONG elapsed = nowTick - startTick;
        if (elapsed >= durationMs) return 0.0f;
        float p = (float)elapsed / (float)durationMs;
        float decay = (1.0f - p) * (1.0f - p);
        return std::sin(p * 5.0f * 3.14159265f) * ampDip * decay;
    }
}

// Unified second-order spring for 60/120Hz smooth interruptible transitions.
struct Spring1D {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;
    float stiffness = 500.0f;
    float damping = 44.7f; // Critically damped default (zeta = 1.0)

    // 4 canonical project-wide spring presets:
    // Tactile : micro-response (hover, press, focus, checkmarks) -- ~110 ms, zero overshoot
    // Snappy  : switches & overlays (tabs, scene pills, menus, toggles, toasts) -- ~175 ms
    // Fluid   : spatial panels & scroll tracking (dock slide, bubble morph) -- ~215 ms
    // Settle  : drag-and-drop settling with subtle tactile landing -- ~200 ms
    static Spring1D Tactile(float init = 0.0f) {
        return Spring1D{ init, 0.0f, init, 900.0f, 60.0f };
    }
    static Spring1D Snappy(float init = 0.0f) {
        return Spring1D{ init, 0.0f, init, 560.0f, 45.8f };
    }
    static Spring1D Fluid(float init = 0.0f) {
        return Spring1D{ init, 0.0f, init, 500.0f, 44.7f };
    }
    static Spring1D Settle(float init = 0.0f) {
        return Spring1D{ init, 0.0f, init, 520.0f, 43.2f };
    }

    [[gnu::noinline]] void Snap(float v) {
        value = v;
        target = v;
        velocity = 0.0f;
    }

    // Returns true while still moving. Pixel-scale callers can pass larger posEps (e.g., 0.25f) to stop cleanly.
    [[gnu::noinline]] bool Step(float dt, float posEps = 0.0015f, float velEps = 0.01f, bool respectReduced = true) {
        if (IsSettled()) return false;
        if ((respectReduced && Motion::IsReducedMotion()) || dt >= 0.45f) {
            Snap(target);
            return false;
        }
        if (dt <= 0.0f) return true;
        dt = std::clamp(dt, 0.001f, 0.250f);
        // Substep <= 5ms guarantees unconditional stability even at stiffness = 900
        int steps = std::clamp((int)std::ceil(dt / 0.005f), 1, 50);
        float h = dt / (float)steps;
        for (int i = 0; i < steps; ++i) {
            float force = (target - value) * stiffness - velocity * damping;
            velocity += force * h;
            value += velocity * h;
        }
        if (std::fabs(target - value) <= posEps && std::fabs(velocity) <= velEps) {
            value = target;
            velocity = 0.0f;
            return false;
        }
        return true;
    }

    [[gnu::noinline]] bool IsSettled() const {
        return value == target && velocity == 0.0f;
    }
};

} // namespace pp
