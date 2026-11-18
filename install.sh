#!/usr/bin/env bash
# ClawTicker daemon installer (macOS launchd, Linux systemd --user).
#   ./install.sh               install, ask what to show, start at login
#   ./install.sh --no-start    write config and service files but do not start anything
#   ./install.sh --uninstall   stop and remove the service (config is kept)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DAEMON="$ROOT/daemon/clawticker_daemon.py"
CONF_DIR="$HOME/.config/clawticker"
CONF="$CONF_DIR/config"
LABEL=com.clawticker.daemon
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
UNIT_DIR="$HOME/.config/systemd/user"
MODE="install"
for arg in "$@"; do
  case "$arg" in
    --uninstall) MODE="uninstall" ;;
    --no-start)  MODE="no-start" ;;
    -h|--help)   sed -n '2,5p' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

if [ "$MODE" = "uninstall" ]; then
  if [ "$(uname)" = "Darwin" ]; then
    launchctl unload "$PLIST" 2>/dev/null || true
    rm -f "$PLIST"
  else
    systemctl --user disable --now clawticker.service 2>/dev/null || true
    rm -f "$UNIT_DIR/clawticker.service"
    systemctl --user daemon-reload 2>/dev/null || true
  fi
  echo "ClawTicker daemon removed. Your config is still in $CONF_DIR (delete it if you like)."
  exit 0
fi

PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 not found. Install Python 3.9 or newer first."; exit 1; }
"$PY" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || { echo "Python 3.9 or newer is required."; exit 1; }

mkdir -p "$CONF_DIR"
if [ ! -f "$CONF" ]; then
  mode=claude
  if [ -t 0 ]; then
    echo "What should the screen show?"
    echo "  1) Claude only"
    echo "  2) Claude + Codex side by side"
    read -r -p "Choose 1 or 2 [1]: " choice
    [ "${choice:-1}" = "2" ] && mode=multi
  fi
  cp "$ROOT/daemon/config.example" "$CONF"
  sed -i.bak "s/^display *=.*/display = $mode/" "$CONF" && rm -f "$CONF.bak"
  echo "Wrote $CONF (display = $mode). You can also switch it later on the board: Settings > Display."
fi

if [ "$(uname)" = "Darwin" ]; then
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
  if [ "$MODE" = "install" ]; then
    launchctl unload "$PLIST" 2>/dev/null || true
    launchctl load "$PLIST"
  fi
  IP="$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo '?')"
else
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
  if [ "$MODE" = "install" ]; then
    systemctl --user daemon-reload
    systemctl --user enable --now clawticker.service
  fi
  IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
fi

PORT="$(sed -n 's/^port *= *//p' "$CONF" | head -1)"; PORT="${PORT:-8787}"
echo
if [ "$MODE" = "install" ]; then echo "ClawTicker daemon is running and will start at login."; else echo "Service files written, nothing started (--no-start)."; fi
echo "Config: $CONF"
echo
echo "On the board's setup page, enter:"
echo "  Computer address: ${IP:-?}"
echo "  Port:             $PORT"
echo "(If that address is wrong, find yours with 'ipconfig getifaddr en0' on macOS or 'hostname -I' on Linux.)"
echo "Check the daemon:  curl http://localhost:$PORT/payload"
echo "On macOS, click Allow if asked whether python3 may accept incoming connections."
