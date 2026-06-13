#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

// ---- Pure logic (no <windows.h>) : unit-testable from the console host ----
//
// Claude Code maintains ~/.claude/sessions/<pid>.json per session. Real TUI
// windows (entrypoint "cli") write a "status" field (idle/busy/shell); headless
// SDK sessions (entrypoint "sdk-cli") do not, so "has a status field" is the
// discriminator that excludes them.

struct SessionEntry {
    bool alive = false;       // owning process is a live Claude window
    bool has_status = false;  // session file carried a "status" field
    std::string status;       // "idle" | "busy" | "shell" | ... (ASCII)
};

struct WindowCounts { int idle = 0; int working = 0; };

// idle iff status=="idle"; any other non-empty status (busy/shell/future active
// states) counts as working. Dead or status-less sessions are ignored, so the
// two buckets always sum to the number of live CLI windows.
inline WindowCounts CountSessions(const std::vector<SessionEntry>& entries) {
    WindowCounts c;
    for (const auto& e : entries) {
        if (!e.alive || !e.has_status || e.status.empty()) continue;
        if (e.status == "idle") ++c.idle;
        else ++c.working;
    }
    return c;
}

// Extract a top-level JSON string field. Matches the quoted key `"<field>"`
// exactly, so "status" never matches the longer key "statusUpdatedAt". Returns
// false when the field is absent. Sufficient for Claude's compact one-line JSON.
inline bool ExtractJsonString(const std::string& json, const char* field, std::string& out) {
    std::string needle = std::string("\"") + field + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return false;
    size_t i = k + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i >= json.size() || json[i] != ':') return false;
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i >= json.size() || json[i] != '"') return false;
    ++i;
    out.clear();
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) { out.push_back(json[i + 1]); i += 2; }
        else { out.push_back(json[i]); ++i; }
    }
    return true;
}

// Extract a top-level JSON numeric field (bare or quoted) as a 64-bit integer.
// Same exact-key matching as ExtractJsonString. Used for "startedAt" (UTC unix-ms).
inline bool ExtractJsonNumber(const std::string& json, const char* field, long long& out) {
    std::string needle = std::string("\"") + field + "\"";
    size_t k = json.find(needle);
    if (k == std::string::npos) return false;
    size_t i = k + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i >= json.size() || json[i] != ':') return false;
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i < json.size() && json[i] == '"') ++i;   // tolerate quoted numbers (e.g. procStart)
    size_t start = i;
    if (i < json.size() && (json[i] == '-' || json[i] == '+')) ++i;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') ++i;
    if (i == start) return false;
    out = strtoll(json.substr(start, i - start).c_str(), nullptr, 10);
    return true;
}

inline std::wstring CountText(int n) {
    if (n < 0) n = 0;
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", n);
    return buf;
}

// PID-reuse guard (pure): the live process's creation time must be within
// tolerance of the time the session recorded at startup. A reused PID's new
// process was created well after the stale file's startedAt, so it fails. An
// unknown startedAt (<=0) fails OPEN so a missing field never zeros the count.
// Tolerance comfortably exceeds the (~sub-second) gap between process creation
// and when Claude writes startedAt, while rejecting reuse minutes/hours/days later.
inline bool StartTimeMatches(long long procCreatedUnixMs, long long startedAtMs) {
    if (startedAtMs <= 0) return true;
    long long diff = procCreatedUnixMs - startedAtMs;
    if (diff < 0) diff = -diff;
    return diff <= 120000LL;   // 2 minutes
}

inline std::wstring FormatWindowStatusLine(const WindowCounts& c) {
    wchar_t buf[80];
    swprintf(buf, 80, L"\n窗口: 工作 %d · 闲置 %d", c.working, c.idle); // 窗口/工作/闲置
    return buf;
}

// ---- Impure: filesystem scan + process liveness (needs <windows.h>) ----
// Guarded so the pure logic above stays importable without GDI/Win32.
#ifdef _WINDOWS_
#include <cwctype>

