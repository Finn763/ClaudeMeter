# ClaudeMeter v2 (Taskbar Progress Bars) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace ClaudeMeter's plain `CC 8%` text with three custom-drawn taskbar progress bars (5h / weekly / Sonnet), each showing a threshold-colored used-fill bar plus its used percentage.

**Architecture:** Pure layout/color math goes in a new header-only `plugin/BarRender.h` (unit-testable, no `<windows.h>`). The plugin enables custom draw (`IsCustomDraw()→true`) and implements `DrawItem()` using GDI driven by `BarRender.h`. Data layer (collector, `usage.ini`, install) is unchanged — the DLL still reads the same INI.

**Tech Stack:** C++17, MSVC v143 (x86, static CRT `/MT`), Win32 GDI. Tests via the existing `plugin/test_host.cpp` console harness.

**Verified facts:**
- v1 is merged to `master` (`0017bdb`). This work is on branch `feature/claudemeter-v2-bars`.
- `PluginInterface.h` custom-draw API: `virtual bool IsCustomDraw() const { return false; }`, `virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) {}`, `virtual int GetItemWidth() const { return 0; }` (96-DPI logical width, host scales). When `IsCustomDraw()==true`, the text getters are ignored (but must stay non-null).
- Existing files: `plugin/UsageData.h` (has `PctText`, `FormatRemaining`, `FormatTooltip`, and an now-obsolete `FormatValue`), `plugin/UsageReader.h` (`ReadUsageFile`/`GetCacheIniPath`), `plugin/ClaudeMeterPlugin.cpp` (v1), `plugin/test_host.cpp` (has `g_failures`, `CHECK`, `unit_tests()`, `dll_integration()`, `wmain`).
- `usage.ini` keys present: `ok`, `five_hour_pct`, `seven_day_pct`, `sonnet_pct` (`-1` = N/A).

**Build/run convention (Windows, git-bash shell):**
Batch files must run in a real `cmd`/PowerShell context (MSYS mangles `cmd /c` flags). Use the PowerShell wrapper, which is verified to work from this shell:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build_test.bat'"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build.bat'"
```
Commit after each task. Use `git -c user.name="ClaudeMeter" -c user.email="xiehongfa1@gmail.com" commit ...` if identity is unset (repo-local identity is already configured).

---

## File Structure

```
plugin/
  BarRender.h            # NEW — pure layout/color math (no <windows.h>), unit-testable
  ClaudeMeterPlugin.cpp  # MODIFY — IsCustomDraw→true, DrawItem(), GetItemWidth→120, drop text/graph paths
  UsageData.h            # MODIFY — remove now-unused FormatValue (keep PctText/FormatRemaining/FormatTooltip)
  test_host.cpp          # MODIFY — add bar_render_tests(); add DrawItem render test + custom-draw ABI asserts
  ClaudeMeter.def        # unchanged
  build.bat/build_test.bat  # unchanged
README.md                # MODIFY (Task 3) — describe v2 bars; drop the obsolete config `primary` note
collector/  install/     # UNCHANGED
```

---

## Task 1: `BarRender.h` pure layout/color logic + unit tests

**Files:**
- Create: `plugin/BarRender.h`
- Modify: `plugin/test_host.cpp` (add `#include "BarRender.h"`, add `bar_render_tests()`, call it in `wmain`)

- [ ] **Step 1: Write the failing unit tests**

In `plugin/test_host.cpp`, add `#include "BarRender.h"` after the existing `#include "UsageReader.h"` line, add the function below before `wmain`, and add a `bar_render_tests();` call inside `wmain` (before `dll_integration();`).

Add this function:
```cpp
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
```

- [ ] **Step 2: Run build_test to verify it FAILS to compile**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build_test.bat'"
```
Expected: compile error — `cannot open include file 'BarRender.h'` (or unresolved `FillWidth`/`BarColor`/`RowAt`/`SplitRow`/`WindowLabel`/`BRect`/`RowParts`). This is the red step.

- [ ] **Step 3: Create `plugin/BarRender.h`**

```cpp
#pragma once
#include <string>
#include "UsageData.h"  // PctText

// COLORREF layout is 0x00BBGGRR. Keep this header free of <windows.h> so the math
// is unit-testable from the console host without GDI.
inline unsigned long Rgb(int r, int g, int b) {
    return (unsigned long)((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16));
}

struct BRect { int x, y, w, h; };
struct RowParts { BRect label, bar, number; };

