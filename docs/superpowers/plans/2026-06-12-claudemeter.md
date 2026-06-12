# ClaudeMeter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A TrafficMonitor plugin that shows Claude Code remaining usage ("CC 67%") in the Windows status bar, with full breakdown (5h / weekly / per-model) on hover.

**Architecture:** A Python collector polls Claude Code's usage data (oauth endpoint primary + statusLine fallback), normalizes it, and atomically writes a tiny ASCII INI cache at `%LOCALAPPDATA%\ClaudeMeter\usage.ini`. A thin x86 C++ DLL reads that file on each `DataRequired()` tick and renders the value + tooltip — it does zero network/JSON/OAuth. The two halves are decoupled by the INI contract.

**Tech Stack:** Python 3.11 (stdlib only: `urllib`, `json`, `configparser`, `unittest`), C++17 built with MSVC v143 (x86), Win32, PowerShell + `schtasks` for install.

**Verified facts baked into this plan (2026-06-12):**
- TrafficMonitor.exe is **x86** → DLL target is x86. Plugin dir: `D:\Downloads\TrafficMonitor\plugins\`.
- MSVC ready: `cl.exe` at `...\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86\`; Windows SDK `10.0.26100.0` with x86 libs.
- oauth endpoint `GET https://api.anthropic.com/api/oauth/usage` returns `{five_hour:{utilization,resets_at}, seven_day:{...}, seven_day_sonnet:{...}, seven_day_opus:null, extra_usage:{...}}`. Field is **`utilization`** (0–100), `resets_at` is **ISO 8601 string**. Headers: `Authorization: Bearer`, `anthropic-beta: oauth-2025-04-20`, `User-Agent: claude-code/<ver>`.
- statusLine `rate_limits` uses **`used_percentage`** (0–100) and `resets_at` as **epoch seconds** — different names/format; collector normalizes both.
- Token: plaintext `~/.claude/.credentials.json` → `claudeAiOauth.accessToken`.

**Conventions:**
- All commands run from project root `D:\code\ClaudeMeter` unless stated. Use Git Bash for `git`, and `cmd`/PowerShell for C++ builds (vcvars).
- Python tests: `python -m unittest discover -s collector -p "test_*.py"` (run from root, no extra deps).
- Commit after every task.

---

## File Structure

```
ClaudeMeter/
  collector/
    usage_cache.py        # normalization + atomic INI write/read (the cache contract)
    cc_usage_collector.py # oauth poller -> usage.ini (one-shot)
    statusline_hook.py    # CC statusLine command -> usage.ini fallback + prints status line
    test_usage_cache.py   # unit tests
    test_collector.py     # unit tests (mock HTTP)
    test_statusline.py    # unit tests (stdin)
  plugin/
    PluginInterface.h     # vendored verbatim from TrafficMonitor repo (DO NOT edit)
    UsageData.h           # pure struct + format functions (header-only, testable)
    UsageReader.h         # file read + INI parse (header-only)
    ClaudeMeterPlugin.cpp # ITMPlugin/IPluginItem impl + export
    ClaudeMeter.def       # exports TMPluginGetInstance
    test_host.cpp         # console test: unit asserts + DLL load/ABI check
    build.bat             # locate MSVC x86 -> build ClaudeMeter.dll
    build_test.bat        # build + run test_host.exe
  install/
    install.ps1
    uninstall.ps1
  README.md
```

---

## Phase 1 — Python collector (offline, TDD)

### Task 1: `usage_cache.py` — normalization + atomic INI write/read

**Files:**
- Create: `collector/usage_cache.py`
- Test: `collector/test_usage_cache.py`

- [ ] **Step 1: Write the failing tests**

Create `collector/test_usage_cache.py`:

```python
import os, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(__file__))
import usage_cache as uc


class TestUsageCache(unittest.TestCase):
    def test_iso_to_epoch(self):
        self.assertEqual(uc.iso_to_epoch('1970-01-01T00:00:00+00:00'), 0)
        self.assertEqual(uc.iso_to_epoch('1970-01-01T01:00:00+00:00'), 3600)
        self.assertEqual(uc.iso_to_epoch('2026-06-12T15:50:00.355853+00:00'), 1781279400)
        self.assertEqual(uc.iso_to_epoch(''), 0)
        self.assertEqual(uc.iso_to_epoch('not-a-date'), 0)

    def test_normalize_oauth(self):
        data = {
            'five_hour': {'utilization': 3.0, 'resets_at': '1970-01-01T01:00:00+00:00'},
            'seven_day': {'utilization': 13.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
            'seven_day_sonnet': {'utilization': 0.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
            'seven_day_opus': None,
            'extra_usage': {'is_enabled': False},
        }
        f = uc.normalize_oauth(data)
        self.assertEqual(f['ok'], 1)
        self.assertEqual(f['source'], 'oauth')
        self.assertEqual(f['five_hour_pct'], 3)
        self.assertEqual(f['five_hour_reset_epoch'], 3600)
        self.assertEqual(f['seven_day_pct'], 13)
        self.assertEqual(f['sonnet_pct'], 0)
        self.assertEqual(f['opus_pct'], -1)   # null -> -1

    def test_normalize_statusline(self):
        rl = {
            'five_hour': {'used_percentage': 67, 'resets_at': 1700000000},
            'seven_day': {'used_percentage': 10, 'resets_at': 1700500000},
        }
        f = uc.normalize_statusline(rl)
        self.assertEqual(f['source'], 'statusline')
        self.assertEqual(f['five_hour_pct'], 67)
        self.assertEqual(f['five_hour_reset_epoch'], 1700000000)
        self.assertEqual(f['seven_day_pct'], 10)

    def test_render_and_read_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            fields = {'ok': 1, 'source': 'oauth', 'updated_epoch': 123,
                      'five_hour_pct': 67, 'seven_day_pct': 10, 'opus_pct': -1, 'sub': 'max'}
            uc.write_cache(fields, p)
            self.assertTrue(os.path.exists(p))
            back = uc.read_cache(p)
            self.assertEqual(back['ok'], 1)
            self.assertEqual(back['source'], 'oauth')
            self.assertEqual(back['five_hour_pct'], 67)
            self.assertEqual(back['opus_pct'], -1)
            self.assertEqual(back['sub'], 'max')
            self.assertEqual(back['seven_day_reset_epoch'], 0)  # default applied

    def test_read_missing_file(self):
        back = uc.read_cache(os.path.join(tempfile.gettempdir(), 'nope_missing.ini'))
        self.assertEqual(back['ok'], 0)
        self.assertEqual(back['five_hour_pct'], -1)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s collector -p "test_usage_cache.py" -v`
