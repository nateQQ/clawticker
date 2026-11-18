import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import claude_usage, codex_usage, coins, agent_events  # noqa: E402
import clawticker_daemon as d  # noqa: E402


class ClaudeAccount(unittest.TestCase):
    def test_default_account_uses_plain_service(self):
        self.assertEqual(claude_usage.keychain_service(None), "Claude Code-credentials")
        self.assertEqual(claude_usage.keychain_service("~/.claude"), "Claude Code-credentials")

    def test_other_dir_gets_hashed_service(self):
        # matches how Claude Code names the entry for CLAUDE_CONFIG_DIR=/Users/natecue/.claude-dcn
        self.assertEqual(claude_usage.keychain_service("/Users/natecue/.claude-dcn"),
                         "Claude Code-credentials-e9524269")


class ClaudeHeaders(unittest.TestCase):
    def test_parse(self):
        h = {"anthropic-ratelimit-unified-5h-utilization": "0.45",
             "anthropic-ratelimit-unified-5h-reset": "1000600",
             "anthropic-ratelimit-unified-7d-utilization": "1.2",
             "anthropic-ratelimit-unified-7d-reset": "1086400"}
        c = claude_usage.parse_headers(h, now=1000000)
        self.assertEqual((c["s"], c["sr"], c["w"], c["wr"]), (45, 10, 100, 1440))

    def test_missing_headers(self):
        self.assertIsNone(claude_usage.parse_headers({}))


class CodexLimits(unittest.TestCase):
    def test_card(self):
        limits = {"primary": {"used_percent": 4.0, "resets_at": 1000300},
                  "secondary": {"used_percent": 20.4, "resets_at": 1003600}}
        c = codex_usage.card_from_limits(limits, now=1000000)
        self.assertEqual((c["s"], c["sr"], c["w"], c["wr"]), (4, 5, 20, 60))

    def test_expired_window_reads_as_zero(self):
        limits = {"primary": {"used_percent": 80, "resets_at": 999000}}
        c = codex_usage.card_from_limits(limits, now=1000000)
        self.assertEqual((c["s"], c["sr"]), (0, -1))

    def test_find_skips_noise(self):
        lines = ['{"payload":{"type":"x"}}', 'garbage "rate_limits"',
                 '{"payload":{"rate_limits":{"primary":{"used_percent":9}}}}']
        self.assertEqual(codex_usage._find_rate_limits(lines)["primary"]["used_percent"], 9)


class Coins(unittest.TestCase):
    def test_parse_list(self):
        self.assertEqual(coins.parse_coin_list("bitcoin:BTC, ethereum"),
                         [("bitcoin", "BTC"), ("ethereum", "ETHERE")])
        self.assertEqual(len(coins.parse_coin_list("a,b,c,d,e,f")), 4)


class Payload(unittest.TestCase):
    cfg = dict(d.DEFAULTS)
    card = {"n": "Claude", "s": 1, "sr": 2, "w": 3, "wr": 4}

    def test_claude_mode_has_one_card(self):
        p = d.build_payload(self.cfg, self.card, None, None, now=0)
        self.assertEqual([c["n"] for c in p["ai"]], ["Claude"])

    def test_multi_mode_has_two_cards_even_without_codex(self):
        cfg = dict(self.cfg, display="multi")
        p = d.build_payload(cfg, self.card, None, [{"s": "BTC", "p": 1.0, "c": 0}], now=0)
        self.assertEqual([c["n"] for c in p["ai"]], ["Claude", "Codex"])
        self.assertEqual(p["ai"][1]["off"], 1)

    def test_bad_display_falls_back(self):
        path = os.path.join(os.path.dirname(__file__), "bad.cfg")
        open(path, "w").write("display = everything\n")
        try:
            self.assertEqual(d.read_config(path)["display"], "claude")
        finally:
            os.remove(path)


class DisplayOverride(unittest.TestCase):
    def test_query_overrides_config(self):
        import threading, json as _json, urllib.request
        from unittest import mock
        patcher = mock.patch.object(d, "read_config", lambda: dict(d.DEFAULTS))   # ignore the user's real config
        patcher.start()
        self.addCleanup(patcher.stop)
        state = d.State()
        state.claude = {"n": "Claude", "s": 1, "sr": 2, "w": 3, "wr": 4}
        srv = d.ThreadingHTTPServer(("127.0.0.1", 0), d.make_handler(state))
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = "http://127.0.0.1:%d/payload" % srv.server_address[1]
        try:
            get = lambda q: _json.load(urllib.request.urlopen(base + q, timeout=5))
            self.assertEqual(get("?display=multi")["mode"], "multi")
            self.assertEqual(len(get("?display=multi")["ai"]), 2)
            self.assertEqual(get("?display=claude")["mode"], "claude")
            self.assertEqual(get("?display=bogus")["mode"], "claude")   # falls back to the config default
        finally:
            srv.shutdown()


class AgentEvents(unittest.TestCase):
    def test_claude_end_turn_vs_tool_use(self):
        done = '{"type":"assistant","uuid":"u1","message":{"stop_reason":"end_turn"}}'
        busy = '{"type":"assistant","uuid":"u2","message":{"stop_reason":"tool_use"}}'
        self.assertEqual(agent_events.claude_turn_end([done]), "u1")
        self.assertIsNone(agent_events.claude_turn_end([done, busy]))

    def test_claude_ignores_subagent(self):
        side = '{"type":"assistant","isSidechain":true,"uuid":"s","message":{"stop_reason":"end_turn"}}'
        self.assertIsNone(agent_events.claude_turn_end([side]))

    def test_codex_complete_then_new_turn(self):
        done = '{"type":"event_msg","timestamp":"T1","payload":{"type":"task_complete"}}'
        start = '{"type":"event_msg","payload":{"type":"task_started"}}'
        self.assertEqual(agent_events.codex_turn_end([start, done]), "T1")
        self.assertIsNone(agent_events.codex_turn_end([done, start]))

    def test_watcher_baseline_then_alert(self):
        import tempfile, json, time
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "s.jsonl")
            line = lambda u: json.dumps({"type": "assistant", "uuid": u, "message": {"stop_reason": "end_turn"}}) + "\n"
            open(p, "w").write(line("a"))
            w = agent_events.Watcher(claude_glob=os.path.join(tmp, "*.jsonl"), codex_glob=os.path.join(tmp, "none*.jsonl"))
            self.assertIsNone(w.poll())               # baseline, old finish is not replayed
            open(p, "a").write(line("b"))
            self.assertEqual(w.poll()["k"], "claude")
            self.assertEqual(w.poll()["i"], 1)        # same turn is not counted twice


if __name__ == "__main__":
    unittest.main()
