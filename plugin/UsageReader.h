#pragma once
#include "UsageData.h"
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ASCII-only cache content -> widen 1:1
inline std::wstring WidenAscii(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

inline UsageData ParseUsageIni(const std::string& raw) {
    auto get = [&](const char* key) -> std::string {
        size_t kl = std::strlen(key), pos = 0;
        while (pos <= raw.size()) {
            size_t eol = raw.find('\n', pos);
            size_t end = (eol == std::string::npos) ? raw.size() : eol;
            std::string line = raw.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > kl && std::memcmp(line.data(), key, kl) == 0 && line[kl] == '=')
                return line.substr(kl + 1);
            if (eol == std::string::npos) break;
            pos = eol + 1;
        }
        return std::string();
    };
    auto geti = [&](const char* k, int def) {
        std::string v = get(k);
        return v.empty() ? def : std::atoi(v.c_str());
    };
    auto getll = [&](const char* k) {
        std::string v = get(k);
        return v.empty() ? 0LL : std::atoll(v.c_str());
    };

    UsageData d;
    d.ok = geti("ok", 0) != 0;
    d.source = WidenAscii(get("source"));
    d.error  = WidenAscii(get("error"));
    d.sub    = WidenAscii(get("sub"));
    d.updated_epoch = getll("updated_epoch");
    d.five_hour_pct = geti("five_hour_pct", -1);
    d.five_hour_reset_epoch = getll("five_hour_reset_epoch");
    d.seven_day_pct = geti("seven_day_pct", -1);
    d.seven_day_reset_epoch = getll("seven_day_reset_epoch");
    d.sonnet_pct = geti("sonnet_pct", -1);
    d.sonnet_reset_epoch = getll("sonnet_reset_epoch");
    d.opus_pct = geti("opus_pct", -1);
    d.opus_reset_epoch = getll("opus_reset_epoch");
    return d;
}

inline UsageData ReadUsageFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return UsageData{};
    std::string raw;
    char buf[2048];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    return ParseUsageIni(raw);
}

inline std::wstring GetCacheIniPath() {
    const wchar_t* la = _wgetenv(L"LOCALAPPDATA");
    std::wstring base = (la && *la) ? la : L".";
    return base + L"\\ClaudeMeter\\usage.ini";
}