Expected: FAIL / ERROR with `ModuleNotFoundError: No module named 'usage_cache'`.

- [ ] **Step 3: Implement `usage_cache.py`**

Create `collector/usage_cache.py`:

```python
"""Normalization + atomic INI cache shared by the collector and statusline hook.

The INI is the contract with the C++ DLL: ASCII, [usage] section, key=value,
percentages 0..100 (or -1 = N/A), reset times as UTC epoch seconds.
"""
import os
import time
import tempfile
import configparser
from datetime import datetime

CACHE_DIR = os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'ClaudeMeter')
CACHE_PATH = os.path.join(CACHE_DIR, 'usage.ini')

FIELD_ORDER = [
    'ok', 'error', 'source', 'updated_epoch',
    'five_hour_pct', 'five_hour_reset_epoch',
    'seven_day_pct', 'seven_day_reset_epoch',
    'sonnet_pct', 'sonnet_reset_epoch',
    'opus_pct', 'opus_reset_epoch', 'sub',
]
DEFAULTS = {
    'ok': 0, 'error': '', 'source': '', 'updated_epoch': 0,
    'five_hour_pct': -1, 'five_hour_reset_epoch': 0,
    'seven_day_pct': -1, 'seven_day_reset_epoch': 0,
    'sonnet_pct': -1, 'sonnet_reset_epoch': 0,
    'opus_pct': -1, 'opus_reset_epoch': 0, 'sub': '',
}
_STR_FIELDS = ('error', 'source', 'sub')


def iso_to_epoch(s):
    """ISO 8601 (with tz) -> int UTC epoch seconds. 0 on empty/failure."""
    if not s:
        return 0
    try:
        dt = datetime.fromisoformat(str(s).replace('Z', '+00:00'))
        return int(dt.timestamp())
    except Exception:
        return 0


def _pct(v):
    """utilization/used_percentage (0..100, float|int|None) -> int; None -> -1."""
    if v is None:
        return -1
    try:
        return int(round(float(v)))
    except Exception:
        return -1


def normalize_oauth(data):
    """oauth /api/oauth/usage JSON -> cache fields dict (full, incl. per-model)."""
    def win(key):
        w = data.get(key)
        if isinstance(w, dict):
            return _pct(w.get('utilization')), iso_to_epoch(w.get('resets_at'))
        return -1, 0
    fh_p, fh_r = win('five_hour')
    sd_p, sd_r = win('seven_day')
    so_p, so_r = win('seven_day_sonnet')
    op_p, op_r = win('seven_day_opus')
    return {
        'ok': 1, 'error': '', 'source': 'oauth',
        'five_hour_pct': fh_p, 'five_hour_reset_epoch': fh_r,
        'seven_day_pct': sd_p, 'seven_day_reset_epoch': sd_r,
        'sonnet_pct': so_p, 'sonnet_reset_epoch': so_r,
        'opus_pct': op_p, 'opus_reset_epoch': op_r,
    }


def normalize_statusline(rl):
    """statusLine rate_limits JSON -> partial fields (5h/7d only; epoch already)."""
    def win(key):
        w = rl.get(key)
        if isinstance(w, dict):
            try:
                reset = int(w.get('resets_at') or 0)
            except Exception:
                reset = 0
            return _pct(w.get('used_percentage')), reset
        return -1, 0
    fh_p, fh_r = win('five_hour')
    sd_p, sd_r = win('seven_day')
    return {
        'ok': 1, 'error': '', 'source': 'statusline',
        'five_hour_pct': fh_p, 'five_hour_reset_epoch': fh_r,
        'seven_day_pct': sd_p, 'seven_day_reset_epoch': sd_r,
    }


def render_ini(fields):
    f = dict(DEFAULTS)
    f.update({k: v for k, v in fields.items() if v is not None})
    lines = ['[usage]']
    for k in FIELD_ORDER:
        lines.append('{}={}'.format(k, f[k]))
    return '\r\n'.join(lines) + '\r\n'


def write_cache(fields, path=CACHE_PATH):
    """Atomically write the cache INI (ASCII). Retries os.replace on sharing locks."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = render_ini(fields)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), suffix='.tmp')
    try:
        with os.fdopen(fd, 'w', encoding='ascii', errors='replace', newline='') as fh:
            fh.write(text)
        last = None
        for _ in range(6):
            try:
                os.replace(tmp, path)
                return
            except PermissionError as e:
                last = e
                time.sleep(0.05)
        raise last
    finally:
        if os.path.exists(tmp):
            try:
                os.remove(tmp)
            except OSError:
                pass


def read_cache(path=CACHE_PATH):
    """Best-effort read into a fields dict (defaults filled). Never raises."""
    out = dict(DEFAULTS)
    cp = configparser.ConfigParser()
    try:
        cp.read(path, encoding='ascii')
        if cp.has_section('usage'):
            for k in FIELD_ORDER:
                if cp.has_option('usage', k):
                    v = cp.get('usage', k)
                    out[k] = v if k in _STR_FIELDS else int(v)
    except Exception:
        pass
    return out
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest discover -s collector -p "test_usage_cache.py" -v`
Expected: PASS (5 tests OK).

