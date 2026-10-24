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

# The suite above compiles with every PIG_FEAT_* at its OFF default, so the
# flag-gated ON paths need their own TU. Phase 2 (adaptive dwell + the two-pass
# sweep) ships ON in the firmwares, so build and run its ON-path tests too.
ON="$(mktemp -d)/test_dwell_on"
"$CXX" -std=c++17 -Wall -Wextra -Wno-unused-parameter     -I stubs     test_dwell_on.cpp -o "$ON"

"$ON"

# Phase 5 (WarHog pair scheduler) also ships flag-gated; test its ON path.
PAIR="$(mktemp -d)/test_pair_on"
"$CXX" -std=c++17 -Wall -Wextra -Wno-unused-parameter     -I stubs     test_pair_on.cpp -o "$PAIR"

"$PAIR"

# Also build the Phase-0 A/B replay runner and smoke-test it on a tiny inline
# corpus (one AP sighted twice -> 1 unique, 1 suppressed).
REPLAY="$(mktemp -d)/replay"
"$CXX" -std=c++17 -Wall -Wextra -Wno-unused-parameter \
    -I stubs \
    replay.cpp -o "$REPLAY"
printf '0,02:11:22:33:44:01,-50,1,2,100\n1,02:11:22:33:44:01,-50,1,2,100\n' \
    | "$REPLAY" >/dev/null && echo "replay runner: built + smoke-tested OK"
