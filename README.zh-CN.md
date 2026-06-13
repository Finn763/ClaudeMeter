# ClaudeMeter

**[English](README.md) · 中文说明**

一个 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 插件，在 Windows 任务栏/状态栏用**三条进度条**直观显示 Claude Code 的剩余用量：5 小时窗口、周·全部模型、Sonnet，每条带已用百分比，鼠标悬停可查看完整明细（含 Opus 与重置时间）。

**v3 起**还会在进度条右侧显示当前打开了几个 Claude Code 窗口、各自在干嘛：绿灯 + 闲置窗口数（等你输入）在上，红灯 + 工作中窗口数（模型生成 / 跑工具 / 跑 `!` 命令）在下。

![ClaudeMeter 任务栏效果](docs/images/taskbar-bars.png)

## 功能特性

- **任务栏三进度条**：`5h` / `7d` / `So` 三个用量窗口，每条 = 小标签 + 进度条 + 已用百分比。
- **阈值变色**：绿 `<50%` / 黄 `50–80%` / 红 `>80%`，一眼看出是否逼近上限；自适应明暗主题。
- **窗口状态指示器（v3）**：进度条右侧两盏灯——绿=闲置窗口数、红=工作中窗口数。DLL 直接扫描 `~/.claude/sessions/*.json`（支持 `CLAUDE_CONFIG_DIR`），只统计进程仍存活的真实窗口，自动排除无头 SDK 会话；读不到则显示 `0`。
- **悬停明细**：鼠标停在条目上，显示 5 小时、周、Sonnet、Opus 的百分比与重置倒计时、数据更新时间，外加 `窗口: 工作 N · 闲置 N`。
- **完全静默后台**：用量采集每 3 分钟在后台无窗口运行，不弹任何终端 / 通知 / UAC。
- **本地优先 + 混合数据源**：主用 Claude Code 的 `oauth` 用量接口；可选官方 `statusLine` 作合规兜底。
- **零密钥外泄**：令牌只用于请求头，从不落盘 / 打印；DLL 本身不碰网络。

## 工作原理

```
Python 采集器 ──(oauth /api/oauth/usage + statusLine 兜底)──▶ %LOCALAPPDATA%\ClaudeMeter\usage.ini
                                                                      │
                                          瘦 x86 DLL 读取该文件 ──▶ TrafficMonitor 任务栏自绘三进度条
```

DLL 不做任何网络请求；Python 采集器（计划任务，每 3 分钟）负责全部接口/令牌逻辑，写一个很小的 INI 文件，DLL 只负责读取并绘制。两边通过这个本地 INI 文件解耦——所有易变/灰色逻辑都隔离在易改的 Python 里，原生 DLL 保持最小、稳定。

## 系统要求

- **TrafficMonitor（x86 版本）**——插件 DLL 架构必须与之匹配。
- **Python 3.11+** 且在 PATH 中。
- 构建 DLL 需 **Visual Studio Build Tools 2022**（MSVC v143，x86）。
- 已登录的 **Claude Code（Pro/Max）**——提供 `~/.claude/.credentials.json`。

## 构建

```cmd
plugin\build.bat        :: 生成 plugin\ClaudeMeter.dll（x86，静态 CRT，无需 VC++ 运行库）
plugin\build_test.bat   :: 可选：运行单元测试 + DLL ABI 测试
python -m unittest discover -s collector -p "test_*.py"   :: Python 测试
```

## 安装

```powershell
# 基础（仅 oauth 轮询）：
powershell -ExecutionPolicy Bypass -File install\install.ps1
# 同时接入官方 statusLine 兜底：
powershell -ExecutionPolicy Bypass -File install\install.ps1 -WithStatusLine
```

安装脚本会自动：建缓存目录、首次取一次数据、注册每 3 分钟的计划任务（`pythonw` 无窗口）、把 DLL 拷到 TrafficMonitor 的 `plugins\` 目录。

随后**重启 TrafficMonitor**，右键状态栏 → **显示设置**，勾选 **Claude Code 用量与窗口** 即可看到三条进度条 + 右侧窗口状态灯。

卸载：`powershell -ExecutionPolicy Bypass -File install\uninstall.ps1`（删任务、撤 DLL、还原 statusLine、清缓存）。

## 显示说明

任务栏项显示三条堆叠的进度条——5 小时、周（全部模型）、Sonnet——每条按已用百分比填充，并按负载着色（绿 `<50%`、黄 `50–80%`、红 `>80%`）。悬停查看完整明细（含 Opus 与重置时间）。无需任何配置文件。

进度条右侧竖排两盏窗口状态灯：上为绿点 + 闲置窗口数（等你输入），下为红点 + 工作中窗口数（模型生成 / 跑工具 / 跑 `!` 命令）。计数来自 Claude Code 自己维护的 `~/.claude/sessions/<pid>.json`——DLL 每次刷新时扫描该目录，仅统计进程仍存活、且写有 `status` 字段的真实交互窗口（无头 SDK 会话因无 `status` 被自动排除）；目录读不到时两盏灯显示 `0`，不影响左侧用量条。

## 数据来源与隐私

所有数据都留在本地。采集器从 `~/.claude/.credentials.json` 读取你自己的 OAuth 令牌，调用 Claude Code 同款的 `api/oauth/usage` 接口，最多每 ~3 分钟一次，**从不记录令牌**。该接口未公开——若其字段/路径变动，`-WithStatusLine` 兜底可在 Claude Code 会话运行时继续提供 5 小时/周用量。

> 说明：直连该接口属于复用个人 OAuth 令牌（ToS 灰色区），但纯本地、只读你自己的用量、低频请求；若有顾虑，可只用 `-WithStatusLine` 的官方 `statusLine` 路径（合规，但只有 5 小时/周、且只在会话运行时更新）。

## 注意事项与限制

- 分模型行（Sonnet / Opus）仅来自 oauth 接口；接口只提供**利用率百分比**，没有绝对 token 数。
- 令牌过期（长时间未用 Claude Code）时，进度条会渲染为空灰条 + `--`，直到下次 Claude Code 活动刷新令牌。
- DLL 必须是 **x86**，与 TrafficMonitor 架构一致，否则会在插件管理里静默加载失败。