- [ ] **Step 5: Commit**

```bash
git add collector/usage_cache.py collector/test_usage_cache.py
git commit -m "feat(collector): usage_cache normalization + atomic INI cache"
```

---

### Task 2: `cc_usage_collector.py` — oauth poller

**Files:**
- Create: `collector/cc_usage_collector.py`
- Test: `collector/test_collector.py`

- [ ] **Step 1: Write the failing tests**

Create `collector/test_collector.py`:

```python
import os, sys, json, unittest, urllib.error
sys.path.insert(0, os.path.dirname(__file__))
import cc_usage_collector as col

SAMPLE = {
    'five_hour': {'utilization': 3.0, 'resets_at': '1970-01-01T01:00:00+00:00'},
    'seven_day': {'utilization': 13.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
    'seven_day_sonnet': {'utilization': 0.0, 'resets_at': '1970-01-01T02:00:00+00:00'},
    'seven_day_opus': None,
    'extra_usage': {'is_enabled': False},
}


class TestCollector(unittest.TestCase):
    def setUp(self):
        self._rt = col.read_token
        self._fu = col.fetch_usage
        self._cv = col.claude_version
        self._cred = col.read_credentials
        col.read_token = lambda: 'sk-ant-oat01-FAKE'
        col.claude_version = lambda: '2.1.174'
        col.read_credentials = lambda: {'claudeAiOauth': {'subscriptionType': 'max'}}

    def tearDown(self):
        col.read_token = self._rt
        col.fetch_usage = self._fu
        col.claude_version = self._cv
        col.read_credentials = self._cred

    def test_collect_success(self):
        col.fetch_usage = lambda token, version: SAMPLE
        f = col.collect()
        self.assertEqual(f['ok'], 1)
        self.assertEqual(f['five_hour_pct'], 3)
        self.assertEqual(f['seven_day_pct'], 13)
        self.assertEqual(f['sonnet_pct'], 0)
        self.assertEqual(f['opus_pct'], -1)
        self.assertEqual(f['sub'], 'max')

    def test_collect_401_token_expired(self):
        def boom(token, version):
            raise urllib.error.HTTPError('u', 401, 'unauth', {}, None)
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'token_expired')

    def test_collect_500_http_error(self):
        def boom(token, version):
            raise urllib.error.HTTPError('u', 500, 'err', {}, None)
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'http_500')

    def test_collect_network_error(self):
        def boom(token, version):
            raise urllib.error.URLError('no route')
        col.fetch_usage = boom
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'network')

    def test_collect_no_token(self):
        col.read_token = lambda: (_ for _ in ()).throw(FileNotFoundError())
        f = col.collect()
        self.assertEqual(f['ok'], 0)
        self.assertEqual(f['error'], 'no_token')


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s collector -p "test_collector.py" -v`
Expected: FAIL / ERROR with `ModuleNotFoundError: No module named 'cc_usage_collector'`.

- [ ] **Step 3: Implement `cc_usage_collector.py`**

Create `collector/cc_usage_collector.py`:

```python
"""One-shot collector: read OAuth token -> GET /api/oauth/usage -> write usage.ini.

Designed to be run every few minutes by a Scheduled Task. Never refreshes the
token itself (Claude Code maintains ~/.claude/.credentials.json); on 401 it just
flags the cache stale. NEVER logs the token value.
"""
import json
import os
import sys
import time
import subprocess
import urllib.request
import urllib.error

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usage_cache

DEFAULT_VERSION = '2.1.174'
USAGE_URL = 'https://api.anthropic.com/api/oauth/usage'
_PRESERVE = (
    'five_hour_pct', 'five_hour_reset_epoch', 'seven_day_pct', 'seven_day_reset_epoch',
    'sonnet_pct', 'sonnet_reset_epoch', 'opus_pct', 'opus_reset_epoch', 'sub',
)


def credentials_path():
    base = os.environ.get('CLAUDE_CONFIG_DIR') or os.path.join(os.path.expanduser('~'), '.claude')
    return os.path.join(base, '.credentials.json')


def read_credentials():
    with open(credentials_path(), encoding='utf-8') as f:
        return json.load(f)


def read_token():
    return read_credentials()['claudeAiOauth']['accessToken']


def claude_version():
    try:
        out = subprocess.run(['claude', '--version'], capture_output=True, text=True, timeout=15)
        v = (out.stdout or '').strip().split()
        return v[0] if v else DEFAULT_VERSION
    except Exception:
        return DEFAULT_VERSION


def fetch_usage(token, version):
    req = urllib.request.Request(USAGE_URL, headers={
        'Authorization': 'Bearer ' + token,
        'anthropic-beta': 'oauth-2025-04-20',
        'User-Agent': 'claude-code/' + version,
        'Content-Type': 'application/json',
        'Accept': 'application/json',
    })
    with urllib.request.urlopen(req, timeout=25) as r:
        return json.loads(r.read().decode('utf-8'))


def collect():
    try:
        token = read_token()
    except Exception:
        return {'ok': 0, 'error': 'no_token', 'source': 'oauth'}
    try:
        data = fetch_usage(token, claude_version())
    except urllib.error.HTTPError as e:
        err = 'token_expired' if e.code == 401 else 'http_{}'.format(e.code)
        return {'ok': 0, 'error': err, 'source': 'oauth'}
    except Exception:
        return {'ok': 0, 'error': 'network', 'source': 'oauth'}
    fields = usage_cache.normalize_oauth(data)
    try:
        fields['sub'] = read_credentials()['claudeAiOauth'].get('subscriptionType', '')
    except Exception:
        pass
    return fields


def main():
    fields = collect()
    fields['updated_epoch'] = int(time.time())
    if not fields.get('ok'):
        # keep last-known percentages so the DLL can show them as stale, not blank
        prev = usage_cache.read_cache()
        for k in _PRESERVE:
            fields.setdefault(k, prev.get(k))
    usage_cache.write_cache(fields)


if __name__ == '__main__':
    main()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest discover -s collector -p "test_collector.py" -v`
