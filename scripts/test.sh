#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I StopWatchTourGuide/src tests/test.cpp \
  StopWatchTourGuide/src/Tour.cpp StopWatchTourGuide/src/RecordStore.cpp \
  StopWatchTourGuide/src/App.cpp StopWatchTourGuide/src/CourseEditor.cpp StopWatchTourGuide/src/ClockSync.cpp -o build/tour-tests
# LeakSanitizer cannot inspect processes in some sandbox/ptrace environments.
# ASan memory checks and UBSan stay enabled; opt into leak checks outside them.
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" ./build/tour-tests
