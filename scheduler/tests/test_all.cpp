// test_all.cpp — host unit tests for the scheduler drop-in modules.
// Build + run: scheduler/tests/run.sh   (or see that script for the g++ line)
#include "../pig_proto.h"
#include "../bssid_cache.h"
#include "../lossy_ring.h"
#include "../yield_sched.h"
#include "../pig_metrics.h"
#include "../pig_replay.h"
#include "../pig_dwell.h"
#include "../pig_queues.h"

#include <cstdio>
#include <cassert>
#include <cstring>

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

  // PIG_MSG_ADMIN_EXT explicit-channel-list message
  CHECK(sizeof(pig_admin_ext_t) == 52, "admin-ext is 52B");
  CHECK(pig_admin_ext_len(0) == 12 && pig_admin_ext_len(11) == 23,
        "admin-ext wire length = 12B header + n channels");
  pig_admin_ext_t ae{};
  memcpy(ae.magic, "ENOW", 4); ae.type = PIG_MSG_ADMIN_EXT; ae.assign_ver = 7;
  ae.n = 3; ae.channels[0] = 1; ae.channels[1] = 6; ae.channels[2] = 11;
  const uint8_t* raw = (const uint8_t*)&ae;
  CHECK(raw[4] == PIG_MSG_ADMIN_EXT, "admin-ext type byte at offset 4 (legacy magic/type-prefix compatible)");
  const pig_admin_ext_t* p = (const pig_admin_ext_t*)raw;
  CHECK(p->n == 3 && p->channels[0] == 1 && p->channels[2] == 11 && p->assign_ver == 7,
        "admin-ext round-trips through raw bytes");
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

  // sched_rank: channels ordered by score (highest yield first)
  {
    uint8_t rchans[4] = {36, 40, 44, 48};
    ChanStat rstat[4];
    rstat[0].yield = 0.1f; rstat[1].yield = 5.0f; rstat[2].yield = 0.2f; rstat[3].yield = 2.0f;
    for (int i = 0; i < 4; ++i) rstat[i].last_scan_ms = now;
    uint8_t order[4];
    sched_rank(rstat, rchans, 4, now, cfg, order);
    CHECK(order[0] == 1, "sched_rank: highest-yield channel ranks first");
    CHECK(order[3] == 0, "sched_rank: lowest-yield channel ranks last");
  }

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

// ---------------------------------------------------------------- pig_metrics
static void test_metrics() {
  printf("[pig_metrics]\n");
  PigMetrics m; m.begin(0);

  for (int i = 0; i < 3; ++i) m.addUnique();
  for (int i = 0; i < 10; ++i) m.addGross();
  m.addMeshTxBytes(2120);
  CHECK(m.uniqTotal() == 3, "uniq_total counts NEW sightings");
  CHECK(m.meshTxBytes() == 2120, "mesh_tx_bytes accumulates");
  CHECK(m.uniqPerMin() == 3 && m.grossPerMin() == 10, "rates include the in-progress second");

  // format writes a tagged, non-empty line
  char buf[256];
  int n = m.format(buf, sizeof(buf), 1000);
  CHECK(n > 0 && strncmp(buf, "PIGM,", 5) == 0, "format emits a PIGM-tagged line");

  // trailing-minute window decays: after >60 s of silence the rates fall to 0
  for (uint32_t t = 2000; t <= 65000; t += 1000) { char b2[256]; m.format(b2, sizeof(b2), t); }
  CHECK(m.uniqPerMin() == 0 && m.grossPerMin() == 0, "rate window decays to 0 after a minute idle");
  CHECK(m.uniqTotal() == 3, "uniq_total is cumulative, not windowed");

  // gross_total + supp_pct (Phase-1 dedup airtime-saved field)
  PigMetrics sp; sp.begin(0);
  for (int i = 0; i < 10; ++i) sp.addGross();
  for (int i = 0; i < 4;  ++i) sp.addSuppressed();
  CHECK(sp.grossTotal() == 10 && sp.suppressed() == 4, "gross/suppressed totals track");
  char sb[256]; sp.format(sb, sizeof(sb), 1000);
  CHECK(strstr(sb, ",10,40") != nullptr, "format appends gross_total,supp_pct (4/10 = 40%)");

  // pos-age percentiles: 100 rows all at 250 ms -> p50/p95 land in the 200-300 bucket
  PigMetrics g; g.begin(0);
  for (int i = 0; i < 100; ++i) g.addGps(3, 250);
  CHECK(g.posP50() == 300 && g.posP95() == 300, "percentile picks the containing bucket upper edge");
  // skewed: 95 rows @ 50 ms, 5 rows @ 1400 ms -> p50 low, p95 high
  PigMetrics g2; g2.begin(0);
  for (int i = 0; i < 95; ++i) g2.addGps(3, 50);
  for (int i = 0; i < 5;  ++i) g2.addGps(3, 1400);
  CHECK(g2.posP50() == 100, "p50 stays in the dense low bucket");
  CHECK(g2.posP95() >= 100 && g2.posP95() <= 1600, "p95 reflects the tail");
  PigMetrics g3; g3.begin(0);
  CHECK(g3.posP95() == 0, "no gps samples -> percentile 0");
}