Expected: PASS (5 tests OK).

- [ ] **Step 5: Live smoke test (optional but recommended)**

Run: `python collector/cc_usage_collector.py && type "%LOCALAPPDATA%\ClaudeMeter\usage.ini"`
Expected: `usage.ini` printed with `ok=1`, `source=oauth`, real `five_hour_pct` etc.
(If `ok=0,error=token_expired`, run any Claude Code command first to refresh the token, then retry.)

- [ ] **Step 6: Commit**

```bash
git add collector/cc_usage_collector.py collector/test_collector.py
git commit -m "feat(collector): oauth usage poller writing usage.ini"
```

---

### Task 3: `statusline_hook.py` — statusLine fallback

**Files:**
- Create: `collector/statusline_hook.py`
- Test: `collector/test_statusline.py`

- [ ] **Step 1: Write the failing tests**

Create `collector/test_statusline.py`:

```python
import os, sys, json, io, tempfile, unittest
sys.path.insert(0, os.path.dirname(__file__))
import statusline_hook as sl
import usage_cache as uc


class TestStatusline(unittest.TestCase):
    def test_build_status_line(self):
        d = {'model': {'display_name': 'Opus 4.8'}, 'workspace': {'current_dir': 'C:/code/ClaudeMeter'},
             'rate_limits': {'five_hour': {'used_percentage': 67}}}
        line = sl.build_status_line(d)
        self.assertIn('Opus 4.8', line)
        self.assertIn('ClaudeMeter', line)
        self.assertIn('67%', line)

    def test_build_status_line_no_ratelimits(self):
        line = sl.build_status_line({'model': {'display_name': 'Sonnet'}})
        self.assertIn('Sonnet', line)

    def test_handle_writes_cache_when_no_oauth(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            payload = {'model': {'display_name': 'Opus'},
                       'rate_limits': {'five_hour': {'used_percentage': 67, 'resets_at': 1700000000},
                                       'seven_day': {'used_percentage': 10, 'resets_at': 1700500000}}}
            sl.handle(payload, cache_path=p)
            back = uc.read_cache(p)
            self.assertEqual(back['source'], 'statusline')
            self.assertEqual(back['five_hour_pct'], 67)
            self.assertEqual(back['seven_day_pct'], 10)

    def test_handle_does_not_clobber_fresh_oauth(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'usage.ini')
            import time as _t
            uc.write_cache({'ok': 1, 'source': 'oauth', 'updated_epoch': int(_t.time()),
                            'five_hour_pct': 3, 'sonnet_pct': 0}, p)
            payload = {'model': {'display_name': 'Opus'},
                       'rate_limits': {'five_hour': {'used_percentage': 99, 'resets_at': 1}}}
            sl.handle(payload, cache_path=p)
            back = uc.read_cache(p)
            self.assertEqual(back['source'], 'oauth')   # unchanged
            self.assertEqual(back['five_hour_pct'], 3)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m unittest discover -s collector -p "test_statusline.py" -v`
Expected: FAIL / ERROR with `ModuleNotFoundError: No module named 'statusline_hook'`.

- [ ] **Step 3: Implement `statusline_hook.py`**

Create `collector/statusline_hook.py`:

```python
"""Claude Code statusLine command. Reads the statusline JSON from stdin, prints a
one-line status, and (fallback) writes the official rate_limits to usage.ini when
no fresh oauth data is present. Does not overwrite per-model fields.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usage_cache

OAUTH_FRESH_SECONDS = 600


def build_status_line(d):
    model = ((d.get('model') or {}).get('display_name')) or 'Claude'
    cwd = (d.get('cwd')
           or (d.get('workspace') or {}).get('current_dir')
           or '')
    base = os.path.basename(str(cwd).rstrip('/\\')) if cwd else ''
    rl = d.get('rate_limits') or {}
    fh = (rl.get('five_hour') or {}).get('used_percentage')
    extra = ' · {}%'.format(fh) if isinstance(fh, (int, float)) else ''
    parts = [model]
    if base:
        parts.append(base)
    return ' · '.join(parts) + extra


def handle(d, cache_path=usage_cache.CACHE_PATH):
    rl = d.get('rate_limits')
    if not isinstance(rl, dict) or not rl:
        return
    prev = usage_cache.read_cache(cache_path)
    now = int(time.time())
    oauth_fresh = (prev.get('source') == 'oauth'
                   and (now - int(prev.get('updated_epoch') or 0)) < OAUTH_FRESH_SECONDS)
    if oauth_fresh:
        return
    fields = usage_cache.normalize_statusline(rl)
    fields['updated_epoch'] = now
    # preserve per-model + sub (statusline has no per-model data)
    for k in ('sonnet_pct', 'sonnet_reset_epoch', 'opus_pct', 'opus_reset_epoch', 'sub'):
        fields[k] = prev.get(k)
    usage_cache.write_cache(fields, cache_path)


def main():
    raw = sys.stdin.read()
    try:
        d = json.loads(raw)
    except Exception:
        print('Claude')
        return
    try:
        handle(d)
    except Exception:
        pass
    print(build_status_line(d))


if __name__ == '__main__':
    main()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m unittest discover -s collector -p "test_statusline.py" -v`
