#pragma once

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
#include <shlobj.h>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <climits>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace pp {

[[gnu::noinline]] inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

[[gnu::noinline]] inline bool Utf8ToWideStrict(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return false;
    out.resize((size_t)len);
    int actual = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), out.data(), len);
    return actual == len;
}

[[gnu::noinline]] inline std::wstring Utf8ToWide(const std::string& s) {
    std::wstring out;
    if (Utf8ToWideStrict(s, out)) return out;
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    out.resize((size_t)len);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

inline std::wstring SanitizeLogField(const std::wstring& s, size_t maxLen = 96) {
    std::wstring out;
    out.reserve(std::min(s.size(), maxLen));
    for (wchar_t c : s) {
        if (out.size() >= maxLen) break;
        if (c == L'\r' || c == L'\n' || c == L'\t' || c < 0x20) {
            out.push_back(L' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Deterministic typography normalizer for generated UI chrome text (never applied to user prompt content).
inline bool IsCjkChar(wchar_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF);
}

inline bool IsAsciiAlnum(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

inline bool IsFullWidthPunct(wchar_t c) {
    switch (c) {
        case L'\xFF0C': case L'\x3002': case L'\xFF01': case L'\xFF1F':
        case L'\xFF1A': case L'\xFF1B': case L'\x3001':
        case L'\x201C': case L'\x201D': case L'\x2018': case L'\x2019':
        case L'\xFF08': case L'\xFF09': case L'\x300C': case L'\x300D':
            return true;
        default:
            return false;
    }
}

[[gnu::noinline]] inline std::wstring UiText(const std::wstring& raw) {
    // Step 1: replace full-width em-dash sequences (U+2014) with "--"
    std::wstring step1;
    step1.reserve(raw.size() + 8);
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == 0x2014) {
            if (i + 1 < raw.size() && raw[i + 1] == 0x2014) ++i;
            step1 += L"--";
        } else {
            step1.push_back(raw[i]);
        }
    }

    // Step 2: remove spaces immediately adjacent to full-width punctuation, and ensure CJK <-> ASCII spacing
    std::wstring out;
    out.reserve(step1.size() + 8);
    for (size_t i = 0; i < step1.size(); ++i) {
        wchar_t c = step1[i];
        if (c == L' ') {
            bool prevFw = !out.empty() && IsFullWidthPunct(out.back());
            bool nextFw = (i + 1 < step1.size()) && IsFullWidthPunct(step1[i + 1]);
            if (prevFw || nextFw) continue;
        }
        if (!out.empty()) {
            wchar_t prev = out.back();
            if ((IsCjkChar(prev) && IsAsciiAlnum(c)) || (IsAsciiAlnum(prev) && IsCjkChar(c))) {
                out.push_back(L' ');
            }
        }
        out.push_back(c);
    }
    return out;
}

struct PromptItem {
    std::wstring id;
    std::wstring title;
    std::wstring content;
    uint64_t useCount = 0;
    int64_t createdAt = 0;
    int64_t lastUsedAt = 0;
};

enum class DockEdge : int {
    None  = 0,
    Left  = 1,
    Right = 2
};

static constexpr int kMaxScenes = 4;
static constexpr int kSlotsPerScene = 10;
static constexpr int kMaxSceneNameWidthUnits = 8;
static constexpr int kCurrentSeedPackRev = 2;

inline const wchar_t* DefaultSceneName(int idx) {
    switch (idx) {
        case 0: return L"\x901A\x7528"; // 通用
        case 1: return L"\x4EA7\x54C1"; // 产品
        case 2: return L"\x5F00\x53D1"; // 开发
        default: return L"\x5199\x4F5C"; // 写作
    }
}

[[gnu::noinline]] inline int VisualWidthUnits(const std::wstring& s) {
    int units = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            ++i;
            units += 2;
        } else if (c >= 0x20 && c <= 0x7E) {
            units += 1;
        } else {
            units += 2;
        }
    }
    return units;
}

[[gnu::noinline]] inline std::wstring TrimSceneName(const std::wstring& raw) {
    std::wstring clean;
    clean.reserve(raw.size());
    for (wchar_t c : raw) {
        if (c < 0x20) continue;
        clean.push_back(c);
    }
    size_t a = 0;
    while (a < clean.size() && clean[a] == L' ') ++a;
    size_t b = clean.size();
    while (b > a && clean[b - 1] == L' ') --b;
    return clean.substr(a, b - a);
}

inline bool IsValidSceneName(const std::wstring& raw) {
    for (wchar_t c : raw) {
        if (c < 0x20) return false;
    }
    std::wstring trimmed = TrimSceneName(raw);
    if (trimmed.empty()) return false;
    int w = VisualWidthUnits(trimmed);
    return w >= 1 && w <= kMaxSceneNameWidthUnits;
}

[[gnu::noinline]] inline std::wstring ClampSceneName(const std::wstring& raw, int fallbackIdx) {
    std::wstring trimmed = TrimSceneName(raw);
    if (trimmed.empty()) return DefaultSceneName(fallbackIdx);
    std::wstring out;
    int units = 0;
    for (size_t i = 0; i < trimmed.size(); ++i) {
        wchar_t c = trimmed[i];
        bool isSurrogate = (c >= 0xD800 && c <= 0xDBFF && i + 1 < trimmed.size() &&
                            trimmed[i + 1] >= 0xDC00 && trimmed[i + 1] <= 0xDFFF);
        int cw = (!isSurrogate && c >= 0x20 && c <= 0x7E) ? 1 : 2;
        if (units + cw > kMaxSceneNameWidthUnits) break;
        out.push_back(c);
        if (isSurrogate) out.push_back(trimmed[++i]);
        units += cw;
    }
    out = TrimSceneName(out);
    return out.empty() ? std::wstring(DefaultSceneName(fallbackIdx)) : out;
}

struct SceneDeck {
    std::wstring name;
    bool enabled = false;
    std::array<std::wstring, 10> slots{};
};

[[gnu::noinline]] inline std::array<SceneDeck, kMaxScenes> MakeDefaultScenes() {
    std::array<SceneDeck, kMaxScenes> arr{};
    for (int i = 0; i < kMaxScenes; ++i) {
        arr[(size_t)i].name = DefaultSceneName(i);
        arr[(size_t)i].enabled = (i < 2); // Default enabled: 通用 & 产品
    }
    return arr;
}

struct AppState {
    [[gnu::noinline]] AppState() = default;
    [[gnu::noinline]] AppState(const AppState&) = default;
    [[gnu::noinline]] AppState(AppState&&) noexcept = default;
    [[gnu::noinline]] AppState& operator=(const AppState&) = default;
    [[gnu::noinline]] AppState& operator=(AppState&&) noexcept = default;
    [[gnu::noinline]] ~AppState() = default;

    int schemaVersion = 4;
    int seedPackRev = kCurrentSeedPackRev;
    DockEdge dockEdge = DockEdge::Right;
    double dockRatioY = 0.32;
    int floatX = 240;
    int floatY = 180;
    bool showHoverBubble = true;
    bool autoPasteOnClick = false;
    bool autoCheckUpdates = true;
    int64_t lastUpdateCheckAt = 0;
    bool onboardingCompleted = true;
    bool trayClickOpensVault = true;
    bool hasDockAnchor = false;
    int dockAnchorX = 0;
    int dockAnchorY = 0;
    int activeSceneIndex = 0;
    std::array<SceneDeck, kMaxScenes> scenes = MakeDefaultScenes();
    std::vector<PromptItem> vault;

    [[gnu::noinline]] int GetEnabledScenes(int outIdx[kMaxScenes]) const {
        int n = 0;
        for (int i = 0; i < kMaxScenes; ++i) {
            if (scenes[(size_t)i].enabled) {
                if (outIdx) outIdx[n] = i;
                ++n;
            }
        }
        if (n == 0) {
            if (outIdx) outIdx[0] = 0;
            return 1;
        }
        return n;
    }

    int EnabledSceneCount() const {
        return GetEnabledScenes(nullptr);
    }

    bool IsMultiSceneActive() const {
        return EnabledSceneCount() > 1;
    }

    [[gnu::noinline]] int EffectiveActiveSceneIndex() const {
        if (activeSceneIndex >= 0 && activeSceneIndex < kMaxScenes && scenes[(size_t)activeSceneIndex].enabled) {
            return activeSceneIndex;
        }
        for (int i = 0; i < kMaxScenes; ++i) {
            if (scenes[(size_t)i].enabled) return i;
        }
        return 0;
    }

    SceneDeck& ActiveScene() {
        return scenes[(size_t)EffectiveActiveSceneIndex()];
    }

    const SceneDeck& ActiveScene() const {
        return scenes[(size_t)EffectiveActiveSceneIndex()];
    }

    std::array<std::wstring, 10>& ActiveSlots() {
        return scenes[(size_t)EffectiveActiveSceneIndex()].slots;
    }

    const std::array<std::wstring, 10>& ActiveSlots() const {
        return scenes[(size_t)EffectiveActiveSceneIndex()].slots;
    }

    [[gnu::noinline]] bool SetSceneEnabled(int sceneIdx, bool enable) {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes) return false;
        if (scenes[(size_t)sceneIdx].enabled == enable) return true;
        if (!enable) {
            int enabledCnt = 0;
            for (int i = 0; i < kMaxScenes; ++i) {
                if (scenes[(size_t)i].enabled) ++enabledCnt;
            }
            if (enabledCnt <= 1) return false; // Always keep at least 1 enabled scene
            scenes[(size_t)sceneIdx].enabled = false;
            if (activeSceneIndex == sceneIdx) {
                activeSceneIndex = EffectiveActiveSceneIndex();
            }
            return true;
        }
        scenes[(size_t)sceneIdx].enabled = true;
        return true;
    }

    bool ToggleSceneEnabled(int sceneIdx) {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes) return false;
        return SetSceneEnabled(sceneIdx, !scenes[(size_t)sceneIdx].enabled);
    }

    [[gnu::noinline]] void ClearSceneSlots(int sceneIdx) {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes) return;
        for (auto& s : scenes[(size_t)sceneIdx].slots) s.clear();
    }

    [[gnu::noinline]] int CountMountedInScene(int sceneIdx) const {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes) return 0;
        int n = 0;
        for (const auto& s : scenes[(size_t)sceneIdx].slots) {
            if (!s.empty()) ++n;
        }
        return n;
    }

    [[gnu::noinline]] PromptItem* FindById(const std::wstring& id) {
        if (id.empty()) return nullptr;
        for (auto& item : vault) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    [[gnu::noinline]] const PromptItem* FindById(const std::wstring& id) const {
        if (id.empty()) return nullptr;
        for (const auto& item : vault) {
            if (item.id == id) return &item;
        }
        return nullptr;
    }

    [[gnu::noinline]] int FindSlotOfIdInScene(int sceneIdx, const std::wstring& id) const {
        if (id.empty() || sceneIdx < 0 || sceneIdx >= kMaxScenes) return -1;
        const auto& sl = scenes[(size_t)sceneIdx].slots;
        for (int i = 0; i < 10; ++i) {
            if (sl[(size_t)i] == id) return i;
        }
        return -1;
    }

    // Returns 0..9 if currently mounted in the active scene, or -1 otherwise.
    int FindSlotOfId(const std::wstring& id) const {
        return FindSlotOfIdInScene(EffectiveActiveSceneIndex(), id);
    }

    // Assigns `id` into the first empty slot of `sceneIdx`. Returns slot index 0..9 (or existing slot), or -1 if full.
    [[gnu::noinline]] int AssignToSceneFirstEmpty(int sceneIdx, const std::wstring& id) {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes || id.empty() || !FindById(id)) return -1;
        int existing = FindSlotOfIdInScene(sceneIdx, id);
        if (existing >= 0) return existing;
        int emptySlot = FindFirstEmptySlotInScene(sceneIdx);
        if (emptySlot >= 0) {
            scenes[(size_t)sceneIdx].slots[(size_t)emptySlot] = id;
        }
        return emptySlot;
    }

    bool RemoveFromScene(int sceneIdx, const std::wstring& id) {
        int slot = FindSlotOfIdInScene(sceneIdx, id);
        if (slot < 0) return false;
        scenes[(size_t)sceneIdx].slots[(size_t)slot].clear();
        return true;
    }

    [[gnu::noinline]] bool IsMountedInAnyEnabledScene(const std::wstring& id) const {
        if (id.empty()) return false;
        for (int s = 0; s < kMaxScenes; ++s) {
            if (scenes[(size_t)s].enabled && FindSlotOfIdInScene(s, id) >= 0) return true;
        }
        return false;
    }

    [[gnu::noinline]] bool IsMountedInAnyScene(const std::wstring& id) const {
        if (id.empty()) return false;
        for (int s = 0; s < kMaxScenes; ++s) {
            if (FindSlotOfIdInScene(s, id) >= 0) return true;
        }
        return false;
    }

    // Counts how many OTHER enabled scenes (excluding `excludeSceneIdx`, or activeScene if < 0) contain `id`.
    [[gnu::noinline]] int CountOtherEnabledScenesWithId(const std::wstring& id, int* outFirstScene = nullptr, int* outFirstSlot = nullptr, int excludeSceneIdx = -1) const {
        if (outFirstScene) *outFirstScene = -1;
        if (outFirstSlot) *outFirstSlot = -1;
        if (id.empty() || !IsMultiSceneActive()) return 0;
        int skipIdx = (excludeSceneIdx >= 0 && excludeSceneIdx < kMaxScenes) ? excludeSceneIdx : EffectiveActiveSceneIndex();
        int count = 0;
        for (int s = 0; s < kMaxScenes; ++s) {
            if (s == skipIdx || !scenes[(size_t)s].enabled) continue;
            int sl = FindSlotOfIdInScene(s, id);
            if (sl >= 0) {
                if (count == 0) {
                    if (outFirstScene) *outFirstScene = s;
                    if (outFirstSlot) *outFirstSlot = sl;
                }
                ++count;
            }
        }
        return count;
    }

    int FindFirstEmptySlotInScene(int sceneIdx) const {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes) return -1;
        const auto& sl = scenes[(size_t)sceneIdx].slots;
        for (int i = 0; i < 10; ++i) {
            if (sl[(size_t)i].empty()) return i;
        }
        return -1;
    }

    int FindFirstEmptySlot() const {
        return FindFirstEmptySlotInScene(EffectiveActiveSceneIndex());
    }

    uint64_t TotalCopies() const {
        uint64_t sum = 0;
        for (const auto& item : vault) sum += item.useCount;
        return sum;
    }

    [[gnu::noinline]] void SanitizeSlots() {
        bool anyEnabled = false;
        for (int s = 0; s < kMaxScenes; ++s) {
            if (scenes[(size_t)s].enabled) anyEnabled = true;
        }
        if (!anyEnabled) scenes[0].enabled = true;
        activeSceneIndex = EffectiveActiveSceneIndex();
        for (int s = 0; s < kMaxScenes; ++s) {
            scenes[(size_t)s].name = ClampSceneName(scenes[(size_t)s].name, s);
            auto& sl = scenes[(size_t)s].slots;
            for (int i = 0; i < 10; ++i) {
                if (!sl[(size_t)i].empty() && !FindById(sl[(size_t)i])) {
                    sl[(size_t)i].clear();
                }
                // Deduplicate strictly WITHIN each scene; cross-scene sharing of the same Prompt ID is allowed!
                for (int j = 0; j < i; ++j) {
                    if (!sl[(size_t)i].empty() && sl[(size_t)i] == sl[(size_t)j]) {
                        sl[(size_t)i].clear();
                    }
                }
            }
        }
    }

    // Assigns prompt `id` into `targetSlot` (0..9) of `sceneIdx`. If `id` was already in another slot of the SAME scene, swaps them cleanly.
    [[gnu::noinline]] void AssignToSlotInScene(int sceneIdx, int targetSlot, const std::wstring& id) {
        if (sceneIdx < 0 || sceneIdx >= kMaxScenes || targetSlot < 0 || targetSlot >= 10) return;
        auto& sl = scenes[(size_t)sceneIdx].slots;
        if (id.empty()) {
            sl[(size_t)targetSlot].clear();
            return;
        }
        if (!FindById(id)) return;
        int prevSlot = FindSlotOfIdInScene(sceneIdx, id);
        if (prevSlot == targetSlot) return;
        if (prevSlot >= 0) {
            std::swap(sl[(size_t)prevSlot], sl[(size_t)targetSlot]);
        } else {
            sl[(size_t)targetSlot] = id;
        }
    }

    // Assigns prompt `id` into `targetSlot` (0..9) of the active scene. If `id` was already in another slot of the SAME scene, swaps them cleanly.
    void AssignToSlot(int targetSlot, const std::wstring& id) {
        AssignToSlotInScene(EffectiveActiveSceneIndex(), targetSlot, id);
    }
};

