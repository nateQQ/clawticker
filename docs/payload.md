# Payload schema

The board polls `GET http://<computer>:<port>/payload` every 5 seconds. Response is JSON:

```json
{
  "v": 1,
  "mode": "multi",
  "t": 1795026149,
  "ai": [
    {"n": "Claude", "s": 45, "sr": 37, "w": 45, "wr": 6277},
    {"n": "Codex",  "s": 4,  "sr": -1, "w": 17, "wr": -1, "off": 1}
  ],
  "coins": [{"s": "BTC", "p": 64176.0, "c": -0.28}],
  "al": {"i": 3, "k": "claude"}
}
```

| Key | Meaning |
|---|---|
| `v` | Schema version, currently 1. The board ignores other versions |
| `mode` | `claude` (one card) or `multi` (two cards) |
| `t` | Local wall-clock time in seconds (UTC epoch plus the computer's UTC offset) |
| `ai[]` | At most 2 cards. `s`/`w` are percent used in the 5-hour and 7-day windows, `sr`/`wr` minutes until reset (-1 unknown), `off: 1` provider not detected |
| `coins[]` | At most 4 rows: symbol, price, 24h change in percent |
| `al` | Optional. Last finished agent turn: `i` counter (the board shows the "finished" screen when it changes), `k` = `claude` or `codex`. The board ignores the value present at boot |