// CLAUDE_CONFIG_DIR else %USERPROFILE%\.claude, then \sessions -- matches the
// Python collector's config-dir resolution.
inline std::wstring GetSessionsDir() {
    const wchar_t* cfg = _wgetenv(L"CLAUDE_CONFIG_DIR");
    std::wstring base;
    if (cfg && *cfg) {
        base = cfg;
    } else {
        const wchar_t* up = _wgetenv(L"USERPROFILE");
        base = (up && *up) ? std::wstring(up) + L"\\.claude" : L".\\.claude";
    }
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
    return base + L"\\sessions";
}

// Bounded read: stops at maxBytes so a corrupt/oversized file can never balloon
// memory on the host's update thread. Real session files are ~300 bytes.
inline std::string ReadSmallFileA(const std::wstring& path, size_t maxBytes = 64u * 1024u) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return std::string();
    std::string raw;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        raw.append(buf, n);
        if (raw.size() >= maxBytes) break;
    }
    fclose(f);
    return raw;
}

// FILETIME (100ns since 1601-01-01 UTC) -> Unix epoch milliseconds (UTC). The
// constant is the 1601->1970 offset in 100ns units; both sides are UTC, so this
// is free of any timezone/DST conversion.
inline long long FileTimeToUnixMs(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

// Is `pid` a live Claude window owning THIS session file? OpenProcess failure =>
// dead. The image-name check rejects only on a confident non-match. The decisive
// PID-reuse guard is the creation-time match: the live process's creation time
// must equal the time the session recorded at startup (startedAtMs, UTC unix-ms)
// within tolerance -- a reused PID's new process was created well after the stale
// file's startedAt, so it fails. Both timestamp paths fail OPEN (treated as alive
// when unavailable) so an API quirk or a startedAt-less file never silently zeros
// the indicator. PROCESS_QUERY_LIMITED_INFORMATION needs no elevation for the
// current user's own processes.
inline bool IsClaudeProcessAlive(unsigned long pid, long long startedAtMs) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = true;
    wchar_t path[MAX_PATH];
    DWORD sz = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
        std::wstring p(path, sz);
        size_t slash = p.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos) ? p : p.substr(slash + 1);
        for (auto& ch : name) ch = (wchar_t)towlower(ch);
        ok = (name == L"claude.exe" || name == L"node.exe");
    }
    if (ok && startedAtMs > 0) {
        FILETIME cre, ex, ker, usr;
        if (GetProcessTimes(h, &cre, &ex, &ker, &usr))
            ok = StartTimeMatches(FileTimeToUnixMs(cre), startedAtMs);
    }
    CloseHandle(h);
    return ok;
}

inline WindowCounts ScanSessions(const std::wstring& dir) {
    std::vector<SessionEntry> entries;
    std::wstring pattern = dir + L"\\*.json";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            // Real session files are ~300 bytes; skip implausibly large ones so a
            // corrupt/planted file can never trigger a large read on the host thread.
            if (fd.nFileSizeHigh != 0 || fd.nFileSizeLow > 64u * 1024u) continue;
            unsigned long pid = (unsigned long)wcstoul(fd.cFileName, nullptr, 10); // "<pid>.json"
            std::string raw = ReadSmallFileA(dir + L"\\" + fd.cFileName);
            long long startedAt = 0;
            ExtractJsonNumber(raw, "startedAt", startedAt);    // anchors the PID-reuse guard
            SessionEntry e;
            e.alive = IsClaudeProcessAlive(pid, startedAt);
            if (e.alive) e.has_status = ExtractJsonString(raw, "status", e.status);
            entries.push_back(e);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return CountSessions(entries);
}

inline WindowCounts ScanSessionsDefault() { return ScanSessions(GetSessionsDir()); }
#endif // _WINDOWS_
