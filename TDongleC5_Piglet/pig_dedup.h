// pig_dedup.h — DROP-IN shared across all Piglet node firmwares.
// Scheduler Phase 1 — worker-side BSSID dedup, flag-gated (default OFF).
//
// When PIG_FEAT_DEDUP=1, a rolling TTL cache classifies each scan result and
// reports which redundant repeats a node may SUPPRESS instead of re-sending to
// the Core every cycle — cutting mesh airtime. The TEXT wire format is unchanged,
// so legacy/Biscuit Cores are unaffected. OFF by default -> zero behavior change,
// zero RAM. Every board uses the SAME thresholds/logic via this one header.
//
// Usage in a node's scan-complete send loop:
//     for (int i = 0; i < n; i++) {
//       if (pigDedupSkip(i)) continue;   // (optional: report to metrics first)
//       ... build + send record ...
//     }
// Requires the scheduler headers on the include path (pio: -I to scheduler;
// Arduino: copy bssid_cache.h + pig_proto.h + this file into the sketch folder).
#pragma once
#include <Arduino.h>

#ifndef PIG_FEAT_DEDUP
#define PIG_FEAT_DEDUP 0
#endif

#if PIG_FEAT_DEDUP
#include <WiFi.h>
#include "bssid_cache.h"
#include "pig_proto.h"   // crc16_ccitt

// DESIGN §6 thresholds (shared by every board for consistent behavior).
#ifndef PIG_DEDUP_TTL_MS
#define PIG_DEDUP_TTL_MS     180000u
#endif
#ifndef PIG_DEDUP_REFRESH_MS
#define PIG_DEDUP_REFRESH_MS 20000u
#endif
#ifndef PIG_DEDUP_RSSI_DELTA
#define PIG_DEDUP_RSSI_DELTA 8u
#endif

// True if WiFi scan result `i` is a redundant repeat within the refresh window
// (the caller should skip sending it). One shared cache across the firmware.
inline bool pigDedupSkip(int i) {
  static BssidCache cache;      // function-local static -> single shared instance,
                               // zero-initialized == begin()
  const uint8_t* bm = WiFi.BSSID(i);
  if (!bm) return false;
  String s = WiFi.SSID(i);
  uint16_t crc = crc16_ccitt((const uint8_t*)s.c_str(), s.length());
  return cache.classify(bm, (int8_t)WiFi.RSSI(i), (uint8_t)WiFi.channel(i), crc,
                        (uint8_t)WiFi.encryptionType(i), millis(),
                        PIG_DEDUP_TTL_MS, PIG_DEDUP_REFRESH_MS, PIG_DEDUP_RSSI_DELTA)
         == OBS_SUPPRESS;
}
#else
inline bool pigDedupSkip(int) { return false; }  // compiled out -> no-op, no RAM
#endif
