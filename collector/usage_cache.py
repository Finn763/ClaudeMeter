"""Normalization + atomic INI cache shared by the collector and statusline hook.

The INI is the contract with the C++ DLL: ASCII, [usage] section, key=value,
percentages 0..100 (or -1 = N/A), reset times as UTC epoch seconds.
"""
import os
import time
import tempfile
import configparser
from datetime import datetime

# NOTE: evaluated at import time; the scheduled-task process always sees LOCALAPPDATA.
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