inline int64_t NowUnixSeconds() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli{};
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

inline std::wstring GeneratePromptId(const AppState* existing = nullptr) {
    static uint32_t seq = 1;
    for (int attempt = 0; attempt < 64; ++attempt) {
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        wchar_t buf[64]{};
        wsprintfW(buf, L"p_%I64x_%08x", (unsigned long long)NowUnixSeconds(),
                  (unsigned)((qpc.LowPart ^ (seq++ * 2654435761u)) ^ ((uint32_t)GetCurrentProcessId() << 16)));
        std::wstring id(buf);
        if (!existing || !existing->FindById(id)) return id;
    }
    wchar_t fallback[64]{};
    wsprintfW(fallback, L"p_%I64x_%08x", (unsigned long long)NowUnixSeconds(), (unsigned)(seq++));
    return fallback;
}

// --- Strict RFC 8259 JSON Serializer & Parser ---

[[gnu::noinline]] inline void AppendJsonEscaped(std::string& out, const std::wstring& w) {
    out.push_back('"');
    size_t runStart = 0;
    auto flushRun = [&](size_t runEnd) {
        if (runEnd > runStart) {
            int len = WideCharToMultiByte(CP_UTF8, 0, w.data() + runStart, (int)(runEnd - runStart), nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                size_t oldSz = out.size();
                out.resize(oldSz + (size_t)len);
                WideCharToMultiByte(CP_UTF8, 0, w.data() + runStart, (int)(runEnd - runStart), out.data() + oldSz, len, nullptr, nullptr);
            }
        }
    };

    for (size_t i = 0; i < w.size(); ++i) {
        wchar_t c = w[i];
        const char* esc = nullptr;
        switch (c) {
            case L'"':  esc = "\\\""; break;
            case L'\\': esc = "\\\\"; break;
            case L'\b': esc = "\\b"; break;
            case L'\f': esc = "\\f"; break;
            case L'\n': esc = "\\n"; break;
            case L'\r': esc = "\\r"; break;
            case L'\t': esc = "\\t"; break;
            default: break;
        }
        if (esc) {
            flushRun(i);
            out += esc;
            runStart = i + 1;
        } else if (c < 0x20) {
            flushRun(i);
            char hex[8]{};
            wsprintfA(hex, "\\u%04x", (unsigned)c);
            out += hex;
            runStart = i + 1;
        } else if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
                ++i; // Valid surrogate pair, stays in current UTF-16 run
            } else {
                // Lone high surrogate: escape so output remains valid UTF-8
                flushRun(i);
                char hex[8]{};
                wsprintfA(hex, "\\u%04x", (unsigned)c);
                out += hex;
                runStart = i + 1;
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            // Lone low surrogate: escape so output remains valid UTF-8
            flushRun(i);
            char hex[8]{};
            wsprintfA(hex, "\\u%04x", (unsigned)c);
            out += hex;
            runStart = i + 1;
        }
    }
    flushRun(w.size());
    out.push_back('"');
}

