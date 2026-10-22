// yield_sched.h — REQUIRES INTEGRATION (host-testable: no Arduino/WiFi deps)
// Pure coordinator-side yield scoring + channel assignment + hysteresis.
// `now` is passed in (not millis()) so this compiles and unit-tests on a host.
// Wire it to the firmware by feeding gStat from yield heartbeats and mapping
// the per-node output channel lists into pig_admin_ext_t.
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdlib.h>

#ifndef PIG_MAX_NODES
#define PIG_MAX_NODES   12
#endif
#ifndef PIG_MAX_CH
#define PIG_MAX_CH      64
#endif

struct ChanStat {
  float    yield    = 0.0f;   // EWMA unique-new BSSIDs/sec
  float    density  = 0.0f;   // EWMA gross observations/sec
  uint32_t last_scan_ms = 0;
};

struct SchedCfg {
  float    alpha        = 0.30f;
  float    eps          = 0.05f;   // exploration floor (never-scanned stays eligible)
  float    starve_k     = 0.50f;
  uint32_t starve_ms    = 20000;
  uint16_t budget_ms    = 900;     // per-worker dwell budget per sweep
  uint16_t max_dwell_ms = 300;
  uint8_t  base_revisit = 110;
  float    density_cost_k = 6.0f;
  // hysteresis
  uint32_t assign_epoch_ms = 8000;
  float    churn_min       = 0.25f;
  float    benefit_min     = 0.15f; // require >15% total-score gain
};

static inline float sched_freshness(const ChanStat& s, uint32_t now, const SchedCfg& c) {
  uint32_t age = now - s.last_scan_ms;
  float a = (float)age / (float)c.starve_ms;
  if (a > 1.0f) a = 1.0f;
  return 1.0f + c.starve_k * a;
}
static inline float sched_band_weight(uint8_t ch) { return (ch <= 14) ? 1.15f : 1.0f; }

static inline float sched_score(const ChanStat& s, uint8_t ch, uint32_t now, const SchedCfg& c) {
  return (s.yield + c.eps) * sched_freshness(s, now, c) * sched_band_weight(ch)
         / sqrtf(1.0f + s.density);
}
static inline uint16_t sched_cost(const ChanStat& s, const SchedCfg& c) {
  float v = (float)c.base_revisit + c.density_cost_k * s.density;
  if (v > (float)c.max_dwell_ms) v = (float)c.max_dwell_ms;
  if (v < (float)c.base_revisit) v = (float)c.base_revisit;
  return (uint16_t)v;
}

// EWMA update from one yield heartbeat for channel index `idx`.
static inline void sched_on_yield(ChanStat* stat, uint8_t idx,
                                  uint8_t uniq, uint8_t gross, float dt_s,
                                  const SchedCfg& c) {
  float iy = (dt_s > 0) ? (float)uniq / dt_s : 0.0f;
  float id = (dt_s > 0) ? (float)gross / dt_s : 0.0f;
  stat[idx].yield   = c.alpha * iy + (1.0f - c.alpha) * stat[idx].yield;
  stat[idx].density = c.alpha * id + (1.0f - c.alpha) * stat[idx].density;
}

// Speed scaling of per-worker budget (stationary = deeper, highway = shallower).
static inline float sched_budget_scale(float v_mps) {
  if (v_mps < 0.5f) return 1.6f;
  if (v_mps > 8.0f) return 0.6f;
  return 1.0f;
}

