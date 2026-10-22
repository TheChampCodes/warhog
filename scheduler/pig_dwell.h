// pig_dwell.h — DROP-IN shared across all Piglet node firmwares.
// Scheduler Phase 2 — adaptive dwell / revisit scan policy, flag-gated (default OFF).
//
// DESIGN §9: a fast ACTIVE first pass surfaces hidden/low-beacon APs quickly and
// builds a per-channel yield estimate; then longer PASSIVE revisits are weighted
// to the high-yield channels (depth without extra probe traffic). DFS 5 GHz
// channels (52..144) are ALWAYS passive — active probing before radar detection
// is prohibited; that is a compliance invariant, kept out of the tunable path.
//
// When PIG_FEAT_ADAPTIVE_DWELL == 0, pig_scan_plan() returns the legacy fixed
// active scan, so a caller that routes its scan through it behaves identically to
// today (zero behavior change, safe to ship). The pure policy function
// pig_scan_plan_adaptive() is always compiled so it can be unit-tested on a host.
#pragma once
#include <Arduino.h>

#ifndef PIG_FEAT_ADAPTIVE_DWELL
#define PIG_FEAT_ADAPTIVE_DWELL 0
#endif

// Legacy fixed dwell — matches NODE_SCAN_DWELL_MS in the firmwares today.
#ifndef PIG_DWELL_LEGACY_MS
#define PIG_DWELL_LEGACY_MS 80u
#endif
// Adaptive timings (DESIGN §9).
#ifndef PIG_DWELL_FAST_MS
#define PIG_DWELL_FAST_MS 50u          // fast active first pass
#endif
#ifndef PIG_DWELL_REVISIT_MIN_MS
#define PIG_DWELL_REVISIT_MIN_MS 110u  // passive revisit floor
#endif
#ifndef PIG_DWELL_REVISIT_MAX_MS
#define PIG_DWELL_REVISIT_MAX_MS 300u  // passive revisit ceiling
#endif
#ifndef PIG_DWELL_REVISIT_PER_YIELD_MS
#define PIG_DWELL_REVISIT_PER_YIELD_MS 20u  // +ms of revisit dwell per new BSSID seen
#endif

enum PigScanPass : uint8_t { PIG_PASS_FAST = 0, PIG_PASS_REVISIT = 1 };

struct PigScanPlan {
  bool     passive;    // true -> passive scan (no probe requests)
  uint16_t dwell_ms;   // per-channel dwell for WiFi.scanNetworks
};

// DFS 5 GHz (U-NII-2/2e): ch 52..144 inclusive. Must be scanned passively.
static inline bool pig_is_dfs(uint8_t ch) { return ch >= 52 && ch <= 144; }

// Pure adaptive policy — ALWAYS compiled (host-testable).
//  - fast pass:   active (except DFS, which is always passive), short dwell
//  - revisit pass: passive, dwell = floor + perYield*yield, clamped to ceiling
static inline PigScanPlan pig_scan_plan_adaptive(uint8_t ch, uint8_t pass, uint16_t yield) {
  PigScanPlan p;
  if (pass == PIG_PASS_FAST) {
    p.passive  = pig_is_dfs(ch);
    p.dwell_ms = (uint16_t)PIG_DWELL_FAST_MS;
  } else {
    p.passive = true;
    uint32_t d = (uint32_t)PIG_DWELL_REVISIT_MIN_MS
               + (uint32_t)yield * (uint32_t)PIG_DWELL_REVISIT_PER_YIELD_MS;
    if (d > PIG_DWELL_REVISIT_MAX_MS) d = PIG_DWELL_REVISIT_MAX_MS;
    p.dwell_ms = (uint16_t)d;
  }
  return p;
}

// Flag-gated chooser the firmwares call. OFF -> legacy fixed active scan.
static inline PigScanPlan pig_scan_plan(uint8_t ch, uint8_t pass, uint16_t yield) {
#if PIG_FEAT_ADAPTIVE_DWELL
  return pig_scan_plan_adaptive(ch, pass, yield);
#else
  (void)pass; (void)yield;
  PigScanPlan p;
  p.passive  = false;                      // legacy: active on every channel
  p.dwell_ms = (uint16_t)PIG_DWELL_LEGACY_MS;
  return p;
#endif
}

// How many passive-revisit channels are worth doing this sweep, given how many
// had any yield on the fast pass. Keeps revisits bounded; 0 when the feature is
// off so callers skip the whole revisit phase.
static inline uint8_t pig_revisit_count(uint8_t channels_with_yield) {
#if PIG_FEAT_ADAPTIVE_DWELL
  // Revisit at most the top ~half of productive channels (bounded to 8).
  uint8_t n = (uint8_t)((channels_with_yield + 1) / 2);
  return n > 8 ? 8 : n;
#else
  (void)channels_with_yield;
  return 0;
#endif
}
