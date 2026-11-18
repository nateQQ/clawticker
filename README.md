# ClawTicker

A small desk display for your AI coding usage and coin prices. An ESP32 board shows how much of your Claude (and optionally Codex) 5-hour and weekly limits you have used, plus a coin price, and updates over your home Wi-Fi.

```
Claude Code / Codex (your computer) ──> daemon ──HTTP on your LAN──> DIYMORE ES3C28P board
CoinGecko ──────────────────────────────┘
```

## Screens

| Screen | What it shows |
|---|---|
| Claude, or AI usage | Session (5h) and weekly (7d) usage, bars, time to reset. Pick one at install: **Claude only**, or **Claude + Codex** side by side |
| Coin | 1 to 4 coins with price and 24h change (default BTC) |
| Splash | A small animated mascot that gets nervous as your session fills up |
| Settings | Brightness, display mode, reset Wi-Fi setup |

Tap the screen to switch screens, tap the gear (bottom right) for Settings. The BOOT button also switches screens.

## What you need

- DIYMORE ES3C28P 2.8" ESP32-S3 touch display (the only supported board)
- A computer that stays on while you work (macOS or Linux), with Python 3.9+
- Claude Code logged in on that computer. For Codex, the Codex CLI used at least once
- [PlatformIO CLI](https://platformio.org/install/cli) to flash the board

## Setup

1. **Install the daemon** on your computer. It asks Claude only or Claude + Codex, then prints the address to use in step 3:
   ```sh
   ./install.sh
   ```
2. **Flash the board** (USB-C data cable):
   ```sh
   ./flash.sh
   ```
3. **Connect the board.** On first boot it shows a setup screen. Join the Wi-Fi `ClawTicker-Setup` on your phone, enter your home Wi-Fi, your computer's address and the port from step 1, then save.

Change what is shown later by editing `~/.config/clawticker/config` (see `daemon/config.example`). No restart needed.

## How it reads usage

- **Claude:** sends a one-token request with the OAuth token Claude Code already stored on your computer, and reads the plan utilization from the response headers. This costs a negligible amount of quota (one request every 2 minutes). It relies on API behavior that is not an official usage endpoint and may change.
- **Codex:** reads rate-limit info that the Codex CLI writes to `~/.codex/sessions`.
- **Coins:** CoinGecko public API, cached by the daemon.

Your token never leaves your computer except to `api.anthropic.com`. The daemon listens on your local network without a password, so only run it on a network you trust.

## Troubleshooting

- **Set up over USB instead of the phone portal:** open a serial monitor (115200) and type `status`, `wifi <ssid>|<password>`, `host <ip>`, `port <n>`, then `reset`.
- **Board shows "Waiting for data":** is the daemon running? `curl http://localhost:8787/payload` on your computer should return JSON.
- **Wrong Wi-Fi or computer address:** Settings, "Reset Wi-Fi setup" (tap twice), or hold the BOOT button for 5 seconds.
- **Claude card shows `--`:** log in to Claude Code on the computer, then wait up to 2 minutes.
- Daemon log on macOS: `~/.config/clawticker/daemon.log`.

## Development

```sh
python3 -I -m unittest discover -s daemon/tests   # daemon tests
cd firmware && pio run                              # build firmware
python3 tools/shot.py /dev/cu.usbmodemXXXX out.png  # screenshot of the board over USB (needs pyserial, pillow)
```

Payload format: [docs/payload.md](docs/payload.md).

## License

MIT, see [LICENSE](LICENSE). Third-party notices in [NOTICE.md](NOTICE.md). Not affiliated with Anthropic or OpenAI.
