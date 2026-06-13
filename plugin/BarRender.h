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

// Window-status indicator dots: green = idle, red = working. Same hues as the
// BarColor green (<50) and red (>80) bands, for visual consistency.
inline unsigned long IdleDotColor(bool dark)    { return dark ? Rgb(60, 200, 90) : Rgb(40, 170, 70); }
inline unsigned long WorkingDotColor(bool dark) { return dark ? Rgb(235, 95, 85) : Rgb(210, 60, 50); }

inline const wchar_t* WindowLabel(int i) {
    switch (i) {
    case 0: return L"5h";
    case 1: return L"7d";
    case 2: return L"So";
    default: return L"";
    }
}
