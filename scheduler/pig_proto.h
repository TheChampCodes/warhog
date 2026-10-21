// pig_proto.h — DROP-IN
// ESP-NOW wire layout for the yield-driven scheduler. Little-endian / native.
// Kept at 212 bytes on the wire for Biscuit-Pro compatibility (it drops < 212B).
#pragma once
#include <Arduino.h>

static const char PIG_MAGIC[4] = {'J', 'C', 'M', 'K'};  // keep for mixed-mesh compat

// New message types — chosen above the legacy 1..5 range so legacy Cores,
// which gate on `type == JCMK_MSG_*`, ignore them.
enum PigMsgType : uint8_t {
  PIG_MSG_OBS_BATCH   = 0x20,  // binary observation batch (new/changed records)
  PIG_MSG_DELTA_BATCH = 0x21,  // compact deltas / repeat-summaries
  PIG_MSG_YIELD_HB    = 0x22,  // heartbeat + per-channel yield counters
  PIG_MSG_ADMIN_EXT   = 0x23   // extended assignment (explicit channel list + knobs)
};

// observation flags
enum PigObsFlags : uint8_t {
  PIG_F_HIDDEN  = 0x01,
  PIG_F_5GHZ    = 0x02,
  PIG_F_CHANGED = 0x04,
  PIG_F_WPS     = 0x08
};

typedef struct __attribute__((packed)) {
  uint8_t  bssid[6];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  enc;
  uint8_t  flags;
  uint16_t ssid_crc;
  uint32_t first_rx_ms;
  uint32_t last_rx_ms;
  uint8_t  seen_count;
  uint8_t  ssid_len;        // SSID bytes follow iff ssid_len > 0 (FULL record)
} pig_obs_t;                // 22 B fixed part

typedef struct __attribute__((packed)) {
  char     magic[4];        // "JCMK" (mixed-mesh compat)
  uint8_t  type;            // PIG_MSG_OBS_BATCH / PIG_MSG_DELTA_BATCH
  uint8_t  assign_ver;      // assignment this batch was gathered under
  uint8_t  seq;             // per-node monotonic batch seq (gap = loss detector)
  uint8_t  n;               // record count packed in payload
  uint16_t gps_hint_crc;    // CRC16 of node's last GPS blob, 0 if none
  uint8_t  payload[202];    // packed pig_obs_t records / deltas (212 - 10B header)
} pig_batch_t;              // 212 B total

static_assert(sizeof(pig_batch_t) == 212, "pig_batch_t must be 212B for Biscuit compat");

// CRC16-CCITT (0x1021, init 0xFFFF, MSB-first, no reflect, no xorout).
// Check value: crc16_ccitt("123456789", 9) == 0x29B1.
static inline uint16_t crc16_ccitt(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; ++i)
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}
