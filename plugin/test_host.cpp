// Console test for ClaudeMeter pure logic (and, in Task 6, the built DLL).
#include "UsageData.h"
#include "UsageReader.h"
#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)){ wprintf(L"FAIL %hs:%d  %hs\n", __FILE__, __LINE__, #cond); ++g_failures; } } while(0)

static void unit_tests() {
    // FormatRemaining
    CHECK(FormatRemaining(0, 100) == L"--");
    CHECK(FormatRemaining(100, 100) == L"now");
    CHECK(FormatRemaining(100 + 90, 100) == L"1m");
    CHECK(FormatRemaining(100 + 3600 + 120, 100) == L"1h 2m");
    CHECK(FormatRemaining(100 + 86400LL * 6 + 3600 * 3, 100) == L"6d 3h");

    // FormatValue
    UsageData d; d.ok = true; d.five_hour_pct = 67; d.seven_day_pct = 10; d.sonnet_pct = 0;
    CHECK(FormatValue(d, L"five_hour") == L"67%");
    CHECK(FormatValue(d, L"seven_day") == L"10%");
    UsageData bad; bad.ok = false;
    CHECK(FormatValue(bad, L"five_hour") == L"--");

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

int wmain() {
    unit_tests();
    if (g_failures == 0) { wprintf(L"ALL UNIT TESTS PASSED\n"); return 0; }
    wprintf(L"%d FAILURE(S)\n", g_failures);
    return 1;
}
