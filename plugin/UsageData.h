#pragma once
#include <string>
#include <cstdio>

struct UsageData {
    bool ok = false;
    std::wstring source, error, sub;
    int five_hour_pct = -1, seven_day_pct = -1, sonnet_pct = -1, opus_pct = -1;
    long long updated_epoch = 0;
    long long five_hour_reset_epoch = 0, seven_day_reset_epoch = 0,
              sonnet_reset_epoch = 0, opus_reset_epoch = 0;
};

inline std::wstring FormatRemaining(long long reset_epoch, long long now_epoch) {
    if (reset_epoch <= 0) return L"--";
    long long s = reset_epoch - now_epoch;
    if (s <= 0) return L"now";
    long long m = s / 60, h = m / 60, d = h / 24;
    wchar_t buf[40];
    if (d >= 1)      swprintf(buf, 40, L"%lldd %lldh", d, h % 24);
    else if (h >= 1) swprintf(buf, 40, L"%lldh %lldm", h, m % 60);
    else             swprintf(buf, 40, L"%lldm", m);
    return buf;
}

inline std::wstring PctText(int pct) {
    if (pct < 0) return L"--";
    wchar_t buf[16];
    swprintf(buf, 16, L"%d%%", pct);
    return buf;
}

// primary: L"five_hour" or L"seven_day"
inline std::wstring FormatValue(const UsageData& d, const std::wstring& primary) {
    if (!d.ok) return L"--";
    int pct = (primary == L"seven_day") ? d.seven_day_pct : d.five_hour_pct;
    return PctText(pct);
}

inline std::wstring FormatTooltip(const UsageData& d, long long now_epoch) {
    std::wstring t = L"Claude Code 用量"; // 用量
    if (!d.ok) {
        t += L"\n(数据不可用"; // 数据不可用
        if (!d.error.empty()) { t += L": "; t += d.error; }
        t += L")";
        return t;
    }
    wchar_t line[200];
    swprintf(line, 200, L"\n5 小时:  %s   重置 %s", // 5 小时 / 重置
             PctText(d.five_hour_pct).c_str(),
             FormatRemaining(d.five_hour_reset_epoch, now_epoch).c_str());
    t += line;
    swprintf(line, 200, L"\n周·全部: %s   重置 %s", // 周·全部
             PctText(d.seven_day_pct).c_str(),
             FormatRemaining(d.seven_day_reset_epoch, now_epoch).c_str());
    t += line;
    if (d.sonnet_pct >= 0) {
        swprintf(line, 200, L"\nSonnet:  %s", PctText(d.sonnet_pct).c_str());
        t += line;
    }
    if (d.opus_pct >= 0) {
        swprintf(line, 200, L"\nOpus:    %s", PctText(d.opus_pct).c_str());
        t += line;
    }
    long long age = now_epoch - d.updated_epoch;
    std::wstring ageStr;
    if (d.updated_epoch <= 0) ageStr = L"未知"; // 未知
    else if (age < 90)        ageStr = L"刚刚"; // 刚刚
    else { wchar_t b[40]; swprintf(b, 40, L"%lld分钟前", age / 60); ageStr = b; } // 分钟前
    swprintf(line, 200, L"\n更新 %s · 来源 %s", // 更新 / 来源
             ageStr.c_str(), d.source.empty() ? L"-" : d.source.c_str());
    t += line;
    return t;
}
