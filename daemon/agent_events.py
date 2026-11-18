"""Detect when a Claude Code or Codex turn has just finished.

Both tools append every step of a session to a local JSONL file. A turn is over
when the newest entry is:
  - Claude Code: an assistant message with stop_reason "end_turn"
  - Codex:       an event_msg of type "task_complete"
The board uses this to show a "your turn" screen.
"""
import glob
import json
import os
import time

SCAN_FILES = 6          # newest session files per tool
TAIL_BYTES = 128 * 1024
FRESH_SECONDS = 120     # ignore turns that ended before this window (daemon just started, old logs)


def _claude_dirs():
    base = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.expanduser("~/.claude")
    return os.path.join(base, "projects", "*", "*.jsonl")


CODEX_GLOB = os.path.expanduser("~/.codex/sessions/*/*/*/*.jsonl")


def _newest(pattern):
    try:
        paths = glob.glob(pattern)
        return sorted(paths, key=os.path.getmtime, reverse=True)[:SCAN_FILES]
    except OSError:
        return []


def _tail_lines(path):
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        f.seek(max(0, f.tell() - TAIL_BYTES))
        return f.read().decode("utf-8", "replace").splitlines()


def claude_turn_end(lines):
    """Return the uuid of the last finished top-level assistant turn, or None.

    Only the newest assistant entry counts: if it is still mid-turn (tool_use),
    nothing is reported."""
    for line in reversed(lines):
        if '"assistant"' not in line:
            continue
        try:
            d = json.loads(line)
        except ValueError:
            continue
        msg = d.get("message")
        if d.get("type") != "assistant" or not isinstance(msg, dict) or d.get("isSidechain"):
            continue
        return d.get("uuid") if msg.get("stop_reason") == "end_turn" else None
    return None


def codex_turn_end(lines):
    """Return an id for the last `task_complete` if it is the newest turn event."""
    for line in reversed(lines):
        if '"task_started"' in line:
            return None
        if '"task_complete"' not in line:
            continue
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get("type") == "event_msg" and d.get("payload", {}).get("type") == "task_complete":
            return str(d.get("timestamp") or d.get("ordinal") or "done")
    return None


class Watcher:
    """Call poll() every few seconds; it returns the latest alert dict or None.

    The first poll only records a baseline, so restarting the daemon never
    replays an old finish."""

    def __init__(self, claude_glob=None, codex_glob=CODEX_GLOB):
        self.claude_glob = claude_glob or _claude_dirs()
        self.codex_glob = codex_glob
        self.seen = {}        # path -> last turn id already counted
        self.counter = 0
        self.latest = None
        self.baseline_done = False

    def _scan(self, kind, pattern, finder, now):
        for path in _newest(pattern):
            try:
                mtime = os.path.getmtime(path)
                turn = finder(_tail_lines(path))
            except OSError:
                continue
            if turn is None or self.seen.get(path) == turn:
                continue
            self.seen[path] = turn
            if self.baseline_done and now - mtime <= FRESH_SECONDS:
                self.counter += 1
                self.latest = {"i": self.counter, "k": kind, "t": int(now)}

    def poll(self, now=None):
        now = time.time() if now is None else now
        self._scan("claude", self.claude_glob, claude_turn_end, now)
        self._scan("codex", self.codex_glob, codex_turn_end, now)
        self.baseline_done = True
        return self.latest
