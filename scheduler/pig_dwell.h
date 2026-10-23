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
// had any yield on the fast pass. Revisit at most the top ~half of productive
// channels (bounded to 8). Pure + always compiled so the host can test it.
static inline uint8_t pig_revisit_count_adaptive(uint8_t channels_with_yield) {
  uint8_t n = (uint8_t)((channels_with_yield + 1) / 2);
  return n > 8 ? 8 : n;
}
// Flag-gated wrapper: 0 when the feature is off so callers skip the whole
// revisit phase (sweep stays fast-only = legacy behavior).
static inline uint8_t pig_revisit_count(uint8_t channels_with_yield) {
#if PIG_FEAT_ADAPTIVE_DWELL
  return pig_revisit_count_adaptive(channels_with_yield);
#else
  (void)channels_with_yield;
  return 0;
#endif
}

// ---------------------------------------------------------------------------
//  Two-pass sweep FSM (DESIGN §9). Drives one worker's channel sweep: a FAST
//  active pass over all assigned channels builds a per-channel yield estimate,
//  then a PASSIVE REVISIT pass dwells deeper on the top-yield channels (depth
//  where APs actually are, without extra probe traffic). Pure + deterministic
//  -> host-testable. Firmware usage, per sweep:
//      PigSweep sw; pig_sweep_begin(&sw, channels, n);
//      uint8_t ch; PigScanPlan plan;
//      while (pig_sweep_next(&sw, &ch, &plan)) {
//        int newBssids = firmwareScanChannel(ch, plan);  // radio work
//        pig_sweep_report(&sw, newBssids);
//      }
//  With PIG_FEAT_ADAPTIVE_DWELL == 0, pig_revisit_count() returns 0 so the
//  sweep is FAST-only and every plan is the legacy active fixed dwell -> a
//  caller routed through this behaves exactly like today (safe to ship).
// ---------------------------------------------------------------------------
#ifndef PIG_SWEEP_MAXCH
#define PIG_SWEEP_MAXCH 40   // matches PIG_ADMIN_EXT_MAXCH (max channels a worker is assigned)
#endif

struct PigSweep {
  uint8_t  channels[PIG_SWEEP_MAXCH];
  uint16_t yield[PIG_SWEEP_MAXCH];        // new BSSIDs seen per channel on the fast pass
  uint8_t  revisit_order[PIG_SWEEP_MAXCH];
  uint8_t  n;            // channel count
  uint8_t  pass;         // PIG_PASS_FAST / PIG_PASS_REVISIT
  uint8_t  cursor;       // index within the current pass
  uint8_t  revisit_n;    // channels selected for the revisit pass
  bool     transitioned; // fast->revisit selection already computed
};

// Rank channel indices by fast-pass yield (descending; stable, tie-break by
// index ascending). Fills order[0..n-1]; returns how many channels had yield>0.
// Pure + always compiled (host-testable).
static inline uint8_t pig_sweep_rank(const uint16_t* yield, uint8_t n, uint8_t* order) {
  for (uint8_t i = 0; i < n; ++i) order[i] = i;
  for (uint8_t i = 0; i < n; ++i)
    for (uint8_t j = (uint8_t)(i + 1); j < n; ++j)
      if (yield[order[j]] > yield[order[i]]) { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
  uint8_t withYield = 0;
  for (uint8_t i = 0; i < n; ++i) if (yield[i] > 0) withYield++;
  return withYield;
}

static inline void pig_sweep_begin(PigSweep* s, const uint8_t* channels, uint8_t n) {
  if (n > PIG_SWEEP_MAXCH) n = PIG_SWEEP_MAXCH;
  s->n = n;
  for (uint8_t i = 0; i < n; ++i) { s->channels[i] = channels[i]; s->yield[i] = 0; }
  s->pass = PIG_PASS_FAST;
  s->cursor = 0;
  s->revisit_n = 0;
  s->transitioned = false;
}

// Select the revisit set: the top pig_revisit_count() channels by yield, never
// including a zero-yield channel. Flag OFF -> cap 0 -> no revisits.
static inline void pig_sweep_select_revisit(PigSweep* s) {
  uint8_t order[PIG_SWEEP_MAXCH];
  uint8_t withYield = pig_sweep_rank(s->yield, s->n, order);
  uint8_t cap = pig_revisit_count(withYield);
  uint8_t k = 0;
  for (uint8_t i = 0; i < s->n && k < cap; ++i) {
    if (s->yield[order[i]] == 0) break;   // sorted desc -> rest are zero too
    s->revisit_order[k++] = order[i];
  }
  s->revisit_n = k;
}

// Peek the next channel + scan plan. Returns false when the sweep is complete.
// Idempotent until pig_sweep_report() advances the cursor.
static inline bool pig_sweep_next(PigSweep* s, uint8_t* out_ch, PigScanPlan* out_plan) {
  if (s->pass == PIG_PASS_FAST) {
    if (s->cursor < s->n) {
      uint8_t ch = s->channels[s->cursor];
      *out_ch = ch;
      *out_plan = pig_scan_plan(ch, PIG_PASS_FAST, 0);
      return true;
    }
    if (!s->transitioned) {   // fast pass exhausted -> pick revisits, switch pass
      pig_sweep_select_revisit(s);
      s->transitioned = true;
      s->pass = PIG_PASS_REVISIT;
      s->cursor = 0;
    }
  }
  if (s->pass == PIG_PASS_REVISIT && s->cursor < s->revisit_n) {
    uint8_t idx = s->revisit_order[s->cursor];
    uint8_t ch = s->channels[idx];
    *out_ch = ch;
    *out_plan = pig_scan_plan(ch, PIG_PASS_REVISIT, s->yield[idx]);
    return true;
  }
  return false;
}

// Report how many NEW BSSIDs the channel just returned by pig_sweep_next()
// yielded, then advance to the next step.
static inline void pig_sweep_report(PigSweep* s, uint16_t new_bssids) {
  if (s->pass == PIG_PASS_FAST && s->cursor < s->n) s->yield[s->cursor] = new_bssids;
  s->cursor++;
}
