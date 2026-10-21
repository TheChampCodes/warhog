// pig_metrics.h — DROP-IN (host-testable; no Serial dependency)
// Phase-0 instrumentation: the 1 Hz acquisition metrics line from DESIGN §13.
//
// Pure accountant. Feed it events from existing code paths (this adds no behavior
// change — it only *observes*), then once per second call due()/format() to emit
// one CSV line. `now` is always a parameter so this compiles and unit-tests on a
// host exactly like yield_sched.h — the firmware caller supplies millis() and does
// the Serial.println(buf); this header never touches the radio, SD, or UART.
//
// Emitted line (prefix "PIGM," so log scrapers can grep; fields in §13 order):
//   PIGM,t_ms,uniq_bssid_total,uniq_per_min,gross_per_min,mesh_tx_bytes,
//        mesh_tx_bytes_per_uniq,qObs_drops,qStore_drops,sd_lat_ewma_ms,
//        assign_count,reassign_rejected_by_gate,cache_fill_pct,nodes_active,
//        gps_fix_type_hist(f0:f1:f2:f3:f4),pos_age_ms_p50,pos_age_ms_p95
#pragma once
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

class PigMetrics {
public:
  static const uint32_t EMIT_MS     = 1000;  // 1 Hz
  static const int      RATE_WIN    = 60;     // trailing-minute window, 1 s buckets
  static const int      POS_BUCKETS = 16;     // 100 ms each; bucket 15 = >=1500 ms
  static const int      POS_W       = 100;

  static const char* header() {  // optional CSV header for log files
    return "PIGM,t_ms,uniq_bssid_total,uniq_per_min,gross_per_min,mesh_tx_bytes,"
           "mesh_tx_bytes_per_uniq,qObs_drops,qStore_drops,sd_lat_ewma_ms,"
           "assign_count,reassign_rejected_by_gate,cache_fill_pct,nodes_active,"
           "gps_fix_type_hist,pos_age_ms_p50,pos_age_ms_p95,gross_total,supp_pct";
  }

  void begin(uint32_t now) {
    memset(this, 0, sizeof(*this));
    last_emit_ms_ = now;
    cur_s_        = now / 1000;
    started_      = true;
  }

  // --- event hooks (cheap; call from existing scan/mesh/GPS paths) ----------
  void addUnique(uint32_t n = 1) { uniq_total_ += n; cur_u_ += n; }  // NEW BSSID
  void addGross (uint32_t n = 1) { cur_g_ += n; gross_total_ += n; } // any sighting
  void addSuppressed(uint32_t n = 1) { suppressed_total_ += n; }     // dedup skipped a repeat
  void addMeshTxBytes(uint32_t b) { mesh_tx_bytes_ += b; }           // bytes on the mesh
  void addAssign()           { assign_count_++; }
  void addReassignRejected() { reassign_rej_++; }
  void addGps(uint8_t fix_type, uint32_t pos_age_ms) {              // one positioned row
    if (fix_type < 5) gps_fix_hist_[fix_type]++;
    int idx = (int)(pos_age_ms / POS_W);
    if (idx >= POS_BUCKETS) idx = POS_BUCKETS - 1;
    pos_hist_[idx]++; pos_n_++;
  }

  // --- gauges sampled from existing state just before an emit ---------------
  void setGauges(uint32_t qobs_drops, uint32_t qstore_drops, float sd_lat_ewma_ms,
                 uint8_t cache_fill_pct, uint8_t nodes_active) {
    qobs_drops_ = qobs_drops; qstore_drops_ = qstore_drops;
    sd_lat_ewma_ = sd_lat_ewma_ms; cache_fill_ = cache_fill_pct; nodes_ = nodes_active;
  }

  // --- 1 Hz emit ------------------------------------------------------------
  bool due(uint32_t now) const { return (uint32_t)(now - last_emit_ms_) >= EMIT_MS; }

