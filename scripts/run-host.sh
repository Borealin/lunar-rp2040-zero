#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${BINARY:-$PROJECT_DIR/build-host/lunar-sensor-bridge}"

if [[ ! -x "$BINARY" ]]; then
  "$PROJECT_DIR/scripts/build-host.sh"
fi

exec "$BINARY" "$@"
