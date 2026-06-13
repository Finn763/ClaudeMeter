# ClaudeMeter

A [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) plugin that shows
Claude Code remaining usage in the Windows status bar — `CC 67%` at a glance, full
breakdown (5-hour / weekly / per-model) on hover.

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

Then restart TrafficMonitor and enable **Claude Code 用量** under 右键 → 显示设置.

Uninstall: `powershell -ExecutionPolicy Bypass -File install\uninstall.ps1`

## Configuration

`%LOCALAPPDATA%\ClaudeMeter\config.ini`:
```ini
[display]
primary=five_hour   ; or seven_day
```

## Data source & privacy

All data stays local. The collector reads your own OAuth token from
`~/.claude/.credentials.json` and calls the same `api/oauth/usage` endpoint Claude
Code uses, at most every ~3 minutes; it never logs the token. The oauth endpoint is
undocumented — if it changes, the `-WithStatusLine` fallback keeps 5h/weekly working
during active Claude Code sessions.

## Notes & limitations

- Per-model lines (Sonnet/Opus) come only from the oauth endpoint.
- If the token expires (no recent Claude Code use), the status bar shows `CC --`
  until the next Claude Code activity refreshes the token.