[[gnu::noinline]] inline std::string SerializeStateToJson(const AppState& st, bool compatV2 = false) {
    std::string s;
    s.reserve(4096);
    int ratioMilli = (int)(st.dockRatioY * 10000.0 + 0.5);
    char hdr[832]{};
    _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
        "{\n"
        "  \"schemaVersion\": %d,\n"
        "  \"dockEdge\": %d,\n"
        "  \"dockRatioMilli\": %d,\n"
        "  \"floatX\": %d,\n"
        "  \"floatY\": %d,\n"
        "  \"showHoverBubble\": %s,\n"
        "  \"autoPasteOnClick\": %s,\n"
        "  \"autoCheckUpdates\": %s,\n"
        "  \"lastUpdateCheckAt\": %I64d,\n"
        "  \"onboardingCompleted\": %s,\n"
        "  \"trayClickOpensVault\": %s,\n"
        "  \"hasDockAnchor\": %s,\n"
        "  \"dockAnchorX\": %d,\n"
        "  \"dockAnchorY\": %d,\n",
        compatV2 ? 2 : 4,
        (int)st.dockEdge,
        ratioMilli,
        st.floatX,
        st.floatY,
        st.showHoverBubble ? "true" : "false",
        st.autoPasteOnClick ? "true" : "false",
        st.autoCheckUpdates ? "true" : "false",
        (long long)st.lastUpdateCheckAt,
        st.onboardingCompleted ? "true" : "false",
        st.trayClickOpensVault ? "true" : "false",
        st.hasDockAnchor ? "true" : "false",
        st.dockAnchorX,
        st.dockAnchorY);
    s += hdr;
    if (compatV2) {
        const auto& expSlots = st.scenes[0].enabled ? st.scenes[0].slots : st.ActiveSlots();
        s += "  \"slots\": [\n";
        for (size_t i = 0; i < 10; ++i) {
            s += "    ";
            AppendJsonEscaped(s, expSlots[i]);
            s += (i + 1 < 10) ? ",\n" : "\n";
        }
        s += "  ],\n";
    } else {
        char scHdr[128]{};
        _snprintf_s(scHdr, sizeof(scHdr), _TRUNCATE,
            "  \"seedPackRev\": %d,\n"
            "  \"activeSceneIndex\": %d,\n"
            "  \"scenes\": [\n",
            st.seedPackRev,
            st.EffectiveActiveSceneIndex());
        s += scHdr;
        for (size_t sc = 0; sc < (size_t)kMaxScenes; ++sc) {
            s += "    {\n      \"name\": ";
            AppendJsonEscaped(s, ClampSceneName(st.scenes[sc].name, (int)sc));
            s += st.scenes[sc].enabled ? ",\n      \"enabled\": true,\n      \"slots\": ["
                                       : ",\n      \"enabled\": false,\n      \"slots\": [";
            for (size_t i = 0; i < 10; ++i) {
                AppendJsonEscaped(s, st.scenes[sc].slots[i]);
                if (i + 1 < 10) s += ", ";
            }
            s += (sc + 1 < (size_t)kMaxScenes) ? "]\n    },\n" : "]\n    }\n";
        }
        s += "  ],\n";
    }
    s += "  \"vault\": [\n";
    for (size_t i = 0; i < st.vault.size(); ++i) {
        const auto& it = st.vault[i];
        s += "    {\n      \"id\": "; AppendJsonEscaped(s, it.id);
        s += ",\n      \"title\": "; AppendJsonEscaped(s, it.title);
        s += ",\n      \"content\": "; AppendJsonEscaped(s, it.content);
        char meta[192]{};
        _snprintf_s(meta, sizeof(meta), _TRUNCATE,
            ",\n      \"useCount\": %I64u,\n      \"createdAt\": %I64d,\n      \"lastUsedAt\": %I64d\n    }%s\n",
            (unsigned long long)it.useCount,
            (long long)it.createdAt,
            (long long)it.lastUsedAt,
            (i + 1 < st.vault.size()) ? "," : "");
        s += meta;
    }
    s += "  ]\n}\n";
    return s;
}

struct JsonReader {
    const std::string& src;
    size_t pos = 0;

    void SkipWs() {
        // Also skip UTF-8 BOM at beginning if present
        if (pos == 0 && src.size() >= 3 &&
            (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
            pos = 3;
        }
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos;
            else break;
        }
    }

