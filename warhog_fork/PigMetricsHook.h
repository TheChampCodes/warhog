// PigMetricsHook.h — Phase-0 instrumentation bridge (XIAO C5 Core).
//
// Thin glue between this firmware's existing scan / mesh / GPS paths and the
// host-tested scheduler/pig_metrics.h accountant. OBSERVE-ONLY: it reads what the
// firmware already produces and prints one 1 Hz "PIGM,..." line over Serial. It
// changes no scan, mesh, or storage behavior — nothing here alters a packet, a
// channel, or a record. Flip PIG_FEAT_METRICS to 0 to compile it out entirely.
//
// Requires the scheduler headers on the include path (platformio.ini adds
// -I../scheduler). See scheduler/DESIGN.md §13 and scheduler/HANDOFF.md.
#pragma once
#include <Arduino.h>

#ifndef PIG_FEAT_METRICS
#define PIG_FEAT_METRICS 1
#endif

// One sighting from the local async scan path (node role). `bssid` is 6 raw
// bytes (WiFi.BSSID(i)); `ssid` may be empty for hidden APs.
void pigMetricsOnScanResult(const uint8_t bssid[6], int8_t rssi, uint8_t ch,
                            const char* ssid, uint8_t enc);

// One record ingested from a worker over the mesh (Core role). `bssid` is the
// "AA:BB:CC:DD:EE:FF" string from the CSV record line.
void pigMetricsOnRecordStr(const char* bssid, int rssi, int ch,
                           const char* ssid, const char* auth);

// Bytes of a record/observation frame handed to the ESP-NOW driver (node role).
void pigMetricsOnMeshTx(uint32_t bytes);

// One sighting the dedup path suppressed (Phase 1) — for the supp_pct metric.
void pigMetricsOnSuppressed();

// Call once per loop(); prints the header on first call and a PIGM line at 1 Hz.
void pigMetricsTick();
