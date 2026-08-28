#!/usr/bin/env bash
set -euo pipefail

defaults write fyi.lunar.Lunar sensorHostname 127.0.0.1
defaults write fyi.lunar.Lunar sensorPort 4765
defaults write fyi.lunar.Lunar sensorPathPrefix /

printf 'Lunar sensor endpoint configured as http://127.0.0.1:4765/. Restart Lunar if it is running.\n'
