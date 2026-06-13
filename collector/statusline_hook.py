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
    try:
        print(build_status_line(d))
    except Exception:
        print('Claude')


if __name__ == '__main__':
    main()
