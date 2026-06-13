# ClaudeMeter v3 设计：Claude 窗口状态指示器

> 在现有用量控件**右侧**增加两盏指示灯：🟢 闲置窗口数 / 🔴 工作中窗口数。
> 日期：2026-06-13 · 状态：已批准（方案 A），进入实现 · 基于 v2（merge `9ace826`）

## 1. 目标

让任务栏控件在三条用量进度条右边，竖排显示当前打开的 Claude Code 窗口状态：

```
5h ▓▓▓░░ 32%   🟢 2     ← 绿灯 + 闲置窗口数
7d ▓▓░░░ 18%
So ▓▓▓▓░ 71%   🔴 3     ← 红灯 + 工作中窗口数
```

左侧仍是 v2 的三条进度条；右侧新增一个约 25% 宽的指示区，上行绿点+闲置数、下行红点+工作中数。合并为**同一个** `IPluginItem`（用户选择"焊在用量右边"，而非独立任务栏项），`GetItemId` 保持 `claudemeter.usage` 不变以免打乱用户已有的任务栏摆放。

## 2. 关键发现（数据来源）

Claude Code 自身在 `~/.claude/sessions/<pid>.json` 为每个会话维护一份状态文件（文件名即进程 PID）。实测字段：

```json
{"pid":45492,"cwd":"D:\\code\\ClaudeMeter","kind":"interactive",
 "entrypoint":"cli","status":"busy","updatedAt":1781340075504,"statusUpdatedAt":1781340075504}
```

实测要点（5 个真实会话）：
- `status` 取值：`idle`（等输入）/ `busy`（模型生成或跑工具）/ `shell`（跑 `!` 命令）。
- **`kind` 不能区分**：交互窗口与无头 SDK 进程都是 `kind:"interactive"`。真正的判别字段是 **是否存在 `status`**：只有真实 TUI 窗口（`entrypoint:"cli"`）会写 `status`；无头 SDK（旧 `sdk-cli` 残留、claude-mem observer）**没有 `status` 字段**。
- 进程映像名实测为 `claude.exe`（路径 `~/.local/bin/claude.EXE`），非 `node.exe`。

## 3. 关键约束 / 前提

- 沿用 v2：插件为 x86、静态 CRT（`/MT`）、`IsCustomDraw()==true`、自绘 `DrawItem`。
- **采集器 / `usage.ini` / 计划任务 / 安装脚本完全不动**——本特性纯在 DLL 端新增（与 v2 同样只动 `plugin/`）。
- 配置目录解析与采集器一致：`CLAUDE_CONFIG_DIR` 优先，否则 `%USERPROFILE%\.claude`，再拼 `\sessions`。
- 依赖 Claude 内部 `sessions/*.json` 结构（与项目已依赖的 oauth 端点同等风险）；读不到/格式变 → 显示 `0/0`，绝不影响左侧用量条。
- DrawItem 在宿主进程内被频繁调用（~1s）：扫描 + 判活必须轻量、无 GDI/句柄泄漏、无异常逃逸。

## 4. 设计

### 4.1 计数语义（用户已批准）
- 活着 + 有 `status`：`status=="idle"` → 闲置（绿）；其余非空 `status`（`busy`/`shell`/任何未来的活动态）→ 工作中（红）。
- 不活 或 无 `status` → 跳过（自动排除无头 SDK 进程）。
- 计数恒等于"存活的 CLI 窗口总数"，便于排错。
- 注：弹出权限确认（等 yes/no）登记表记为 `idle` → 默认归绿灯（用户接受）。

### 4.2 纯逻辑（新增 `plugin/SessionScan.h`，可单测，不依赖 windows.h）
- `struct SessionEntry { bool alive; bool has_status; std::string status; };`
- `struct WindowCounts { int idle; int working; };`
- `WindowCounts CountSessions(const std::vector<SessionEntry>&)` — 按 4.1 计数。
- `bool ExtractJsonString(const std::string& json, const char* field, std::string& out)` — 提取顶层 JSON 字符串字段；以带引号的键 `"<field>"` 精确匹配（`"status"` 不会误匹配 `"statusUpdatedAt"`）；字段缺失返回 false。
- `std::wstring CountText(int n)` — 渲染用整数文本。
- `std::wstring FormatWindowStatusLine(const WindowCounts&)` — tooltip 行 `\n窗口: 工作 X · 闲置 Y`。

### 4.3 扫描与判活（`SessionScan.h` 内，`#ifdef _WINDOWS_` 守护，依赖 Win32）
- `std::wstring GetSessionsDir()` — 同 4.3 约束解析目录。
- `WindowCounts ScanSessions(const std::wstring& dir)` — `FindFirstFileW(dir\*.json)`；逐文件：先按 `fd.nFileSizeLow/High` **跳过 >64KB 的异常大文件**（防止在宿主线程上触发大读）；PID 取自文件名（`wcstoul`）；读文件（`ReadSmallFileA`，上限 64KB）→ `ExtractJsonNumber(...,"startedAt",...)`；`IsClaudeProcessAlive(pid, startedAt)` 判活；活着再 `ExtractJsonString(...,"status",...)`；汇总成 `SessionEntry` 列表交给 `CountSessions`。
- `bool IsClaudeProcessAlive(unsigned long pid, long long startedAtMs)` — `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` 成功视为存活；`QueryFullProcessImageNameW` 取映像名 basename，**仅当**确信非 `claude.exe`/`node.exe` 时才否决。**关键防 PID 复用**：用 `GetProcessTimes` 取进程创建时间，转成 UTC unix 毫秒（`FileTimeToUnixMs`，固定 1601→1970 偏移，无时区/DST 转换），与会话文件记录的 `startedAt` 比对，差值 >2 分钟即判为"PID 已被别的进程复用"而否决（纯函数 `StartTimeMatches`，可单测）。两条时间戳路径都**失败即放行**（取不到 `startedAt` 或 API 失败时按存活处理），确保任何异常都不会把指示器静默清零。
- `WindowCounts ScanSessionsDefault()` — `ScanSessions(GetSessionsDir())`；调用方（`DataRequired`）用 `try/catch(...)` 包裹，任何异常都不逃逸到宿主。

