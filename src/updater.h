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
#include <winhttp.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include "version.h"
#include "model.h"
#include "d2d_ctx.h"
#include "dark_menu.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace pp {

struct SrwGuard {
    SRWLOCK& lk;
    explicit SrwGuard(SRWLOCK& m) : lk(m) { AcquireSRWLockExclusive(&lk); }
    ~SrwGuard() { ReleaseSRWLockExclusive(&lk); }
    SrwGuard(const SrwGuard&) = delete;
    SrwGuard& operator=(const SrwGuard&) = delete;
};

static constexpr UINT WM_UPDATE_STATE_CHANGED = WM_APP + 21;
static constexpr UINT WM_UPDATE_RESTART_READY = WM_APP + 22;

// ============================================================================
// SemVer Parsing & Comparison
// ============================================================================
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
};

inline SemVer ParseSemVer(const std::wstring& raw) {
    SemVer out{};
    size_t i = 0;
    while (i < raw.size() && (raw[i] == L' ' || raw[i] == L'\t')) ++i;
    if (i < raw.size() && (raw[i] == L'v' || raw[i] == L'V')) ++i;
    size_t end = raw.size();
    while (end > i && (raw[end - 1] == L' ' || raw[end - 1] == L'\t' || raw[end - 1] == L'\r' || raw[end - 1] == L'\n')) --end;

    int parts[3] = { 0, 0, 0 };
    for (int p = 0; p < 3; ++p) {
        if (i >= end || raw[i] < L'0' || raw[i] > L'9') return {};
        if (raw[i] == L'0' && (i + 1 < end) && raw[i + 1] >= L'0' && raw[i + 1] <= L'9') return {}; // Reject leading zeros
        int val = 0;
        while (i < end && raw[i] >= L'0' && raw[i] <= L'9') {
            int d = raw[i] - L'0';
            if (val > (999999 - d) / 10) return {};
            val = val * 10 + d;
            ++i;
        }
        parts[p] = val;
        if (p < 2) {
            if (i >= end || raw[i] != L'.') return {};
            ++i;
        }
    }
    if (i != end) return {}; // Reject pre-release/build suffixes or trailing garbage
    out.major = parts[0];
    out.minor = parts[1];
    out.patch = parts[2];
    out.valid = true;
    return out;
}

inline int CompareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return (a.major < b.major) ? -1 : 1;
    if (a.minor != b.minor) return (a.minor < b.minor) ? -1 : 1;
    if (a.patch != b.patch) return (a.patch < b.patch) ? -1 : 1;
    return 0;
}

// ============================================================================
// URL Helpers, GitHub Host Whitelist & Diagnostics / Feedback Builders
// ============================================================================
inline std::wstring ToLowerAsciiW(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) {
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    }
    return out;
}

inline bool IsAllowedGitHubHost(const std::wstring& host) {
    std::wstring h = ToLowerAsciiW(host);
    return h == L"github.com" ||
           h == L"api.github.com" ||
           h == L"objects.githubusercontent.com" ||
           h == L"release-assets.githubusercontent.com" ||
           h == L"github-releases.githubusercontent.com";
}

inline bool IsOfficialAssetDownloadUrl(const std::wstring& url) {
    std::wstring prefix = L"https://github.com/" APP_GITHUB_REPO_W L"/releases/download/";
    if (url.size() <= prefix.size()) return false;
    if (ToLowerAsciiW(url.substr(0, prefix.size())) != ToLowerAsciiW(prefix)) return false;
    // Reject path traversal or whitespace/control chars
    for (wchar_t c : url) {
        if (c <= 0x20 || c == L'\\' || c == L'"' || c == L'\'' || c == L'<' || c == L'>') return false;
    }
    if (url.find(L"..") != std::wstring::npos) return false;
    return true;
}

inline bool IsValidSha256Hex(const std::wstring& s) {
    if (s.size() != 64) return false;
    for (wchar_t c : s) {
        bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!ok) return false;
    }
    return true;
}

inline std::wstring ExtractTagFromReleaseLocation(const std::wstring& location) {
    // Expected: https://github.com/<owner>/<repo>/releases/tag/v0.6.0 (or relative /<owner>/<repo>/releases/tag/v0.6.0)
    static const wchar_t* kMarker = L"/releases/tag/";
    std::wstring lower = ToLowerAsciiW(location);
    size_t pos = lower.find(kMarker);
    if (pos == std::wstring::npos) return {};
    size_t start = pos + wcslen(kMarker);
    size_t end = start;
    while (end < location.size() && location[end] != L'?' && location[end] != L'#' && location[end] != L'/' && location[end] > 0x20) {
        ++end;
    }
    if (end <= start) return {};
    return location.substr(start, end - start);
}

inline std::wstring UrlEncodeUtf8(const std::wstring& text) {
    std::string utf8 = WideToUtf8(text);
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(utf8.size() * 2);
    for (unsigned char b : utf8) {
        bool unreserved = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
                          (b >= '0' && b <= '9') || b == '-' || b == '_' || b == '.' || b == '~';
        if (unreserved) {
            out.push_back((wchar_t)b);
        } else {
            out.push_back(L'%');
            out.push_back(kHex[(b >> 4) & 0xF]);
            out.push_back(kHex[b & 0xF]);
        }
    }
    return out;
}

inline std::wstring GetRealWindowsVersionString() {
    struct RtlOsVersionInfoW {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    };
    using RtlGetVersionFn = LONG (WINAPI*)(RtlOsVersionInfoW*);
    RtlOsVersionInfoW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(hNt, "RtlGetVersion")));
        if (fn && fn(&vi) == 0) {
            const wchar_t* name = (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000) ? L"Windows 11" :
                                  (vi.dwMajorVersion == 10 ? L"Windows 10" : L"Windows");
            wchar_t buf[96]{};
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%ls (%lu.%lu.%lu, x64)",
                          name, (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion, (unsigned long)vi.dwBuildNumber);
            return buf;
        }
    }
    return L"Windows (x64)";
}

