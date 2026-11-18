# Serial commands

Connect with any serial monitor at **115200 baud** (for example `pio device monitor`). Type a command and press Enter.

| Command | What it does |
|---|---|
| `status` | Wi-Fi name, computer address, Wi-Fi state, board IP, seconds since the last good poll, uptime, last reset reason code and free heap |
| `wifi <ssid>\|<password>` | Save Wi-Fi credentials (a vertical bar separates them). Then `reset` |
| `host <ip>` | Save the computer address. Then `reset` |
| `port <n>` | Save the daemon port. Then `reset` |
| `reset` | Restart the board |
| `portal` | Open the setup page on the board's own address without erasing anything. Saving restarts the board |
| `next` | Go to the next screen |
| `settings` | Open or close Settings |
| `tap <x> <y>` | Send a synthetic tap at screen coordinates (320x240) |
| `alert claude` / `alert codex` | Show the Finished screen |
| `shot` | Dump the current screen as raw RGB565 (used by `tools/shot.py`) |
| `perf` | Worst-case loop time, UI tick time and poll time since the last call |

Reset reason codes follow ESP-IDF `esp_reset_reason_t` (1 = power on, 3 = software reset, 4 = panic, 7 = watchdog, 9 = brownout).
