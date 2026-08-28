#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"

: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to a Pico SDK checkout}"

cmake_args=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -G Ninja
  -DPICO_BOARD=waveshare_rp2040_zero
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
)

if [[ -n "${PICO_TOOLCHAIN_PATH:-}" ]]; then
  cmake_args+=("-DPICO_TOOLCHAIN_PATH=$PICO_TOOLCHAIN_PATH")
fi

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR"

printf 'Built %s/lunar_rp2040_zero.uf2\n' "$BUILD_DIR"