// Row `index` (0..count-1) of `count` rows stacked vertically in [x,y,w,h], vgap px apart.
inline BRect RowAt(int x, int y, int w, int h, int index, int count, int vgap) {
    if (count < 1) count = 1;
    int rowH = (h - vgap * (count - 1)) / count;
    if (rowH < 1) rowH = 1;
    BRect r;
    r.x = x;
    r.y = y + index * (rowH + vgap);
    r.w = w;
    r.h = rowH;
    return r;
}

// Split a row horizontally into [label | bar | number] with hgap between blocks.
inline RowParts SplitRow(const BRect& row, int labelW, int numberW, int hgap) {
    RowParts p;
    p.label = BRect{ row.x, row.y, labelW, row.h };
    int barX = row.x + labelW + hgap;
    int barW = row.w - labelW - numberW - 2 * hgap;
    if (barW < 1) barW = 1;
    p.bar = BRect{ barX, row.y, barW, row.h };
    p.number = BRect{ barX + barW + hgap, row.y, numberW, row.h };
    return p;
}

// Used-fill width in px. pct<0 (N/A) -> 0; clamps pct to [0,100].
inline int FillWidth(int pct, int barWidth) {
    if (barWidth < 0) barWidth = 0;
    if (pct < 0) return 0;
    if (pct > 100) pct = 100;
    return barWidth * pct / 100;
}

// Threshold color: <50 green, 50..80 yellow, >80 red, <0 (N/A) neutral gray.
inline unsigned long BarColor(int pct, bool dark) {
    if (pct < 0)  return dark ? Rgb(90, 90, 90)   : Rgb(170, 170, 170);
    if (pct < 50) return dark ? Rgb(60, 200, 90)  : Rgb(40, 170, 70);
    if (pct <= 80) return dark ? Rgb(235, 195, 60) : Rgb(205, 160, 30);
    return dark ? Rgb(235, 95, 85) : Rgb(210, 60, 50);
}

inline unsigned long TrackColor(bool dark) { return dark ? Rgb(70, 70, 70) : Rgb(214, 214, 214); }
inline unsigned long TextColor(bool dark)  { return dark ? Rgb(235, 235, 235) : Rgb(30, 30, 30); }

inline const wchar_t* WindowLabel(int i) {
    switch (i) {
    case 0: return L"5h";
    case 1: return L"7d";
    case 2: return L"So";
    default: return L"";
    }
}
```

- [ ] **Step 4: Run build_test to verify it PASSES**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build_test.bat'"
```
Expected: compiles; `ALL TESTS PASSED` (the existing `unit_tests()` and `dll_integration()` against the current v1 DLL still pass, plus the new bar tests). The `vswhere`-not-found line in stderr is a harmless artifact of the PowerShell wrapper; the build still completes.

- [ ] **Step 5: Commit**

```bash
git add plugin/BarRender.h plugin/test_host.cpp
git commit -m "feat(plugin): BarRender pure layout/color logic + unit tests"
```

---

## Task 2: Custom-draw three bars in the plugin + DrawItem render test

**Files:**
- Modify: `plugin/ClaudeMeterPlugin.cpp` (full rewrite below)
- Modify: `plugin/UsageData.h` (remove unused `FormatValue`)
- Modify: `plugin/test_host.cpp` (remove `FormatValue` asserts; add DrawItem render test + custom-draw ABI asserts)

- [ ] **Step 1: Write the failing DrawItem test + ABI asserts**

In `plugin/test_host.cpp`:

(a) In `unit_tests()`, find the `FormatValue` block (5 lines):
```cpp
    UsageData d; d.ok = true; d.five_hour_pct = 67; d.seven_day_pct = 10; d.sonnet_pct = 0;
    CHECK(FormatValue(d, L"five_hour") == L"67%");
    CHECK(FormatValue(d, L"seven_day") == L"10%");
    UsageData bad; bad.ok = false;
    CHECK(FormatValue(bad, L"five_hour") == L"--");
```
and REPLACE all 5 lines with just:
```cpp
    UsageData bad; bad.ok = false;
```
(`bad` is still used by the `FormatTooltip(bad, ...)` check later in `unit_tests()`; `d` is no longer referenced, so it must be removed to avoid an unused-variable warning. Keep everything else in `unit_tests()`.)

(b) Add this render-test helper before `dll_integration`:
```cpp
static void draw_item_render_test(IPluginItem* item) {
    const int W = 120, H = 40;
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

    item->DrawItem((void*)mem, 0, 0, W, H, true);     // dark mode must not crash
    COLORREF px2 = GetPixel(mem, 30, 6);
    CHECK(px2 != SENTINEL);

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}
```

