#!/usr/bin/env bash
# Host-compile and run the scheduler module unit tests.
# No ESP32 toolchain needed: the drop-in headers are compiled against a tiny
# Arduino.h shim in stubs/. This validates logic only, not on-target timing.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
OUT="$(mktemp -d)/test_all"

"$CXX" -std=c++17 -Wall -Wextra -Wno-unused-parameter \
    -I stubs \
    test_all.cpp -o "$OUT"

"$OUT"
