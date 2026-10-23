// test_dwell_on.cpp — host unit tests for the Phase-2 sweep FSM with
// PIG_FEAT_ADAPTIVE_DWELL == 1.
//
// test_all.cpp compiles the whole suite with the scheduler defaults (every
// PIG_FEAT_* OFF) and so can only assert the legacy fall-through. The flag is a
// compile-time #if, so the ON behavior — the passive revisit pass, its dwell
// scaling, and the DFS invariant — needs its own translation unit. The released
// firmwares build with the flag ON, so this is the path that actually ships.
//
// Build + run: scheduler/tests/run.sh   (or see that script for the g++ line)
#define PIG_FEAT_ADAPTIVE_DWELL 1
#include "../pig_dwell.h"

#include <cstdio>
#include <cstring>

uint32_t g_test_millis = 0;   // backing store for the Arduino.h shim

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

// ---------------------------------------------------------------- scan plan (ON)
static void test_plan_on() {
  printf("[pig_dwell ON]\n");

  PigScanPlan f = pig_scan_plan(6, PIG_PASS_FAST, 0);
  CHECK(!f.passive && f.dwell_ms == PIG_DWELL_FAST_MS, "ON: fast pass is active at the fast dwell");

  PigScanPlan dfs = pig_scan_plan(100, PIG_PASS_FAST, 0);
  CHECK(dfs.passive, "ON: DFS channel stays passive even on the active fast pass");

  PigScanPlan r0 = pig_scan_plan(6, PIG_PASS_REVISIT, 0);
  CHECK(r0.passive && r0.dwell_ms == PIG_DWELL_REVISIT_MIN_MS, "ON: revisit is passive at the floor dwell");

  PigScanPlan r3 = pig_scan_plan(6, PIG_PASS_REVISIT, 3);
  CHECK(r3.dwell_ms == PIG_DWELL_REVISIT_MIN_MS + 3 * PIG_DWELL_REVISIT_PER_YIELD_MS,
        "ON: revisit dwell scales with the fast-pass yield");
  CHECK(pig_scan_plan(6, PIG_PASS_REVISIT, 999).dwell_ms == PIG_DWELL_REVISIT_MAX_MS,
        "ON: revisit dwell clamps to the ceiling");

  CHECK(pig_revisit_count(4) == 2, "ON: revisit cap is the top ~half of the productive channels");
  CHECK(pig_revisit_count(0) == 0, "ON: nothing productive -> no revisit phase");
}

// ---------------------------------------------------------------- sweep FSM (ON)
static void test_sweep_on() {
  printf("[pig_sweep ON]\n");

  // 4 channels; fast-pass yields 2/5/0/1 -> 3 productive -> cap 2 -> revisit
  // ch 6 (yield 5) then ch 1 (yield 2). Zero-yield ch 11 is never revisited.
  const uint8_t  chans[4]  = {1, 6, 11, 36};
  const uint16_t yields[4] = {2, 5, 0, 1};

  PigSweep sw;
  pig_sweep_begin(&sw, chans, 4);

  uint8_t ch; PigScanPlan pl;
  bool fastOk = true;
  for (int i = 0; i < 4; ++i) {
    if (!pig_sweep_next(&sw, &ch, &pl)) { fastOk = false; break; }
    if (ch != chans[i] || pl.passive || pl.dwell_ms != PIG_DWELL_FAST_MS) fastOk = false;
    pig_sweep_report(&sw, yields[i]);
  }
  CHECK(fastOk, "sweep ON: fast pass visits every channel in order, active at the fast dwell");
  CHECK(sw.revisit_n == 0 && !sw.transitioned, "sweep ON: revisit set not chosen until the fast pass ends");

  // First revisit step: highest yield (ch 6, yield 5), passive, dwell scaled.
  CHECK(pig_sweep_next(&sw, &ch, &pl), "sweep ON: revisit pass has steps");
  CHECK(sw.pass == PIG_PASS_REVISIT && sw.revisit_n == 2,
        "sweep ON: 3 productive channels -> 2 revisits");
  CHECK(ch == 6 && pl.passive, "sweep ON: revisits the top-yield channel first, passively");
  CHECK(pl.dwell_ms == PIG_DWELL_REVISIT_MIN_MS + 5 * PIG_DWELL_REVISIT_PER_YIELD_MS,
        "sweep ON: revisit dwell comes from that channel's fast-pass yield");
  pig_sweep_report(&sw, 0);

  // Second revisit step: next highest (ch 1, yield 2).
  CHECK(pig_sweep_next(&sw, &ch, &pl) && ch == 1 && pl.passive,
        "sweep ON: second revisit is the next-highest-yield channel");
  CHECK(pl.dwell_ms == PIG_DWELL_REVISIT_MIN_MS + 2 * PIG_DWELL_REVISIT_PER_YIELD_MS,
        "sweep ON: second revisit dwell tracks its own yield");
  pig_sweep_report(&sw, 0);

  CHECK(!pig_sweep_next(&sw, &ch, &pl) && pig_sweep_done(&sw), "sweep ON: sweep ends after the revisit set");

  // A revisit report must not overwrite the fast-pass yield it was chosen from.
  CHECK(sw.yield[1] == 5, "sweep ON: a revisit's report never clobbers the fast-pass yield");

  // DFS stays passive on the fast pass, and a DFS channel can still be revisited.
  const uint8_t dfsCh[2] = {100, 6};
  PigSweep dsw;
  pig_sweep_begin(&dsw, dfsCh, 2);
  pig_sweep_next(&dsw, &ch, &pl);
  CHECK(ch == 100 && pl.passive, "sweep ON: DFS fast pass is passive (compliance invariant)");
  pig_sweep_report(&dsw, 4);
  pig_sweep_next(&dsw, &ch, &pl);
  pig_sweep_report(&dsw, 0);
  CHECK(pig_sweep_next(&dsw, &ch, &pl) && ch == 100 && pl.passive,
        "sweep ON: a productive DFS channel is revisited, still passive");

  // Zero yield everywhere -> no revisit phase at all (sweep is fast-only).
  PigSweep zsw;
  pig_sweep_begin(&zsw, chans, 4);
  for (int i = 0; i < 4; ++i) { pig_sweep_next(&zsw, &ch, &pl); pig_sweep_report(&zsw, 0); }
  CHECK(!pig_sweep_next(&zsw, &ch, &pl) && zsw.revisit_n == 0,
        "sweep ON: a barren fast pass skips the revisit phase entirely");

  // Revisit set is capped at 8 even when every channel is productive.
  uint8_t many[20];
  for (uint8_t i = 0; i < 20; ++i) many[i] = (uint8_t)(i + 1);
  PigSweep msw;
  pig_sweep_begin(&msw, many, 20);
  for (int i = 0; i < 20; ++i) { pig_sweep_next(&msw, &ch, &pl); pig_sweep_report(&msw, (uint16_t)(i + 1)); }
  CHECK(pig_sweep_next(&msw, &ch, &pl) && msw.revisit_n == 8, "sweep ON: revisit set clamps to 8 channels");
  CHECK(ch == 20, "sweep ON: revisit order starts at the single best channel");
}