Expected: PASS (4 tests OK).

- [ ] **Step 5: Run the whole Python suite**

Run: `python -m unittest discover -s collector -p "test_*.py" -v`
Expected: PASS (14 tests total).

- [ ] **Step 6: Commit**

```bash
git add collector/statusline_hook.py collector/test_statusline.py
git commit -m "feat(collector): statusLine fallback hook"
```

---

## Phase 2 — C++ thin DLL (x86, MSVC)

### Task 4: Vendor header + pure logic (`UsageData.h`, `UsageReader.h`) with C++ unit tests

**Files:**
- Create: `plugin/PluginInterface.h` (download verbatim)
- Create: `plugin/UsageData.h`, `plugin/UsageReader.h`
- Create: `plugin/test_host.cpp`, `plugin/build_test.bat`

- [ ] **Step 1: Vendor the real plugin interface header**

Run (Git Bash):
```bash
curl -fsSL "https://raw.githubusercontent.com/zhongyang219/TrafficMonitor/master/include/PluginInterface.h" -o plugin/PluginInterface.h
head -8 plugin/PluginInterface.h
```
Expected: file saved; header begins with `TrafficMonitor 插件接口` comment block. DO NOT edit this file — it defines the ABI the host expects.

- [ ] **Step 2: Write the failing C++ unit tests**

Create `plugin/test_host.cpp`:

```cpp
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
```

- [ ] **Step 3: Write `build_test.bat`**

Create `plugin/build_test.bat`:

```bat
@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] VS C++ tools not found & exit /b 1 )
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /EHsc /std:c++17 /DUNICODE /D_UNICODE test_host.cpp /Fe:test_host.exe || exit /b 1
echo --- running test_host.exe ---
test_host.exe
endlocal
```

- [ ] **Step 4: Run the test build to verify it FAILS to compile**

Run (cmd): `plugin\build_test.bat`
Expected: compile errors — `UsageData.h` / `UsageReader.h` do not exist yet (`cannot open include file 'UsageData.h'`).

- [ ] **Step 5: Implement `UsageData.h` (pure logic)**

Create `plugin/UsageData.h`:

```cpp
#pragma once
#include <string>
#include <cstdio>

struct UsageData {
    bool ok = false;
    std::wstring source, error, sub;
    int five_hour_pct = -1, seven_day_pct = -1, sonnet_pct = -1, opus_pct = -1;
    long long updated_epoch = 0;
    long long five_hour_reset_epoch = 0, seven_day_reset_epoch = 0,
              sonnet_reset_epoch = 0, opus_reset_epoch = 0;
};

inline std::wstring FormatRemaining(long long reset_epoch, long long now_epoch) {
    if (reset_epoch <= 0) return L"--";
    long long s = reset_epoch - now_epoch;
    if (s <= 0) return L"now";
    long long m = s / 60, h = m / 60, d = h / 24;
    wchar_t buf[40];
    if (d >= 1)      swprintf(buf, 40, L"%lldd %lldh", d, h % 24);
    else if (h >= 1) swprintf(buf, 40, L"%lldh %lldm", h, m % 60);
    else             swprintf(buf, 40, L"%lldm", m);
    return buf;
}

inline std::wstring PctText(int pct) {
    if (pct < 0) return L"--";
    wchar_t buf[16];
    swprintf(buf, 16, L"%d%%", pct);
    return buf;
}

// primary: L"five_hour" or L"seven_day"
inline std::wstring FormatValue(const UsageData& d, const std::wstring& primary) {
    if (!d.ok) return L"--";
    int pct = (primary == L"seven_day") ? d.seven_day_pct : d.five_hour_pct;
    return PctText(pct);
}

inline std::wstring FormatTooltip(const UsageData& d, long long now_epoch) {
    std::wstring t = L"Claude Code 用量"; // 用量
    if (!d.ok) {
        t += L"\n(数据不可用"; // 数据不可用
        if (!d.error.empty()) { t += L": "; t += d.error; }
        t += L")";
        return t;
    }
    wchar_t line[200];
    swprintf(line, 200, L"\n5 小时:  %s   重置 %s", // 5 小时 / 重置
             PctText(d.five_hour_pct).c_str(),
             FormatRemaining(d.five_hour_reset_epoch, now_epoch).c_str());
    t += line;
    swprintf(line, 200, L"\n周·全部: %s   重置 %s", // 周·全部
             PctText(d.seven_day_pct).c_str(),
             FormatRemaining(d.seven_day_reset_epoch, now_epoch).c_str());
    t += line;
    if (d.sonnet_pct >= 0) {
        swprintf(line, 200, L"\nSonnet:  %s", PctText(d.sonnet_pct).c_str());
        t += line;
    }
    if (d.opus_pct >= 0) {
        swprintf(line, 200, L"\nOpus:    %s", PctText(d.opus_pct).c_str());
        t += line;
    }
    long long age = now_epoch - d.updated_epoch;
    std::wstring ageStr;
    if (d.updated_epoch <= 0) ageStr = L"未知"; // 未知
    else if (age < 90)        ageStr = L"刚刚"; // 刚刚
    else { wchar_t b[40]; swprintf(b, 40, L"%lld分钟前", age / 60); ageStr = b; } // 分钟前
    swprintf(line, 200, L"\n更新 %s · 来源 %s", // 更新 / 来源
             ageStr.c_str(), d.source.empty() ? L"-" : d.source.c_str());
    t += line;
    return t;
}
```

