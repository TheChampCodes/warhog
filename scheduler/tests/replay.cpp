// replay.cpp — host A/B runner for the scan-corpus replay harness (Phase 0).
// Feeds a captured corpus through the SAME input to two transmit models so the
// dedup win can be measured on identical data rather than live RF:
//
//   BASELINE : every sighting is (re)transmitted as a legacy TEXT record
//   DEDUP    : BssidCache classifies; only NEW/CHANGED (FULL) + REPEAT (DELTA)
//              are transmitted, SUPPRESS sends nothing
//
// It also emits the Phase-0 §13 metrics line (PigMetrics) once per simulated
// second for the BASELINE model, so the on-target line format can be eyeballed.
//
// Byte figures are a documented FIRST-ORDER airtime model (packetization only):
// records pack into 212 B batches at the per-DESIGN-§4 record densities below.
// Absolute on-wire bytes come from the on-target mesh_tx counter; this runner is
// for deterministic RELATIVE A/B.
//
// Usage:  replay [corpus.csv]        (reads stdin if no path given)
// Build:  see run.sh  /  g++ -std=c++17 -I stubs replay.cpp -o replay
#include "../pig_proto.h"
#include "../bssid_cache.h"
#include "../pig_metrics.h"
#include "../pig_replay.h"

#include <cstdio>
#include <cstdint>

uint32_t g_test_millis = 0;   // satisfies the Arduino.h shim (millis() unused here)

// packetization model (DESIGN §4): 212 B batch holds ~2 text / 7 full / 9 delta records
static const double TEXT_RECS_PER_PKT  = 2.0;
static const double FULL_RECS_PER_PKT  = 7.0;
static const double DELTA_RECS_PER_PKT = 9.0;
static const uint32_t PKT_BYTES        = 212;

static uint32_t pkt_bytes(double records, double per_pkt) {
  if (records <= 0) return 0;
  uint32_t pkts = (uint32_t)((records + per_pkt - 1) / per_pkt);  // ceil
  return pkts * PKT_BYTES;
}

int main(int argc, char** argv) {
  FILE* f = stdin;
  if (argc > 1) {
    f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "replay: cannot open %s\n", argv[1]); return 2; }
  }

  BssidCache truth;  truth.begin();   // unique-truth + dedup classifier (same cache)
  PigMetrics m;      m.begin(0);
  const uint32_t TTL = 180000, REFRESH = 20000; const uint8_t RD = 8;

  uint64_t gross = 0, uniq = 0, full = 0, delta = 0, suppress = 0, lines = 0;
  char buf[256], line[512];

  printf("# %s\n", PigMetrics::header());
  while (fgets(line, sizeof(line), f)) {
    ReplayEvent ev;
    if (!replay_parse(line, ev)) continue;
    lines++;

    ObsClass c = truth.classify(ev.bssid, ev.rssi, ev.channel, ev.ssid_crc, ev.enc,
                                ev.t_ms, TTL, REFRESH, RD);
    gross++;
    if (c == OBS_NEW) uniq++;
    switch (c) {
      case OBS_NEW: case OBS_CHANGED: full++;     break;
      case OBS_REPEAT:                 delta++;    break;
      case OBS_SUPPRESS:               suppress++; break;
    }

    // feed BASELINE model into the §13 line: every sighting a text record
    m.addGross();
    if (c == OBS_NEW) m.addUnique();
    m.addMeshTxBytes((uint32_t)(PKT_BYTES / TEXT_RECS_PER_PKT));  // ~per-record text cost
    m.setGauges(0, 0, 0.0f, truth.fillPercent(), 1);
    if (m.due(ev.t_ms)) { m.format(buf, sizeof(buf), ev.t_ms); printf("%s\n", buf); }
  }
  if (f != stdin) fclose(f);

  uint32_t base_bytes  = pkt_bytes((double)gross, TEXT_RECS_PER_PKT);
  uint32_t dedup_bytes = pkt_bytes((double)full, FULL_RECS_PER_PKT)
                       + pkt_bytes((double)delta, DELTA_RECS_PER_PKT);
  double base_bpu  = uniq ? (double)base_bytes  / (double)uniq : 0.0;
  double dedup_bpu = uniq ? (double)dedup_bytes / (double)uniq : 0.0;
  double reduction = base_bytes ? 100.0 * (1.0 - (double)dedup_bytes / (double)base_bytes) : 0.0;

  printf("\n=== replay A/B summary ===\n");
  printf("corpus events     : %llu (%llu lines parsed)\n",
         (unsigned long long)gross, (unsigned long long)lines);
  printf("unique BSSIDs      : %llu\n", (unsigned long long)uniq);
  printf("classify full/delta/suppress : %llu / %llu / %llu\n",
         (unsigned long long)full, (unsigned long long)delta, (unsigned long long)suppress);
  printf("baseline mesh bytes: %u  (%.1f B / unique)\n", base_bytes, base_bpu);
  printf("dedup   mesh bytes : %u  (%.1f B / unique)\n", dedup_bytes, dedup_bpu);
  printf("bytes/unique change: %+.1f%%  (target <= -40%%)\n", -reduction);
  return 0;
}
