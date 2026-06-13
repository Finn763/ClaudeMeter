# ClaudeMeter v2 设计：任务栏三进度条

> 把状态栏显示从纯文本 `CC 8%` 改为自绘三条横向进度条（5小时 / 周·全部 / Sonnet），每条含已用百分比数值。
> 日期：2026-06-13 · 状态：已批准，待出实施计划 · 基于 v1（merge `0017bdb`）

## 1. 目标

任务栏显示三条上中下堆叠的横向进度条，更直观地呈现 Claude Code 三个用量窗口的已用比例：
- 上：5 小时窗口
- 中：周·全部模型
- 下：Sonnet

每条 = 小标签（`5h`/`7d`/`So`）+ 进度条（灰底 + 已用填充）+ 已用百分比（如 `67%`）。阈值变色。鼠标悬停 tooltip 保持 v1 的完整明细。

```
5h ▏███████░░░▏ 67%
7d ▏█░░░░░░░░░▏ 10%
So ▏░░░░░░░░░░▏  0%
```

## 2. 关键约束 / 前提

- Anthropic 接口只给 utilization 百分比（0–100），**无绝对 token 数**；数值即"已用百分比"。
- 数据层（采集器、`usage.ini`、计划任务、安装脚本）**完全不动**——v2 只改插件 DLL 的渲染。`usage.ini` 已含 `five_hour_pct` / `seven_day_pct` / `sonnet_pct`（-1 = N/A）。
- TrafficMonitor 为 x86；DLL 仍编译 x86、静态 CRT（/MT）。
- 任务栏高度有限（~30–40px @100%）；三行平分，字体取小号。
- 自绘要求：`IsCustomDraw()` 返回 true，并实现 `DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)`；`GetItemWidth()` 返回 96-DPI 逻辑宽度（宿主按 DPI 缩放）。自绘为 true 时，`GetItemLableText/GetItemValueText/GetItemValueSampleText` 被宿主忽略（但仍须非 null）。

## 3. 设计

### 3.1 渲染流程
- `IsCustomDraw()` → `true`。
- `GetItemWidth()` → 固定逻辑宽度 `120`（约容纳 标签16 + 条62 + 数值34 + 间距）。
- `DrawItem(hDC,x,y,w,h,dark_mode)`：把高度 `h` 三等分成三行（行间留 1–2px 间距）；每行内部水平切分为 [标签区][进度条区][数值区]；用 GDI 绘制。读取已缓存的 `m_data`（`DataRequired` 已填）。
- `IsDrawResourceUsageGraph()` → `0`（关闭背景资源图，避免与自绘叠加；v1 曾返回 1）。
- `DataRequired()` 不变：读 `usage.ini` → `m_data`。`GetTooltipInfo()` 不变（v1 的完整明细）。

### 3.2 纯逻辑（抽到 `plugin/BarRender.h`，可单测，不依赖 windows.h）
颜色用 `unsigned long`（COLORREF = 0x00BBGGRR），自带 `inline unsigned long Rgb(int r,int g,int b)`。
- `struct Rect { int x, y, w, h; };`
- `Rect RowAt(int x,int y,int w,int h,int index,int count,int vgap)` — 第 index 行（0..count-1）的矩形，行间 vgap。
- `struct RowParts { Rect label, bar, number; };`
- `RowParts SplitRow(const Rect& row,int labelW,int numberW,int hgap)` — 行内切三块。
- `int FillWidth(int pct,int barWidth)` — pct<0→0；否则 clamp[0,100] 后 `barWidth*pct/100`。
- `unsigned long BarColor(int pct,bool dark)` — 阈值：`pct<50`→绿；`50<=pct<=80`→黄；`pct>80`→红；`pct<0`（N/A）→中性灰。明/暗给不同明度但同色相。
- `unsigned long TrackColor(bool dark)` / `unsigned long TextColor(bool dark)` — 底槽灰 / 文本色。
- `const wchar_t* WindowLabel(int i)` — `0→L"5h"`,`1→L"7d"`,`2→L"So"`。
- `std::wstring BarNumber(int pct)` — `pct<0→L"--"`，否则 `L"67%"`（复用 `PctText`）。