- [ ] **Step 6: Implement `UsageReader.h` (file read + INI parse)**

Create `plugin/UsageReader.h`:

```cpp
#pragma once
#include "UsageData.h"
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ASCII-only cache content -> widen 1:1
inline std::wstring WidenAscii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

inline UsageData ParseUsageIni(const std::string& raw) {
    auto get = [&](const char* key) -> std::string {
        size_t kl = std::strlen(key), pos = 0;
        while (pos <= raw.size()) {
            size_t eol = raw.find('\n', pos);
            size_t end = (eol == std::string::npos) ? raw.size() : eol;
            std::string line = raw.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > kl && std::memcmp(line.data(), key, kl) == 0 && line[kl] == '=')
                return line.substr(kl + 1);
            if (eol == std::string::npos) break;
            pos = eol + 1;
        }
        return std::string();
    };
    auto geti = [&](const char* k, int def) {
        std::string v = get(k);
        return v.empty() ? def : std::atoi(v.c_str());
    };
    auto getll = [&](const char* k) {
        std::string v = get(k);
        return v.empty() ? 0LL : std::atoll(v.c_str());
    };

    UsageData d;
    d.ok = geti("ok", 0) != 0;
    d.source = WidenAscii(get("source"));
    d.error  = WidenAscii(get("error"));
    d.sub    = WidenAscii(get("sub"));
    d.updated_epoch = getll("updated_epoch");
    d.five_hour_pct = geti("five_hour_pct", -1);
    d.five_hour_reset_epoch = getll("five_hour_reset_epoch");
    d.seven_day_pct = geti("seven_day_pct", -1);
    d.seven_day_reset_epoch = getll("seven_day_reset_epoch");
    d.sonnet_pct = geti("sonnet_pct", -1);
    d.sonnet_reset_epoch = getll("sonnet_reset_epoch");
    d.opus_pct = geti("opus_pct", -1);
    d.opus_reset_epoch = getll("opus_reset_epoch");
    return d;
}

inline UsageData ReadUsageFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return UsageData{};
    std::string raw;
    char buf[2048];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    return ParseUsageIni(raw);
}

inline std::wstring GetCacheIniPath() {
    const wchar_t* la = _wgetenv(L"LOCALAPPDATA");
    std::wstring base = (la && *la) ? la : L".";
    return base + L"\\ClaudeMeter\\usage.ini";
}
```

- [ ] **Step 7: Run the test build to verify it PASSES**

Run (cmd): `plugin\build_test.bat`
Expected: compiles; prints `ALL UNIT TESTS PASSED`.

- [ ] **Step 8: Commit**

```bash
git add plugin/PluginInterface.h plugin/UsageData.h plugin/UsageReader.h plugin/test_host.cpp plugin/build_test.bat
git commit -m "feat(plugin): vendored interface + pure usage logic with unit tests"
```

---

### Task 5: `ClaudeMeterPlugin.cpp` + `.def` + `build.bat` — build the DLL

**Files:**
- Create: `plugin/ClaudeMeterPlugin.cpp`, `plugin/ClaudeMeter.def`, `plugin/build.bat`

- [ ] **Step 1: Write `ClaudeMeter.def` (export contract)**

Create `plugin/ClaudeMeter.def`:

```
EXPORTS
    TMPluginGetInstance
```

- [ ] **Step 2: Write `ClaudeMeterPlugin.cpp`**

Create `plugin/ClaudeMeterPlugin.cpp`:

```cpp
#include <windows.h>
#include <ctime>
#include <string>
#include "PluginInterface.h"
#include "UsageData.h"
#include "UsageReader.h"

class CClaudeItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"Claude Code 用量"; } // 用量
    const wchar_t* GetItemId() const override { return L"claudemeter.usage"; }
    const wchar_t* GetItemLableText() const override { return L"CC"; }
    const wchar_t* GetItemValueText() const override { return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }
    int IsDrawResourceUsageGraph() const override { return 1; }
    float GetResourceUsageGraphValue() const override { return m_graph; }

    void Update(const UsageData& d, const std::wstring& primary) {
        m_data = d;
        m_value = FormatValue(d, primary);
        int pct = (primary == L"seven_day") ? d.seven_day_pct : d.five_hour_pct;
        m_graph = (d.ok && pct >= 0) ? static_cast<float>(pct) / 100.0f : 0.0f;
    }
    std::wstring BuildTooltip() const {
        return FormatTooltip(m_data, static_cast<long long>(time(nullptr)));
    }

private:
    UsageData m_data;
    std::wstring m_value = L"--";
    float m_graph = 0.0f;
};

class CClaudeMeterPlugin : public ITMPlugin {
public:
    static CClaudeMeterPlugin& Instance() { static CClaudeMeterPlugin i; return i; }

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }

    void DataRequired() override {
        UsageData d = ReadUsageFile(GetCacheIniPath());
        m_item.Update(d, m_primary);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"ClaudeMeter";
        case TMI_DESCRIPTION: return L"在状态栏显示 Claude Code 剩余用量"; // 在状态栏显示...
        case TMI_AUTHOR:      return L"ClaudeMeter";
        case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
        case TMI_VERSION:     return L"1.0.0";
        case TMI_URL:         return L"https://github.com/";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override {
        m_tooltip = m_item.BuildTooltip();
        return m_tooltip.c_str();
    }

    void OnInitialize(ITrafficMonitor* pApp) override {
        m_app = pApp;
        LoadConfig();
    }

private:
    void LoadConfig() {
        std::wstring p = GetCacheIniPath();
        size_t pos = p.rfind(L"usage.ini");
        if (pos != std::wstring::npos) p.replace(pos, wcslen(L"usage.ini"), L"config.ini");
        wchar_t buf[32];
        GetPrivateProfileStringW(L"display", L"primary", L"five_hour", buf, 32, p.c_str());
        m_primary = buf;
    }

    CClaudeItem m_item;
    ITrafficMonitor* m_app = nullptr;
    std::wstring m_tooltip;
    std::wstring m_primary = L"five_hour";
};

extern "C" ITMPlugin* TMPluginGetInstance() {
    return &CClaudeMeterPlugin::Instance();
}
```