  // Write one §13 line into out[]; returns snprintf length. Rolls the rate window.
  int format(char* out, size_t cap, uint32_t now) {
    roll_(now);
    last_emit_ms_ = now;
    uint32_t upm = cur_u_, gpm = cur_g_;
    for (int i = 0; i < RATE_WIN; ++i) { upm += uring_[i]; gpm += gring_[i]; }
    double   bpu = uniq_total_ ? (double)mesh_tx_bytes_ / (double)uniq_total_ : 0.0;
    uint32_t p50 = pctile_(50), p95 = pctile_(95);
    // Airtime saved by dedup: suppressed sightings as a % of all sightings seen.
    unsigned supp_pct = gross_total_ ? (unsigned)((100ull * suppressed_total_) / gross_total_) : 0u;
    return snprintf(out, cap,
      "PIGM,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,%.1f,%lu,%lu,%u,%u,"
      "%lu:%lu:%lu:%lu:%lu,%lu,%lu,%lu,%u",
      (unsigned long)now, (unsigned long)uniq_total_, (unsigned long)upm,
      (unsigned long)gpm, (unsigned long)mesh_tx_bytes_, bpu,
      (unsigned long)qobs_drops_, (unsigned long)qstore_drops_, (double)sd_lat_ewma_,
      (unsigned long)assign_count_, (unsigned long)reassign_rej_,
      (unsigned)cache_fill_, (unsigned)nodes_,
      (unsigned long)gps_fix_hist_[0], (unsigned long)gps_fix_hist_[1],
      (unsigned long)gps_fix_hist_[2], (unsigned long)gps_fix_hist_[3],
      (unsigned long)gps_fix_hist_[4], (unsigned long)p50, (unsigned long)p95,
      (unsigned long)gross_total_, supp_pct);
  }

  // --- accessors (tests / UI snapshots) -------------------------------------
  uint32_t uniqTotal()   const { return uniq_total_; }
  uint32_t meshTxBytes() const { return mesh_tx_bytes_; }
  uint32_t uniqPerMin()  const { uint32_t s=cur_u_; for(int i=0;i<RATE_WIN;++i) s+=uring_[i]; return s; }
  uint32_t grossPerMin() const { uint32_t s=cur_g_; for(int i=0;i<RATE_WIN;++i) s+=gring_[i]; return s; }
  uint32_t posP50()      const { return pctile_(50); }
  uint32_t posP95()      const { return pctile_(95); }
  uint32_t grossTotal()  const { return gross_total_; }
  uint32_t suppressed()  const { return suppressed_total_; }

private:
  void roll_(uint32_t now) {               // close completed seconds into the ring
    uint32_t s = now / 1000;
    if (!started_) { started_ = true; cur_s_ = s; return; }
    while (s > cur_s_) {
      uint32_t idx = cur_s_ % RATE_WIN;
      uring_[idx] = cur_u_; gring_[idx] = cur_g_;
      cur_u_ = cur_g_ = 0;
      cur_s_++;
    }
  }
  uint32_t pctile_(int p) const {          // bucket upper-edge estimate, in ms
    if (pos_n_ == 0) return 0;
    uint32_t need = (uint32_t)(((uint64_t)pos_n_ * (uint32_t)p + 99) / 100);  // ceil
    uint32_t cum = 0;
    for (int i = 0; i < POS_BUCKETS; ++i) {
      cum += pos_hist_[i];
      if (cum >= need) return (uint32_t)((i + 1) * POS_W);
    }
    return (uint32_t)(POS_BUCKETS * POS_W);
  }

  // cumulative counters
  uint32_t uniq_total_, mesh_tx_bytes_, assign_count_, reassign_rej_;
  uint32_t gross_total_, suppressed_total_;
  uint32_t qobs_drops_, qstore_drops_;
  float    sd_lat_ewma_;
  uint8_t  cache_fill_, nodes_;
  uint32_t gps_fix_hist_[5];
  uint32_t pos_hist_[POS_BUCKETS];
  uint32_t pos_n_;
  // trailing-minute rate window
  uint32_t uring_[RATE_WIN], gring_[RATE_WIN];
  uint32_t cur_u_, cur_g_, cur_s_;
  uint32_t last_emit_ms_;
  bool     started_;
};
