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
# Prevent a console window from flashing when the windowless scheduled task
# (pythonw) spawns the console app `claude.exe --version` on Windows.
_NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
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


def claude_version():
    try:
        out = subprocess.run(['claude', '--version'], capture_output=True, text=True,
                             timeout=15, creationflags=_NO_WINDOW)
        toks = (out.stdout or '').strip().split()
        v = toks[0] if toks else ''
        return v if v[:1].isdigit() else DEFAULT_VERSION
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
        creds = read_credentials()
        token = creds['claudeAiOauth']['accessToken']
    except Exception:
        return {'ok': 0, 'error': 'no_token', 'source': 'oauth'}
    try:
        data = fetch_usage(token, claude_version())
    except urllib.error.HTTPError as e:
        err = 'token_expired' if e.code == 401 else 'http_{}'.format(e.code)
        return {'ok': 0, 'error': err, 'source': 'oauth'}
    except Exception:
        return {'ok': 0, 'error': 'network', 'source': 'oauth'}
    try:
        fields = usage_cache.normalize_oauth(data)
    except Exception:
        return {'ok': 0, 'error': 'bad_response', 'source': 'oauth'}
    fields['sub'] = (creds.get('claudeAiOauth') or {}).get('subscriptionType', '')
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
