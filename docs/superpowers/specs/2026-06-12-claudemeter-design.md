# ClaudeMeter 设计文档

> TrafficMonitor 插件：在 Windows 状态栏/任务栏显示 Claude Code 剩余用量（5 小时窗口、周窗口、分模型）。
> 日期：2026-06-12 · 状态：已批准，待出实施计划

## 1. 目标与范围

**目标**：开发一个 TrafficMonitor 插件，在状态栏显示 Claude Code 的剩余用量（复刻 `/usage` 面板：5 小时限额、周·全部模型、Sonnet only），鼠标悬停显示完整明细。

**非目标**：
- 不做用量历史图表/趋势分析（仅显示当前快照）。
- 不自己刷新 OAuth 令牌（依赖 Claude Code 维护 `.credentials.json`）。
- v1 不做阈值变色（纯文本档，颜色跟随 TrafficMonitor 主题；预留自绘接口）。

## 2. 关键约束（已实地确认）

| 约束 | 事实 | 来源 |
|---|---|---|
| 宿主架构 | TrafficMonitor.exe 是 **x86 (32 位)**（PE machine=0x14C）→ 插件必须编译 x86 DLL，架构不匹配静默失败 | `D:\Downloads\TrafficMonitor\TrafficMonitor.exe` |
| 插件目录 | `D:\Downloads\TrafficMonitor\plugins\`（已存在，空） | 实地 |
| 编译器 | **本机无任何 C++ 编译器**（无 MSVC/MinGW）；只有 VS Code（编辑器，不能编译） | 实地搜索 |
| 包管理器 | `winget` 可用 → 装 VS Build Tools 2022 | 实地 |
| 运行时 | Python 3.11.15 可用；`git` 可用 | 实地 |
| 令牌存储 | 明文文件 `C:\Users\FinnX\.claude\.credentials.json`，`claudeAiOauth.{accessToken,refreshToken,expiresAt,scopes,subscriptionType,rateLimitTier}`；`subscriptionType=max` | 实地（不记录令牌串） |
| Claude Code 版本 | 2.1.174（≥ v2.1.80，支持 statusLine `rate_limits`） | `claude --version` |

## 3. 数据源（混合策略，已实地验证）

### 3.1 主力：未公开 oauth-usage 接口 ✅ 已用本机令牌实测 HTTP 200

```
GET https://api.anthropic.com/api/oauth/usage
Headers:
  Authorization: Bearer <accessToken>          # 取自 .credentials.json
  anthropic-beta: oauth-2025-04-20
  User-Agent: claude-code/<version>            # 必须，否则 429
  Content-Type: application/json
```

**实测返回结构**（数值为真实用量快照）：

```jsonc
{
  "five_hour":        { "utilization": 3.0,  "resets_at": "2026-06-12T15:50:00.355853+00:00" },
  "seven_day":        { "utilization": 13.0, "resets_at": "2026-06-18T23:00:00.355874+00:00" },
  "seven_day_sonnet": { "utilization": 0.0,  "resets_at": "2026-06-18T23:00:00.355885+00:00" },
  "seven_day_opus":   null,    // 用了 Opus 才出现
  "seven_day_oauth_apps": null, "seven_day_cowork": null, /* 其余实验性桶均 null */
  "extra_usage": { "is_enabled": false, "monthly_limit": null, "used_credits": null,
                   "utilization": null, "currency": null, "disabled_reason": null }
}
```

**重要纠正**：字段名是 `utilization`（0–100 数字，**不是** `used_percentage`）；`resets_at` 是 **ISO 8601 带时区字符串**（**不是** epoch 秒）。

优点：唯一能给出分模型（Sonnet/Opus）数据，且**无 CC 会话也能拉**。
代价：接口未公开、令牌限定 CC/claude.ai（灰色 ToS 区，破坏风险低但非零）。

### 3.2 兜底：官方 statusLine（合规）

Claude Code v2.1.80+ 的 statusLine 脚本从 stdin JSON 拿到 `rate_limits` 对象：
- `rate_limits.five_hour.used_percentage`（0–100）、`rate_limits.five_hour.resets_at`（**epoch 秒**）
- `rate_limits.seven_day.used_percentage`、`rate_limits.seven_day.resets_at`

**注意字段名/格式与 oauth 接口不同，采集器必须归一化。** 仅在 CC 会话运行、且首次 API 响应后才有；无分模型数据。作为接口失效时的合规兜底。

### 3.3 兜底之兜底：JSONL 估算（不实现，仅记录）

`~/.claude/projects/**/*.jsonl` 的 `message.usage.*` 只能估算 token，无法还原官方百分比。本设计**不实现**，仅作认知。

## 4. 架构

```
   [主力] oauth 接口 ──▶┌──────────────────────────────┐
   每 ~3 分钟轮询        │ cc_usage_collector.py        │──写──▶ %LOCALAPPDATA%\ClaudeMeter\usage.ini
   [兜底] statusLine ──▶│ statusline_hook.py (会话时)   │              ▲ 读 (本地, <1KB, 极快)
   (官方,合规)          └──────────────────────────────┘       ┌──────┴──────────┐
                                                               │ ClaudeMeter.dll  │──▶ TrafficMonitor 状态栏
                                                               │ (瘦 x86 插件)     │   "CC 3%"  悬停看明细
                                                               └──────────────────┘