inline std::wstring BuildSanitizedDiagnostics(const AppState& st, const StorageManager& sm, bool includeRecentLogs) {
    std::wstring osStr = GetRealWindowsVersionString();
    int monCount = GetSystemMetrics(SM_CMONITORS);
    int priW = GetSystemMetrics(SM_CXSCREEN);
    int priH = GetSystemMetrics(SM_CYSCREEN);
    int vW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    POINT originPt{ 0, 0 };
    int dpiPct = (int)(GetDpiScaleForPoint(originPt) * 100.0f + 0.5f);

    int mountedSlots = st.CountMountedInScene(st.EffectiveActiveSceneIndex());

    const wchar_t* edgeLabel = (st.dockEdge == DockEdge::Right) ? L"Right"
                             : (st.dockEdge == DockEdge::Left)  ? L"Left"
                                                                : L"Floating";

    wchar_t hdr[704]{};
    _snwprintf_s(
        hdr, _countof(hdr), _TRUNCATE,
        L"- App: PromptPod v%ls\n"
        L"- OS: %ls\n"
        L"- Display: %d monitor(s), primary=%dx%d, virtual=%dx%d, DPI=%d%%\n"
        L"- Dock: edge=%ls, hoverBubble=%ls, autoStart=%ls, autoUpdate=%ls, multiScene=%ls(count=%d,active=%d)\n"
        L"- VaultStats: %u items (%d / 10 slots mounted), storage=%ls, readOnlyRecovery=%ls",
        APP_VERSION_W,
        osStr.c_str(),
        monCount, priW, priH, vW, vH, dpiPct,
        edgeLabel,
        st.showHoverBubble ? L"on" : L"off",
        AutoStart::IsEnabled() ? L"on" : L"off",
        st.autoCheckUpdates ? L"on" : L"off",
        st.IsMultiSceneActive() ? L"on" : L"off",
        st.EnabledSceneCount(),
        st.EffectiveActiveSceneIndex(),
        (unsigned)st.vault.size(), mountedSlots,
        sm.SafeDirLabelForLog().c_str(),
        sm.readOnlyRecoveryMode ? L"yes" : L"no"
    );

    std::wstring out = hdr;
    if (includeRecentLogs) {
        std::wstring logs = DiagLogger::Instance().ReadRecentLogExcerpt(18, 1500);
        if (!logs.empty()) {
            out += L"\n\n### Recent Sanitized Logs\n```text\n";
            out += logs;
            out += L"\n```";
        }
    }
    return out;
}

inline std::wstring BuildGitHubIssueUrl(bool isBug,
                                        const std::wstring& title,
                                        const std::wstring& description,
                                        const std::wstring& osLabel,
                                        const std::wstring& diagnosticsSummary,
                                        bool* outBodyTruncated = nullptr,
                                        bool* outDiagTruncated = nullptr) {
    if (outBodyTruncated) *outBodyTruncated = false;
    if (outDiagTruncated) *outDiagTruncated = false;
    std::wstring url = APP_GITHUB_ISSUES_W L"/new?template=";
    url += isBug ? L"bug_report.yml" : L"feature_request.yml";

    auto appendParam = [&](const wchar_t* key, const std::wstring& val, size_t maxTotalLen) -> bool {
        if (val.empty()) return true;
        std::wstring encoded = UrlEncodeUtf8(val);
        std::wstring seg = L"&";
        seg += key;
        seg += L"=";
        seg += encoded;
        if (url.size() + seg.size() <= maxTotalLen) {
            url += seg;
            return true;
        }
        return false;
    };

    constexpr size_t kMaxUrlLen = 1880;
    appendParam(L"title", title, kMaxUrlLen);
    if (isBug) {
        appendParam(L"version", L"v" APP_VERSION_W, kMaxUrlLen);
        appendParam(L"os", osLabel, kMaxUrlLen);
        bool bodyTr = false;
        bool diagTr = false;
        std::wstring encodedDesc = description.empty() ? L"" : (L"&description=" + UrlEncodeUtf8(description));
        std::wstring encodedDiag = diagnosticsSummary.empty() ? L"" : (L"&diagnostics=" + UrlEncodeUtf8(diagnosticsSummary));

        if (!encodedDesc.empty() && url.size() + encodedDesc.size() <= kMaxUrlLen) {
            url += encodedDesc;
        } else if (!encodedDesc.empty()) {
            bodyTr = true;
            // Check whether diagnostics will ALSO miss URL budget so placeholder matches actual clipboard payload 1:1
            bool diagWillMiss = !encodedDiag.empty() && (url.size() + encodedDiag.size() + 260 > kMaxUrlLen);
            std::wstring note = diagWillMiss
                ? L"（反馈正文与脱敏诊断已一并复制到剪贴板，请直接在此框按 Ctrl+V 粘贴）"
                : L"（反馈正文较长，已自动完整复制到剪贴板，请直接在此框按 Ctrl+V 粘贴）";
            appendParam(L"description", note, kMaxUrlLen);
        }

        if (!encodedDiag.empty()) {
            if (url.size() + encodedDiag.size() <= kMaxUrlLen) {
                url += encodedDiag;
            } else {
                diagTr = true;
                std::wstring note = bodyTr
                    ? L"（已随正文一并复制到剪贴板，粘贴正文后即可包含完整诊断）"
                    : L"（完整脱敏诊断已自动复制到剪贴板，请直接在此框按 Ctrl+V 粘贴）";
                appendParam(L"diagnostics", note, kMaxUrlLen);
            }
        }
        if (outBodyTruncated) *outBodyTruncated = bodyTr;
        if (outDiagTruncated) *outDiagTruncated = diagTr;
    } else {
        if (!appendParam(L"proposal", description, kMaxUrlLen)) {
            if (outBodyTruncated) *outBodyTruncated = true;
            std::wstring note = L"（建议正文较长，已自动完整复制到剪贴板，请直接在此框按 Ctrl+V 粘贴）";
            appendParam(L"proposal", note, kMaxUrlLen);
        }
        if (!diagnosticsSummary.empty()) {
            appendParam(L"context", L"App: PromptPod v" APP_VERSION_W L" | " + osLabel, kMaxUrlLen);
        }
    }
    return url;
}