### 4.4 颜色（`plugin/BarRender.h` 新增）
- `unsigned long IdleDotColor(bool dark)` — 绿，复用 BarColor 的 `<50` 绿（亮 `Rgb(40,170,70)` / 暗 `Rgb(60,200,90)`）。
- `unsigned long WorkingDotColor(bool dark)` — 红，复用 BarColor 的 `>80` 红（亮 `Rgb(210,60,50)` / 暗 `Rgb(235,95,85)`）。

### 4.5 渲染（`ClaudeMeterPlugin.cpp`）
- `GetItemWidth()` → `165`（v2 为 120）。
- `DrawItem`：先按比例切出右侧指示区 `indW = w*42/165`（夹在 `[30, w/2]`），左侧 `usageW=w-indW` 复用 v2 三条进度条逻辑（仅把 `w` 换成 `usageW`）。右侧 `[indX,y,indW,h]` 两行（`RowAt(...,2,vgap)`）：每行左侧 GDI `Ellipse` 实心圆（直径 `min(rowH-2,12)`，同色描边避免黑边），右侧 `DrawTextW` 计数（复用同一字体）。
- `DataRequired()`：在读 `usage.ini` 之后追加 `m_item.SetCounts(ScanSessionsDefault())`。
- `GetTooltipInfo()`：在 v2 tooltip 末尾追加 `FormatWindowStatusLine`。
- GDI 资源全部 `DeleteObject`/复原，无泄漏。

### 4.6 边界与健壮性
- 目录不存在 / 无文件 / 读失败 → `{0,0}`，照常画两盏灯显示 `0`。
- `status` 字段缺失 → 该会话不计入任一桶。
- `w`/`h` 极小 → `indW` 夹取、直径下限，矩形非负，不越界。
- 异常大 / 损坏 / 投放的 `<pid>.json` → 64KB 大小上限（`FindFirstFileW` 的 `nFileSize` 预检 + `ReadSmallFileA` 内部截断）杜绝大读；`DataRequired` 的 `try/catch(...)` 兜底 `bad_alloc` 等异常，绝不崩宿主。
- PID 复用（关闭窗口残留的 `<pid>.json` + PID 被复用）→ `startedAt` 创建时间比对否决（见 4.3）。

> 4.3/4.6 的 size-cap、`try/catch`、`startedAt` 创建时间比对三项，是本特性经一轮对抗式多智能体代码审查（4 维度并行 + 独立怀疑者复核）确认后加固的：审查证实"无界读 + 无异常护栏"会让损坏/投放文件崩掉宿主，且 `node.exe` 白名单 + 残留会话文件会在 PID 复用时虚报窗口数。

## 5. 改动范围（仅 `plugin/`）
- 新增 `plugin/SessionScan.h`（纯计数 + 扫描判活）。
- 改 `plugin/BarRender.h`：加 `IdleDotColor` / `WorkingDotColor`。
- 改 `plugin/ClaudeMeterPlugin.cpp`：`GetItemWidth→165`、`DrawItem` 加右侧指示区、`DataRequired` 加扫描、`BuildTooltip` 加状态行、版本→`3.0.0`、名称/描述补"窗口状态"。
- 改 `plugin/test_host.cpp`：加 `CountSessions`/`ExtractJsonString`/`CountText`/`FormatWindowStatusLine` 单测 + 真机 `ScanSessionsDefault` 冒烟打印；集成测改用 `GetItemWidth()` 尺寸并断言右侧指示区已绘制。
- `collector/` `install/` `usage.ini` **不动**。

## 6. 测试
- **纯单测**：`ExtractJsonString`（`status` 命中、`statusUpdatedAt` 不误匹配、缺失返回 false）、`CountSessions`（idle/busy/shell/未知态/无状态/已死 各分支）、`CountText`、`FormatWindowStatusLine`（含中文与数字）。
- **集成测**：`GetItemWidth()==165`；DrawItem 渲染到内存 DC 后右侧指示区出现非哨兵像素（灯/数字已画）；明暗两种模式不崩。
- **真机冒烟**：`ScanSessionsDefault()` 打印 working/idle（非断言，便于人工核对，本机应为 工作 2 · 闲置 1）。
- 构建：`build.bat` 出 x86 /MT DLL；`build_test.bat` 跑全部断言。

## 7. 非目标（YAGNI）
- 不做独立任务栏项（用户选了焊在右边）。
- 不做"等待权限确认"黄灯第三态（登记表记为 idle，本期归绿）。
- 不引入常驻 Python 守护进程（方案 B 被否；方案 A 由 DLL 实时扫描）。
- 不做基于 `procStart`（本地时区 .NET ticks）的精确比对——改用 `startedAt`（UTC unix 毫秒）做创建时间比对，天然规避时区/DST。
- 不做"等关闭就立刻清理残留会话文件"（那是 Claude Code 的职责）；本端用 `startedAt` 比对让残留文件不影响计数即可。
