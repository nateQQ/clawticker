"""Read Codex CLI rate-limit usage from its local session logs.

Codex writes a `token_count` event with `rate_limits` into every session file
under ~/.codex/sessions/YYYY/MM/DD/. The newest such event is the current state.
"""
import glob
import json
import os
import time

SESSIONS_DIR = os.path.expanduser("~/.codex/sessions")
MAX_FILES = 5          # newest session files to scan
TAIL_BYTES = 256 * 1024


def _latest_files():
    paths = glob.glob(os.path.join(SESSIONS_DIR, "*", "*", "*", "*.jsonl"))
    return sorted(paths, key=os.path.getmtime, reverse=True)[:MAX_FILES]


def _tail_lines(path):
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(max(0, size - TAIL_BYTES))
        return f.read().decode("utf-8", "replace").splitlines()


def _find_rate_limits(lines):
    for line in reversed(lines):
        if '"rate_limits"' not in line:
            continue
        try:
            payload = json.loads(line).get("payload", {})
        except ValueError:
            continue
        limits = payload.get("rate_limits")
        if isinstance(limits, dict) and isinstance(limits.get("primary"), dict):
            return limits
    return None


def _minutes_until(window, now):
    if _expired(window, now):
        return -1
    reset = window.get("resets_at")
    if reset is None and window.get("resets_in_seconds") is not None:
        return max(0, int(window["resets_in_seconds"] // 60))
    try:
        return max(0, int((int(reset) - now) // 60))
    except (TypeError, ValueError):
        return -1


def _expired(window, now):
    """The log keeps the last value forever; once the window's reset time has
    passed, that usage no longer counts."""
    try:
        return int(window["resets_at"]) <= now
    except (KeyError, TypeError, ValueError):
        return False


def _pct(window, now):
    if _expired(window, now):
        return 0
    try:
        return max(0, min(100, round(float(window.get("used_percent", 0)))))
    except (TypeError, ValueError):
        return 0


def card_from_limits(limits, now=None):
    now = time.time() if now is None else now
    primary, secondary = limits["primary"], limits.get("secondary") or {}
    return {
        "n": "Codex",
        "s": _pct(primary, now), "sr": _minutes_until(primary, now),
        "w": _pct(secondary, now), "wr": _minutes_until(secondary, now) if secondary else -1,
    }


def fetch():
    """Return a Codex card, or None when Codex is not installed / never used."""
    for path in _latest_files():
        try:
            limits = _find_rate_limits(_tail_lines(path))
        except OSError:
            continue
        if limits:
            return card_from_limits(limits)
    return None
