#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# -ne 1 || -z "$1" || "$1" == -* ]]; then
  echo '使い方: ./scripts/upload-board.sh <PORT>' >&2
  echo '例: ./scripts/upload-board.sh /dev/ttyACM0' >&2
  exit 2
fi
if [[ -d .arduino/data ]]; then
  export ARDUINO_DIRECTORIES_DATA="$PWD/.arduino/data"
  export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/.arduino/downloads"
  export ARDUINO_DIRECTORIES_USER="$PWD/.arduino/user"
fi
cli="${ARDUINO_CLI:-arduino-cli}"
if ! command -v "$cli" >/dev/null 2>&1; then
  echo 'arduino-cli がありません。README.md の導入手順を確認してください。' >&2
  exit 127
fi
if [[ ! -f build/firmware/StopWatchTourGuide.ino.bin || ! -f build/arduino/build.options.json ]]; then
  echo '先に ./scripts/build-board.sh でファームウェアを生成してください。' >&2
  exit 1
fi
python3 scripts/verify-firmware.py
# Follow the same USB socket if Linux renumbers ttyACM after reset.
sync_port="$1"
for link in /dev/serial/by-path/*; do
  if [[ "$link" -ef "$1" ]]; then
    sync_port="$link"
    break
  fi
done
"$cli" upload --fqbn m5stack:esp32:m5stack_stopwatch \
  --port "$1" --protocol serial --input-dir "$PWD/build/firmware" \
  StopWatchTourGuide 2>&1 | tee build/board-upload.log
echo '書き込みが完了しました。PCの現地日時を端末に設定します。'
if ! python3 scripts/sync-clock.py "$sync_port"; then
  printf '書き込みは完了しています。時刻のみ再同期するには python3 scripts/sync-clock.py %q を実行してください。\n' "$sync_port" >&2
  exit 1
fi
