#include <windows.h>
#include <ctime>
#include <string>
#include "PluginInterface.h"
#include "UsageData.h"
#include "UsageReader.h"

// Threading: TrafficMonitor calls DataRequired() and the Get*() methods serially on
// its single update thread, so the cached members need no locking. Returned const
// wchar_t* point to members that stay valid until the next DataRequired().

class CClaudeItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"Claude Code 用量"; } // 用量
    const wchar_t* GetItemId() const override { return L"claudemeter.usage"; }
    const wchar_t* GetItemLableText() const override { return L"CC"; }
    const wchar_t* GetItemValueText() const override { return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }
    int IsDrawResourceUsageGraph() const override { return 1; }
    float GetResourceUsageGraphValue() const override { return m_graph; }

    void Update(const UsageData& d, const std::wstring& primary) {
        m_data = d;
        m_value = FormatValue(d, primary);
        int pct = (primary == L"seven_day") ? d.seven_day_pct : d.five_hour_pct;
        m_graph = (d.ok && pct >= 0) ? static_cast<float>(pct) / 100.0f : 0.0f;
    }
    std::wstring BuildTooltip() const {
        return FormatTooltip(m_data, static_cast<long long>(time(nullptr)));
    }

private:
    UsageData m_data;
    std::wstring m_value = L"--";
    float m_graph = 0.0f;
};

class CClaudeMeterPlugin : public ITMPlugin {
public:
    static CClaudeMeterPlugin& Instance() { static CClaudeMeterPlugin i; return i; }

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }

    void DataRequired() override {
        UsageData d = ReadUsageFile(GetCacheIniPath());
        m_item.Update(d, m_primary);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"ClaudeMeter";
        case TMI_DESCRIPTION: return L"在状态栏显示 Claude Code 剩余用量"; // 在状态栏显示...
        case TMI_AUTHOR:      return L"ClaudeMeter";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"1.0.0";
        case TMI_URL:         return L"https://github.com/";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override {
        m_tooltip = m_item.BuildTooltip();
        return m_tooltip.c_str();
    }

    void OnInitialize(ITrafficMonitor* pApp) override {
        m_app = pApp;
        LoadConfig();
    }

private:
    void LoadConfig() {
        std::wstring p = GetCacheIniPath();
        size_t pos = p.rfind(L"usage.ini");
        if (pos != std::wstring::npos) p.replace(pos, wcslen(L"usage.ini"), L"config.ini");
        wchar_t buf[32];
        GetPrivateProfileStringW(L"display", L"primary", L"five_hour", buf, 32, p.c_str());
        m_primary = buf;
    }

    CClaudeItem m_item;
    ITrafficMonitor* m_app = nullptr;
    std::wstring m_tooltip;
    std::wstring m_primary = L"five_hour";
};

extern "C" ITMPlugin* TMPluginGetInstance() {
    return &CClaudeMeterPlugin::Instance();
}
