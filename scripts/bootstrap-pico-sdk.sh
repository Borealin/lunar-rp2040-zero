#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SDK_VERSION="${PICO_SDK_VERSION:-2.3.0}"
SDK_DIR="${PICO_SDK_DIR:-$PROJECT_DIR/.deps/pico-sdk}"

if [[ -f "$SDK_DIR/pico_sdk_init.cmake" ]]; then
  printf 'Pico SDK already exists at %s\n' "$SDK_DIR"
  exit 0
fi

if [[ -e "$SDK_DIR" ]]; then
  printf 'Refusing to replace non-SDK path: %s\n' "$SDK_DIR" >&2
  exit 1
fi

mkdir -p "$(dirname "$SDK_DIR")"
git clone --branch "$SDK_VERSION" --depth 1 --recurse-submodules \
  https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"

printf 'Installed Pico SDK %s at %s\n' "$SDK_VERSION" "$SDK_DIR"

