import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import claude_usage, codex_usage, coins  # noqa: E402
import clawticker_daemon as d  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
