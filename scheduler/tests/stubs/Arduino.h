// Arduino.h — HOST TEST SHIM (not for firmware; selected via -Ischeduler/tests/stubs)
// Provides the minimal surface the drop-in headers touch so they compile and run
// natively under g++/clang. millis() is driven by a test-controllable global.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>

extern uint32_t g_test_millis;
static inline uint32_t millis() { return g_test_millis; }
static inline void     test_set_millis(uint32_t v) { g_test_millis = v; }
static inline void     test_advance_millis(uint32_t d) { g_test_millis += d; }
