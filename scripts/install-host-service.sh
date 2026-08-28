#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_BINARY="${BINARY:-$PROJECT_DIR/build-host/lunar-sensor-bridge}"
LABEL="fyi.lunar.rp2040-sensor"
APP_DIR="$HOME/Library/Application Support/LunarRP2040Sensor"
LOG_DIR="$HOME/Library/Logs/LunarRP2040Sensor"
AGENT_DIR="$HOME/Library/LaunchAgents"
INSTALLED_BINARY="$APP_DIR/lunar-sensor-bridge"
PLIST_PATH="$AGENT_DIR/$LABEL.plist"
TEMPLATE="$PROJECT_DIR/packaging/macos/$LABEL.plist"
DOMAIN="gui/$(id -u)"

if [[ ! -x "$SOURCE_BINARY" ]]; then
  "$PROJECT_DIR/scripts/build-host.sh"
fi

mkdir -p "$APP_DIR" "$LOG_DIR" "$AGENT_DIR"
/usr/bin/install -m 0755 "$SOURCE_BINARY" "$INSTALLED_BINARY"
/bin/cp "$TEMPLATE" "$PLIST_PATH"
/usr/libexec/PlistBuddy -c "Set :ProgramArguments:0 $INSTALLED_BINARY" "$PLIST_PATH"
/usr/libexec/PlistBuddy -c "Set :StandardOutPath $LOG_DIR/bridge.log" "$PLIST_PATH"
/usr/libexec/PlistBuddy -c "Set :StandardErrorPath $LOG_DIR/bridge.log" "$PLIST_PATH"
/usr/bin/plutil -lint "$PLIST_PATH" >/dev/null

/bin/launchctl bootout "$DOMAIN/$LABEL" >/dev/null 2>&1 || true
/bin/launchctl bootstrap "$DOMAIN" "$PLIST_PATH"
/bin/launchctl enable "$DOMAIN/$LABEL"
/bin/launchctl kickstart -k "$DOMAIN/$LABEL"

printf 'Installed and started %s\n' "$LABEL"
printf 'Binary: %s\nLog: %s/bridge.log\n' "$INSTALLED_BINARY" "$LOG_DIR"
