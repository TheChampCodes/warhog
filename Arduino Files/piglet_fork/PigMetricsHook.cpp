// PigMetricsHook.cpp — see PigMetricsHook.h. Observe-only Phase-0 instrumentation.
#include "PigMetricsHook.h"

#if PIG_FEAT_METRICS
#include <string.h>
#include "pig_metrics.h"   // scheduler/ (via -I../../scheduler)
#include "pig_proto.h"     // crc16_ccitt
#include "bssid_cache.h"
#include "Globals.h"       // TinyGPSPlus gps; bool gpsHasFix;
#include "MeshNode.h"      // uint8_t coreNodeCount;

// Dedup thresholds mirror DESIGN §6 / the host tests. The cache here is
// MEASUREMENT-ONLY: it exists so uniq_bssid_total is meaningful at Phase 0; it
// does not gate, drop, or alter any transmitted record (that is Phase 1).
static const uint32_t PM_TTL_MS     = 180000;
static const uint32_t PM_REFRESH_MS = 20000;
static const uint8_t  PM_RSSI_DELTA = 8;

static PigMetrics s_m;
static BssidCache s_cache;
static bool       s_init = false;

static void ensureInit() {
  if (s_init) return;
  s_cache.begin();
  s_m.begin(millis());
  Serial.println(PigMetrics::header());
  s_init = true;
}

static bool parseMac(const char* s, uint8_t out[6]) {
  int nib = 0; uint8_t cur = 0;
  for (const char* p = s; *p; ++p) {
    char c = *p;
    if (c == ':' || c == '-') continue;
    int d;
    if      (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    cur = (uint8_t)((cur << 4) | d);
    if (((++nib) & 1) == 0) { out[(nib / 2) - 1] = cur; cur = 0; }
  }
  return nib == 12;
}

void pigMetricsOnScanResult(const uint8_t bssid[6], int8_t rssi, uint8_t ch,
                            const char* ssid, uint8_t enc) {
  ensureInit();
  if (!bssid) return;
  uint16_t crc = crc16_ccitt((const uint8_t*)(ssid ? ssid : ""),
                             ssid ? strlen(ssid) : 0);
  ObsClass c = s_cache.classify(bssid, rssi, ch, crc, enc, millis(),
                                PM_TTL_MS, PM_REFRESH_MS, PM_RSSI_DELTA);
  s_m.addGross();
  if (c == OBS_NEW) s_m.addUnique();
}

void pigMetricsOnRecordStr(const char* bssid, int rssi, int ch,
                           const char* ssid, const char* auth) {
  ensureInit();
  uint8_t mac[6];
  if (!bssid || !parseMac(bssid, mac)) return;
  uint16_t crc = crc16_ccitt((const uint8_t*)(ssid ? ssid : ""),
                             ssid ? strlen(ssid) : 0);
  // Stable per-auth-type code so an encryption change still reads as CHANGED.
  uint8_t enc = (uint8_t)crc16_ccitt((const uint8_t*)(auth ? auth : ""),
                                     auth ? strlen(auth) : 0);
  ObsClass c = s_cache.classify(mac, (int8_t)rssi, (uint8_t)ch, crc, enc, millis(),
                                PM_TTL_MS, PM_REFRESH_MS, PM_RSSI_DELTA);
  s_m.addGross();
  if (c == OBS_NEW) s_m.addUnique();
}

void pigMetricsOnMeshTx(uint32_t bytes) { ensureInit(); s_m.addMeshTxBytes(bytes); }

void pigMetricsTick() {
  ensureInit();
  uint32_t now = millis();
  if (!s_m.due(now)) return;

  // Sample GPS once per emit. fix_type: 0 none / 1 2D / 2 3D (this firmware has
  // no interp/dead-reckon yet, so 3/4 are unused at Phase 0).
  uint8_t fix = 0; uint32_t age = 0;
  if (gpsHasFix && gps.location.isValid()) {
    fix = gps.altitude.isValid() ? 2 : 1;
    age = gps.location.age();
  }
  s_m.addGps(fix, age);

  // qObs/qStore/sd_lat have no source until the Phase-4 queues exist -> 0 for now.
  s_m.setGauges(0, 0, 0.0f, s_cache.fillPercent(), coreNodeCount);

  char line[256];
  s_m.format(line, sizeof(line), now);
  Serial.println(line);
}

#else  // PIG_FEAT_METRICS == 0: compiled out, zero cost

void pigMetricsOnScanResult(const uint8_t*, int8_t, uint8_t, const char*, uint8_t) {}
void pigMetricsOnRecordStr(const char*, int, int, const char*, const char*) {}
void pigMetricsOnMeshTx(uint32_t) {}
void pigMetricsTick() {}

#endif
