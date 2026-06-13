#include <windows.h>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#include <ctime>
#include <string>
#include "PluginInterface.h"
#include "UsageData.h"
#include "UsageReader.h"
#include "BarRender.h"
#include "SessionScan.h"

// Threading: TrafficMonitor calls DataRequired(), DrawItem(), and the Get*() methods
// serially on its single update thread, so the cached members need no locking.

// Draw the two window-status indicators (green=idle / red=working) in [x,y,w,h]:
// two stacked rows, each a filled dot + its count, using the caller's font.
static void DrawWindowIndicators(HDC dc, int x, int y, int w, int h, int vgap,
                                 const WindowCounts& counts, bool dark) {
    int values[2]        = { counts.idle, counts.working };
    unsigned long cols[2] = { IdleDotColor(dark), WorkingDotColor(dark) };
    SetTextColor(dc, static_cast<COLORREF>(TextColor(dark)));
    for (int i = 0; i < 2; ++i) {
        BRect row = RowAt(x, y, w, h, i, 2, vgap);
        int d = row.h - 2; if (d > 12) d = 12; if (d < 4) d = 4;
        int dotL = row.x + 3;
        int dotT = row.y + (row.h - d) / 2;

        HBRUSH brush = CreateSolidBrush(static_cast<COLORREF>(cols[i]));
        HPEN pen = CreatePen(PS_SOLID, 1, static_cast<COLORREF>(cols[i])); // same hue: no dark border
        HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
        HPEN oldPen = (HPEN)SelectObject(dc, pen);
        Ellipse(dc, dotL, dotT, dotL + d, dotT + d);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        std::wstring num = CountText(values[i]);
        RECT nr = { dotL + d + 4, row.y, x + w - 1, row.y + row.h };
        DrawTextW(dc, num.c_str(), -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

class CClaudeItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"Claude Code 用量与窗口"; } // 用量与窗口
    const wchar_t* GetItemId() const override { return L"claudemeter.usage"; }
    // Ignored while IsCustomDraw()==true, but the contract still forbids null.
    const wchar_t* GetItemLableText() const override { return L"CC"; }
    const wchar_t* GetItemValueText() const override { return L""; }
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }

    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return 165; } // 96-DPI logical; host scales by DPI

    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override {
        HDC dc = static_cast<HDC>(hDC);
        const int vgap = (h >= 18) ? 2 : 1;
        const int hgap = 3;
        const int labelW = 16;
        const int numberW = 30;

        // Reserve a right-hand zone for the two window-status indicators; the
        // usage bars keep the rest. Proportional split stays stable across DPI.
        int indW = w * 42 / 165;
        if (indW < 30) indW = 30;
        if (indW > w / 2) indW = w / 2;   // never starve the bars on a narrow item
        int usageW = w - indW;
        int indX = x + usageW;

        int oldBk = SetBkMode(dc, TRANSPARENT);
        COLORREF oldText = GetTextColor(dc);

        BRect probe = RowAt(x, y, usageW, h, 0, 3, vgap);
        int fontH = probe.h - 1;
        if (fontH > 14) fontH = 14;
        if (fontH < 8) fontH = 8;
        HFONT font = CreateFontW(-fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(dc, font);

        int pcts[3] = { m_data.five_hour_pct, m_data.seven_day_pct, m_data.sonnet_pct };
        if (!m_data.ok) { pcts[0] = pcts[1] = pcts[2] = -1; }

        HBRUSH trackBrush = CreateSolidBrush(static_cast<COLORREF>(TrackColor(dark_mode)));
        SetTextColor(dc, static_cast<COLORREF>(TextColor(dark_mode)));

        for (int i = 0; i < 3; ++i) {
            BRect row = RowAt(x, y, usageW, h, i, 3, vgap);
            RowParts rp = SplitRow(row, labelW, numberW, hgap);

            RECT lr = { rp.label.x, rp.label.y, rp.label.x + rp.label.w, rp.label.y + rp.label.h };
            DrawTextW(dc, WindowLabel(i), -1, &lr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            int barH = rp.bar.h; if (barH > 12) barH = 12; if (barH < 3) barH = 3;
            int barY = rp.bar.y + (rp.bar.h - barH) / 2;
            RECT track = { rp.bar.x, barY, rp.bar.x + rp.bar.w, barY + barH };
            FillRect(dc, &track, trackBrush);

            int fw = FillWidth(pcts[i], rp.bar.w);
            if (fw > 0) {
                HBRUSH fillBrush = CreateSolidBrush(static_cast<COLORREF>(BarColor(pcts[i], dark_mode)));
                RECT fill = { rp.bar.x, barY, rp.bar.x + fw, barY + barH };
                FillRect(dc, &fill, fillBrush);
                DeleteObject(fillBrush);
            }

            std::wstring num = PctText(pcts[i]);
            RECT nr = { rp.number.x, rp.number.y, rp.number.x + rp.number.w, rp.number.y + rp.number.h };
            DrawTextW(dc, num.c_str(), -1, &nr,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        DeleteObject(trackBrush);

        // Right-hand window-status indicators (uses the same font).
        DrawWindowIndicators(dc, indX, y, indW, h, vgap, m_counts, dark_mode);

        SelectObject(dc, oldFont);
        DeleteObject(font);
        SetTextColor(dc, oldText);
        SetBkMode(dc, oldBk);
    }

    void SetData(const UsageData& d) { m_data = d; }
    void SetCounts(const WindowCounts& c) { m_counts = c; }
    std::wstring BuildTooltip() const {
        std::wstring t = FormatTooltip(m_data, static_cast<long long>(time(nullptr)));
        t += FormatWindowStatusLine(m_counts);
        return t;
    }

private:
    UsageData m_data;
    WindowCounts m_counts;
};

class CClaudeMeterPlugin : public ITMPlugin {
public:
    static CClaudeMeterPlugin& Instance() { static CClaudeMeterPlugin i; return i; }

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }

    void DataRequired() override {
        m_item.SetData(ReadUsageFile(GetCacheIniPath()));
        WindowCounts counts;
        try { counts = ScanSessionsDefault(); }
        catch (...) { counts = WindowCounts{}; }   // never let a scan error reach the host
        m_item.SetCounts(counts);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"ClaudeMeter";
        case TMI_DESCRIPTION: return L"在状态栏显示 Claude Code 用量与窗口状态"; // ...用量与窗口状态
        case TMI_AUTHOR:      return L"ClaudeMeter";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"3.0.0";
        case TMI_URL:         return L"https://github.com/";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override {
        m_tooltip = m_item.BuildTooltip();
        return m_tooltip.c_str();
    }

    void OnInitialize(ITrafficMonitor* pApp) override { m_app = pApp; }

private:
    CClaudeItem m_item;
    ITrafficMonitor* m_app = nullptr;
    std::wstring m_tooltip;
};

extern "C" ITMPlugin* TMPluginGetInstance() { return &CClaudeMeterPlugin::Instance(); }
