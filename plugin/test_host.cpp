// Console test for ClaudeMeter pure logic (and, in Task 6, the built DLL).
#include <windows.h>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#include "PluginInterface.h"
#include "UsageData.h"
#include "UsageReader.h"
#include "BarRender.h"
#include "SessionScan.h"
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)){ wprintf(L"FAIL %hs:%d  %hs\n", __FILE__, __LINE__, #cond); ++g_failures; } } while(0)

static void unit_tests() {
    // FormatRemaining
    CHECK(FormatRemaining(0, 100) == L"--");
    CHECK(FormatRemaining(100, 100) == L"now");
    CHECK(FormatRemaining(100 + 90, 100) == L"1m");
    CHECK(FormatRemaining(100 + 3600 + 120, 100) == L"1h 2m");
    CHECK(FormatRemaining(100 + 86400LL * 6 + 3600 * 3, 100) == L"6d 3h");

    UsageData bad; bad.ok = false;

    // ParseUsageIni
    std::string ini =
        "[usage]\r\nok=1\r\nsource=oauth\r\nupdated_epoch=123\r\n"
        "five_hour_pct=67\r\nfive_hour_reset_epoch=1000003600\r\n"
        "seven_day_pct=10\r\nseven_day_reset_epoch=2000000000\r\n"
        "sonnet_pct=0\r\nsonnet_reset_epoch=2000000000\r\n"
        "opus_pct=-1\r\nopus_reset_epoch=0\r\nsub=max\r\n";
    UsageData p = ParseUsageIni(ini);
    CHECK(p.ok == true);
    CHECK(p.source == L"oauth");
    CHECK(p.five_hour_pct == 67);
    CHECK(p.seven_day_pct == 10);
    CHECK(p.sonnet_pct == 0);
    CHECK(p.opus_pct == -1);
    CHECK(p.five_hour_reset_epoch == 1000003600LL);
    CHECK(p.sub == L"max");

    UsageData empty = ParseUsageIni("");
    CHECK(empty.ok == false);
    CHECK(empty.five_hour_pct == -1);

    // FormatTooltip smoke (must contain percentages and not crash)
    std::wstring tip = FormatTooltip(p, 1000000000LL);
    CHECK(tip.find(L"67%") != std::wstring::npos);
    CHECK(tip.find(L"Sonnet") != std::wstring::npos);
    std::wstring tipBad = FormatTooltip(bad, 100);
    CHECK(tipBad.find(L"不可用") != std::wstring::npos); // 不可用
}

static void draw_item_render_test(IPluginItem* item) {
    const int W = item->GetItemWidth() > 0 ? item->GetItemWidth() : 120, H = 40;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;            // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, dib);

    const COLORREF SENTINEL = RGB(255, 0, 255); // magenta background
    RECT full = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(SENTINEL);
    FillRect(mem, &full, bg);
    DeleteObject(bg);

    item->DrawItem((void*)mem, 0, 0, W, H, false);   // light mode
    // Inside the top row's bar area (x=30 lands in the bar track at any fill level):
    COLORREF px = GetPixel(mem, 30, 6);
    CHECK(px != SENTINEL);                            // DrawItem painted the bar region

    // Right-hand indicator zone (dots + counts) must have painted something too.
    bool indicatorPainted = false;
    for (int yy = 2; yy < H - 2 && !indicatorPainted; ++yy)
        for (int xx = W - 24; xx < W - 1; ++xx)
            if (GetPixel(mem, xx, yy) != SENTINEL) { indicatorPainted = true; break; }
    CHECK(indicatorPainted);                          // window-status dots drawn

    item->DrawItem((void*)mem, 0, 0, W, H, true);     // dark mode must not crash
    COLORREF px2 = GetPixel(mem, 30, 6);
    CHECK(px2 != SENTINEL);

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void bar_render_tests() {
    // FillWidth: clamps, N/A -> 0, integer-floor
    CHECK(FillWidth(0, 100) == 0);
    CHECK(FillWidth(50, 100) == 50);
    CHECK(FillWidth(100, 100) == 100);
    CHECK(FillWidth(67, 60) == 40);     // 60*67/100 = 40
    CHECK(FillWidth(-1, 100) == 0);     // N/A
    CHECK(FillWidth(150, 100) == 100);  // clamp high

    // BarColor thresholds: <50 green, 50..80 yellow, >80 red, <0 gray
    CHECK(BarColor(49, false) == BarColor(0, false));    // green band
    CHECK(BarColor(50, false) == BarColor(80, false));   // yellow band
    CHECK(BarColor(81, false) == BarColor(100, false));  // red band
    CHECK(BarColor(49, false) != BarColor(50, false));   // green != yellow
    CHECK(BarColor(80, false) != BarColor(81, false));   // yellow != red
    CHECK(BarColor(-1, false) != BarColor(0, false));    // N/A gray != green
    CHECK(BarColor(67, true) != BarColor(67, false));    // dark differs from light

    // RowAt: 3 rows in height 30, vgap 0 -> 10px rows, ascending non-overlapping y
    BRect r0 = RowAt(0, 0, 100, 30, 0, 3, 0);
    BRect r1 = RowAt(0, 0, 100, 30, 1, 3, 0);
    BRect r2 = RowAt(0, 0, 100, 30, 2, 3, 0);
    CHECK(r0.h == 10 && r1.h == 10 && r2.h == 10);
    CHECK(r0.y == 0 && r1.y == 10 && r2.y == 20);
    CHECK(r1.y >= r0.y + r0.h);

    // SplitRow: [label | bar | number] partitions the row width exactly
    RowParts p = SplitRow(BRect{0, 0, 120, 10}, 16, 30, 3);
    CHECK(p.label.x == 0 && p.label.w == 16);
    CHECK(p.bar.x == 16 + 3);
    CHECK(p.bar.w == 120 - 16 - 30 - 2 * 3);      // 68
    CHECK(p.number.x == p.bar.x + p.bar.w + 3);
    CHECK(p.number.w == 30);
    CHECK(p.number.x + p.number.w == 120);

    // WindowLabel + number text
    CHECK(std::wstring(WindowLabel(0)) == L"5h");
    CHECK(std::wstring(WindowLabel(1)) == L"7d");
    CHECK(std::wstring(WindowLabel(2)) == L"So");
    CHECK(PctText(67) == L"67%");
    CHECK(PctText(-1) == L"--");
}

static void session_scan_tests() {
    // ExtractJsonString: "status" matches; "statusUpdatedAt" does not; absent -> false.
    std::string busy = "{\"pid\":45492,\"kind\":\"interactive\",\"entrypoint\":\"cli\","
                       "\"status\":\"busy\",\"updatedAt\":1,\"statusUpdatedAt\":2}";
    std::string sdk  = "{\"pid\":32188,\"kind\":\"interactive\",\"entrypoint\":\"sdk-cli\"}";
    std::string s;
    CHECK(ExtractJsonString(busy, "status", s) == true);
    CHECK(s == "busy");
    CHECK(ExtractJsonString(sdk, "status", s) == false);          // headless SDK: no status
    // statusUpdatedAt appearing before status must not derail the match:
    std::string idle = "{\"statusUpdatedAt\":2,\"status\":\"idle\"}";
    CHECK(ExtractJsonString(idle, "status", s) == true && s == "idle");
    // "statusUpdatedAt" alone (no real status key) -> not found
    std::string only = "{\"statusUpdatedAt\":2}";
    CHECK(ExtractJsonString(only, "status", s) == false);

    // ExtractJsonNumber: bare number, quoted number, exact-key, absent.
    long long ll = -1;
    std::string started = "{\"pid\":4128,\"startedAt\":1781338625797,\"status\":\"idle\"}";
    CHECK(ExtractJsonNumber(started, "startedAt", ll) == true && ll == 1781338625797LL);
    CHECK(ExtractJsonNumber(busy, "updatedAt", ll) == true && ll == 1); // not fooled by statusUpdatedAt
    std::string quoted = "{\"procStart\":\"639169642249492710\"}";       // quoted FILETIME-tick number
    CHECK(ExtractJsonNumber(quoted, "procStart", ll) == true && ll == 639169642249492710LL);
    CHECK(ExtractJsonNumber(started, "missing", ll) == false);
    CHECK(ExtractJsonNumber(sdk, "startedAt", ll) == false);            // absent in this sample

    // CountSessions: idle vs working buckets, with dead/status-less skipped.
    std::vector<SessionEntry> v = {
        { true,  true,  "idle" },        // green
        { true,  true,  "busy" },        // red
        { true,  true,  "shell" },       // red
        { true,  false, "" },            // headless SDK (no status) -> skip
        { false, true,  "busy" },        // dead process -> skip
        { true,  true,  "compacting" },  // unknown active state -> red
    };
    WindowCounts c = CountSessions(v);
    CHECK(c.idle == 1);
    CHECK(c.working == 3);
    WindowCounts empty = CountSessions(std::vector<SessionEntry>{});
    CHECK(empty.idle == 0 && empty.working == 0);

    // StartTimeMatches: the PID-reuse guard. Genuine startup jitter passes; a
    // process created minutes/hours later (PID reuse) is rejected; unknown -> open.
    const long long t0 = 1781338625797LL;
    CHECK(StartTimeMatches(t0, t0) == true);                  // exact
    CHECK(StartTimeMatches(t0 - 848, t0) == true);            // ~0.8s real startup jitter
    CHECK(StartTimeMatches(t0 + 119000, t0) == true);         // within 2 min
    CHECK(StartTimeMatches(t0 + 121000, t0) == false);        // beyond 2 min -> reused
    CHECK(StartTimeMatches(t0 + 86400000LL, t0) == false);    // a day later -> reused
    CHECK(StartTimeMatches(999, 0) == true);                  // startedAt unknown -> fail open

    // CountText
    CHECK(CountText(0) == L"0");
    CHECK(CountText(12) == L"12");
    CHECK(CountText(-1) == L"0");

    // FormatWindowStatusLine: contains the Chinese labels and both numbers.
    std::wstring line = FormatWindowStatusLine(c);
    CHECK(line.find(L"工作") != std::wstring::npos);  // working
    CHECK(line.find(L"闲置") != std::wstring::npos);  // idle
    CHECK(line.find(L"3") != std::wstring::npos);     // working count
    CHECK(line.find(L"1") != std::wstring::npos);     // idle count
}

static void session_scan_live_smoke() {
    WindowCounts c = ScanSessionsDefault();
    wprintf(L"[live] sessions dir: %s\n", GetSessionsDir().c_str());
    wprintf(L"[live] windows: working=%d idle=%d\n", c.working, c.idle);
}

static void dll_integration() {
    HMODULE h = LoadLibraryW(L"ClaudeMeter.dll");
    if (!h) { wprintf(L"FAIL: LoadLibrary ClaudeMeter.dll err=%lu\n", GetLastError()); ++g_failures; return; }
    typedef ITMPlugin* (*GetInstanceFn)();
    GetInstanceFn fn = reinterpret_cast<GetInstanceFn>(GetProcAddress(h, "TMPluginGetInstance"));
    if (!fn) { wprintf(L"FAIL: no TMPluginGetInstance export\n"); ++g_failures; FreeLibrary(h); return; }
    ITMPlugin* plugin = fn();
    CHECK(plugin != nullptr);
    CHECK(plugin->GetAPIVersion() == 7);
    IPluginItem* item = plugin->GetItem(0);
    CHECK(item != nullptr);
    CHECK(plugin->GetItem(1) == nullptr);
    plugin->DataRequired();
    CHECK(item->IsCustomDraw() == true);
    CHECK(item->GetItemWidth() > 0);
    draw_item_render_test(item);
    const wchar_t* val = item->GetItemValueText();
    const wchar_t* tip = plugin->GetTooltipInfo();
    CHECK(val != nullptr);
    CHECK(tip != nullptr);
    CHECK(item->GetItemLableText() != nullptr);
    CHECK(item->GetItemValueSampleText() != nullptr);
    wprintf(L"[dll] label='%s' value='%s'\n[dll] tooltip:\n%s\n",
            item->GetItemLableText(), val, tip);
    FreeLibrary(h);
}

int wmain() {
    unit_tests();
    bar_render_tests();
    session_scan_tests();
    session_scan_live_smoke();
    dll_integration();
    if (g_failures == 0) { wprintf(L"ALL TESTS PASSED\n"); return 0; }
    wprintf(L"%d FAILURE(S)\n", g_failures);
    return 1;
}
