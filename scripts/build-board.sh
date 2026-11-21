#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
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
"$cli" board listall --format json > build/boards.json
fqbn=$(python3 - <<'PY'
import json
import sys
from pathlib import Path
inventory = json.loads(Path('build/boards.json').read_text())
boards = inventory.get('boards', [])
found = [b['fqbn'] for b in boards if b.get('name') == 'M5StopWatch' and b['fqbn'].startswith('m5stack:esp32:')]
if len(found) != 1:
    for warning in inventory.get('warnings', []):
        print(warning, file=sys.stderr)
    raise SystemExit('M5StopWatch が一意に見つかりません。M5Stack ボードパッケージを確認してください。')
print(found[0])
PY
)
{
  "$cli" version
  "$cli" core list
  "$cli" lib list
  echo "FQBN=$fqbn"
} > build/board-versions.txt
"$cli" compile --fqbn "$fqbn" --warnings all \
  --build-path "$PWD/build/arduino" --output-dir "$PWD/build/firmware" \
  StopWatchTourGuide 2>&1 | tee build/board-build.log
python3 scripts/verify-firmware.py | tee build/firmware-check.log
echo 'ビルドが完了しました。端末への書き込みは ./scripts/upload-board.sh <PORT> で実行してください。'
