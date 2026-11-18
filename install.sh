#!/usr/bin/env bash
# Install the ClawTicker daemon as a login service (macOS launchd or Linux systemd --user).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DAEMON="$ROOT/daemon/clawticker_daemon.py"
CONF_DIR="$HOME/.config/clawticker"
CONF="$CONF_DIR/config"
PY="$(command -v python3 || true)"

[ -n "$PY" ] || { echo "python3 not found. Install Python 3.9 or newer first."; exit 1; }
"$PY" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || { echo "Python 3.9 or newer is required."; exit 1; }

mkdir -p "$CONF_DIR"
if [ ! -f "$CONF" ]; then
  echo "What should the screen show?"
  echo "  1) Claude only"
  echo "  2) Claude + Codex side by side"
  read -r -p "Choose 1 or 2 [1]: " choice
  mode=claude; [ "${choice:-1}" = "2" ] && mode=multi
  cp "$ROOT/daemon/config.example" "$CONF"
  sed -i.bak "s/^display *=.*/display = $mode/" "$CONF" && rm -f "$CONF.bak"
fi

if [ "$(uname)" = "Darwin" ]; then
  LABEL=com.clawticker.daemon
  PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
  mkdir -p "$HOME/Library/LaunchAgents"
  cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$PY</string><string>$DAEMON</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardErrorPath</key><string>$CONF_DIR/daemon.log</string>
</dict></plist>
PL
  launchctl unload "$PLIST" 2>/dev/null || true
  launchctl load "$PLIST"
  IP="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo '?')"
else
  UNIT_DIR="$HOME/.config/systemd/user"
  mkdir -p "$UNIT_DIR"
  cat > "$UNIT_DIR/clawticker.service" <<UN
[Unit]
Description=ClawTicker daemon
[Service]
ExecStart=$PY $DAEMON
Restart=always
[Install]
WantedBy=default.target
UN
  systemctl --user daemon-reload
  systemctl --user enable --now clawticker.service
  IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
fi

PORT="$(sed -n 's/^port *= *//p' "$CONF" | head -1)"; PORT="${PORT:-8787}"
echo
echo "ClawTicker daemon is running. Config: $CONF"
echo "On the board's setup page, enter:  computer address ${IP:-?}   port $PORT"