// ---------------------------------------------------------------- cycle driver
static void test_driver() {
  printf("[pig_sweep driver]\n");

  PigSweep sw;                       // zero-init == "needs rebuild", like a static
  memset(&sw, 0, sizeof(sw));
  CHECK(!pig_sweep_valid(&sw) && pig_sweep_done(&sw),
        "driver: a zero-initialized sweep is invalid and done (firmware rebuilds it)");

  const uint8_t chans[3] = {1, 6, 11};
  pig_sweep_begin(&sw, chans, 3);
  CHECK(pig_sweep_valid(&sw) && !pig_sweep_done(&sw), "driver: begin() makes the sweep valid");
  pig_sweep_invalidate(&sw);
  CHECK(!pig_sweep_valid(&sw), "driver: invalidate() forces a rebuild next cycle");

  // report() on an invalidated sweep must not advance anything: a scan still in
  // flight when the Core re-assigns channels lands here.
  pig_sweep_report(&sw, 7);
  CHECK(sw.cursor == 0 && !pig_sweep_valid(&sw), "driver: report() on an invalidated sweep is a no-op");

  // Build from an explicit list (Phase 3 / Biscuit sparse plan).
  const uint8_t table[8] = {1, 6, 11, 36, 52, 100, 149, 165};
  const uint8_t expl[4]  = {1, 6, 0, 36};     // ch 0 = empty slot, must be dropped
  uint8_t out[PIG_SWEEP_MAXCH];
  uint8_t k = pig_sweep_build(out, expl, 4, table, 8, 0, 7, /*allow_5ghz*/true);
  CHECK(k == 3 && out[0] == 1 && out[1] == 6 && out[2] == 36,
        "build: explicit list wins, ch 0 slots dropped");

  k = pig_sweep_build(out, expl, 4, table, 8, 0, 7, /*allow_5ghz*/false);
  CHECK(k == 2 && out[0] == 1 && out[1] == 6, "build: 5 GHz dropped on 2.4-only silicon");

  // Build from a contiguous table slice when there is no explicit list.
  k = pig_sweep_build(out, nullptr, 0, table, 8, 1, 4, true);
  CHECK(k == 4 && out[0] == 6 && out[3] == 52, "build: contiguous table slice [start,end]");

  k = pig_sweep_build(out, nullptr, 0, table, 8, 1, 4, false);
  CHECK(k == 2 && out[0] == 6 && out[1] == 11, "build: slice drops 5 GHz on 2.4-only silicon");

  k = pig_sweep_build(out, nullptr, 0, table, 8, 6, 2, true);
  CHECK(k == 0, "build: end < start -> no channels (nothing assigned)");

  k = pig_sweep_build(out, nullptr, 0, table, 8, 5, 200, true);
  CHECK(k == 3, "build: slice is clamped to the table length");

  // An all-5 GHz assignment on 2.4-only silicon builds empty -> the firmware
  // must not begin a sweep (it would be instantly "done" and spin the cycle).
  const uint8_t only5[2] = {52, 100};
  CHECK(pig_sweep_build(out, only5, 2, table, 8, 0, 7, false) == 0,
        "build: unscannable assignment yields 0 channels, not a bogus sweep");
}

int main() {
  printf("=== Phase-2 adaptive dwell / sweep tests (PIG_FEAT_ADAPTIVE_DWELL=1) ===\n");
  test_plan_on();
  test_sweep_on();
  test_driver();
  printf("-----------------------------------\n");
  printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
