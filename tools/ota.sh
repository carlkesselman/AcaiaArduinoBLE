#!/bin/sh
# Update the main firmware over WiFi: ask the board to restart into its
# recovery firmware, wait for it, then upload.
#
#   export SHOTSTOPPER_OTA_PASSWORD=<upload password>
#   tools/ota.sh [host]          host defaults to shotstopper.local
#
# The board only answers while its WiFi is on: for 5 minutes after power-on, or
# after 1 is written to FF14 over Bluetooth. If it is already in recovery
# (yellow blink), the first step is skipped.
set -e
HOST=${1:-shotstopper.local}
: "${SHOTSTOPPER_OTA_PASSWORD:?set SHOTSTOPPER_OTA_PASSWORD to the upload password}"
cd "$(dirname "$0")/.."

page() { curl -s -m 5 "http://$HOST/" || true; }

if page | grep -q "RECOVERY firmware"; then
  echo "board is already in recovery"
else
  echo "asking the board to restart into recovery..."
  curl -s -m 10 -u "ota:$SHOTSTOPPER_OTA_PASSWORD" -X POST "http://$HOST/recovery" || {
    echo "no answer from http://$HOST/ : is the board's WiFi on?"; exit 1; }
  i=0
  until page | grep -q "RECOVERY firmware"; do
    i=$((i + 1))
    if [ "$i" -gt 40 ]; then echo "the board did not come up in recovery"; exit 1; fi
    sleep 3
  done
  echo "board is in recovery"
fi
exec pio run -e micra_wifi -t upload --upload-port "$HOST"
