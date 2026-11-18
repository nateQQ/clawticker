#!/usr/bin/env python3
"""ClawTicker daemon: gathers AI usage + coin prices, serves them to the board.

The board polls GET http://<this-machine>:<port>/payload every few seconds.
Run `python3 clawticker_daemon.py` (the installer sets this up as a login
service). Config: ~/.config/clawticker/config, re-read on every refresh.
"""
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import claude_usage  # noqa: E402
import codex_usage   # noqa: E402
import coins as coins_mod  # noqa: E402
import agent_events  # noqa: E402

CONFIG_PATH = os.path.expanduser(os.environ.get("CLAWTICKER_CONFIG", "~/.config/clawticker/config"))
DEFAULTS = {
    "display": "claude",                       # claude | multi
    "port": "8787",
    "coins": "bitcoin:BTC",
    "vs": "usd",
    "claude_config_dir": "",                   # empty = default account (~/.claude)
}
AI_INTERVAL = 120      # seconds between Claude probes (each is a 1-token request)
CODEX_INTERVAL = 30    # local file read, cheap
COIN_INTERVAL = 90     # CoinGecko free tier allows roughly 10-30 calls/min


def read_config(path=CONFIG_PATH):
    cfg = dict(DEFAULTS)
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if "=" in line:
                    k, _, v = line.partition("=")
                    cfg[k.strip().lower()] = v.strip()
    except OSError:
        pass
    if cfg["display"] not in ("claude", "multi"):
        cfg["display"] = "claude"
    return cfg


def build_payload(cfg, claude_card, codex_card, coin_rows, alert=None, now=None):
    """Assemble the JSON the firmware reads. Schema is documented in docs/payload.md."""
    now = time.time() if now is None else now
    cards = []
    if claude_card:
        cards.append(claude_card)
    else:
        cards.append({"n": "Claude", "s": 0, "sr": -1, "w": 0, "wr": -1, "off": 1})
    if cfg["display"] == "multi":
        cards.append(codex_card or {"n": "Codex", "s": 0, "sr": -1, "w": 0, "wr": -1, "off": 1})
    payload = {
        "v": 1,
        "mode": cfg["display"],
        "t": int(now) + time.localtime(now).tm_gmtoff,   # local wall-clock seconds
        "ai": cards,
        "coins": coin_rows or [],
    }
    if alert:
        payload["al"] = {"i": alert["i"], "k": alert["k"]}
    return payload


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.claude = self.codex = self.coin_rows = self.alert = None

    def snapshot(self):
        with self.lock:
            return self.claude, self.codex, self.coin_rows, self.alert


def _loop(interval, work):
    while True:
        try:
            work()
        except Exception as e:  # keep the daemon alive whatever a provider does
            print("refresh error:", type(e).__name__, e, file=sys.stderr)
        time.sleep(interval)


def start_workers(state):
    def claude():
        card = claude_usage.fetch(read_config()["claude_config_dir"] or None)
        if card:
            with state.lock:
                state.claude = card

    def codex():
        if read_config()["display"] != "multi":
            return
        card = codex_usage.fetch()
        if card:
            with state.lock:
                state.codex = card

    def coins():
        cfg = read_config()
        rows = coins_mod.fetch(coins_mod.parse_coin_list(cfg["coins"]), cfg["vs"])
        if rows:
            with state.lock:
                state.coin_rows = rows

    watcher = agent_events.Watcher()

    def alerts():
        latest = watcher.poll()
        with state.lock:
            state.alert = latest

    for interval, fn in ((AI_INTERVAL, claude), (CODEX_INTERVAL, codex), (COIN_INTERVAL, coins), (3, alerts)):
        threading.Thread(target=_loop, args=(interval, fn), daemon=True).start()


def make_handler(state):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.split("?")[0] != "/payload":
                self.send_error(404)
                return
            body = json.dumps(build_payload(read_config(), *state.snapshot())).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass
    return Handler


def main():
    cfg = read_config()
    state = State()
    start_workers(state)
    server = ThreadingHTTPServer(("0.0.0.0", int(cfg["port"])), make_handler(state))
    print(f"ClawTicker daemon on port {cfg['port']} (display={cfg['display']})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
