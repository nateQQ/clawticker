"""Read Claude Code plan usage (5-hour and 7-day windows).

Claude Code stores an OAuth token on this machine. A one-token request to the
Messages API returns the plan's utilization in response headers, so no usage
endpoint or scraping is needed. Only your own local token is used and it is
never logged or sent anywhere except api.anthropic.com.
"""
import getpass
import hashlib
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


def default_config_dir():
    return os.path.expanduser(os.environ.get("CLAUDE_CONFIG_DIR") or "~/.claude")


def keychain_service(config_dir=None):
    """Keychain service name Claude Code uses for a config dir: the plain name
    for the default account, with a short hash of the directory path when
    CLAUDE_CONFIG_DIR points elsewhere (e.g. a second, work account)."""
    config_dir = os.path.expanduser(config_dir) if config_dir else None
    if not config_dir or config_dir == os.path.expanduser("~/.claude"):
        return KEYCHAIN_PREFIX
    return KEYCHAIN_PREFIX + "-" + hashlib.sha256(config_dir.rstrip("/").encode()).hexdigest()[:8]


def read_token(config_dir=None):
    """Return the OAuth access token of ONE account, or None if it is not logged in.

    config_dir=None means the default account (~/.claude, or CLAUDE_CONFIG_DIR if
    set). Other accounts are never picked up by accident: set `claude_config_dir`
    in the daemon config to read a different one."""
    config_dir = os.path.expanduser(config_dir) if config_dir else default_config_dir()
    if platform.system() == "Darwin":
        # Two items can share the service name; the real login is the one whose
        # account is the macOS user name.
        try:
            out = subprocess.run(["security", "find-generic-password", "-s", keychain_service(config_dir),
                                  "-a", getpass.getuser(), "-w"], capture_output=True, text=True, timeout=10).stdout
        except (OSError, subprocess.SubprocessError):
            out = ""
        tok = _token_from_json(out)
        if tok:
            return tok
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


def fetch(config_dir=None, timeout=20):
    """Return a Claude card, or None when logged out / offline / no headers."""
    token = read_token(config_dir)
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