// ---------------------------------------------------------------- pig_replay
static void test_replay() {
  printf("[pig_replay]\n");
  ReplayEvent ev;

  CHECK(!replay_parse("# comment", ev), "comment line skipped");
  CHECK(!replay_parse("   \n", ev), "blank line skipped");
  CHECK(!replay_parse("", ev), "empty line skipped");

  CHECK(replay_parse("1500,02:11:22:33:44:01,-57,6,2,4660", ev), "valid colon-mac line parses");
  CHECK(ev.t_ms == 1500, "t_ms parsed");
  CHECK(ev.bssid[0]==0x02 && ev.bssid[4]==0x44 && ev.bssid[5]==0x01, "bssid bytes parsed");
  CHECK(ev.rssi == -57, "signed rssi parsed");
  CHECK(ev.channel == 6 && ev.enc == 2 && ev.ssid_crc == 4660, "channel/enc/crc parsed");
  CHECK(ev.ssid_len == 0, "absent optional ssid_len defaults 0");

  CHECK(replay_parse("200,021122334402,-80,11,4,65535,7", ev), "no-colon mac + ssid_len parses");
  CHECK(ev.bssid[0]==0x02 && ev.bssid[5]==0x02, "colon-less mac parsed");
  CHECK(ev.ssid_crc == 65535 && ev.ssid_len == 7, "max crc + ssid_len parsed");

  CHECK(!replay_parse("1500,02:11:22:33:44,-57,6,2,4660", ev), "short mac (10 nibbles) rejected");
  CHECK(!replay_parse("1500,ZZ1122334401,-57,6,2,4660", ev), "non-hex mac rejected");
  CHECK(!replay_parse("1500,021122334401,-57,6", ev), "missing fields rejected");

  // end-to-end: a tiny corpus through the same cache the runner uses.
  // 3 distinct macs, each sighted twice back-to-back -> 3 unique, 3 suppressed.
  const char* corpus[] = {
    "0,02:11:22:33:44:01,-50,1,2,100",
    "1,02:11:22:33:44:02,-50,6,2,101",
    "2,02:11:22:33:44:03,-50,11,2,102",
    "3,02:11:22:33:44:01,-50,1,2,100",
    "4,02:11:22:33:44:02,-50,6,2,101",
    "5,02:11:22:33:44:03,-50,11,2,102",
  };
  BssidCache c; c.begin();
  const uint32_t TTL = 180000, REFRESH = 20000; const uint8_t RD = 8;
  int neu = 0, sup = 0;
  for (const char* ln : corpus) {
    CHECK(replay_parse(ln, ev), "corpus line parses");
    ObsClass k = c.classify(ev.bssid, ev.rssi, ev.channel, ev.ssid_crc, ev.enc,
                            ev.t_ms, TTL, REFRESH, RD);
    if (k == OBS_NEW) neu++;
    if (k == OBS_SUPPRESS) sup++;
  }
  CHECK(neu == 3, "replayed corpus yields 3 unique BSSIDs");
  CHECK(sup == 3, "immediate re-sightings suppressed (dedup win is measurable)");
}

