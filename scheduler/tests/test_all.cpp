// test_all.cpp — host unit tests for the scheduler drop-in modules.
// Build + run: scheduler/tests/run.sh   (or see that script for the g++ line)
#include "../pig_proto.h"
#include "../bssid_cache.h"
#include "../lossy_ring.h"
#include "../yield_sched.h"

#include <cstdio>
#include <cassert>

uint32_t g_test_millis = 0;   // backing store for the Arduino.h shim

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
  } while (0)

static void mac(uint8_t out[6], uint8_t last) {
  out[0]=0x02; out[1]=0x11; out[2]=0x22; out[3]=0x33; out[4]=0x44; out[5]=last;
}

// ---------------------------------------------------------------- CRC16
static void test_crc() {
  printf("[crc16]\n");
  const char* s = "123456789";
  uint16_t c = crc16_ccitt((const uint8_t*)s, 9);
  CHECK(c == 0x29B1, "CRC-CCITT check value 0x29B1");
  CHECK(crc16_ccitt((const uint8_t*)"", 0) == 0xFFFF, "empty input -> init value");
}

// ---------------------------------------------------------------- wire sizes
static void test_proto() {
  printf("[proto]\n");
  CHECK(sizeof(pig_batch_t) == 212, "batch is 212B (Biscuit-safe)");
  CHECK(sizeof(pig_obs_t) == 22, "obs fixed part is 22B");
  // one batch payload holds 202 bytes -> >= 9 fixed-part deltas or ~7 full records.
  CHECK(202 / (int)sizeof(pig_obs_t) >= 9, "payload fits >=9 delta records");
}

// ---------------------------------------------------------------- BSSID cache
static void test_cache() {
  printf("[bssid_cache]\n");
  BssidCache cache; cache.begin();
  const uint32_t TTL = 180000, REFRESH = 20000; const uint8_t RD = 8;
  uint8_t m[6]; mac(m, 0x01);

  g_test_millis = 1000;
  ObsClass c1 = cache.classify(m,-40,6,0x1234,2, millis(), TTL,REFRESH,RD);
  CHECK(c1 == OBS_NEW, "first sighting -> NEW");

  test_advance_millis(500);
  ObsClass c2 = cache.classify(m,-41,6,0x1234,2, millis(), TTL,REFRESH,RD);
  CHECK(c2 == OBS_SUPPRESS, "immediate re-sight (small rssi delta) -> SUPPRESS");

  test_advance_millis(REFRESH);  // now 21500, age since last update >= refresh
  ObsClass c3 = cache.classify(m,-41,6,0x1234,2, millis(), TTL,REFRESH,RD);
  CHECK(c3 == OBS_REPEAT, "after refresh window -> REPEAT (delta)");

  test_advance_millis(100);
  ObsClass c4 = cache.classify(m,-20,6,0x1234,2, millis(), TTL,REFRESH,RD);  // rssi jump 21dB
  CHECK(c4 == OBS_CHANGED, "big rssi delta -> CHANGED");

  test_advance_millis(100);
  ObsClass c5 = cache.classify(m,-20,6,0x9999,2, millis(), TTL,REFRESH,RD);  // ssid crc change
  CHECK(c5 == OBS_CHANGED, "ssid crc change -> CHANGED");

  test_advance_millis(100);
  ObsClass c6 = cache.classify(m,-20,6,0x9999,4, millis(), TTL,REFRESH,RD);  // enc change
  CHECK(c6 == OBS_CHANGED, "enc change -> CHANGED");

  // TTL expiry -> treated as NEW again
  g_test_millis += TTL + 1;
  ObsClass c7 = cache.classify(m,-20,6,0x9999,4, millis(), TTL,REFRESH,RD);
  CHECK(c7 == OBS_NEW, "re-sight after TTL -> NEW");

  // fill behavior: insert many distinct BSSIDs, fill% must stay bounded <=100
  BssidCache big; big.begin(); g_test_millis = 10;
  int news = 0;
  for (int i = 0; i < 2000; ++i) {
    uint8_t b[6] = {0x02,0x11,0x22, (uint8_t)(i>>8), (uint8_t)(i & 0xff), (uint8_t)(i*7)};
    if (big.classify(b,-50,1,(uint16_t)i,2, millis(), TTL,REFRESH,RD) == OBS_NEW) news++;
    g_test_millis += 1;
  }
  CHECK(news == 2000, "all 2000 distinct macs classified NEW (eviction keeps serving)");
  CHECK(big.fillPercent() <= 100, "fill% bounded <=100 under overfill");
  CHECK(big.used() <= BSSID_CACHE_SIZE, "used never exceeds capacity");
}

// ---------------------------------------------------------------- lossy ring
static void test_ring() {
  printf("[lossy_ring]\n");
  LossyRing<int, 8> r;
  CHECK(r.empty(), "starts empty");
  for (int i = 0; i < 5; ++i) CHECK(r.push(i, true), "push full under capacity ok");
  int v;
  CHECK(r.pop(v) && v == 0, "FIFO order: first out is 0");

  // Fill to >=80% then verify deltas are refused but fulls still accepted.
  LossyRing<int, 10> r2;
  for (int i = 0; i < 8; ++i) r2.push(i, true);       // 8/10 = 80%
  bool delta_ok = r2.push(999, false);
  CHECK(!delta_ok && r2.drops() >= 1, "delta refused at >=80% fill");
  bool full_ok = r2.push(1000, true);
  CHECK(full_ok, "full still accepted at 80% fill");

  // Saturate with fulls -> overwrite oldest, drops increment.
  LossyRing<int, 4> r3;
  uint32_t d0 = r3.drops();
  for (int i = 0; i < 10; ++i) r3.push(i, true);
  CHECK(r3.drops() > d0, "overwrite-oldest increments drops when saturated");
  CHECK(r3.size() <= 4, "size never exceeds N");
}

