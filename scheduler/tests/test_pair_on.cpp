// test_pair_on.cpp — host unit tests for the Phase-5 WarHog pair scheduler with
// PIG_FEAT_PAIR_SCHED == 1.
//
// test_all.cpp compiles the suite with every PIG_FEAT_* OFF, so the pair-scheduler
// ON path (per-channel dwell table + single-pass sweep) needs its own TU, same as
// test_dwell_on.cpp does for Phase 2.
//
// Build + run: scheduler/tests/run.sh
#define PIG_FEAT_PAIR_SCHED 1
#include "../pig_dwell.h"

#include <cstdio>
#include <cstring>

uint32_t g_test_millis = 0;   // backing store for the Arduino.h shim

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

// ---------------------------------------------------------------- per-channel dwell
static void test_pair_dwell() {
  printf("[pig_pair dwell table]\n");

  PigScanPlan hi24 = pig_scan_plan(6, PIG_PASS_FAST, 0);
  CHECK(!hi24.passive && hi24.dwell_ms == PIG_PAIR_DWELL_HI24_MS, "ch6 -> 70 ms active");
  CHECK(pig_scan_plan(1, PIG_PASS_FAST, 0).dwell_ms == 70, "ch1 -> 70 ms");
  CHECK(pig_scan_plan(11, PIG_PASS_FAST, 0).dwell_ms == 70, "ch11 -> 70 ms");

  PigScanPlan hi5 = pig_scan_plan(157, PIG_PASS_FAST, 0);
  CHECK(!hi5.passive && hi5.dwell_ms == PIG_PAIR_DWELL_HI5_MS, "ch157 -> 60 ms active");
  CHECK(pig_scan_plan(36, PIG_PASS_FAST, 0).dwell_ms == 60, "ch36 -> 60 ms");
  CHECK(pig_scan_plan(161, PIG_PASS_FAST, 0).dwell_ms == 60, "ch161 -> 60 ms");

  PigScanPlan low = pig_scan_plan(4, PIG_PASS_FAST, 0);
  CHECK(!low.passive && low.dwell_ms == PIG_PAIR_DWELL_LOW_MS, "ch4 -> 30 ms active");
  CHECK(pig_scan_plan(165, PIG_PASS_FAST, 0).dwell_ms == 30 &&
        !pig_scan_plan(165, PIG_PASS_FAST, 0).passive, "ch165 -> 30 ms active (non-DFS, low value)");

  PigScanPlan dfs = pig_scan_plan(52, PIG_PASS_FAST, 0);
  CHECK(dfs.passive && dfs.dwell_ms == PIG_PAIR_DWELL_PASV_MS, "ch52 (DFS) -> 120 ms passive");
  CHECK(pig_scan_plan(100, PIG_PASS_FAST, 0).passive, "ch100 (DFS) passive");
  CHECK(pig_scan_plan(144, PIG_PASS_FAST, 0).passive, "ch144 (DFS) passive");
  CHECK(pig_scan_plan(13, PIG_PASS_FAST, 0).passive &&
        pig_scan_plan(13, PIG_PASS_FAST, 0).dwell_ms == 120, "ch13 -> 120 ms passive");
}

// --------------------------------------------------------------- complementary lists
static void test_pair_lists() {
  printf("[pig_pair A/B lists]\n");
  CHECK(PIG_PAIR_LIST_A_N == 10 && PIG_PAIR_LIST_B_N == 10, "both role lists are 10 channels");

  // The two nodes on an antenna must scan disjoint channels — no overlap A∩B.
  int overlap = 0;
  for (uint8_t i = 0; i < PIG_PAIR_LIST_A_N; ++i)
    for (uint8_t j = 0; j < PIG_PAIR_LIST_B_N; ++j)
      if (PIG_PAIR_LIST_A[i] == PIG_PAIR_LIST_B[j]) overlap++;
  CHECK(overlap == 0, "A and B lists are disjoint (pair anti-overlap)");

  // High-value 2.4 GHz is split across the pair, not all on one node.
  bool aHas6 = false, bHas1 = false, bHas11 = false;
  for (uint8_t i = 0; i < PIG_PAIR_LIST_A_N; ++i) if (PIG_PAIR_LIST_A[i] == 6) aHas6 = true;
  for (uint8_t j = 0; j < PIG_PAIR_LIST_B_N; ++j) { if (PIG_PAIR_LIST_B[j]==1) bHas1=true; if (PIG_PAIR_LIST_B[j]==11) bHas11=true; }
  CHECK(aHas6 && bHas1 && bHas11, "2.4 high-value split: A=6, B=1+11");
}

// --------------------------------------------------------------- single-pass sweep
static void test_pair_sweep_single_pass() {
  printf("[pig_pair single-pass sweep]\n");
  // PAIR_SCHED: pig_revisit_count() is 0, so the sweep is one pass over the list,
  // each channel at its per-channel dwell — no yield-weighted revisit pass.
  CHECK(pig_revisit_count(10) == 0, "pair sched: no revisit pass");

  PigSweep sw;
  uint8_t list[4] = {6, 36, 4, 52};
  pig_sweep_begin(&sw, list, 4);
  uint8_t ch; PigScanPlan plan; int steps = 0;
  uint16_t dwells[8]; bool pass[8];
  while (pig_sweep_next(&sw, &ch, &plan)) {
    dwells[steps] = plan.dwell_ms; pass[steps] = plan.passive;
    pig_sweep_report(&sw, 3);   // pretend each returned 3 new BSSIDs
    steps++;
    if (steps > 7) break;       // guard against a runaway sweep
  }
  CHECK(steps == 4, "single pass visits each assigned channel exactly once");
  CHECK(dwells[0] == 70 && dwells[1] == 60 && dwells[2] == 30 && dwells[3] == 120,
        "per-channel dwell applied in the sweep (70/60/30/120)");
  CHECK(!pass[0] && !pass[1] && !pass[2] && pass[3], "DFS channel scanned passively");
}

int main() {
  printf("=== Phase-5 WarHog pair scheduler tests (PIG_FEAT_PAIR_SCHED=1) ===\n");
  test_pair_dwell();
  test_pair_lists();
  test_pair_sweep_single_pass();
  printf("-----------------------------------\n");
  printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
