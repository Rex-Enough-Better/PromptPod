#pragma once

#include <d2d1.h>
#include <cmath>
#include <algorithm>

namespace pp {

inline D2D1_COLOR_F Rgba(uint32_t hexRgb, float alpha = 1.0f) {
    float r = ((hexRgb >> 16) & 0xFF) / 255.0f;
    float g = ((hexRgb >> 8) & 0xFF) / 255.0f;
    float b = (hexRgb & 0xFF) / 255.0f;
    return D2D1::ColorF(r, g, b, alpha);
}

inline D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
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

    static inline void SceneAccentColors(int sceneIdx, uint32_t& topHex, uint32_t& botHex) {
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

    static inline uint32_t SceneTextAccent(int sceneIdx) {
        if (sceneIdx == 1) return 0xC4B5FD;
        if (sceneIdx == 2) return 0x6EE7B7;
        if (sceneIdx == 3) return 0xFDE68A;
        return AccentCyan;
    }
}

// Critically-damped second-order spring for 60/120Hz smooth interruptible transitions.
struct Spring1D {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;
    float stiffness = 520.0f;
    float damping = 45.6f; // 2 * sqrt(520) ~= 45.6 -> critical damping (no edge overshoot)

    void Snap(float v) {
        value = v;
        target = v;
        velocity = 0.0f;
    }

    // Returns true while still moving.
    bool Step(float dt) {
        if (dt <= 0.0f) return !IsSettled();
        // Snap cleanly if thread was suspended/blocked for a long interval
        if (dt >= 0.45f) {
            Snap(target);
            return false;
        }
        dt = std::clamp(dt, 0.001f, 0.250f);
        int steps = std::clamp((int)std::ceil(dt / 0.010f), 1, 25);
        float h = dt / (float)steps;
        for (int i = 0; i < steps; ++i) {
            float force = (target - value) * stiffness - velocity * damping;
            velocity += force * h;
            value += velocity * h;
        }
        if (std::fabs(target - value) < 0.0012f && std::fabs(velocity) < 0.008f) {
            value = target;
            velocity = 0.0f;
            return false;
        }
        return true;
    }

    bool IsSettled() const {
        return value == target && velocity == 0.0f;
    }
};

} // namespace pp