// ---------------------------------------------------------------- yield scheduler
static void test_sched() {
  printf("[yield_sched]\n");
  SchedCfg cfg;
  // 6-channel toy table (2.4GHz) ; stat parallel to channels[]
  uint8_t channels[6] = {1, 6, 11, 2, 7, 13};
  ChanStat stat[6];
  uint32_t now = 100000;

  // Give ch index 1 (channel 6) high yield, index 0 (channel 1) low.
  stat[1].yield = 5.0f; stat[1].last_scan_ms = now;
  stat[0].yield = 0.1f; stat[0].last_scan_ms = now;
  float s_hi = sched_score(stat[1], channels[1], now, cfg);
  float s_lo = sched_score(stat[0], channels[0], now, cfg);
  CHECK(s_hi > s_lo, "higher yield -> higher score");

  // Freshness floor: a stale, zero-yield channel outscores a just-scanned one
  // once it has starved long enough.
  ChanStat fresh, stale;
  fresh.yield = 0.2f; fresh.last_scan_ms = now;
  stale.yield = 0.2f; stale.last_scan_ms = now - cfg.starve_ms;  // fully starved
  CHECK(sched_score(stale, 6, now, cfg) > sched_score(fresh, 6, now, cfg),
        "starved channel gains priority via freshness");

  // density penalty: more gross/noise, same yield -> lower score
  ChanStat quiet, noisy;
  quiet.yield = 1.0f; quiet.density = 0.0f; quiet.last_scan_ms = now;
  noisy.yield = 1.0f; noisy.density = 50.0f; noisy.last_scan_ms = now;
  CHECK(sched_score(quiet, 6, now, cfg) > sched_score(noisy, 6, now, cfg),
        "density penalizes noisy-but-no-new channels");

  // cost is clamped to [base_revisit, max_dwell]
  ChanStat hd; hd.density = 1000.0f;
  CHECK(sched_cost(hd, cfg) == cfg.max_dwell_ms, "cost clamps to max_dwell");
  ChanStat zd; zd.density = 0.0f;
  CHECK(sched_cost(zd, cfg) == cfg.base_revisit, "cost floors at base_revisit");

  // assignment: coverage floor -> every channel placed at least once (2 nodes, ample budget)
  bool active[2] = {true, true};
  uint8_t out_ch[PIG_MAX_NODES][PIG_MAX_CH];
  uint8_t out_n[PIG_MAX_NODES];
  int placed = sched_assign(stat, channels, 6, 2, active, 2.0f, now, cfg, out_ch, out_n);
  CHECK(placed >= 6, "all channels covered at least once");
  // each channel appears somewhere
  for (int ci = 0; ci < 6; ++ci) {
    bool found = false;
    for (int w = 0; w < 2 && !found; ++w)
      for (int m = 0; m < out_n[w]; ++m) if (out_ch[w][m] == channels[ci]) found = true;
    CHECK(found, "channel present in some worker's list");
  }
  // no worker holds a duplicate channel
  for (int w = 0; w < 2; ++w)
    for (int a = 0; a < out_n[w]; ++a)
      for (int b = a + 1; b < out_n[w]; ++b)
        CHECK(out_ch[w][a] != out_ch[w][b], "no duplicate channel within a worker");

  // budget scaling by speed
  CHECK(sched_budget_scale(0.0f) > 1.0f, "stationary -> larger budget");
  CHECK(sched_budget_scale(20.0f) < 1.0f, "highway -> smaller budget");

  // jaccard
  uint8_t A[3] = {1,6,11}, B[3] = {1,6,11}, C[3] = {2,7,13};
  CHECK(sched_jaccard(A,3,B,3) == 0.0f, "identical sets -> 0 distance");
  CHECK(sched_jaccard(A,3,C,3) == 1.0f, "disjoint sets -> 1 distance");

  // hysteresis gates
  // A: too soon
  CHECK(!sched_should_reassign(1000, 0, 1.0f, 100.0f, 10.0f, cfg), "time gate blocks early reassign");
  // B: churn too small
  CHECK(!sched_should_reassign(100000, 0, 0.1f, 100.0f, 10.0f, cfg), "churn gate blocks tiny reshuffle");
  // C: benefit too small (needs >15%)
  CHECK(!sched_should_reassign(100000, 0, 1.0f, 100.0f, 95.0f, cfg), "benefit gate blocks marginal gain");
  // all pass
  CHECK(sched_should_reassign(100000, 0, 1.0f, 100.0f, 10.0f, cfg), "all gates pass -> reassign");
}

int main() {
  printf("=== scheduler module unit tests ===\n");
  test_crc();
  test_proto();
  test_cache();
  test_ring();
  test_sched();
  printf("-----------------------------------\n");
  printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