// ---------------------------------------------------------------- pig_dwell (Phase 2)
static void test_dwell() {
  printf("[pig_dwell]\n");
  // DFS gating (ch 52..144 passive-only; compliance invariant)
  CHECK(!pig_is_dfs(6) && !pig_is_dfs(36), "2.4 + non-DFS 5GHz are not DFS");
  CHECK(pig_is_dfs(52) && pig_is_dfs(100) && pig_is_dfs(144), "ch 52..144 are DFS");
  CHECK(!pig_is_dfs(149) && !pig_is_dfs(165), "ch >=149 (U-NII-3) not DFS");

  // fast pass: non-DFS active, DFS passive, short dwell
  PigScanPlan f6  = pig_scan_plan_adaptive(6,  PIG_PASS_FAST, 0);
  CHECK(!f6.passive && f6.dwell_ms == PIG_DWELL_FAST_MS, "fast pass ch6 = active + fast dwell");
  PigScanPlan f52 = pig_scan_plan_adaptive(52, PIG_PASS_FAST, 0);
  CHECK(f52.passive && f52.dwell_ms == PIG_DWELL_FAST_MS, "fast pass DFS ch52 = passive");

  // revisit: always passive; dwell scales with yield, clamped to max
  PigScanPlan r0 = pig_scan_plan_adaptive(6, PIG_PASS_REVISIT, 0);
  CHECK(r0.passive && r0.dwell_ms == PIG_DWELL_REVISIT_MIN_MS, "revisit yield0 = floor, passive");
  PigScanPlan r5 = pig_scan_plan_adaptive(6, PIG_PASS_REVISIT, 5);
  CHECK(r5.dwell_ms == PIG_DWELL_REVISIT_MIN_MS + 5 * PIG_DWELL_REVISIT_PER_YIELD_MS,
        "revisit dwell scales with yield");
  PigScanPlan rBig = pig_scan_plan_adaptive(6, PIG_PASS_REVISIT, 100);
  CHECK(rBig.dwell_ms == PIG_DWELL_REVISIT_MAX_MS, "revisit dwell clamps to max");

  // flag-gated wrapper: default host build has the flag OFF -> legacy behavior
  PigScanPlan leg = pig_scan_plan(6, PIG_PASS_FAST, 0);
  CHECK(!leg.passive && leg.dwell_ms == PIG_DWELL_LEGACY_MS, "flag OFF -> legacy active fixed dwell");
  CHECK(pig_revisit_count(10) == 0, "flag OFF -> no revisit phase");
}

// ---------------------------------------------------------------- pig_queues (Phase 4 groundwork)
static void test_queues() {
  printf("[pig_queues]\n");
  // qObs: lossy RT->nonRT; DELTA dropped before FULL under backpressure
  PigObsQueue q;
  pig_obs_t o{};
  bool allFull = true;
  for (int i = 0; i < 120; ++i) if (!q.push(o, true)) allFull = false;
  CHECK(allFull, "qObs: 120 FULLs accepted below capacity");
  CHECK(!q.push(o, false), "qObs: DELTA refused at >=80% fill");
  CHECK(q.push(o, true), "qObs: FULL still accepted at high fill (unique metric protected)");

  // qGps: latest-wins mailbox
  PigMailbox<int> mb;
  int v;
  CHECK(!mb.get(v), "qGps mailbox: empty -> get() false");
  mb.set(5); mb.set(9);
  CHECK(mb.get(v) && v == 9, "qGps mailbox: latest-wins");
  uint32_t s1 = mb.seq(); mb.set(1);
  CHECK(mb.seq() == s1 + 1, "qGps mailbox: seq bumps on set (reader can detect new)");

  // qCmd: reliable ring — reject when full, never overwrite, FIFO
  ReliableRing<int, 4> rr;
  bool allPushed = true;
  for (int i = 0; i < 4; ++i) if (!rr.push(i)) allPushed = false;
  CHECK(allPushed, "qCmd: pushes under capacity ok");
  CHECK(!rr.push(99) && rr.rejects() >= 1, "qCmd: reject when full (command never dropped)");
  int x;
  CHECK(rr.pop(x) && x == 0, "qCmd: FIFO — oldest command kept, not overwritten");
}

int main() {
  printf("=== scheduler module unit tests ===\n");
  test_crc();
  test_proto();
  test_cache();
  test_ring();
  test_sched();
  test_metrics();
  test_replay();
  test_dwell();
  test_queues();
  printf("-----------------------------------\n");
  printf("%d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