    bool Consume(char expected) {
        SkipWs();
        if (pos < src.size() && src[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    char Peek() {
        SkipWs();
        return (pos < src.size()) ? src[pos] : '\0';
    }

    static int HexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ParseHex4(uint16_t& out) {
        if (pos + 4 > src.size()) return false;
        uint16_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int h = HexVal(src[pos + (size_t)i]);
            if (h < 0) return false;
            v = (uint16_t)((v << 4) | h);
        }
        pos += 4;
        out = v;
        return true;
    }

    bool ParseString(std::wstring& out) {
        out.clear();
        if (!Consume('"')) return false;
        std::string utf8Chunk;
        auto flushChunk = [&]() -> bool {
            if (!utf8Chunk.empty()) {
                std::wstring wide;
                if (!Utf8ToWideStrict(utf8Chunk, wide)) return false;
                out += wide;
                utf8Chunk.clear();
            }
            return true;
        };
        while (pos < src.size()) {
            unsigned char uc = (unsigned char)src[pos++];
            if (uc < 0x20) return false; // Unescaped control characters forbidden by RFC 8259
            char c = (char)uc;
            if (c == '"') {
                return flushChunk();
            }
            if (c == '\\') {
                if (pos >= src.size()) return false;
                char esc = src[pos++];
                switch (esc) {
                    case '"':  utf8Chunk.push_back('"'); break;
                    case '\\': utf8Chunk.push_back('\\'); break;
                    case '/':  utf8Chunk.push_back('/'); break;
                    case 'b':  utf8Chunk.push_back('\b'); break;
                    case 'f':  utf8Chunk.push_back('\f'); break;
                    case 'n':  utf8Chunk.push_back('\n'); break;
                    case 'r':  utf8Chunk.push_back('\r'); break;
                    case 't':  utf8Chunk.push_back('\t'); break;
                    case 'u': {
                        if (!flushChunk()) return false;
                        uint16_t u1 = 0;
                        if (!ParseHex4(u1)) return false;
                        if (u1 == 0) return false; // Reject embedded NUL
                        if (u1 >= 0xDC00 && u1 <= 0xDFFF) return false; // Lone low surrogate
                        if (u1 >= 0xD800 && u1 <= 0xDBFF) {
                            if (pos + 6 > src.size() || src[pos] != '\\' || src[pos + 1] != 'u') return false;
                            pos += 2;
                            uint16_t u2 = 0;
                            if (!ParseHex4(u2) || u2 < 0xDC00 || u2 > 0xDFFF) return false;
                            out.push_back((wchar_t)u1);
                            out.push_back((wchar_t)u2);
                        } else {
                            out.push_back((wchar_t)u1);
                        }
                        break;
                    }
                    default:
                        return false; // Reject unknown escape sequences
                }
            } else {
                utf8Chunk.push_back(c);
            }
        }
        return false;
    }

    bool ParseUInt64(uint64_t& out) {
        SkipWs();
        if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') return false;
        if (src[pos] == '0' && pos + 1 < src.size() && src[pos + 1] >= '0' && src[pos + 1] <= '9') {
            return false; // Reject leading zeros
        }
        uint64_t val = 0;
        while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') {
            uint64_t d = (uint64_t)(src[pos] - '0');
            if (val > (UINT64_MAX - d) / 10ULL) return false;
            val = val * 10ULL + d;
            ++pos;
        }
        if (pos < src.size() && (src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E')) return false;
        out = val;
        return true;
    }

    bool ParseInt64(int64_t& out) {
        SkipWs();
        bool neg = false;
        if (pos < src.size() && src[pos] == '-') {
            neg = true;
            ++pos;
        }
        if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') return false;
        if (src[pos] == '0' && pos + 1 < src.size() && src[pos + 1] >= '0' && src[pos + 1] <= '9') {
            return false; // Reject leading zeros
        }
        constexpr uint64_t kMaxPos = (uint64_t)INT64_MAX;
        constexpr uint64_t kMaxNegMag = kMaxPos + 1ULL;
        uint64_t limit = neg ? kMaxNegMag : kMaxPos;
        uint64_t val = 0;
        while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') {
            uint64_t d = (uint64_t)(src[pos] - '0');
            if (val > (limit - d) / 10ULL) return false;
            val = val * 10ULL + d;
            ++pos;
        }
        if (pos < src.size() && (src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E')) return false;
        if (neg) {
            out = (val == kMaxNegMag) ? INT64_MIN : -(int64_t)val;
        } else {
            out = (int64_t)val;
        }
        return true;
    }

    bool ParseBool(bool& out) {
        SkipWs();
        if (src.compare(pos, 4, "true") == 0) { pos += 4; out = true; return true; }
        if (src.compare(pos, 5, "false") == 0) { pos += 5; out = false; return true; }
        return false;
    }

    bool SkipNumber() {
        SkipWs();
        if (pos < src.size() && src[pos] == '-') ++pos;
        if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') return false;
        if (src[pos] == '0') {
            ++pos;
            if (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') return false;
        } else {
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') return false;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            if (pos >= src.size() || src[pos] < '0' || src[pos] > '9') return false;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        return true;
    }

    static constexpr int kMaxDepth = 32;

    bool SkipValue(int depth = 0) {
        if (depth >= kMaxDepth) return false;
        char c = Peek();
        if (c == '"') {
            std::wstring dummy;
            return ParseString(dummy);
        }
        if (c == '{') {
            Consume('{');
            if (Consume('}')) return true;
            while (true) {
                std::wstring key;
                if (!ParseString(key) || !Consume(':') || !SkipValue(depth + 1)) return false;
                if (Consume(',')) continue;
                return Consume('}');
            }
        }
        if (c == '[') {
            Consume('[');
            if (Consume(']')) return true;
            while (true) {
                if (!SkipValue(depth + 1)) return false;
                if (Consume(',')) continue;
                return Consume(']');
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return SkipNumber();
        }
        if (src.compare(pos, 4, "true") == 0) { pos += 4; return true; }
        if (src.compare(pos, 5, "false") == 0) { pos += 5; return true; }
        if (src.compare(pos, 4, "null") == 0) { pos += 4; return true; }
        return false;
    }
};

inline bool ParseStateFromJson(const std::string& json, AppState& out) {
    JsonReader r{json, 0};
    if (!r.Consume('{')) return false;
    AppState st{};
    st.schemaVersion = 2; // Default if omitted by legacy v1/v2 files
    st.seedPackRev = 0;   // Default for legacy files before seed pack tracking
    bool hasSlots = false;
    bool hasScenes = false;
    bool hasVault = false;
    bool legacyMultiScene = false;
    int legacySceneCount = 3;
    bool anySceneHadEnabled = false;
    std::array<std::wstring, 10> legacySlots{};
    std::vector<std::wstring> seenIds;

    auto parseSlotArray = [&](std::array<std::wstring, 10>& dest) -> bool {
        if (!r.Consume('[')) return false;
        size_t idx = 0;
        if (!r.Consume(']')) {
            while (true) {
                std::wstring slotId;
                if (!r.ParseString(slotId)) return false;
                if (idx < 10) dest[idx++] = slotId;
                if (r.Consume(',')) continue;
                if (!r.Consume(']')) return false;
                break;
            }
        }
        return true;
    };

    if (r.Consume('}')) {
        return false; // Reject empty {} object as invalid schema
    }
    while (true) {
        std::wstring key;
        if (!r.ParseString(key) || !r.Consume(':')) return false;
        if (key == L"schemaVersion") {
            int64_t v = 1;
            if (!r.ParseInt64(v) || v < 1 || v > 4) return false;
            st.schemaVersion = (int)v;
        } else if (key == L"seedPackRev") {
            int64_t v = 0;
            if (!r.ParseInt64(v) || v < 0) return false;
            st.seedPackRev = (int)v;
        } else if (key == L"dockEdge") {
            int64_t v = 2;
            if (!r.ParseInt64(v) || v < 0 || v > 2) return false;
            st.dockEdge = (DockEdge)v;
        } else if (key == L"dockRatioMilli") {
            int64_t v = 3200;
            if (!r.ParseInt64(v)) return false;
            st.dockRatioY = std::clamp((double)v / 10000.0, 0.05, 0.92);
        } else if (key == L"floatX") {
            int64_t v = 240;
            if (!r.ParseInt64(v)) return false;
            st.floatX = (int)std::clamp<int64_t>(v, -32768, 32768);
        } else if (key == L"floatY") {
            int64_t v = 180;
            if (!r.ParseInt64(v)) return false;
            st.floatY = (int)std::clamp<int64_t>(v, -32768, 32768);
        } else if (key == L"showHoverBubble") {
            bool b = true;
            if (!r.ParseBool(b)) return false;
            st.showHoverBubble = b;
        } else if (key == L"autoPasteOnClick") {
            bool b = false;
            if (!r.ParseBool(b)) return false;
            st.autoPasteOnClick = b;
        } else if (key == L"autoCheckUpdates") {
            bool b = true;
            if (!r.ParseBool(b)) return false;
            st.autoCheckUpdates = b;
        } else if (key == L"lastUpdateCheckAt") {
            int64_t v = 0;
            if (!r.ParseInt64(v) || v < 0) return false;
            st.lastUpdateCheckAt = v;
        } else if (key == L"onboardingCompleted") {
            bool b = true;
            if (!r.ParseBool(b)) return false;
            st.onboardingCompleted = b;
        } else if (key == L"trayClickOpensVault") {
            bool b = true;
            if (!r.ParseBool(b)) return false;
            st.trayClickOpensVault = b;
        } else if (key == L"hasDockAnchor") {
            bool b = false;
            if (!r.ParseBool(b)) return false;
            st.hasDockAnchor = b;
        } else if (key == L"dockAnchorX") {
            int64_t v = 0;
            if (!r.ParseInt64(v)) return false;
            st.dockAnchorX = (int)std::clamp<int64_t>(v, -32768, 32768);
        } else if (key == L"dockAnchorY") {
            int64_t v = 0;
            if (!r.ParseInt64(v)) return false;
            st.dockAnchorY = (int)std::clamp<int64_t>(v, -32768, 32768);
        } else if (key == L"multiSceneEnabled") {
            bool b = false;
            if (!r.ParseBool(b)) return false;
            legacyMultiScene = b;
        } else if (key == L"sceneCount") {
            int64_t v = 3;
            if (!r.ParseInt64(v)) return false;
            legacySceneCount = (int)std::clamp<int64_t>(v, 2, kMaxScenes);
        } else if (key == L"activeSceneIndex") {
            int64_t v = 0;
            if (!r.ParseInt64(v)) return false;
            st.activeSceneIndex = (int)std::clamp<int64_t>(v, 0, kMaxScenes - 1);
        } else if (key == L"slots") {
            hasSlots = true;
            if (!parseSlotArray(legacySlots)) return false;
        } else if (key == L"scenes") {
            hasScenes = true;
            if (!r.Consume('[')) return false;
            size_t scIdx = 0;
            if (!r.Consume(']')) {
                while (true) {
                    if (!r.Consume('{')) return false;
                    SceneDeck deck{};
                    deck.name = DefaultSceneName((int)(scIdx < (size_t)kMaxScenes ? scIdx : 0));
                    deck.enabled = false;
                    if (!r.Consume('}')) {
                        while (true) {
                            std::wstring skey;
                            if (!r.ParseString(skey) || !r.Consume(':')) return false;
                            if (skey == L"name") {
                                if (!r.ParseString(deck.name)) return false;
                            } else if (skey == L"enabled") {
                                bool b = false;
                                if (!r.ParseBool(b)) return false;
                                deck.enabled = b;
                                anySceneHadEnabled = true;
                            } else if (skey == L"slots") {
                                if (!parseSlotArray(deck.slots)) return false;
                            } else {
                                if (!r.SkipValue(1)) return false;
                            }
                            if (r.Consume(',')) continue;
                            if (!r.Consume('}')) return false;
                            break;
                        }
                    }
                    if (scIdx < (size_t)kMaxScenes) {
                        st.scenes[scIdx] = std::move(deck);
                    }
                    ++scIdx;
                    if (r.Consume(',')) continue;
                    if (!r.Consume(']')) return false;
                    break;
                }
            }
            if (scIdx == 0) return false;
        } else if (key == L"vault") {
            hasVault = true;
            if (!r.Consume('[')) return false;
            if (!r.Consume(']')) {
                while (true) {
                    if (!r.Consume('{')) return false;
                    PromptItem item{};
                    if (!r.Consume('}')) {
                        while (true) {
                            std::wstring ikey;
                            if (!r.ParseString(ikey) || !r.Consume(':')) return false;
                            if (ikey == L"id") {
                                if (!r.ParseString(item.id)) return false;
                            } else if (ikey == L"title") {
                                if (!r.ParseString(item.title)) return false;
                            } else if (ikey == L"content") {
                                if (!r.ParseString(item.content)) return false;
                            } else if (ikey == L"useCount") {
                                uint64_t v = 0;
                                if (!r.ParseUInt64(v)) return false;
                                item.useCount = v;
                            } else if (ikey == L"createdAt") {
                                if (!r.ParseInt64(item.createdAt)) return false;
                            } else if (ikey == L"lastUsedAt") {
                                if (!r.ParseInt64(item.lastUsedAt)) return false;
                            } else {
                                if (!r.SkipValue(1)) return false;
                            }
                            if (r.Consume(',')) continue;
                            if (!r.Consume('}')) return false;
                            break;
                        }
                    }
                    if (!item.id.empty() && !item.title.empty()) {
                        seenIds.push_back(item.id);
                        st.vault.push_back(std::move(item));
                    }
                    if (r.Consume(',')) continue;
                    if (!r.Consume(']')) return false;
                    break;
                }
            }
        } else {
            if (!r.SkipValue(1)) return false;
        }

        if (r.Consume(',')) continue;
        if (r.Consume('}')) break;
        return false;
    }

    r.SkipWs();
    if (r.pos != json.size()) return false; // Strict EOF check
    if ((!hasScenes && !hasSlots) || !hasVault) return false; // Require core schema keys

    if (seenIds.size() > 1) {
        std::sort(seenIds.begin(), seenIds.end());
        for (size_t i = 1; i < seenIds.size(); ++i) {
            if (seenIds[i] == seenIds[i - 1]) return false; // Reject duplicate prompt IDs
        }
    }

    if (!hasScenes && hasSlots) {
        st.scenes[0].slots = legacySlots;
        st.scenes[0].enabled = true;
        for (int s = 1; s < kMaxScenes; ++s) st.scenes[(size_t)s].enabled = false;
        st.activeSceneIndex = 0;
    } else if (hasScenes && !anySceneHadEnabled) {
        // Legacy schemaVersion 3 file (multiSceneEnabled + sceneCount) -> map cleanly to per-scene enabled flags
        int keepCnt = legacyMultiScene ? std::clamp(legacySceneCount, 2, kMaxScenes) : 1;
        for (int s = 0; s < kMaxScenes; ++s) {
            st.scenes[(size_t)s].enabled = (s < keepCnt);
        }
        if (!legacyMultiScene) st.activeSceneIndex = 0;
    }

    st.SanitizeSlots();
    out = std::move(st);
    return true;
}

// Lightweight Rolling Diagnostic Logger (%APPDATA%\PromptPod\logs\promptpod.log)
class DiagLogger {
public:
    std::wstring logDir;
    std::wstring logFilePath;
    std::wstring oldLogFilePath;
    LONGLONG maxBytes = 512 * 1024; // 512 KB per file, 1 MB total cap
    uint32_t writeFailureCount = 0;
    uint32_t rotateFailureCount = 0;

    static DiagLogger& Instance() {
        static DiagLogger inst;
        return inst;
    }

    static void EnsureDir(const std::wstring& dir) {
        if (dir.empty()) return;
        for (size_t i = 3; i < dir.size(); ++i) {
            if (dir[i] == L'\\' || dir[i] == L'/') {
                std::wstring sub = dir.substr(0, i);
                CreateDirectoryW(sub.c_str(), nullptr);
            }
        }
        CreateDirectoryW(dir.c_str(), nullptr);
    }

    void Init(const std::wstring& baseDataDir) {
        logDir = baseDataDir + L"\\logs";
        EnsureDir(logDir);
        logFilePath = logDir + L"\\promptpod.log";
        oldLogFilePath = logDir + L"\\promptpod.old.log";
        writeFailureCount = 0;
        rotateFailureCount = 0;
    }

    [[gnu::noinline]] bool Log(const char* level, const char* tag, const std::wstring& msg) {
        if (logFilePath.empty()) return false;

        // Check rotation cap before opening for append
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(logFilePath.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER uli{};
            uli.LowPart = fad.nFileSizeLow;
            uli.HighPart = fad.nFileSizeHigh;
            if ((LONGLONG)uli.QuadPart >= maxBytes) {
                if (!MoveFileExW(logFilePath.c_str(), oldLogFilePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    ++rotateFailureCount;
                }
            }
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);
        char prefix[128]{};
        _snprintf_s(prefix, sizeof(prefix), _TRUNCATE,
                      "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [PID:%lu] [%s] [%s] ",
                      (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                      (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, (unsigned)st.wMilliseconds,
                      (unsigned long)GetCurrentProcessId(),
                      level ? level : "INFO",
                      tag ? tag : "APP");

        // Enforce single-line log hygiene so external strings cannot forge log entries
        std::wstring cleanMsg;
        cleanMsg.reserve(msg.size());
        for (wchar_t c : msg) {
            if (c == L'\r' || c == L'\n') cleanMsg.push_back(L' ');
            else cleanMsg.push_back(c);
        }

        std::string line = prefix;
        line += WideToUtf8(cleanMsg);
        line += "\r\n";

        HANDLE h = CreateFileW(logFilePath.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            ++writeFailureCount;
            return false;
        }
        DWORD written = 0;
        BOOL ok = WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr);
        CloseHandle(h);
        if (!ok || written != (DWORD)line.size()) {
            ++writeFailureCount;
            return false;
        }
        return true;
    }

    [[gnu::noinline]] void LogFmt(const char* level, const char* tag, const wchar_t* fmt, ...) {
        if (logFilePath.empty() || !fmt) return;
        wchar_t buf[1024]{};
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
        va_end(args);
        Log(level, tag, buf);
    }

    void LogMonitorTopology(const wchar_t* triggerReason) {
        if (logFilePath.empty()) return;
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int monCount = GetSystemMetrics(SM_CMONITORS);

        struct EnumCtx {
            std::wstring summary;
            int idx = 0;
        } ctx;

        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<EnumCtx*>(lp);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hMon, &mi)) {
                wchar_t item[192]{};
                _snwprintf_s(item, _countof(item), _TRUNCATE,
                              L"M%d%ls mon=(%ld,%ld)-(%ld,%ld) work=(%ld,%ld)-(%ld,%ld); ",
                              c->idx++,
                              (mi.dwFlags & MONITORINFOF_PRIMARY) ? L"[PRI]" : L"",
                              mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
                              mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
                c->summary += item;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        LogFmt("INFO", "DISPLAY", L"Topology(%ls): monitors=%d virtual=(%d,%d %dx%d) | %ls",
               triggerReason ? triggerReason : L"check",
               monCount, vx, vy, vw, vh, ctx.summary.c_str());
    }

    static std::wstring SanitizeSensitiveUserPaths(const std::wstring& input) {
        std::wstring out = input;
        auto maskEnv = [&](const wchar_t* envName, const wchar_t* token) {
            wchar_t val[MAX_PATH * 2]{};
            DWORD n = GetEnvironmentVariableW(envName, val, _countof(val));
            if (n < 3 || n >= _countof(val)) return;
            std::wstring needle(val, n);
            std::wstring lowerOut = out;
            std::wstring lowerNeedle = needle;
            for (auto& c : lowerOut) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            for (auto& c : lowerNeedle) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            size_t pos = 0;
            while ((pos = lowerOut.find(lowerNeedle, pos)) != std::wstring::npos) {
                out.replace(pos, needle.size(), token);
                lowerOut.replace(pos, needle.size(), token);
                pos += wcslen(token);
            }
        };
        maskEnv(L"APPDATA", L"%APPDATA%");
        maskEnv(L"LOCALAPPDATA", L"%LOCALAPPDATA%");
        maskEnv(L"USERPROFILE", L"%USERPROFILE%");
        return out;
    }

    std::wstring ReadRecentLogExcerpt(size_t maxLines = 20, size_t maxChars = 1600) const {
        if (logFilePath.empty()) return {};
        HANDLE h = CreateFileW(logFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
            CloseHandle(h);
            return {};
        }
        constexpr LONGLONG kTailCap = 8192;
        LONGLONG startPos = (sz.QuadPart > kTailCap) ? (sz.QuadPart - kTailCap) : 0;
        LARGE_INTEGER seekPos{};
        seekPos.QuadPart = startPos;
        SetFilePointerEx(h, seekPos, nullptr, FILE_BEGIN);

        DWORD toRead = (DWORD)(sz.QuadPart - startPos);
        std::string raw(toRead, '\0');
        DWORD got = 0;
        BOOL ok = ReadFile(h, raw.data(), toRead, &got, nullptr);
        CloseHandle(h);
        if (!ok || got == 0) return {};
        raw.resize(got);

        // If started mid-file, drop partial first line
        if (startPos > 0) {
            size_t nl = raw.find('\n');
            if (nl != std::string::npos && nl + 1 < raw.size()) {
                raw.erase(0, nl + 1);
            }
        }

        std::vector<std::string> lines;
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t nl = raw.find('\n', pos);
            std::string ln = (nl == std::string::npos) ? raw.substr(pos) : raw.substr(pos, nl - pos);
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (!ln.empty()) lines.push_back(std::move(ln));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        if (lines.size() > maxLines) {
            lines.erase(lines.begin(), lines.begin() + (ptrdiff_t)(lines.size() - maxLines));
        }

        std::wstring result;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::wstring wln = SanitizeSensitiveUserPaths(Utf8ToWide(lines[i]));
            if (!result.empty() && result.size() + wln.size() + 1 > maxChars) break;
            if (!result.empty()) result.push_back(L'\n');
            result += wln;
        }
        return result;
    }
};

class StorageManager {
public:
    [[gnu::noinline]] StorageManager() = default;
    [[gnu::noinline]] ~StorageManager() = default;

    static constexpr size_t kMaxDataFileBytes = 32 * 1024 * 1024;

    std::wstring dataDir;
    std::wstring dataFilePath;
    std::wstring backupFilePath;
    std::wstring logFilePath;
    bool isDefaultAppDataDir = false;
    bool readOnlyRecoveryMode = false;
    bool recoveredFromBackup = false;
    mutable bool pendingLegacyBackup = false;
    mutable std::wstring pendingLegacyBakName;
    mutable std::string pendingLegacyRawBytes;
    std::wstring loadFailureHint;

    static void CreateDirectoriesRecursive(const std::wstring& dir) {
        DiagLogger::EnsureDir(dir);
    }

    static std::wstring ResolveDefaultDataDir() {
        wchar_t appData[MAX_PATH * 4]{};
        DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH * 4);
        if (len > 0 && len < MAX_PATH * 4) {
            return std::wstring(appData, len) + L"\\PromptPod";
        }
        wchar_t modPath[MAX_PATH * 4]{};
        DWORD mlen = GetModuleFileNameW(nullptr, modPath, MAX_PATH * 4);
        std::wstring p(modPath, mlen);
        size_t slash = p.find_last_of(L"\\/");
        return (slash != std::wstring::npos) ? p.substr(0, slash) : L".";
    }

    static std::wstring CanonicalizeDirPath(const std::wstring& raw) {
        if (raw.empty()) return {};
        DWORD needed = GetFullPathNameW(raw.c_str(), 0, nullptr, nullptr);
        std::wstring full = raw;
        if (needed > 0) {
            full.resize(needed);
            DWORD actual = GetFullPathNameW(raw.c_str(), needed, full.data(), nullptr);
            if (actual > 0 && actual < needed) full.resize(actual);
        }
        for (auto& c : full) {
            if (c == L'/') c = L'\\';
            else if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        }
        while (full.size() > 3 && full.back() == L'\\') full.pop_back();
        return full;
    }

    [[gnu::noinline]] void Init(const std::wstring& customDataDir = L"") {
        std::wstring defDir = ResolveDefaultDataDir();
        if (!customDataDir.empty()) {
            dataDir = customDataDir;
            isDefaultAppDataDir = (CanonicalizeDirPath(dataDir) == CanonicalizeDirPath(defDir));
        } else {
            dataDir = defDir;
            isDefaultAppDataDir = true;
        }
        CreateDirectoriesRecursive(dataDir);
        dataFilePath = dataDir + L"\\data.json";
        backupFilePath = dataDir + L"\\data.json.bak";
        readOnlyRecoveryMode = false;
        recoveredFromBackup = false;
        pendingLegacyBackup = false;
        pendingLegacyBakName.clear();
        std::string().swap(pendingLegacyRawBytes);
        loadFailureHint.clear();
        DiagLogger::Instance().Init(dataDir);
        logFilePath = DiagLogger::Instance().logFilePath;
    }

    static bool IsSameExistingFile(const std::wstring& pathA, const std::wstring& pathB) {
        if (pathA.empty() || pathB.empty()) return false;
        HANDLE hA = CreateFileW(pathA.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hA == INVALID_HANDLE_VALUE) return false;
        HANDLE hB = CreateFileW(pathB.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hB == INVALID_HANDLE_VALUE) {
            CloseHandle(hA);
            return false;
        }
        BY_HANDLE_FILE_INFORMATION infoA{}, infoB{};
        bool same = GetFileInformationByHandle(hA, &infoA) && GetFileInformationByHandle(hB, &infoB) &&
                    infoA.dwVolumeSerialNumber == infoB.dwVolumeSerialNumber &&
                    infoA.nFileIndexHigh == infoB.nFileIndexHigh &&
                    infoA.nFileIndexLow == infoB.nFileIndexLow;
        CloseHandle(hA);
        CloseHandle(hB);
        return same;
    }

    bool IsProtectedInternalPath(const std::wstring& targetPath) const {
        if (targetPath.empty()) return false;
        std::wstring canonTarget = CanonicalizeDirPath(targetPath);
        if (!dataFilePath.empty() &&
            (canonTarget == CanonicalizeDirPath(dataFilePath) || IsSameExistingFile(targetPath, dataFilePath))) {
            return true;
        }
        if (!backupFilePath.empty() &&
            (canonTarget == CanonicalizeDirPath(backupFilePath) || IsSameExistingFile(targetPath, backupFilePath))) {
            return true;
        }
        return false;
    }

    bool HasValidBackupFile() const {
        if (backupFilePath.empty()) return false;
        std::string bakRaw;
        AppState probe{};
        return ReadAllBytes(backupFilePath, bakRaw) && ParseStateFromJson(bakRaw, probe);
    }

    std::wstring SafeDirLabelForLog() const {
        return isDefaultAppDataDir ? L"%APPDATA%\\PromptPod" : L"<custom-data-dir>";
    }

    [[gnu::noinline]] static bool ReadAllBytes(const std::wstring& path, std::string& out, DWORD* outWin32Err = nullptr) {
        if (outWin32Err) *outWin32Err = 0;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (outWin32Err) *outWin32Err = GetLastError();
            return false;
        }
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || (uint64_t)sz.QuadPart > kMaxDataFileBytes) {
            if (outWin32Err) *outWin32Err = ERROR_FILE_TOO_LARGE;
            CloseHandle(h);
            return false;
        }
        out.resize((size_t)sz.QuadPart);
        DWORD totalRead = 0;
        while (totalRead < out.size()) {
            DWORD chunk = 0;
            if (!ReadFile(h, out.data() + totalRead, (DWORD)(out.size() - totalRead), &chunk, nullptr) || chunk == 0) {
                if (outWin32Err) *outWin32Err = GetLastError();
                CloseHandle(h);
                out.clear();
                return false;
            }
            totalRead += chunk;
        }
        CloseHandle(h);
        return true;
    }

    [[gnu::noinline]] static bool WriteAllBytesAtomic(const std::wstring& path, const std::string& bytes) {
        if (bytes.size() > kMaxDataFileBytes) return false;

        static uint32_t tmpSeq = 1;
        std::wstring tmpPath;
        HANDLE h = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 8; ++attempt) {
            wchar_t suffix[48]{};
            wsprintfW(suffix, L".%lu.%u.tmp", (unsigned long)GetCurrentProcessId(), (unsigned)(tmpSeq++));
            tmpPath = path + suffix;
            h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_FILE_EXISTS) return false;
        }
        if (h == INVALID_HANDLE_VALUE) return false;

        BOOL ok = TRUE;
        size_t totalWritten = 0;
        while (totalWritten < bytes.size()) {
            DWORD chunk = 0;
            DWORD toWrite = (DWORD)std::min<size_t>(bytes.size() - totalWritten, 4 * 1024 * 1024);
            if (!WriteFile(h, bytes.data() + totalWritten, toWrite, &chunk, nullptr) || chunk == 0) {
                ok = FALSE;
                break;
            }
            totalWritten += chunk;
        }
        if (ok && !FlushFileBuffers(h)) {
            ok = FALSE;
        }
        CloseHandle(h);

        if (!ok || totalWritten != bytes.size()) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
        if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
        return true;
    }

    [[gnu::noinline]] static PromptItem MakeSeed11(int64_t now) {
        return PromptItem{
            L"seed_11",
            L"UX 体验全面审查",
            L"复杂应该留给我们系统，不要留给用户，用户体验至高无上。\n"
            L"按这条原则审查我们整个项目，同时也让Gemini当整个的方案、文案、交互顾问，重点看：\n"
            L"- 系统能判断的，不留给用户\n"
            L"- 此刻没用的，就不出现\n"
            L"- 能替人做的，就直接做\n"
            L"- 同一件事，处处一样\n"
            L"- 按下去有回应，再按能收回\n"
            L"- 不打断人手上的事\n"
            L"- 常用的好做，会丢东西的难误碰\n"
            L"- 界面自己说明自己\n"
            L"然后直接修好，修完亲手用一遍确认，再用GPT帮忙做一下审查。最后直接合并上线。",
            0, now, 0
        };
    }

    [[gnu::noinline]] static PromptItem MakeSeed12(int64_t now) {
        return PromptItem{
            L"seed_12",
            L"苹果级动效审查",
            L"把项目的动效整体做到苹果那种细腻、丝滑的水准。你自己审查整个项目，同时也让Gemini当整个的方案、文案、交互顾问，审完直接做。\n"
            L"怎么找：不要按部件去列，按\"状态变化\"去找。界面上任何东西的状态一变——出现、消失、移动、变大小、换内容、换图标、数值跳动、按下、悬停、加载、完成、出错——都问一句：这个变化用户看得懂吗？是突然跳过去的，还是有来有去、交代清楚了？顺着这个思路，大到页面切换，小到一个图标、一个数字、一个勾，都在范围里。\n"
            L"好动效的标准：\n"
            L"- 动效是在解释发生了什么：东西从哪来、到哪去、和什么有关，而不是装饰。\n"
            L"- 整个项目一套语言：同一类变化用同一种曲线和节奏，项目里已有的统一下来，不要每处自己发明。\n"
            L"- 克制：快、轻、不抢戏；用户反复做的操作要更快，宁可少动也不乱动。\n"
            L"- 跟手、可打断：拖动和调整大小时直接跟手，动画进行中再次操作也能自然接上。\n"
            L"- 不付性能代价：只动必要的东西，长内容里也要稳定满帧，自己量过再交付。\n"
            L"- 不引入新问题：不抖、不闪、不重排、不溢出，尊重系统的\"减少动态效果\"。\n"
            L"- 手机、窄窗口、宽屏、深浅色都要成立。\n"
            L"做完以后让GPT进行一下审查，最后直接合并上线。",
            0, now, 0
        };
    }

    [[gnu::noinline]] static void SeedDefaults(AppState& st, bool mountAllTen = false) {
        int64_t now = NowUnixSeconds();
        st.vault = {
            {
                L"seed_01",
                L"第一性原理修复",
                L"从第一性原理出发，找到最本质原因，修复一下。",
                0, now, 0
            },
            {
                L"seed_02",
                L"先对齐再出 3 版",
                L"先问一些不清晰的问题，对齐我的喜好，然后再出 3 版方案。",
                0, now, 0
            },
            {
                L"seed_03",
                L"重审底层真问题",
                L"不要默认我提出的问题定义就是正确的。你先分析我真正想解决的底层需求，判断我有没有把症状误认为原因、把手段误认为目标。如果存在更值得解决的问题，直接指出来。",
                0, now, 0
            },
            {
                L"seed_04",
                L"推演二三阶影响",
                L"不要只考虑这个改动产生的直接效果。继续推演它的二阶、三阶影响，尤其关注对其他模块、用户行为、系统复杂度和长期维护成本的潜在影响。",
                0, now, 0
            },
            {
                L"seed_05",
                L"跨领域寻找最优解",
                L"其他领域里有没有类似的方案，用跨领域的方式全面思考，找到最优解。",
                0, now, 0
            },
            {
                L"seed_06",
                L"最强反对者证伪",
                L"先假设你当前认为最合理的方案是错误的。站在最强反对者的角度，寻找能够推翻它的证据、反例和边界情况。然后重新评估方案，看看有没有更好的解法。",
                0, now, 0
            },
            {
                L"seed_07",
                L"Git 演进清包袱",
                L"深入分析整个代码库的 Git 历史、关键 PR 和架构演变过程。找出哪些设计是有意为之，哪些是历史遗留，哪些是为了解决已经不存在的问题而引入的。识别可以安全移除的历史包袱。",
                0, now, 0
            },
            {
                L"seed_08",
                L"接手认知成本审查",
                L"假设明天有一个完全不了解项目、但是能力很强的工程师来接手。请评估他理解整个项目需要付出的认知成本，找出那些命名混乱、抽象过度、职责不清晰、需要大量上下文才能理解的代码，并提出简化方案。",
                0, now, 0
            },
            {
                L"seed_09",
                L"近期 PR 卡顿排查",
                L"全面分析最近 XX 天里的所有的 PR，找出最慢的那几个，看看有没有导致卡顿、可以优化的地方。",
                0, now, 0
            },
            {
                L"seed_10",
                L"复盘根因沉淀规则",
                L"全面复盘最近 XX 时间的 Bug、返工、回滚和重复修改。不要只统计发生了什么，要分析这些问题背后有没有共同的根因。把能够预防的共性问题沉淀成项目规则、自动化或者 Skill，避免我们反复支付同样的成本。",
                0, now, 0
            },
            MakeSeed11(now),
            MakeSeed12(now)
        };
        st.schemaVersion = 4;
        st.seedPackRev = kCurrentSeedPackRev;
        st.scenes = MakeDefaultScenes(); // 通用 & 产品 enabled by default
        st.activeSceneIndex = 0;
        size_t mountCount = mountAllTen ? 10 : 2;
        for (size_t i = 0; i < mountCount && i < 10 && i < st.vault.size(); ++i) {
            st.scenes[0].slots[i] = st.vault[i].id;
        }
        st.scenes[1].slots[0] = L"seed_11";
        st.scenes[1].slots[1] = L"seed_12";
    }

    // Lossless one-time upgrade for existing users upgrading from pre-v0.10.0 (seedPackRev < 2):
    // appends the 2 new Khazix product prompts to vault, and ONLY initializes Scene 1 ("产品") if it is empty
    // and still has a default name -- NEVER overwrites existing user-configured scene slots or custom scene names!
    [[gnu::noinline]] static bool ApplyBuiltinSeedUpgradeIfNeeded(AppState& st) {
        if (st.seedPackRev >= kCurrentSeedPackRev) return false;
        int64_t now = NowUnixSeconds();
        if (!st.FindById(L"seed_11")) st.vault.push_back(MakeSeed11(now));
        if (!st.FindById(L"seed_12")) st.vault.push_back(MakeSeed12(now));
        if (st.CountMountedInScene(1) == 0 &&
            (st.scenes[1].name == L"\x601D\x8003" || st.scenes[1].name == L"\x4EA7\x54C1" || st.scenes[1].name.empty())) {
            st.scenes[1].name = L"\x4EA7\x54C1"; // 产品
            st.scenes[1].slots[0] = L"seed_11";
            st.scenes[1].slots[1] = L"seed_12";
            st.scenes[1].enabled = true;
        }
        st.seedPackRev = kCurrentSeedPackRev;
        st.schemaVersion = 4;
        st.SanitizeSlots();
        return true;
    }

    static bool IsUntouchedV010Seed(const AppState& st) {
        if (st.schemaVersion != 1 || st.vault.size() != 4 || st.TotalCopies() != 0) return false;
        if (st.vault[0].id != L"seed_01" || st.vault[1].id != L"seed_02" ||
            st.vault[2].id != L"seed_03" || st.vault[3].id != L"seed_04") {
            return false;
        }
        // Only migrate when slot layout is also untouched (slots 0..3 == seed_01..04, slots 4..9 empty)
        for (size_t i = 0; i < 4; ++i) {
            if (st.scenes[0].slots[i] != st.vault[i].id || st.vault[i].lastUsedAt != 0) return false;
        }
        for (size_t i = 4; i < 10; ++i) {
            if (!st.scenes[0].slots[i].empty()) return false;
        }
        return true;
    }

    bool LoadOrSeed(AppState& st) {
        readOnlyRecoveryMode = false;
        recoveredFromBackup = false;
        pendingLegacyBackup = false;
        pendingLegacyBakName.clear();
        std::string().swap(pendingLegacyRawBytes);
        loadFailureHint.clear();

        DWORD attr = GetFileAttributesW(dataFilePath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                // First launch: file truly does not exist yet
                st = AppState{};
                SeedDefaults(st, false);
                st.onboardingCompleted = false;
                if (!Save(st)) {
                    readOnlyRecoveryMode = true;
                    loadFailureHint = L"无法写入本地数据目录，当前处于只读保护模式";
                    DiagLogger::Instance().Log("ERROR", "STORAGE", L"Initial SeedDefaults Save() failed; entering read-only protection");
                    return false;
                }
                return true;
            }
            // Drive/permission/transient error: NEVER overwrite an existing inaccessible file
            readOnlyRecoveryMode = true;
            loadFailureHint = L"\x65E0\x6CD5\x8BBF\x95EE data.json\xFF0C\x5DF2\x8FDB\x5165\x53EA\x8BFB\x4FDD\x62A4\x6A21\x5F0F";
            DiagLogger::Instance().LogFmt("ERROR", "STORAGE",
                L"GetFileAttributesW(data.json) failed err=%lu; entering read-only recovery mode", (unsigned long)err);
            st = AppState{};
            SeedDefaults(st, false);
            st.onboardingCompleted = true;
            return false;
        }

        std::string raw;
        DWORD readErr = 0;
        if (ReadAllBytes(dataFilePath, raw, &readErr) && ParseStateFromJson(raw, st)) {
            if (IsUntouchedV010Seed(st)) {
                // Keep a lossless backup before upgrading v0.1.0 seed layout
                WriteAllBytesAtomic(dataDir + L"\\data.v1.bak.json", raw);
                DockEdge edge = st.dockEdge;
                double ratioY = st.dockRatioY;
                int fx = st.floatX, fy = st.floatY;
                st = AppState{};
                st.dockEdge = edge;
                st.dockRatioY = ratioY;
                st.floatX = fx;
                st.floatY = fy;
                st.onboardingCompleted = true;
                SeedDefaults(st, true);
                Save(st);
            } else {
                int loadedSchema = st.schemaVersion;
                if (loadedSchema < 3) {
                    pendingLegacyBackup = true;
                    pendingLegacyBakName = L"data.v2.bak.json";
                    pendingLegacyRawBytes = raw;
                    st.schemaVersion = 4;
                } else if (loadedSchema < 4) {
                    pendingLegacyBackup = true;
                    pendingLegacyBakName = L"data.v3.bak.json";
                    pendingLegacyRawBytes = raw;
                    st.schemaVersion = 4;
                }
                // Keep a last-known-good backup once per clean startup BEFORE applying lossless seed upgrade
                WriteAllBytesAtomic(backupFilePath, raw);
                if (ApplyBuiltinSeedUpgradeIfNeeded(st)) {
                    DiagLogger::Instance().LogFmt("INFO", "STORAGE",
                        L"Applied lossless v0.10.0 seed pack upgrade (from schema v%d, vault=%u)",
                        loadedSchema, (unsigned)st.vault.size());
                    Save(st);
                }
            }
            return true;
        }

        // CRITICAL P0 GUARD: existing data.json failed to read or parse -- NEVER overwrite it!
        readOnlyRecoveryMode = true;
        loadFailureHint = (readErr != 0)
            ? L"data.json \x8BFB\x53D6\x5931\x8D25\xFF0C\x5DF2\x4FDD\x7559\x539F\x6587\x4EF6\x5E76\x8FDB\x5165\x53EA\x8BFB\x4FDD\x62A4"
            : L"data.json \x89E3\x6790\x5931\x8D25\xFF0C\x5DF2\x4FDD\x7559\x539F\x6587\x4EF6\x5E76\x8FDB\x5165\x53EA\x8BFB\x4FDD\x62A4";
        DiagLogger::Instance().LogFmt("ERROR", "STORAGE",
            L"Existing data.json failed to load (readErr=%lu, bytes=%u); preserving original file and blocking silent overwrite",
            (unsigned long)readErr, (unsigned)raw.size());

        // Fallback to last-known-good backup in memory only (still read-only so original data.json stays untouched)
        std::string bakRaw;
        if (ReadAllBytes(backupFilePath, bakRaw) && ParseStateFromJson(bakRaw, st)) {
            recoveredFromBackup = true;
            DiagLogger::Instance().Log("WARN", "STORAGE", L"Loaded in-memory fallback from data.json.bak (read-only protection active)");
        } else {
            recoveredFromBackup = false;
            st = AppState{};
            SeedDefaults(st);
        }
        return false;
    }

    void FlushPendingLegacyBackupIfNeeded() const {
        if (!pendingLegacyBackup) return;
        if (!pendingLegacyBakName.empty() && !pendingLegacyRawBytes.empty()) {
            std::wstring bakPath = dataDir + L"\\" + pendingLegacyBakName;
            if (GetFileAttributesW(bakPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                if (WriteAllBytesAtomic(bakPath, pendingLegacyRawBytes)) {
                    DiagLogger::Instance().LogFmt("INFO", "STORAGE", L"Saved lossless pre-upgrade backup to %ls", pendingLegacyBakName.c_str());
                } else {
                    DiagLogger::Instance().LogFmt("WARN", "STORAGE", L"Failed to write %ls before upgrade", pendingLegacyBakName.c_str());
                }
            }
        }
        pendingLegacyBackup = false;
        pendingLegacyBakName.clear();
        std::string().swap(pendingLegacyRawBytes);
    }

    [[gnu::noinline]] bool Save(const AppState& st) const {
        if (readOnlyRecoveryMode) {
            DiagLogger::Instance().Log("WARN", "STORAGE", L"Blocked Save() while read-only recovery protection is active");
            return false;
        }
        FlushPendingLegacyBackupIfNeeded();
        bool ok = WriteAllBytesAtomic(dataFilePath, SerializeStateToJson(st));
        if (!ok) {
            DiagLogger::Instance().LogFmt("ERROR", "STORAGE", L"WriteAllBytesAtomic(data.json) failed err=%lu", (unsigned long)GetLastError());
        }
        return ok;
    }

    bool CreatePreImportSnapshot(std::wstring* outSnapshotPath = nullptr) const {
        if (outSnapshotPath) outSnapshotPath->clear();
        DWORD attr = GetFileAttributesW(dataFilePath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            DWORD err = GetLastError();
            return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
        }
        std::string curBytes;
        if (!ReadAllBytes(dataFilePath, curBytes)) {
            return false;
        }
        wchar_t snapName[80]{};
        wsprintfW(snapName, L"\\data.pre-import.%I64d.json", (long long)NowUnixSeconds());
        std::wstring snapPath = dataDir + snapName;
        if (!WriteAllBytesAtomic(snapPath, curBytes)) {
            return false;
        }
        if (outSnapshotPath) *outSnapshotPath = snapPath;
        return true;
    }

    // Explicit user-confirmed replacement (e.g., importing a validated backup): archives broken original or snapshots current data FIRST
    bool SaveExplicitReplacement(const AppState& st, std::wstring* outSnapshotPath = nullptr, std::wstring* outErr = nullptr) {
        if (outSnapshotPath) outSnapshotPath->clear();
        if (outErr) outErr->clear();

        DWORD attr = GetFileAttributesW(dataFilePath.c_str());
        bool existingOnDisk = (attr != INVALID_FILE_ATTRIBUTES);

        if (readOnlyRecoveryMode && existingOnDisk) {
            wchar_t bakName[80]{};
            wsprintfW(bakName, L"\\data.corrupt.%I64d.json", (long long)NowUnixSeconds());
            std::wstring archivePath = dataDir + bakName;
            if (!CopyFileW(dataFilePath.c_str(), archivePath.c_str(), FALSE)) {
                DiagLogger::Instance().LogFmt("ERROR", "STORAGE",
                    L"Failed to archive corrupt data.json before replacement err=%lu", (unsigned long)GetLastError());
                if (outErr) *outErr = L"无法归档损坏的原 data.json，已中止覆盖以保护原文件";
                return false;
            }
            if (outSnapshotPath) *outSnapshotPath = archivePath;
        } else if (!readOnlyRecoveryMode && existingOnDisk) {
            if (!CreatePreImportSnapshot(outSnapshotPath)) {
                DiagLogger::Instance().Log("ERROR", "STORAGE",
                    L"Failed to create verified pre-import snapshot; aborting replacement");
                if (outErr) *outErr = L"无法创建导入前本地快照，已中止替换以保护现有词库";
                return false;
            }
        }

        std::string serialized = SerializeStateToJson(st);
        bool ok = WriteAllBytesAtomic(dataFilePath, serialized);
        if (ok) {
            readOnlyRecoveryMode = false;
            recoveredFromBackup = false;
            pendingLegacyBackup = false;
            pendingLegacyBakName.clear();
            std::string().swap(pendingLegacyRawBytes);
            loadFailureHint.clear();
            WriteAllBytesAtomic(backupFilePath, serialized);
        } else if (outErr) {
            *outErr = L"写入本地 data.json 失败";
        }
        return ok;
    }

    bool RestoreFromLastBackup(AppState& liveState, std::wstring& outErr) {
        outErr.clear();
        std::string bakRaw;
        AppState parsed{};
        if (!ReadAllBytes(backupFilePath, bakRaw) || !ParseStateFromJson(bakRaw, parsed)) {
            outErr = L"未找到有效的上次备份（data.json.bak）";
            return false;
        }
        std::wstring archivedPath;
        if (!SaveExplicitReplacement(parsed, &archivedPath, &outErr)) {
            if (outErr.empty()) outErr = L"恢复备份失败：无法写入 data.json";
            return false;
        }
        liveState = std::move(parsed);
        DiagLogger::Instance().Log("INFO", "STORAGE", L"Successfully restored data.json from last-known-good data.json.bak");
        return true;
    }
};

} // namespace pp
