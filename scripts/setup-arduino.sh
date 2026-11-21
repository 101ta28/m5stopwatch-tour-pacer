#!/usr/bin/env bash
# Run after installing Arduino CLI. All downloaded packages stay in this project.
set -euo pipefail
cd "$(dirname "$0")/.."
cli="${ARDUINO_CLI:-arduino-cli}"
command -v "$cli" >/dev/null
export ARDUINO_DIRECTORIES_DATA="$PWD/.arduino/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/.arduino/downloads"
export ARDUINO_DIRECTORIES_USER="$PWD/.arduino/user"
index='https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json'
"$cli" core update-index --additional-urls "$index"
"$cli" core install m5stack:esp32@3.3.7 --additional-urls "$index"
# Pin both libraries explicitly: dependency resolution would otherwise upgrade
# M5GFX to whichever release is newest at installation time.
"$cli" lib install --no-deps 'M5GFX@0.3.0' 'M5Unified@0.2.28'
