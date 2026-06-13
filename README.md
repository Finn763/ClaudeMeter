# ClaudeMeter

**English · [中文说明](README.zh-CN.md)**

A [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) plugin that shows
Claude Code remaining usage in the Windows status bar — three threshold-colored
progress bars (5-hour / weekly / Sonnet), each with its used percentage, plus a full
breakdown (incl. Opus + reset times) on hover.

Since **v3** it also shows, to the right of the bars, how many Claude Code windows you
have open and what they're doing: a green dot with the idle count (waiting for input)
and a red dot with the working count (model generating, running a tool, or a `!` shell).

![ClaudeMeter taskbar: three usage bars plus green idle / red working window-count indicators](docs/images/taskbar-v3.png)

## How it works

```
Python collector  --(oauth /api/oauth/usage + statusLine fallback)-->  %LOCALAPPDATA%\ClaudeMeter\usage.ini
                                                                              |
                                                          thin x86 DLL reads it -> TrafficMonitor status bar
```

The DLL does no networking; the Python collector (a Scheduled Task, every 3 min)
owns all the API/token logic and writes a small INI the DLL renders.

## Requirements

- TrafficMonitor (x86 build) — plugin DLL must match its architecture.
- Python 3.11+ on PATH.
- Visual Studio Build Tools 2022 (MSVC v143, x86) to build the DLL.
- A logged-in Claude Code (Pro/Max) — provides `~/.claude/.credentials.json`.

## Build

```cmd
plugin\build.bat        :: produces plugin\ClaudeMeter.dll (x86)
plugin\build_test.bat   :: optional: run unit + DLL ABI tests
python -m unittest discover -s collector -p "test_*.py"   :: Python tests
```

## Install

```powershell
# basic (oauth poller only):
powershell -ExecutionPolicy Bypass -File install\install.ps1
# also wire the official statusLine fallback:
powershell -ExecutionPolicy Bypass -File install\install.ps1 -WithStatusLine
```

Then restart TrafficMonitor and enable **Claude Code 用量与窗口** under 右键 → 显示设置.

Uninstall: `powershell -ExecutionPolicy Bypass -File install\uninstall.ps1`

## Display

The taskbar item shows three stacked progress bars — 5-hour, weekly (all models), and
Sonnet — each filled to its used percentage and colored by load (green < 50%, yellow
50–80%, red > 80%). Hover for the full breakdown (incl. Opus and reset times). No
configuration file is required.

To the right of the bars, two indicators report your open Claude Code windows: a green
dot + idle count (waiting for input) above, a red dot + working count (busy or running a
`!` shell) below. The DLL reads `~/.claude/sessions/*.json` (honoring `CLAUDE_CONFIG_DIR`),
counting only live windows whose owning process is still running; headless SDK sessions
are excluded. If that directory can't be read, the indicators simply show `0`. Hover adds
a `窗口: 工作 N · 闲置 N` line.

## Data source & privacy

All data stays local. The collector reads your own OAuth token from
`~/.claude/.credentials.json` and calls the same `api/oauth/usage` endpoint Claude
Code uses, at most every ~3 minutes; it never logs the token. The oauth endpoint is
undocumented — if it changes, the `-WithStatusLine` fallback keeps 5h/weekly working
during active Claude Code sessions.

## Notes & limitations

- Per-model lines (Sonnet/Opus) come only from the oauth endpoint.
- If the token expires (no recent Claude Code use), the bars render empty (gray) with
  `--` until the next Claude Code activity refreshes the token.