// ============================================================================
// Update Manifest Model & Strict Parser
// ============================================================================
struct UpdateManifest {
    int schemaVersion = 1;
    std::wstring version;
    std::wstring tag;
    std::wstring releaseUrl;
    std::wstring notes;
    std::wstring assetName;
    std::wstring assetUrl;
    uint64_t assetSize = 0;
    std::wstring sha256;
};

inline bool ParseUpdateManifestJson(const std::string& json, const std::wstring& expectedVersion, UpdateManifest& out) {
    JsonReader r{ json, 0 };
    if (!r.Consume('{')) return false;
    if (r.Consume('}')) return false;

    UpdateManifest m{};
    bool hasVer = false;
    bool hasAssetUrl = false;
    bool hasSha = false;

    while (true) {
        std::wstring key;
        if (!r.ParseString(key) || !r.Consume(':')) return false;
        if (key == L"schemaVersion") {
            int64_t v = 1;
            if (!r.ParseInt64(v) || v != 1) return false;
            m.schemaVersion = (int)v;
        } else if (key == L"version") {
            if (!r.ParseString(m.version)) return false;
            hasVer = true;
        } else if (key == L"tag") {
            if (!r.ParseString(m.tag)) return false;
        } else if (key == L"releaseUrl") {
            if (!r.ParseString(m.releaseUrl)) return false;
        } else if (key == L"notes") {
            if (!r.ParseString(m.notes)) return false;
        } else if (key == L"assetName") {
            if (!r.ParseString(m.assetName)) return false;
        } else if (key == L"assetUrl") {
            if (!r.ParseString(m.assetUrl)) return false;
            hasAssetUrl = true;
        } else if (key == L"assetSize") {
            if (!r.ParseUInt64(m.assetSize)) return false;
        } else if (key == L"sha256") {
            if (!r.ParseString(m.sha256)) return false;
            hasSha = true;
        } else {
            if (!r.SkipValue(1)) return false;
        }

        if (r.Consume(',')) continue;
        if (r.Consume('}')) break;
        return false;
    }

    r.SkipWs();
    if (r.pos != json.size()) return false;
    if (!hasVer || !hasAssetUrl || !hasSha) return false;

    SemVer parsed = ParseSemVer(m.version);
    if (!parsed.valid) return false;
    if (!expectedVersion.empty()) {
        SemVer exp = ParseSemVer(expectedVersion);
        if (!exp.valid || CompareSemVer(parsed, exp) != 0) return false;
    }

    if (!IsOfficialAssetDownloadUrl(m.assetUrl)) return false;
    m.sha256 = ToLowerAsciiW(m.sha256);
    if (!IsValidSha256Hex(m.sha256)) return false;
    if (m.assetSize < 64ULL * 1024ULL || m.assetSize > 16ULL * 1024ULL * 1024ULL) return false;

    if (m.tag.empty()) m.tag = L"v" + m.version;
    if (m.releaseUrl.empty() || ToLowerAsciiW(m.releaseUrl).rfind(L"https://github.com/", 0) != 0) {
        m.releaseUrl = std::wstring(APP_GITHUB_URL_W) + L"/releases/tag/" + m.tag;
    }
    out = std::move(m);
    return true;
}

// ============================================================================
// BCrypt Streaming SHA-256 & File Helpers
// ============================================================================
class Sha256Hasher {
public:
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<UCHAR> hashObj;

    ~Sha256Hasher() { Close(); }

    bool Init() {
        Close();
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
        DWORD objLen = 0, cbData = 0;
        if (!NT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbData, 0))) {
            Close();
            return false;
        }
        hashObj.resize(objLen);
        if (!NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0))) {
            Close();
            return false;
        }
        return true;
    }

    bool Update(const void* data, size_t len) {
        if (!hHash) return false;
        if (len == 0) return true;
        return NT_SUCCESS(BCryptHashData(hHash, const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data)), (ULONG)len, 0));
    }

    bool FinalHex(std::wstring& outHex) {
        outHex.clear();
        if (!hHash) return false;
        UCHAR digest[32]{};
        if (!NT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0))) return false;
        static const wchar_t kHex[] = L"0123456789abcdef";
        outHex.reserve(64);
        for (int i = 0; i < 32; ++i) {
            outHex.push_back(kHex[(digest[i] >> 4) & 0xF]);
            outHex.push_back(kHex[digest[i] & 0xF]);
        }
        return true;
    }

    void Close() {
        if (hHash) { BCryptDestroyHash(hHash); hHash = nullptr; }
        if (hAlg)  { BCryptCloseAlgorithmProvider(hAlg, 0); hAlg = nullptr; }
        hashObj.clear();
    }
};

inline bool ComputeFileSha256Hex(const std::wstring& path, std::wstring& outHex) {
    outHex.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    Sha256Hasher hasher;
    if (!hasher.Init()) {
        CloseHandle(h);
        return false;
    }
    std::vector<uint8_t> buf(65536);
    while (true) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr)) {
            CloseHandle(h);
            return false;
        }
        if (got == 0) break;
        if (!hasher.Update(buf.data(), got)) {
            CloseHandle(h);
            return false;
        }
    }
    CloseHandle(h);
    return hasher.FinalHex(outHex);
}

