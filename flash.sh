#!/usr/bin/env bash
# Build and flash the firmware. Usage: ./flash.sh [serial-port]
set -euo pipefail
cd "$(dirname "$0")/firmware"
command -v pio >/dev/null || { echo "Install PlatformIO first: https://platformio.org/install/cli"; exit 1; }
if [ $# -ge 1 ]; then pio run -t upload --upload-port "$1"; else pio run -t upload; fi