```

**核心设计**：采集器与 DLL 通过**本地 INI 缓存文件**解耦。DLL 不碰网络/令牌/JSON，只在 `DataRequired()` 用 Win32 `GetPrivateProfile*` 读文件 → C++ 量最小、无需后台线程/JSON 库、ABI 风险最低。所有易变/灰色逻辑隔离在 Python。

## 5. 组件契约

### 5.1 缓存文件 `usage.ini`（采集器写、DLL 读 —— 唯一接口）

路径：`%LOCALAPPDATA%\ClaudeMeter\usage.ini`（采集器与 DLL 均由 `LOCALAPPDATA` 环境变量解析）

```ini
[usage]
ok=1                        ; 1=数据有效, 0=出错(看 error)
error=                      ; 空或简短错误码: token_expired / network / http_<code>
source=oauth               ; oauth | statusline
updated_epoch=1749712200   ; 采集器写入时刻(UTC epoch 秒)
five_hour_pct=3
five_hour_reset_epoch=1749743400
seven_day_pct=13
seven_day_reset_epoch=1750287600
sonnet_pct=0               ; -1 = N/A
sonnet_reset_epoch=1750287600
opus_pct=-1                ; -1 = N/A (null)
opus_reset_epoch=0
sub=max
```

- 采集器把 ISO `resets_at` 与 statusLine 的 epoch **统一归一化为 UTC epoch 秒**；百分比统一为整数 0–100。
- 原子写：先写 `usage.ini.tmp` 再 `os.replace`，避免 DLL 读到半截。

### 5.2 `cc_usage_collector.py`（主力采集器）

职责：单次运行（one-shot）→ 读令牌 → GET oauth 接口 → 归一化 → 原子写 `usage.ini` → 退出。
- 从 `.credentials.json` 读 `accessToken`；从 `claude --version` 取 UA 版本（失败回退硬编码）。
- 不打印/记录令牌串。
- 错误处理：401→`ok=0,error=token_expired`；网络异常→`ok=0,error=network`；其它 HTTP→`ok=0,error=http_<code>`。出错时保留上次 `*_pct`（仅翻 ok=0），让 DLL 可显示"陈旧"态。
- 不自己刷新令牌：每次重读凭据文件（CC 自身会刷新）。

### 5.3 `statusline_hook.py`（兜底，hybrid）

职责：作为 CC `statusLine.command`，从 stdin 读 JSON → 若含 `rate_limits` 则归一化写 `usage.ini`（`source=statusline`）→ 同时向 stdout 打印一行正常状态栏文字（如 `model · cwd`）。
- 仅在 oauth 数据较旧或缺失时覆盖（避免无分模型的 statusline 覆盖更全的 oauth 数据）：写入前比较 `updated_epoch`，statusline 只在更新更近时写 5h/7d，不动 sonnet/opus。

### 5.4 `ClaudeMeter.dll`（瘦插件，x86）

实现 `ITMPlugin`（单例）+ 一个 `IPluginItem`：
- `DataRequired()`：用 `GetPrivateProfileInt/String` 读 `usage.ini` → 更新成员 `wstring`（value/tooltip）。读文件失败/`ok=0` → value 显示 `--`、tooltip 标注陈旧/错误。
- `IPluginItem::GetItemValueText()`：返回缓存的 `m_value`（如 `L"3%"`），无 I/O。
- `GetItemLableText()` → `L"CC"`；`GetItemValueSampleText()` → `L"100%"`（定宽）。
- `GetItemName/Id`：`L"Claude Code 用量"` / 稳定唯一 id（如 `L"claudemeter.usage"`）。
- `GetTooltipInfo()`：多行明细，用 `*_reset_epoch` 与当前时间实时算"剩余 Xh Ym"（两次轮询间倒计时持续走）。
- 可选 `IsDrawResourceUsageGraph()`→1、`GetResourceUsageGraphValue()`→`five_hour_pct/100`：任务栏迷你进度条。
- `GetInfo()`：插件元数据（名/作者/版本/URL）。
- `OnInitialize(ITrafficMonitor*)`：存宿主指针（备用）。
- 字符串全部 `const wchar_t*`、永不返回 null、由成员/静态 `wstring` 托底。
- 预留自绘接口（`IsCustomDraw/DrawItem`）以便后续加阈值变色/进度条，但 v1 关闭。

### 5.5 显示规格

- 状态栏：`CC 3%`（label `CC` + value `3%`）。默认主指标=5 小时；`config.ini` 可改 `primary=seven_day`。
- Tooltip：
  ```
  Claude Code 用量
  5 小时:  3%    重置 4h12m
  周·全部: 13%   重置 6d
  Sonnet:  0%
  更新 1分钟前 · 来源 oauth
  ```
- v1 颜色随主题；自绘变色为后续可选项。

### 5.6 DLL 显示配置 `config.ini`（可选）

`%LOCALAPPDATA%\ClaudeMeter\config.ini`：
```ini
[display]
primary=five_hour      ; five_hour | seven_day
show_label=1
show_graph=1           ; 任务栏迷你进度条
```

## 6. 安装与构建

### 6.1 工具链
- 装 **VS Build Tools 2022**（winget，Workload `Microsoft.VisualStudio.Workload.VCTools` + 推荐组件 = MSVC v143 + Windows SDK）。
- 不用 MinGW：GCC 与 MSVC 在 x86 上 thiscall 调用约定/虚表 ABI 不兼容，会导致宿主调用插件虚函数崩溃。

### 6.2 构建
- `plugin/build.bat`：用 `vswhere` 定位 VS → 调 `vcvarsall.bat x86` → `cl /LD /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE ClaudeMeterPlugin.cpp /Fe:ClaudeMeter.dll`。
- 备选 `CMakeLists.txt`（生成 Win32/x86）。
- 产物 `ClaudeMeter.dll`（x86）。

### 6.3 安装 `install.ps1`
1. 建 `%LOCALAPPDATA%\ClaudeMeter\`。
2. 先跑一次采集器生成首版 `usage.ini`。
3. 注册计划任务：登录时启动 + 每 3 分钟运行 `pythonw cc_usage_collector.py`（隐藏窗口、self-healing one-shot）。
4. （hybrid）若用户同意，配置 `~/.claude/settings.json` 的 `statusLine.command` 指向 `statusline_hook.py`（当前 statusLine 为空，安全）。
5. 拷 `ClaudeMeter.dll` 到 `D:\Downloads\TrafficMonitor\plugins\`，提示重启 TrafficMonitor 并在显示设置勾选。
- `uninstall.ps1`：删计划任务、还原 statusLine、删 DLL 与缓存目录。

## 7. 风险与缓解

| 风险 | 缓解 |
|---|---|
| oauth 接口未公开、ToS 灰色 | 纯本地、只读自己用量、3 分钟低频；hybrid statusLine 作合规兜底 |
| 接口字段/路径变更 | 归一化集中 Python 一处；DLL 不依赖接口细节 |
| 令牌过期（长期不用 CC） | 接口 401 → `ok=0` → DLL 灰显 `CC --`，不崩 |
| 架构不匹配 | 明确只出 x86；构建脚本固定 x86 目标 |
| UI 线程阻塞 | DLL 只读本地小文件（亚毫秒）；网络全在 Python |

## 8. 测试策略

- **Python（TDD）**：单测覆盖 oauth 归一化、statusLine 归一化、ISO↔epoch 转换、各错误分支、原子写。可用录制的样例 JSON（已有真实样本）离线测。
- **DLL**：写 console 测试宿主 `LoadLibrary("ClaudeMeter.dll")` → `TMPluginGetInstance` → `DataRequired` → 打印 value/tooltip，脱离 TrafficMonitor 验证 ABI 与逻辑。构造不同 `usage.ini`（正常/ok=0/缺字段）验证健壮性。
- **端到端**：拷进 `plugins\`，重启 TrafficMonitor，右键显示设置勾选，肉眼核对与 `/usage` 一致。

## 9. 项目结构

```
ClaudeMeter/
  plugin/
    PluginInterface.h          # 从 TrafficMonitor repo 原样复制
    ClaudeMeterPlugin.cpp      # 瘦插件
    build.bat                  # 定位 MSVC x86 + 编译
    CMakeLists.txt             # 备选构建
    test_host.cpp              # DLL console 测试宿主
  collector/
    cc_usage_collector.py
    statusline_hook.py
    test_collector.py          # pytest/unittest
    samples/                   # 录制的接口/ statusLine 样例 JSON
  install/
    install.ps1
    uninstall.ps1
  docs/superpowers/specs/2026-06-12-claudemeter-design.md
  README.md
```
