// pig_replay.h — DROP-IN (host/target scan-corpus replay)
// Phase-0 replay harness: parse a captured scan corpus so A/B comparisons run on
// *identical* input instead of live RF (DESIGN §13 — "replay harness"). Deliberately
// dependency-light: a hand parser (no sscanf/stdio) so the same header drives a host
// A/B runner today and an on-target SD-fed replay later.
//
// Corpus line (CSV; blank lines and lines starting with '#' are skipped):
//   t_ms,bssid,rssi,channel,enc,ssid_crc[,ssid_len]
// bssid = 12 hex nibbles, optional ':' separators (e.g. 02:11:22:33:44:01).
// All fields integer/hex so a corpus is deterministic, diffable, and reproducible.
#pragma once
#include <Arduino.h>
#include <string.h>

struct ReplayEvent {
  uint32_t t_ms;
  uint8_t  bssid[6];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  enc;
  uint16_t ssid_crc;
  uint8_t  ssid_len;    // 0 if absent in the corpus line
};

namespace pig_replay_detail {
  static inline int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  static inline bool puint(const char*& p, uint32_t& out) {
    uint32_t v = 0; bool any = false;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; any = true; }
    if (!any) return false;
    out = v; return true;
  }
}

// Returns true if `line` parsed into `ev`; false for blank/comment/malformed lines.
static inline bool replay_parse(const char* line, ReplayEvent& ev) {
  using namespace pig_replay_detail;
  const char* p = line;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') return false;
  memset(&ev, 0, sizeof(ev));

  uint32_t v = 0;
  if (!puint(p, v)) return false;
  ev.t_ms = v;
  if (*p++ != ',') return false;

  // bssid: 12 nibbles, ':' separators optional
  int nib = 0; uint8_t cur = 0;
  while (*p && *p != ',') {
    char c = *p++;
    if (c == ':') continue;
    int d = hexval(c);
    if (d < 0) return false;
    cur = (uint8_t)((cur << 4) | d);
    if (((++nib) & 1) == 0) { ev.bssid[(nib / 2) - 1] = cur; cur = 0; }
  }
  if (nib != 12) return false;
  if (*p++ != ',') return false;

  // rssi: signed
  int sign = 1;
  if (*p == '-') { sign = -1; p++; } else if (*p == '+') { p++; }
  if (!puint(p, v)) return false;
  ev.rssi = (int8_t)(sign * (int)v);
  if (*p++ != ',') return false;

  if (!puint(p, v)) return false;
  ev.channel = (uint8_t)v;
  if (*p++ != ',') return false;
  if (!puint(p, v)) return false;
  ev.enc = (uint8_t)v;
  if (*p++ != ',') return false;
  if (!puint(p, v)) return false;
  ev.ssid_crc = (uint16_t)v;

  if (*p == ',') { p++; if (!puint(p, v)) return false; ev.ssid_len = (uint8_t)v; }
  return true;
}