(c) Inside `dll_integration()`, AFTER the existing `plugin->DataRequired();` line, add:
```cpp
    CHECK(item->IsCustomDraw() == true);
    CHECK(item->GetItemWidth() > 0);
    draw_item_render_test(item);
```

- [ ] **Step 2: Run build_test to verify the new asserts FAIL against the current v1 DLL**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build_test.bat'"
```
Expected: compiles, but FAILS at runtime — `item->IsCustomDraw()` is `false` and the base no-op `DrawItem` leaves the pixel as SENTINEL, so you see `FAIL ... item->IsCustomDraw() == true` and `FAIL ... px != SENTINEL`, ending `N FAILURE(S)`. This is the red step (the v1 DLL has no custom draw yet).

- [ ] **Step 3: Remove `FormatValue` from `plugin/UsageData.h`**

Delete this entire function from `plugin/UsageData.h`:
```cpp
// primary: L"five_hour" or L"seven_day"
inline std::wstring FormatValue(const UsageData& d, const std::wstring& primary) {
    if (!d.ok) return L"--";
    int pct = (primary == L"seven_day") ? d.seven_day_pct : d.five_hour_pct;
    return PctText(pct);
}
```
(Keep `UsageData`, `FormatRemaining`, `PctText`, and `FormatTooltip`.)

- [ ] **Step 4: Replace `plugin/ClaudeMeterPlugin.cpp` with the v2 implementation**

Full new content:
```cpp
#include <windows.h>
#include <ctime>
#include <string>
#include "PluginInterface.h"
#include "UsageData.h"
#include "UsageReader.h"
#include "BarRender.h"

// Threading: TrafficMonitor calls DataRequired(), DrawItem(), and the Get*() methods
// serially on its single update thread, so the cached members need no locking.

class CClaudeItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"Claude Code 用量"; } // 用量
    const wchar_t* GetItemId() const override { return L"claudemeter.usage"; }
    // Ignored while IsCustomDraw()==true, but the contract still forbids null.
    const wchar_t* GetItemLableText() const override { return L"CC"; }
    const wchar_t* GetItemValueText() const override { return L""; }
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }

    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return 120; } // 96-DPI logical; host scales by DPI

    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override {
        HDC dc = static_cast<HDC>(hDC);
        const int vgap = (h >= 18) ? 2 : 1;
        const int hgap = 3;
        const int labelW = 16;
        const int numberW = 30;

        int oldBk = SetBkMode(dc, TRANSPARENT);
        COLORREF oldText = GetTextColor(dc);

        BRect probe = RowAt(x, y, w, h, 0, 3, vgap);
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
            BRect row = RowAt(x, y, w, h, i, 3, vgap);
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
        SelectObject(dc, oldFont);
        DeleteObject(font);
        SetTextColor(dc, oldText);
        SetBkMode(dc, oldBk);
    }

    void SetData(const UsageData& d) { m_data = d; }
    std::wstring BuildTooltip() const {
        return FormatTooltip(m_data, static_cast<long long>(time(nullptr)));
    }

private:
    UsageData m_data;
};

class CClaudeMeterPlugin : public ITMPlugin {
public:
    static CClaudeMeterPlugin& Instance() { static CClaudeMeterPlugin i; return i; }

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }

    void DataRequired() override { m_item.SetData(ReadUsageFile(GetCacheIniPath())); }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"ClaudeMeter";
        case TMI_DESCRIPTION: return L"在状态栏显示 Claude Code 剩余用量"; // ...剩余用量
        case TMI_AUTHOR:      return L"ClaudeMeter";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"2.0.0";
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
```

Note: this removes v1's `IsDrawResourceUsageGraph()`/`GetResourceUsageGraphValue()` overrides (defaults now apply: graph off) and the old `LoadConfig`/`m_value`/`m_graph` paths.

- [ ] **Step 5: Rebuild the DLL**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build.bat'"
```
Expected: `[OK] built ClaudeMeter.dll` and the dumpbin line lists `TMPluginGetInstance`.

- [ ] **Step 6: Confirm the DLL is still x86 + static-CRT**

Run (from repo root `D:\code\ClaudeMeter`):
```bash
python -c "import struct;d=open('plugin/ClaudeMeter.dll','rb').read(0x200);pe=struct.unpack_from('<I',d,0x3C)[0];print('machine=0x%X'%struct.unpack_from('<H',d,pe+4)[0])"
```
Expected: `machine=0x14C`.

- [ ] **Step 7: Run build_test to verify all tests PASS against the v2 DLL**