// ============================================================================
// NTFS Same-Volume Lock-Free Binary Swap & Stale Artifact Cleanup
// ============================================================================
inline bool SwapBinaryWithBackup(const std::wstring& targetExe,
                                 const std::wstring& stagedNewExe,
                                 std::wstring& outOldBackupPath) {
    outOldBackupPath = targetExe + L".old";
    DeleteFileW(outOldBackupPath.c_str()); // Best-effort clear previous backup
    if (!MoveFileExW(targetExe.c_str(), outOldBackupPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        wchar_t alt[48]{};
        wsprintfW(alt, L".%lu.old", (unsigned long)GetCurrentProcessId());
        outOldBackupPath = targetExe + alt;
        if (!MoveFileExW(targetExe.c_str(), outOldBackupPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            outOldBackupPath.clear();
            return false;
        }
    }
    if (!MoveFileExW(stagedNewExe.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // Atomic rollback: restore original executable immediately
        MoveFileExW(outOldBackupPath.c_str(), targetExe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    return true;
}

inline void CleanupStaleUpdateFiles(const std::wstring& explicitOldPath = L"") {
    if (!explicitOldPath.empty()) {
        for (int i = 0; i < 10; ++i) {
            if (DeleteFileW(explicitOldPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) break;
            Sleep(120);
        }
    }
    wchar_t modPath[MAX_PATH * 4]{};
    DWORD n = GetModuleFileNameW(nullptr, modPath, _countof(modPath));
    if (n == 0 || n >= _countof(modPath)) return;
    std::wstring exePath(modPath, n);
    size_t slash = exePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    std::wstring dir = exePath.substr(0, slash + 1);
    std::wstring name = exePath.substr(slash + 1);

    auto sweepPattern = [&](const std::wstring& pat) {
        WIN32_FIND_DATAW fd{};
        HANDLE hf = FindFirstFileW((dir + pat).c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                DeleteFileW((dir + fd.cFileName).c_str());
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    };
    sweepPattern(name + L"*.old");
    sweepPattern(name + L".update.*.new");
}

// ============================================================================
// Native WinHTTP Async Updater (Anti-Rate-Limit Static Manifest + SHA-256)
// ============================================================================
enum class UpdatePhase : int {
    Idle = 0,
    Checking = 1,
    UpToDate = 2,
    Available = 3,
    Downloading = 4,
    Installing = 5,
    Error = 6
};

struct UpdateSnapshot {
    UpdatePhase phase = UpdatePhase::Idle;
    bool silentCheck = false;
    bool canAutoInstall = false;
    int downloadPercent = 0;
    uint64_t downloadedBytes = 0;
    uint64_t totalBytes = 0;
    std::wstring latestVersion;
    std::wstring releaseUrl = APP_GITHUB_RELEASES_W;
    std::wstring releaseNotes;
    std::wstring assetUrl;
    std::wstring sha256;
    uint64_t assetSize = 0;
    std::wstring statusText;
};

class UpdateManager {
public:
    static constexpr int64_t kSilentCheckIntervalSec = 24 * 3600;

    HWND notifyHwnd = nullptr;
    std::wstring customDataDir;

    ~UpdateManager() { Shutdown(); }

    void Init(HWND hwndNotify, const std::wstring& customDir) {
        notifyHwnd = hwndNotify;
        customDataDir = customDir;
    }

    UpdateSnapshot GetSnapshot() const {
        SrwGuard lk(mu);
        return snap;
    }

    bool IsBusy() const {
        SrwGuard lk(mu);
        return snap.phase == UpdatePhase::Checking ||
               snap.phase == UpdatePhase::Downloading ||
               snap.phase == UpdatePhase::Installing;
    }

    bool HasAvailableUpdate() const {
        SrwGuard lk(mu);
        return snap.phase == UpdatePhase::Available ||
               snap.phase == UpdatePhase::Downloading ||
               snap.phase == UpdatePhase::Installing;
    }

    bool StartCheck(bool silent) {
        {
            SrwGuard lk(mu);
            if (snap.phase == UpdatePhase::Checking ||
                snap.phase == UpdatePhase::Downloading ||
                snap.phase == UpdatePhase::Installing) {
                return false;
            }
            snap.phase = UpdatePhase::Checking;
            snap.silentCheck = silent;
            snap.downloadPercent = 0;
            snap.downloadedBytes = 0;
            snap.totalBytes = 0;
            snap.statusText = L"正在检查新版本...";
        }
        NotifyUi();
        if (!LaunchWorker([this, silent](std::shared_ptr<CancelCtx> ctx) {
            RunCheckWorker(ctx, silent);
        })) {
            {
                SrwGuard lk(mu);
                snap.phase = silent ? UpdatePhase::Idle : UpdatePhase::Error;
                snap.statusText = silent ? L"" : L"暂时无法启动更新检查，请稍后重试或前往发布页";
            }
            NotifyUi();
            return false;
        }
        return true;
    }

    bool StartDownloadAndInstall() {
        UpdateManifest target{};
        {
            SrwGuard lk(mu);
            if (snap.phase != UpdatePhase::Available || !snap.canAutoInstall) return false;
            target.version = snap.latestVersion;
            target.releaseUrl = snap.releaseUrl;
            target.assetUrl = snap.assetUrl;
            target.assetSize = snap.assetSize;
            target.sha256 = snap.sha256;

            snap.phase = UpdatePhase::Downloading;
            snap.downloadPercent = 0;
            snap.downloadedBytes = 0;
            snap.totalBytes = target.assetSize;
            snap.statusText = L"正在下载 v" + target.version + L"（0%）...";
        }
        NotifyUi();
        if (!LaunchWorker([this, target](std::shared_ptr<CancelCtx> ctx) {
            RunInstallWorker(ctx, target);
        })) {
            FailInstall(L"无法启动更新下载线程，请稍后重试或前往发布页下载");
            return false;
        }
        return true;
    }

    void Shutdown() {
        std::shared_ptr<CancelCtx> ctx;
        HANDLE hThread = nullptr;
        {
            SrwGuard lk(mu);
            ctx = activeCtx;
            hThread = workerThread;
            workerThread = nullptr;
        }
        if (ctx) ctx->Cancel();
        if (hThread) {
            WaitForSingleObject(hThread, 3000);
            CloseHandle(hThread);
        }
    }

private:
    struct CancelCtx {
        std::atomic<bool> cancelled{ false };
        SRWLOCK hMu = SRWLOCK_INIT;
        HINTERNET hSession = nullptr;
        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;

        void SetHandles(HINTERNET s, HINTERNET c, HINTERNET r) {
            SrwGuard lk(hMu);
            hSession = s;
            hConnect = c;
            hRequest = r;
        }

        void CloseHandles() {
            HINTERNET r = nullptr, c = nullptr, s = nullptr;
            {
                SrwGuard lk(hMu);
                r = hRequest; hRequest = nullptr;
                c = hConnect; hConnect = nullptr;
                s = hSession; hSession = nullptr;
            }
            if (r) WinHttpCloseHandle(r);
            if (c) WinHttpCloseHandle(c);
            if (s) WinHttpCloseHandle(s);
        }

        void Cancel() {
            cancelled.store(true);
            CloseHandles();
        }
    };

    struct HttpResp {
        bool ok = false;
        DWORD statusCode = 0;
        DWORD winErr = 0;
        std::wstring location;
        std::string body;
    };

    mutable SRWLOCK mu = SRWLOCK_INIT;
    UpdateSnapshot snap{};
    std::shared_ptr<CancelCtx> activeCtx;
    HANDLE workerThread = nullptr;

    void NotifyUi() const {
        if (notifyHwnd && IsWindow(notifyHwnd)) {
            PostMessageW(notifyHwnd, WM_UPDATE_STATE_CHANGED, 0, 0);
        }
    }

    bool LaunchWorker(std::function<void(std::shared_ptr<CancelCtx>)> task) {
        HANDLE prevThread = nullptr;
        auto ctx = std::make_shared<CancelCtx>();
        {
            SrwGuard lk(mu);
            prevThread = workerThread;
            workerThread = nullptr;
            activeCtx = ctx;
        }
        if (prevThread) {
            WaitForSingleObject(prevThread, 1500);
            CloseHandle(prevThread);
        }

        struct ThreadParam {
            std::function<void(std::shared_ptr<CancelCtx>)> fn;
            std::shared_ptr<CancelCtx> ctx;
        };
        auto* param = new ThreadParam{ std::move(task), ctx };
        HANDLE h = CreateThread(nullptr, 0, [](LPVOID lp) -> DWORD {
            std::unique_ptr<ThreadParam> p(reinterpret_cast<ThreadParam*>(lp));
            p->fn(p->ctx);
            return 0;
        }, param, 0, nullptr);
        if (!h) {
            delete param;
            return false;
        }
        {
            SrwGuard lk(mu);
            workerThread = h;
        }
        return true;
    }

    static bool CrackHttpsUrl(const std::wstring& url, std::wstring& host, INTERNET_PORT& port, std::wstring& pathAndQuery) {
        URL_COMPONENTSW uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t hostBuf[256]{};
        wchar_t pathBuf[2048]{};
        wchar_t extraBuf[1024]{};
        uc.lpszHostName = hostBuf;
        uc.dwHostNameLength = _countof(hostBuf);
        uc.lpszUrlPath = pathBuf;
        uc.dwUrlPathLength = _countof(pathBuf);
        uc.lpszExtraInfo = extraBuf;
        uc.dwExtraInfoLength = _countof(extraBuf);
        if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
        if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
        host.assign(hostBuf, uc.dwHostNameLength);
        port = uc.nPort ? uc.nPort : INTERNET_DEFAULT_HTTPS_PORT;
        pathAndQuery.assign(pathBuf, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength > 0) {
            pathAndQuery.append(extraBuf, uc.dwExtraInfoLength);
        }
        if (pathAndQuery.empty()) pathAndQuery = L"/";
        return IsAllowedGitHubHost(host);
    }

    static HINTERNET OpenWinHttpSession() {
        std::wstring ua = L"PromptPod/" APP_VERSION_W L" (Windows x64)";
        HINTERNET h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h) {
            h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (h) {
            DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS 1.3 */;
            WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
        }
        return h;
    }

    static HttpResp HttpGetInternal(const std::shared_ptr<CancelCtx>& ctx,
                                    const std::wstring& initialUrl,
                                    int maxRedirects,
                                    size_t maxBodyBytes,
                                    const std::function<bool(const void*, size_t, uint64_t)>& streamSink = nullptr,
                                    int recvTimeoutMs = 15000) {
        HttpResp res{};
        std::wstring curUrl = initialUrl;

        for (int hop = 0; hop <= maxRedirects; ++hop) {
            if (ctx->cancelled.load()) return res;

            std::wstring host, pathAndQuery;
            INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
            if (!CrackHttpsUrl(curUrl, host, port, pathAndQuery)) {
                res.winErr = ERROR_INVALID_PARAMETER;
                return res;
            }

            HINTERNET hSession = OpenWinHttpSession();
            if (!hSession) { res.winErr = GetLastError(); return res; }
            WinHttpSetTimeouts(hSession, 8000, 8000, 10000, recvTimeoutMs);

            HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
            if (!hConnect) {
                res.winErr = GetLastError();
                WinHttpCloseHandle(hSession);
                return res;
            }

            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathAndQuery.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE);
            if (!hRequest) {
                res.winErr = GetLastError();
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return res;
            }

            // Always handle redirects manually so every hop is strictly HTTPS + GitHub host whitelisted
            DWORD disableFeat = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &disableFeat, sizeof(disableFeat));

            ctx->SetHandles(hSession, hConnect, hRequest);
            if (ctx->cancelled.load()) {
                ctx->CloseHandles();
                return res;
            }

            const wchar_t* extraHdrs = L"Accept: application/json, application/octet-stream, */*\r\nCache-Control: no-cache\r\n";
            BOOL sent = WinHttpSendRequest(hRequest, extraHdrs, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            BOOL recvd = sent ? WinHttpReceiveResponse(hRequest, nullptr) : FALSE;
            if (!recvd) {
                res.winErr = GetLastError();
                ctx->CloseHandles();
                return res;
            }

            DWORD status = 0;
            DWORD statusSz = sizeof(status);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSz, WINHTTP_NO_HEADER_INDEX);
            res.statusCode = status;

            std::wstring loc;
            DWORD locLen = 0;
            if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     nullptr, &locLen, WINHTTP_NO_HEADER_INDEX) &&
                GetLastError() == ERROR_INSUFFICIENT_BUFFER && locLen > 0 && locLen < 8192) {
                loc.resize(locLen / sizeof(wchar_t));
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                        loc.data(), &locLen, WINHTTP_NO_HEADER_INDEX)) {
                    while (!loc.empty() && loc.back() == L'\0') loc.pop_back();
                } else {
                    loc.clear();
                }
            }
            res.location = loc;

            bool isRedirect = (status == 301 || status == 302 || status == 303 || status == 307 || status == 308);
            if (isRedirect && !loc.empty() && hop < maxRedirects) {
                ctx->CloseHandles();

                if (loc[0] == L'/') {
                    curUrl = L"https://" + host + loc;
                } else {
                    curUrl = loc;
                }
                continue;
            }

            if (isRedirect || status != 200) {
                res.ok = true;
                ctx->CloseHandles();
                return res;
            }

            // Read Content-Length if provided
            uint64_t contentLen = 0;
            wchar_t clBuf[64]{};
            DWORD clSz = sizeof(clBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                    clBuf, &clSz, WINHTTP_NO_HEADER_INDEX)) {
                contentLen = _wcstoui64(clBuf, nullptr, 10);
                if (contentLen > maxBodyBytes) {
                    res.winErr = ERROR_FILE_TOO_LARGE;
                    ctx->CloseHandles();
                    return res;
                }
            }

            res.body.clear();
            uint64_t totalRead = 0;
            std::vector<char> chunkBuf(32768);
            bool streamOk = true;
            while (!ctx->cancelled.load()) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
                    res.winErr = GetLastError();
                    streamOk = false;
                    break;
                }
                if (avail == 0) break;
                DWORD toRead = (DWORD)std::min<DWORD>(avail, (DWORD)chunkBuf.size());
                DWORD got = 0;
                if (!WinHttpReadData(hRequest, chunkBuf.data(), toRead, &got)) {
                    res.winErr = GetLastError();
                    streamOk = false;
                    break;
                }
                if (got == 0) break;
                totalRead += got;
                if (totalRead > maxBodyBytes) {
                    res.winErr = ERROR_FILE_TOO_LARGE;
                    streamOk = false;
                    break;
                }
                if (streamSink) {
                    if (!streamSink(chunkBuf.data(), got, contentLen)) {
                        streamOk = false;
                        break;
                    }
                } else {
                    res.body.append(chunkBuf.data(), got);
                }
            }

            ctx->CloseHandles();

            res.ok = streamOk && !ctx->cancelled.load();
            return res;
        }
        return res;
    }

    static std::wstring FormatWinHttpError(DWORD err) {
        if (err == ERROR_WINHTTP_TIMEOUT) return L"连接 GitHub 超时，请检查网络或代理后重试";
        if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED) return L"无法解析 GitHub 域名，请检查网络连接";
        if (err == ERROR_WINHTTP_CANNOT_CONNECT) return L"无法连接 GitHub，请检查网络或前往发布页下载";
        if (err == ERROR_WINHTTP_SECURE_FAILURE) return L"HTTPS 证书校验失败，请检查系统时间或网络代理";
        return L"检查更新失败，请检查网络后重试或前往发布页查看";
    }

    void RunCheckWorker(const std::shared_ptr<CancelCtx>& ctx, bool silent) {
        SemVer localVer = ParseSemVer(APP_VERSION_W);
        DiagLogger::Instance().LogFmt("INFO", "UPDATER", L"Check started (silent=%d, local=v%ls)", silent ? 1 : 0, APP_VERSION_W);

        // Stage 1: zero-rate-limit 302 Location probe on /releases/latest
        HttpResp probe = HttpGetInternal(ctx, APP_GITHUB_RELEASES_W, 0, 16 * 1024);
        if (ctx->cancelled.load()) return;

        std::wstring latestTag;
        if (probe.ok && (probe.statusCode == 301 || probe.statusCode == 302 || probe.statusCode == 303 || probe.statusCode == 307 || probe.statusCode == 308)) {
            latestTag = ExtractTagFromReleaseLocation(probe.location);
        }

        // Fallback if proxy/CDN prevented 302 location extraction: query GitHub API once
        if (latestTag.empty() && !ctx->cancelled.load()) {
            std::wstring apiUrl = L"https://api.github.com/repos/" APP_GITHUB_REPO_W L"/releases/latest";
            HttpResp apiResp = HttpGetInternal(ctx, apiUrl, 3, 128 * 1024);
            if (ctx->cancelled.load()) return;
            if (apiResp.ok && apiResp.statusCode == 200) {
                JsonReader r{ apiResp.body, 0 };
                if (r.Consume('{') && !r.Consume('}')) {
                    while (true) {
                        std::wstring k;
                        if (!r.ParseString(k) || !r.Consume(':')) break;
                        if (k == L"tag_name") {
                            r.ParseString(latestTag);
                        } else {
                            if (!r.SkipValue(1)) break;
                        }
                        if (r.Consume(',')) continue;
                        break;
                    }
                }
            } else if (!probe.ok) {
                std::wstring errMsg = FormatWinHttpError(probe.winErr ? probe.winErr : apiResp.winErr);
                DiagLogger::Instance().LogFmt("WARN", "UPDATER", L"Check failed (probeErr=%lu, apiErr=%lu, status=%lu)",
                                              (unsigned long)probe.winErr, (unsigned long)apiResp.winErr, (unsigned long)apiResp.statusCode);
                {
                    SrwGuard lk(mu);
                    snap.phase = silent ? UpdatePhase::Idle : UpdatePhase::Error;
                    snap.statusText = silent ? L"" : errMsg;
                }
                NotifyUi();
                return;
            }
        }

        SemVer remoteVer = ParseSemVer(latestTag);
        if (!remoteVer.valid) {
            DiagLogger::Instance().LogFmt("WARN", "UPDATER", L"Unable to parse latest release tag '%ls'", SanitizeLogField(latestTag).c_str());
            {
                SrwGuard lk(mu);
                snap.phase = silent ? UpdatePhase::Idle : UpdatePhase::Error;
                snap.statusText = silent ? L"" : L"未检测到正式发布版本，可点击前往 Releases 页面查看";
            }
            NotifyUi();
            return;
        }

        wchar_t verStr[48]{};
        wsprintfW(verStr, L"%d.%d.%d", remoteVer.major, remoteVer.minor, remoteVer.patch);
        std::wstring remoteVerStr = verStr;

        if (CompareSemVer(remoteVer, localVer) <= 0) {
            DiagLogger::Instance().LogFmt("INFO", "UPDATER", L"Already up to date (local=v%ls, latest=v%ls)", APP_VERSION_W, remoteVerStr.c_str());
            {
                SrwGuard lk(mu);
                snap.phase = UpdatePhase::UpToDate;
                snap.canAutoInstall = false;
                snap.latestVersion = remoteVerStr;
                snap.releaseUrl = APP_GITHUB_RELEASES_W;
                snap.statusText = L"已是最新版本（v" APP_VERSION_W L"）";
            }
            NotifyUi();
            return;
        }

        // Stage 2: fetch static update-manifest.json attached to this GitHub Release (zero API rate limit)
        std::wstring manifestUrl = std::wstring(APP_GITHUB_URL_W) + L"/releases/download/" + latestTag + L"/update-manifest.json";
        HttpResp mResp = HttpGetInternal(ctx, manifestUrl, 5, 128 * 1024);
        if (ctx->cancelled.load()) return;

        UpdateManifest manifest{};
        bool manifestValid = (mResp.ok && mResp.statusCode == 200 && ParseUpdateManifestJson(mResp.body, remoteVerStr, manifest));

        {
            SrwGuard lk(mu);
            snap.phase = UpdatePhase::Available;
            snap.latestVersion = remoteVerStr;
            if (manifestValid) {
                snap.canAutoInstall = true;
                snap.releaseUrl = manifest.releaseUrl;
                snap.releaseNotes = manifest.notes;
                snap.assetUrl = manifest.assetUrl;
                snap.assetSize = manifest.assetSize;
                snap.sha256 = manifest.sha256;
                snap.statusText = L"发现新版本 v" + remoteVerStr + L"，点击右侧「更新并重启」即可自动升级";
            } else {
                snap.canAutoInstall = false;
                snap.releaseUrl = std::wstring(APP_GITHUB_URL_W) + L"/releases/tag/" + latestTag;
                snap.releaseNotes.clear();
                snap.assetUrl.clear();
                snap.assetSize = 0;
                snap.sha256.clear();
                snap.statusText = L"发现新版本 v" + remoteVerStr + L"，请点击「打开发布页」手动下载替换";
            }
        }
        DiagLogger::Instance().LogFmt("INFO", "UPDATER", L"Update available: v%ls (canAutoInstall=%d, manifestStatus=%lu)",
                                      remoteVerStr.c_str(), manifestValid ? 1 : 0, (unsigned long)mResp.statusCode);
        NotifyUi();
    }

    void RunInstallWorker(const std::shared_ptr<CancelCtx>& ctx, const UpdateManifest& target) {
        wchar_t modPath[MAX_PATH * 4]{};
        DWORD n = GetModuleFileNameW(nullptr, modPath, _countof(modPath));
        if (n == 0 || n >= _countof(modPath)) {
            FailInstall(L"无法获取当前程序路径，请前往 Releases 页面手动下载");
            return;
        }
        std::wstring exePath(modPath, n);

        wchar_t stageSuffix[64]{};
        wsprintfW(stageSuffix, L".update.%lu.new", (unsigned long)GetCurrentProcessId());
        std::wstring stagedPath = exePath + stageSuffix;

        HANDLE hOut = CreateFileW(stagedPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hOut == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            DiagLogger::Instance().LogFmt("WARN", "UPDATER", L"Target dir not writable for in-place update (err=%lu)", (unsigned long)err);
            FailInstall(err == ERROR_ACCESS_DENIED
                ? L"当前程序目录无写权限，无法原地更新 -- 请点击前往发布页手动替换"
                : L"无法创建更新临时文件，请点击前往发布页手动下载");
            return;
        }

        Sha256Hasher hasher;
        if (!hasher.Init()) {
            CloseHandle(hOut);
            DeleteFileW(stagedPath.c_str());
            FailInstall(L"初始化 SHA-256 校验组件失败");
            return;
        }

        uint64_t writtenTotal = 0;
        ULONGLONG lastUiTick = 0;
        int lastPct = -1;
        bool mzValid = true;
        uint8_t firstTwo[2]{};
        size_t firstGot = 0;

        auto sink = [&](const void* data, size_t len, uint64_t contentLen) -> bool {
            if (ctx->cancelled.load()) return false;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
            size_t offset = 0;
            while (firstGot < 2 && offset < len) {
                firstTwo[firstGot++] = bytes[offset++];
                if (firstGot == 2 && (firstTwo[0] != 'M' || firstTwo[1] != 'Z')) {
                    mzValid = false;
                }
            }
            if (!mzValid) return false;

            DWORD w = 0;
            if (!WriteFile(hOut, data, (DWORD)len, &w, nullptr) || w != (DWORD)len) return false;
            if (!hasher.Update(data, len)) return false;
            writtenTotal += len;

            uint64_t denom = (contentLen > 0) ? contentLen : target.assetSize;
            int pct = (denom > 0) ? (int)std::min<uint64_t>(99ULL, (writtenTotal * 100ULL) / denom) : 0;
            ULONGLONG now = GetTickCount64();
            if (pct != lastPct && (pct - lastPct >= 2 || now - lastUiTick >= 120)) {
                lastPct = pct;
                lastUiTick = now;
                wchar_t msg[96]{};
                wsprintfW(msg, L"正在下载 v%ls（%d%%）...", target.version.c_str(), pct);
                {
                    SrwGuard lk(mu);
                    snap.phase = UpdatePhase::Downloading;
                    snap.downloadPercent = pct;
                    snap.downloadedBytes = writtenTotal;
                    snap.totalBytes = denom;
                    snap.statusText = msg;
                }
                NotifyUi();
            }
            return true;
        };

        HttpResp dl = HttpGetInternal(ctx, target.assetUrl, 5, 16 * 1024 * 1024, sink, 30000);
        if (dl.ok) FlushFileBuffers(hOut);
        CloseHandle(hOut);

        if (ctx->cancelled.load()) {
            DeleteFileW(stagedPath.c_str());
            return;
        }
        if (!dl.ok || dl.statusCode != 200 || !mzValid) {
            DeleteFileW(stagedPath.c_str());
            DiagLogger::Instance().LogFmt("ERROR", "UPDATER", L"Download failed (ok=%d, status=%lu, winErr=%lu, mz=%d)",
                                          dl.ok ? 1 : 0, (unsigned long)dl.statusCode, (unsigned long)dl.winErr, mzValid ? 1 : 0);
            FailInstall(FormatWinHttpError(dl.winErr));
            return;
        }

        if (target.assetSize > 0 && writtenTotal != target.assetSize) {
            DeleteFileW(stagedPath.c_str());
            DiagLogger::Instance().LogFmt("ERROR", "UPDATER", L"Downloaded size mismatch: got=%I64u expected=%I64u",
                                          (unsigned long long)writtenTotal, (unsigned long long)target.assetSize);
            FailInstall(L"安装包体积校验不一致，已自动删除临时文件");
            return;
        }

        std::wstring actualSha;
        if (!hasher.FinalHex(actualSha) || actualSha != target.sha256) {
            DeleteFileW(stagedPath.c_str());
            DiagLogger::Instance().Log("ERROR", "UPDATER", L"SHA-256 verification FAILED; staged update deleted");
            FailInstall(L"安装包 SHA-256 完整性校验未通过，已终止安装");
            return;
        }

        {
            SrwGuard lk(mu);
            snap.phase = UpdatePhase::Installing;
            snap.downloadPercent = 100;
            snap.statusText = L"安全校验通过，正在替换并重启新版本...";
        }
        NotifyUi();

        std::wstring oldBackupPath;
        if (!SwapBinaryWithBackup(exePath, stagedPath, oldBackupPath)) {
            DeleteFileW(stagedPath.c_str());
            DiagLogger::Instance().LogFmt("ERROR", "UPDATER", L"SwapBinaryWithBackup failed err=%lu", (unsigned long)GetLastError());
            FailInstall(L"替换当前可执行文件失败，请关闭占用后重试或前往发布页手动覆盖");
            return;
        }

        // Launch newly installed executable cleanly (wait for current PID to release single-instance mutex & sweep .old)
        auto quoteArg = [](const std::wstring& s) -> std::wstring {
            std::wstring out = L"\"";
            size_t bsCount = 0;
            for (wchar_t c : s) {
                if (c == L'\\') {
                    ++bsCount;
                } else if (c == L'"') {
                    out.append(bsCount * 2 + 1, L'\\');
                    out.push_back(L'"');
                    bsCount = 0;
                } else {
                    out.append(bsCount, L'\\');
                    bsCount = 0;
                    out.push_back(c);
                }
            }
            out.append(bsCount * 2, L'\\');
            out.push_back(L'"');
            return out;
        };

        std::wstring cmdLine = quoteArg(exePath);
        if (!customDataDir.empty()) {
            cmdLine += L" --data-dir " + quoteArg(customDataDir);
        }
        wchar_t pidArg[48]{};
        wsprintfW(pidArg, L" --wait-pid %lu", (unsigned long)GetCurrentProcessId());
        cmdLine += pidArg;
        cmdLine += L" --cleanup-old " + quoteArg(oldBackupPath);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');

        if (!CreateProcessW(exePath.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            DWORD err = GetLastError();
            // Rollback so original executable stays intact, and verify rollback actually succeeded
            BOOL restored = MoveFileExW(oldBackupPath.c_str(), exePath.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (restored) {
                DiagLogger::Instance().LogFmt("ERROR", "UPDATER", L"CreateProcessW for updated binary failed err=%lu; rolled back cleanly", (unsigned long)err);
                FailInstall(L"启动新版本失败，已自动恢复原版本");
            } else {
                DWORD rbErr = GetLastError();
                DiagLogger::Instance().LogFmt("ERROR", "UPDATER",
                    L"CreateProcessW failed err=%lu AND rollback MoveFileExW failed err=%lu", (unsigned long)err, (unsigned long)rbErr);
                FailInstall(L"启动新版本失败且自动回滚受阻，请前往发布页重新下载覆盖");
            }
            return;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        DiagLogger::Instance().LogFmt("INFO", "UPDATER", L"In-place update to v%ls launched cleanly; signaling graceful shutdown",
                                      target.version.c_str());
        if (notifyHwnd && IsWindow(notifyHwnd)) {
            PostMessageW(notifyHwnd, WM_UPDATE_RESTART_READY, 0, 0);
        }
    }

    void FailInstall(const std::wstring& msg) {
        {
            SrwGuard lk(mu);
            snap.phase = UpdatePhase::Error;
            snap.downloadPercent = 0;
            snap.statusText = msg;
        }
        NotifyUi();
    }
};

} // namespace pp
