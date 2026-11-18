"""Read Claude Code plan usage (5-hour and 7-day windows).

Claude Code stores an OAuth token on this machine. A one-token request to the
Messages API returns the plan's utilization in response headers, so no usage
endpoint or scraping is needed. Only your own local token is used and it is
never logged or sent anywhere except api.anthropic.com.
"""
import json
import os
import platform
import subprocess
import time
import urllib.error
import urllib.request

API_URL = "https://api.anthropic.com/v1/messages"
PROBE_MODEL = "claude-haiku-4-5"
KEYCHAIN_PREFIX = "Claude Code-credentials"


def _token_from_json(text):
    try:
        return json.loads(text)["claudeAiOauth"]["accessToken"]
    except (ValueError, KeyError, TypeError):
        return None


def _keychain_services(preferred):
    """Service names to try on macOS: configured one, the default, then any
    'Claude Code-credentials*' entry (Claude Code adds a suffix when
    CLAUDE_CONFIG_DIR is set). Listing names does not reveal secrets."""
    names = []
    if preferred:
        names.append(preferred)
    names.append(KEYCHAIN_PREFIX)
    try:
        out = subprocess.run(["security", "dump-keychain"], capture_output=True,
                             text=True, timeout=10).stdout
        for line in out.splitlines():
            line = line.strip()
            if line.startswith('"svce"<blob>="' + KEYCHAIN_PREFIX):
                names.append(line.split('="', 1)[1].rstrip('"'))
    except (OSError, subprocess.SubprocessError):
        pass
    seen = set()
    return [n for n in names if not (n in seen or seen.add(n))]


def read_token(preferred_service=None):
    """Return the OAuth access token, or None if Claude Code is not logged in."""
    if platform.system() == "Darwin":
        for svc in _keychain_services(preferred_service):
            try:
                out = subprocess.run(["security", "find-generic-password", "-s", svc, "-w"],
                                     capture_output=True, text=True, timeout=10).stdout
            except (OSError, subprocess.SubprocessError):
                continue
            tok = _token_from_json(out)
            if tok:
                return tok
    config_dir = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.expanduser("~/.claude")
    try:
        with open(os.path.join(config_dir, ".credentials.json"), encoding="utf-8") as f:
            return _token_from_json(f.read())
    except OSError:
        return None


def _pct(value):
    try:
        return max(0, min(100, round(float(value) * 100)))
    except (TypeError, ValueError):
        return None


def _minutes_until(epoch_text, now):
    try:
        return max(0, int((int(epoch_text) - now) // 60))
    except (TypeError, ValueError):
        return -1


def parse_headers(headers, now=None):
    """Turn rate-limit headers into a card dict, or None if they are missing."""
    now = time.time() if now is None else now
    get = lambda k: headers.get("anthropic-ratelimit-unified-" + k)
    session, weekly = _pct(get("5h-utilization")), _pct(get("7d-utilization"))
    if session is None and weekly is None:
        return None
    return {
        "n": "Claude",
        "s": session if session is not None else 0,
        "sr": _minutes_until(get("5h-reset"), now),
        "w": weekly if weekly is not None else 0,
        "wr": _minutes_until(get("7d-reset"), now),
    }


def fetch(preferred_service=None, timeout=20):
    """Return a Claude card, or None when logged out / offline / no headers."""
    token = read_token(preferred_service)
    if not token:
        return None
    body = json.dumps({"model": PROBE_MODEL, "max_tokens": 1,
                       "messages": [{"role": "user", "content": "hi"}]}).encode()
    req = urllib.request.Request(API_URL, data=body, method="POST", headers={
        "Authorization": "Bearer " + token,
        "anthropic-beta": "oauth-2025-04-20",
        "anthropic-version": "2023-06-01",
        "content-type": "application/json",
    })
    try:
        headers = urllib.request.urlopen(req, timeout=timeout).headers
    except urllib.error.HTTPError as e:
        # A 429 at 100% still carries the headers, which is exactly when the
        # countdown matters most.
        headers = e.headers
    except (urllib.error.URLError, OSError):
        return None
    return parse_headers(headers)