- [ ] **Step 3: Write `build.bat`**

Create `plugin/build.bat`:

```bat
@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] VS C++ tools not found & exit /b 1 )
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /LD /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE /DNDEBUG ClaudeMeterPlugin.cpp /Fe:ClaudeMeter.dll /link /DEF:ClaudeMeter.def || exit /b 1
echo [OK] built ClaudeMeter.dll
echo --- verifying export name ---
dumpbin /nologo /exports ClaudeMeter.dll | findstr /C:"TMPluginGetInstance" || ( echo [ERROR] export missing & exit /b 1 )
endlocal
```

- [ ] **Step 4: Build the DLL**

Run (cmd): `plugin\build.bat`
Expected: `[OK] built ClaudeMeter.dll`, and the dumpbin line lists `TMPluginGetInstance` (undecorated). Confirm `plugin\ClaudeMeter.dll` exists.

- [ ] **Step 5: Confirm it is a 32-bit (x86) DLL**

Run (Git Bash):
```bash
python -c "import struct;d=open('plugin/ClaudeMeter.dll','rb').read(0x200);pe=struct.unpack_from('<I',d,0x3C)[0];print('machine=0x%X'%struct.unpack_from('<H',d,pe+4)[0])"
```
Expected: `machine=0x14C` (x86). If `0x8664`, the wrong vcvars arch was used — re-run with `x86`.

- [ ] **Step 6: Commit**

```bash
git add plugin/ClaudeMeterPlugin.cpp plugin/ClaudeMeter.def plugin/build.bat
git commit -m "feat(plugin): ClaudeMeter DLL (ITMPlugin/IPluginItem) + x86 build"
```

---

### Task 6: Extend `test_host.cpp` with DLL load/ABI integration test

**Files:**
- Modify: `plugin/test_host.cpp`

- [ ] **Step 1: Add the DLL integration test (write failing test)**

In `plugin/test_host.cpp`, add `#include <windows.h>` and `#include "PluginInterface.h"` near the top (after the existing includes), add the function below before `wmain`, and call `dll_integration();` inside `wmain` after `unit_tests();`.

Add include lines at top:
```cpp
#include <windows.h>
#include "PluginInterface.h"
```

Add this function:
```cpp
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
```

Update `wmain`:
```cpp
int wmain() {
    unit_tests();
    dll_integration();
    if (g_failures == 0) { wprintf(L"ALL TESTS PASSED\n"); return 0; }
    wprintf(L"%d FAILURE(S)\n", g_failures);
    return 1;
}
```

- [ ] **Step 2: Run build_test to verify the integration test runs**

Run (cmd): `plugin\build_test.bat`
Expected: compiles; `test_host.exe` runs. `[dll] label='CC' value='...'` is printed and `ALL TESTS PASSED`.
Note: `test_host.exe` and `ClaudeMeter.dll` must be in the same folder (both are in `plugin/`). The value reflects the real `%LOCALAPPDATA%\ClaudeMeter\usage.ini` (run the collector first in Task 2/Step 5 for a non-`--` value).

- [ ] **Step 3: Commit**

```bash
git add plugin/test_host.cpp
git commit -m "test(plugin): DLL load + ABI integration test"
```

---

## Phase 3 — Install & end-to-end

### Task 7: `install.ps1` / `uninstall.ps1`

**Files:**
- Create: `install/install.ps1`, `install/uninstall.ps1`

- [ ] **Step 1: Write `install.ps1`**

Create `install/install.ps1`:

```powershell
param(
    [string]$TrafficMonitorDir = "D:\Downloads\TrafficMonitor",
    [switch]$WithStatusLine
)
$ErrorActionPreference = "Stop"
$Root      = Split-Path -Parent $PSScriptRoot
$Collector = Join-Path $Root "collector\cc_usage_collector.py"
$Hook      = Join-Path $Root "collector\statusline_hook.py"
$Dll       = Join-Path $Root "plugin\ClaudeMeter.dll"
$CacheDir  = Join-Path $env:LOCALAPPDATA "ClaudeMeter"

New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

$pyw = (Get-Command pythonw.exe -ErrorAction SilentlyContinue).Source
if (-not $pyw) { $pyw = (Get-Command python.exe).Source }
Write-Host "python: $pyw"

# Seed the cache once
& $pyw $Collector
Write-Host "seeded: $(Join-Path $CacheDir 'usage.ini')"

# Scheduled task: run collector every 3 minutes
$taskName = "ClaudeMeter Collector"
schtasks /Query /TN $taskName 2>$null | Out-Null
if ($LASTEXITCODE -eq 0) { schtasks /Delete /TN $taskName /F | Out-Null }
$tr = '"' + $pyw + '" "' + $Collector + '"'
schtasks /Create /TN $taskName /TR $tr /SC MINUTE /MO 3 /F | Out-Null
Write-Host "scheduled task '$taskName' created (every 3 min)"

# Copy DLL into TrafficMonitor plugins
if (-not (Test-Path $Dll)) { throw "DLL not built: $Dll  (run plugin\build.bat first)" }
$pluginDir = Join-Path $TrafficMonitorDir "plugins"
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item $Dll (Join-Path $pluginDir "ClaudeMeter.dll") -Force
Write-Host "DLL copied to $pluginDir"

# Optional: official statusLine fallback
if ($WithStatusLine) {
    $settings = Join-Path $env:USERPROFILE ".claude\settings.json"
    if (Test-Path $settings) { $json = Get-Content $settings -Raw | ConvertFrom-Json }
    else { $json = [pscustomobject]@{} }
    $py = (Get-Command python.exe).Source
    $cmd = '"' + $py + '" "' + $Hook + '"'
    $sl = [pscustomobject]@{ type = "command"; command = $cmd; padding = 0 }
    $json | Add-Member -NotePropertyName statusLine -NotePropertyValue $sl -Force
    ($json | ConvertTo-Json -Depth 20) | Set-Content $settings -Encoding UTF8
    Write-Host "statusLine configured in $settings"
}

Write-Host ""
Write-Host "DONE. Restart TrafficMonitor, then right-click -> 显示设置 and enable 'Claude Code 用量'."
```

- [ ] **Step 2: Write `uninstall.ps1`**

Create `install/uninstall.ps1`:

```powershell
param([string]$TrafficMonitorDir = "D:\Downloads\TrafficMonitor")
schtasks /Delete /TN "ClaudeMeter Collector" /F 2>$null | Out-Null
Remove-Item (Join-Path $TrafficMonitorDir "plugins\ClaudeMeter.dll") -Force -ErrorAction SilentlyContinue
$settings = Join-Path $env:USERPROFILE ".claude\settings.json"
if (Test-Path $settings) {
    $json = Get-Content $settings -Raw | ConvertFrom-Json
    if ($json.PSObject.Properties.Name -contains 'statusLine') {
        $json.PSObject.Properties.Remove('statusLine')
        ($json | ConvertTo-Json -Depth 20) | Set-Content $settings -Encoding UTF8
    }
}
Remove-Item (Join-Path $env:LOCALAPPDATA "ClaudeMeter") -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "ClaudeMeter uninstalled."
```

- [ ] **Step 3: Dry-run install (without statusLine)**

Run (PowerShell): `powershell -ExecutionPolicy Bypass -File install\install.ps1`
Expected: prints python path, "seeded ...", "scheduled task created", "DLL copied". Verify:
- `schtasks /Query /TN "ClaudeMeter Collector"` lists the task.
- `dir "%LOCALAPPDATA%\ClaudeMeter\usage.ini"` exists.
- `dir "D:\Downloads\TrafficMonitor\plugins\ClaudeMeter.dll"` exists.

- [ ] **Step 4: Commit**

```bash
git add install/install.ps1 install/uninstall.ps1
git commit -m "feat(install): scheduled-task collector + DLL deploy + optional statusLine"
```

---

### Task 8: README + end-to-end verification

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write `README.md`**

Create `README.md`:

````markdown
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
````

- [ ] **Step 2: Full end-to-end verification**

Perform in order and confirm each:
1. `python -m unittest discover -s collector -p "test_*.py"` → all pass.
2. `plugin\build.bat` → `ClaudeMeter.dll` built (x86 verified in Task 5/Step 5).
3. `plugin\build_test.bat` → `ALL TESTS PASSED`, `[dll] label='CC' value='<n>%'`.
4. `powershell -ExecutionPolicy Bypass -File install\install.ps1` → task + DLL deployed.
5. Restart TrafficMonitor → 右键 → 显示设置 → check **Claude Code 用量** → the item shows `CC <n>%`.
6. Hover the item → tooltip shows 5h / 周·全部 / Sonnet / 更新 lines.
7. Cross-check the number against Claude Code's own `/usage` panel — they should match (allowing for the ≤3-min poll lag).

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: README + end-to-end verification"
```

---

## Self-Review notes (author)

- **Spec coverage:** data sources (oauth primary + statusLine fallback) → Tasks 2,3; thin-DLL-reads-INI architecture → Tasks 4,5; "CC 5h%" display + tooltip → UsageData.h/ClaudeMeterPlugin.cpp; INI contract → Task 1 matches §5.1; x86 build via MSVC → Task 5; scheduled task + statusLine + deploy → Task 7; tests (Python unittest + C++ host) → throughout; config.ini primary metric → ClaudeMeterPlugin LoadConfig + README.
- **Deferred from spec (intentional, YAGNI):** JSONL estimation (spec §3.3 explicitly "not implemented"); custom-draw threshold coloring (spec v1 = plain text; interface left default so it can be added later); options dialog (not needed).
- **Type/name consistency:** INI field names identical across `usage_cache.FIELD_ORDER`, `ParseUsageIni`, and `test_host`; `FormatValue/FormatRemaining/ParseUsageIni` signatures match between `UsageData.h`/`UsageReader.h` and `test_host.cpp`; export name `TMPluginGetInstance` consistent in `.cpp`, `.def`, `build.bat`, `test_host.cpp`.