// Yield-packed greedy assignment (first-fit-decreasing by score) with a coverage
// floor so every channel lands on at least one active worker when budget allows.
// Returns number of (channel,worker) placements. `stat[i]` corresponds to
// `channels[i]`. `out_ch[w][*]` / `out_n[w]` receive each worker's channel list.
// Sets stat[].last_scan_ms = now for every channel that got placed.
static inline int sched_assign(ChanStat* stat, const uint8_t* channels, int n_ch,
                               int n_nodes, const bool* node_active, float v_mps,
                               uint32_t now, const SchedCfg& c,
                               uint8_t out_ch[][PIG_MAX_CH], uint8_t* out_n) {
  uint16_t budget[PIG_MAX_NODES];
  float bscale = sched_budget_scale(v_mps);
  for (int w = 0; w < n_nodes; ++w) {
    budget[w] = (uint16_t)((float)c.budget_ms * bscale);
    out_n[w] = 0;
  }

  // order channel indices by score desc (small n_ch -> insertion-ish selection sort is fine)
  int order[PIG_MAX_CH];
  for (int i = 0; i < n_ch; ++i) order[i] = i;
  for (int i = 0; i < n_ch; ++i)
    for (int j = i + 1; j < n_ch; ++j)
      if (sched_score(stat[order[j]], channels[order[j]], now, c) >
          sched_score(stat[order[i]], channels[order[i]], now, c)) {
        int t = order[i]; order[i] = order[j]; order[j] = t;
      }

  int placed = 0;
  for (int k = 0; k < n_ch; ++k) {
    int idx = order[k];
    uint16_t cst = sched_cost(stat[idx], c);
    int best = -1;
    for (int w = 0; w < n_nodes; ++w) {
      if (!node_active[w]) continue;
      bool owns = false;
      for (int m = 0; m < out_n[w]; ++m) if (out_ch[w][m] == channels[idx]) { owns = true; break; }
      if (owns || budget[w] < cst || out_n[w] >= PIG_MAX_CH) continue;
      if (best < 0 || budget[w] > budget[best]) best = w;
    }
    if (best >= 0) {
      out_ch[best][out_n[best]++] = channels[idx];
      budget[best] -= cst;
      stat[idx].last_scan_ms = now;
      placed++;
    }
  }

  // coverage floor: any channel assigned nowhere -> worker with most budget
  for (int k = 0; k < n_ch; ++k) {
    int idx = order[k];
    bool anywhere = false;
    for (int w = 0; w < n_nodes && !anywhere; ++w)
      for (int m = 0; m < out_n[w]; ++m) if (out_ch[w][m] == channels[idx]) { anywhere = true; break; }
    if (anywhere) continue;
    int best = -1;
    for (int w = 0; w < n_nodes; ++w)
      if (node_active[w] && out_n[w] < PIG_MAX_CH && (best < 0 || budget[w] > budget[best])) best = w;
    if (best >= 0) { out_ch[best][out_n[best]++] = channels[idx]; stat[idx].last_scan_ms = now; placed++; }
  }
  return placed;
}

// Fill order[0..n_ch-1] with channel INDICES sorted by score (highest first).
// Host-testable; firmwares use it to assign productive channels first within a
// band (keeps band-awareness: caller passes only the channels of one band).
static inline void sched_rank(const ChanStat* stat, const uint8_t* channels, int n_ch,
                              uint32_t now, const SchedCfg& c, uint8_t* order) {
  for (int i = 0; i < n_ch; ++i) order[i] = (uint8_t)i;
  for (int i = 0; i < n_ch; ++i)
    for (int j = i + 1; j < n_ch; ++j)
      if (sched_score(stat[order[j]], channels[order[j]], now, c) >
          sched_score(stat[order[i]], channels[order[i]], now, c)) {
        uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
      }
}

// Jaccard distance between two channel sets (0 = identical, 1 = disjoint).
static inline float sched_jaccard(const uint8_t* a, uint8_t na, const uint8_t* b, uint8_t nb) {
  int inter = 0;
  for (int i = 0; i < na; ++i)
    for (int j = 0; j < nb; ++j) if (a[i] == b[j]) { inter++; break; }
  int uni = (int)na + (int)nb - inter;
  return uni ? 1.0f - (float)inter / (float)uni : 0.0f;
}

// Three-gate anti-thrash check. All three must pass to reassign.
static inline bool sched_should_reassign(uint32_t now, uint32_t last_assign_ms,
                                         float max_worker_churn,
                                         float new_total_score, float cur_total_score,
                                         const SchedCfg& c) {
  if (now - last_assign_ms < c.assign_epoch_ms) return false;             // A: time
  if (max_worker_churn <= c.churn_min) return false;                      // B: churn
  if (new_total_score <= cur_total_score * (1.0f + c.benefit_min)) return false; // C: benefit
  return true;
}
