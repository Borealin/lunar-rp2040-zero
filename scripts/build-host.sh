#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$PROJECT_DIR/build-host}"
VERSION="${VERSION:-$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION")}"

mkdir -p "$OUTPUT_DIR"
cd "$PROJECT_DIR/host/lunar-sensor-bridge"
go build -trimpath -ldflags="-s -w -X main.bridgeVersion=$VERSION" \
  -o "$OUTPUT_DIR/lunar-sensor-bridge" .

printf 'Built %s/lunar-sensor-bridge (%s)\n' "$OUTPUT_DIR" "$VERSION"
