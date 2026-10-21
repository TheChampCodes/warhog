// bssid_cache.h — DROP-IN
// Rolling, fixed-capacity BSSID dedup cache with TTL. No heap churn, O(1) avg.
// 512 slots x 16 B ~= 8 KB. Thresholds are passed per-call so the scheduler can
// retune them live (e.g. speed-scaled TTL) without rebuilding the cache.
#pragma once
#include <Arduino.h>
#include <string.h>

#define BSSID_CACHE_BITS   9
#define BSSID_CACHE_SIZE   (1u << BSSID_CACHE_BITS)
#define BSSID_CACHE_MASK   (BSSID_CACHE_SIZE - 1)
#define BSSID_PROBE_MAX    8

typedef struct {               // 16 bytes
  uint8_t  bssid[6];
  int8_t   rssi;
  uint8_t  channel;
  uint16_t ssid_crc;
  uint32_t last_ms;
  uint8_t  enc;
  uint8_t  seen;               // saturating
} bssid_entry_t;

enum ObsClass : uint8_t { OBS_NEW, OBS_CHANGED, OBS_REPEAT, OBS_SUPPRESS };

class BssidCache {
public:
  void begin() { memset(tbl_, 0, sizeof(tbl_)); used_ = 0; }

  ObsClass classify(const uint8_t bssid[6], int8_t rssi, uint8_t ch,
                    uint16_t ssid_crc, uint8_t enc, uint32_t now,
                    uint32_t ttl_ms, uint32_t refresh_ms, uint8_t rssi_delta) {
    uint32_t h = hash(bssid) & BSSID_CACHE_MASK;
    int free_slot = -1, oldest = -1;
    uint32_t oldest_ms = 0xFFFFFFFFu;
    for (uint32_t p = 0; p < BSSID_PROBE_MAX; ++p) {
      uint32_t i = (h + p) & BSSID_CACHE_MASK;
      bssid_entry_t& e = tbl_[i];
      if (e.last_ms == 0 && e.seen == 0) {        // empty slot
        if (free_slot < 0) free_slot = (int)i;
        continue;
      }
      if (match(e.bssid, bssid)) {
        if ((uint32_t)(now - e.last_ms) >= ttl_ms) {   // expired -> fresh again
          reset(e, bssid, rssi, ch, ssid_crc, enc, now);
          return OBS_NEW;
        }
        bool changed = (e.ssid_crc != ssid_crc) || (e.enc != enc) ||
                       (abs((int)rssi - (int)e.rssi) >= (int)rssi_delta);
        uint32_t age = now - e.last_ms;
        e.rssi = rssi; e.channel = ch; e.last_ms = now ? now : 1;
        if (e.seen < 255) e.seen++;
        if (changed) { e.ssid_crc = ssid_crc; e.enc = enc; return OBS_CHANGED; }
        return (age >= refresh_ms) ? OBS_REPEAT : OBS_SUPPRESS;
      }
      if (e.last_ms < oldest_ms) { oldest_ms = e.last_ms; oldest = (int)i; }
    }
    int slot = (free_slot >= 0) ? free_slot : oldest;   // approx-LRU eviction
    reset(tbl_[slot], bssid, rssi, ch, ssid_crc, enc, now);
    if (free_slot >= 0 && used_ < BSSID_CACHE_SIZE) used_++;
    return OBS_NEW;
  }

  uint8_t  fillPercent() const { return (uint8_t)((used_ * 100u) / BSSID_CACHE_SIZE); }
  uint32_t used() const { return used_; }

private:
  bssid_entry_t tbl_[BSSID_CACHE_SIZE];
  uint32_t used_ = 0;

  static bool match(const uint8_t a[6], const uint8_t b[6]) { return memcmp(a, b, 6) == 0; }
  static uint32_t hash(const uint8_t b[6]) {            // FNV-1a over 6 bytes
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; ++i) { h ^= b[i]; h *= 16777619u; }
    return h ^ (h >> 15);
  }
  static void reset(bssid_entry_t& e, const uint8_t b[6], int8_t rssi, uint8_t ch,
                    uint16_t crc, uint8_t enc, uint32_t now) {
    memcpy(e.bssid, b, 6); e.rssi = rssi; e.channel = ch;
    e.ssid_crc = crc; e.enc = enc; e.last_ms = now ? now : 1; e.seen = 1;
  }
};