### 3.3 三个窗口取值（从 `m_data`）
索引 0/1/2 → `(five_hour_pct, seven_day_pct, sonnet_pct)`。`ok==false` 时三条都按 `pct=-1` 处理（空灰条 + `--`）。

### 3.4 GDI 细节（`DrawItem` 内）
- `HDC dc=(HDC)hDC;` `SetBkMode(dc,TRANSPARENT);`
- 小字体：`CreateFontW`，高度按行高（约 `min(rowH, 14)`），抗锯齿默认。
- 进度条：底槽 `FillRect(track)`；已用 `FillRect(fill=BarColor)`，宽度 `FillWidth`。可用 1px 描边（可选）。
- 文本：标签左对齐于 label 区；数值右对齐于 number 区；`SetTextColor(TextColor)`。
- 资源管理：所有 `CreateFont/CreateSolidBrush` 用完 `DeleteObject`，`SelectObject` 复原；不泄漏 GDI 句柄（每次 DrawItem 调用频繁）。

### 3.5 边界与健壮性
- `pct=-1` 或 `ok=0` → 空灰条 + `--`，不崩。
- `w`/`h` 极小（任务栏很矮）时仍不越界（FillWidth clamp；矩形非负）。
- DrawItem 在宿主进程内被频繁调用：必须无泄漏、无异常逃逸、句柄全部释放。

## 4. 改动范围（仅 `plugin/`）
- 新增 `plugin/BarRender.h`（纯逻辑）。
- 改 `plugin/ClaudeMeterPlugin.cpp`：`IsCustomDraw→true`、实现 `DrawItem`、`GetItemWidth→120`、`IsDrawResourceUsageGraph→0`；移除 v1 的 `m_value`/`m_graph` 与 `Update` 的文本/图值逻辑（`DataRequired` 仅存 `m_data`）；文本 getter 返回无害非 null 值；tooltip 不变。
- 改 `plugin/test_host.cpp`：加 BarRender 单测 + DrawItem 渲染到内存 DC 的冒烟/像素测试。
- `plugin/UsageData.h`：保留 `PctText`/`FormatRemaining`/`FormatTooltip`；若 `FormatValue` 变为未使用则删除。
- `collector/` `install/` `README` `usage.ini` **不动**（README 末尾可补一句 v2 显示说明，可选）。

## 5. 测试
- **BarRender 纯单测**（test_host）：`FillWidth`（0/50/100/-1/越界）、`BarColor`（阈值边界 49/50/80/81/-1，明暗）、`RowAt`/`SplitRow`（三行不重叠、和 ≈ 高/宽）、`BarNumber`（67→"67%"，-1→"--"）、`WindowLabel`。
- **DrawItem 集成测**（test_host）：建一个 120×40 的 DIB 内存 DC，背景填成哨兵色（品红）；用样例 `m_data`（67/10/0）调 `DrawItem`；断言：不崩；5h 条填充区内的像素**不再是品红**（说明画上了）且接近预期阈值色族；Sonnet（0%）填充区仍为底槽色（未被填充）。
- **DLL ABI 测**（沿用）：`IsCustomDraw()==true`、`GetItemWidth()>0`、`DataRequired`+`GetTooltipInfo` 正常。
- 构建：`plugin/build.bat` 出 x86 /MT DLL（验证 `machine=0x14C` 与导出名）。

## 6. 部署
重新编译 DLL → 拷到 `D:\Downloads\TrafficMonitor\plugins\` → 用户重启 TrafficMonitor 生效。采集器/计划任务/安装脚本不变。

## 7. 非目标（YAGNI）
- 不做绝对 token 数（接口不提供）。
- 不做每条独立配色/标签开关（阈值变色 + 固定标签已定）。
- 不保留旧纯文本档（v2 直接替换；如需回退用 v1 的 DLL）。
