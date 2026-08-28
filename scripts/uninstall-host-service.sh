#!/usr/bin/env bash
set -euo pipefail

LABEL="fyi.lunar.rp2040-sensor"
APP_DIR="$HOME/Library/Application Support/LunarRP2040Sensor"
INSTALLED_BINARY="$APP_DIR/lunar-sensor-bridge"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

/bin/launchctl bootout "$DOMAIN/$LABEL" >/dev/null 2>&1 || true
/bin/rm -f "$PLIST_PATH" "$INSTALLED_BINARY"
/bin/rmdir "$APP_DIR" >/dev/null 2>&1 || true

printf 'Uninstalled %s (logs were retained).\n' "$LABEL"

