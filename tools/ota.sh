#!/usr/bin/env bash
# Build + flash over WiFi via /api/ota — the enclosure stays closed.
# Usage: tools/ota.sh [host]        default host: globo.local
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${1:-globo.local}"

# CDCOnBoot=default is not optional: this board's boards.txt hardcodes
# cdc_on_boot=1, and with the console on USB-Serial-JTAG inbound connections
# (mDNS, web remote — and /api/ota itself) go deaf. Building OTA firmware
# without this flag ships a device you can no longer reach over the air.
arduino-cli compile \
  --fqbn "esp32:esp32:lilygo_t_display_s3:CDCOnBoot=default" \
  --build-property "build.defines=-DBOARD_HAS_PSRAM" \
  --export-binaries \
  .

BIN="build/esp32.esp32.lilygo_t_display_s3/globo_lcd.ino.bin"
echo "[ota] uploading $BIN to $HOST ..."
curl --fail --no-progress-meter --max-time 300 \
  -F "file=@$BIN" "http://$HOST/api/ota"
echo
echo "[ota] done — device is rebooting"