Run:
```
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location 'D:\code\ClaudeMeter\plugin'; & '.\build_test.bat'"
```
Expected: `ALL TESTS PASSED` (bar logic + DLL ABI with `IsCustomDraw()==true`, `GetItemWidth()>0`, and the DrawItem render test now paints over the sentinel).

- [ ] **Step 8: Commit**

```bash
git add plugin/ClaudeMeterPlugin.cpp plugin/UsageData.h plugin/test_host.cpp
git commit -m "feat(plugin): custom-draw three taskbar progress bars (5h/weekly/sonnet)"
```

---

## Task 3: Deploy DLL + README update + final verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Deploy the freshly-built DLL into TrafficMonitor**

Run (from repo root):
```bash
cp -f plugin/ClaudeMeter.dll "/d/Downloads/TrafficMonitor/plugins/ClaudeMeter.dll"
sha256sum plugin/ClaudeMeter.dll "/d/Downloads/TrafficMonitor/plugins/ClaudeMeter.dll"
```
Expected: both SHA-256 hashes identical (the deployed DLL matches the build).

- [ ] **Step 2: Update `README.md` for v2**

In `README.md`, replace the opening description line:
```markdown
Claude Code remaining usage in the Windows status bar — `CC 67%` at a glance, full
breakdown (5-hour / weekly / per-model) on hover.
```
with:
```markdown
Claude Code remaining usage in the Windows status bar — three threshold-colored
progress bars (5-hour / weekly / Sonnet), each with its used percentage, plus a full
breakdown (incl. Opus + reset times) on hover.
```

Then replace the entire `## Configuration` section:
```markdown
## Configuration

`%LOCALAPPDATA%\ClaudeMeter\config.ini`:
```ini
[display]
primary=five_hour   ; or seven_day
```
```
with:
```markdown
## Display

The taskbar item shows three stacked progress bars — 5-hour, weekly (all models), and
Sonnet — each filled to its used percentage and colored by load (green < 50%, yellow
50–80%, red > 80%). Hover for the full breakdown (incl. Opus and reset times). No
configuration file is required.
```

- [ ] **Step 3: Final verification**

Run (from repo root):
```bash
python -c "import struct;d=open(r'D:\Downloads\TrafficMonitor\plugins\ClaudeMeter.dll','rb').read(0x200);pe=struct.unpack_from('<I',d,0x3C)[0];print('deployed machine=0x%X'%struct.unpack_from('<H',d,pe+4)[0])"
python -m unittest discover -s collector -p "test_*.py" 2>&1 | tail -3
```
Expected: `deployed machine=0x14C`; Python suite still `OK` (16 tests — collector untouched, must still pass).

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: README for v2 taskbar progress bars"
```

- [ ] **Step 5: Manual step for the user (report, do not automate)**

The plugin DLL is loaded only at TrafficMonitor startup. Report to the user:
> Restart TrafficMonitor. The "Claude Code 用量" item now shows three progress bars
> (5h / 7d / So) with percentages, colored by load. Hover for the full breakdown.
> If the item looks too narrow/wide, that's the fixed 120-px logical width — tell me to adjust.

---

## Self-Review notes (author)

- **Spec coverage:** three bars 5h/weekly/Sonnet + label + used% → Task 2 DrawItem + Task 1 BarRender; threshold colors → `BarColor` (Task 1); custom draw enablement + width → Task 2 (`IsCustomDraw`/`GetItemWidth`); N/A & ok=0 → `pcts[]=-1` path in DrawItem + `FillWidth(-1)`/`PctText(-1)`; tooltip unchanged → kept `BuildTooltip`/`FormatTooltip`; data layer untouched → only `plugin/` + README modified; tests (pure + render + ABI) → Tasks 1–2; x86/`/MT` preserved → unchanged build.bat, verified Task 2 Step 6; deploy → Task 3.
- **Removed (intended):** v1 `FormatValue`, `IsDrawResourceUsageGraph`/`GetResourceUsageGraphValue`, `LoadConfig`/`primary` config, `m_value`/`m_graph` — all obsolete under custom draw; README config section replaced.
- **Placeholder scan:** none — all code blocks complete; build/run commands explicit.
- **Type/name consistency:** `BRect`/`RowParts`/`RowAt`/`SplitRow`/`FillWidth`/`BarColor`/`TrackColor`/`TextColor`/`WindowLabel` identical between `BarRender.h`, the Task 1 tests, and the Task 2 `DrawItem`. `PctText` reused for the number text (no separate `BarNumber`). `SetData`/`BuildTooltip`/`m_data` consistent across the plugin and the render test path (test drives `DrawItem` via the DLL's `IPluginItem`, so no cross-TU access to `CClaudeItem` is needed).
```
