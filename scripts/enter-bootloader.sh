#!/usr/bin/env bash
set -euo pipefail

SENSOR_URL="${SENSOR_URL:-http://127.0.0.1:4765}"

if curl --fail --silent --show-error --max-time 3 \
  -X POST "$SENSOR_URL/system/bootloader"; then
  printf '\nBootloader request accepted over HTTP.\n'
  exit 0
fi

printf 'CDC bridge reset unavailable; trying the USB vendor reset interface.\n' >&2
exec picotool reboot --vid 51966 --pid 16416 -f -u
