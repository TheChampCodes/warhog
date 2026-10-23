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

// Extended admin (PIG_MSG_ADMIN_EXT): hands a node an EXPLICIT channel list (not a
// contiguous start/end range), which is what unlocks yield-packed, NON-contiguous
// assignment (DESIGN §4). Also carries the Phase-2 dwell knobs + scan policy and the
// transmit-slot scheduling fields. Piglet<->Piglet only (legacy Cores never send 0x23,
// legacy nodes ignore it). Send offsetof(pig_admin_ext_t, channels)+n bytes on the wire.
#define PIG_ADMIN_EXT_MAXCH 40
enum PigScanPolicyBits : uint8_t {
  PIG_SP_ACTIVE_ALLOWED = 0x01,  // active probing permitted on non-DFS channels
  PIG_SP_PASSIVE_5G     = 0x02,  // 5 GHz passive-only (DFS compliance)
  PIG_SP_SUMMARY_ONLY   = 0x04   // backpressure: send summaries only
};
typedef struct __attribute__((packed)) {
  char     magic[4];        // mesh magic — sender fills (this fork: "ENOW")
  uint8_t  type;            // PIG_MSG_ADMIN_EXT (0x23)
  uint8_t  assign_ver;      // assignment version (node resyncs on change)
  uint8_t  slot_index;      // this node's deterministic transmit slot
  uint8_t  slot_count;      // total Piglet nodes in the slot schedule
  uint8_t  fast_dwell;      // ms, Phase-2 fast active pass (0 = node default)
  uint8_t  base_revisit;    // ms, Phase-2 passive revisit floor (0 = node default)
  uint8_t  scan_policy;     // PigScanPolicyBits
  uint8_t  n;               // number of valid entries in channels[]
  uint8_t  channels[PIG_ADMIN_EXT_MAXCH];  // explicit channel list (first n valid)
} pig_admin_ext_t;          // 12 B header + 40 B list = 52 B max on the wire
static_assert(sizeof(pig_admin_ext_t) == 52, "pig_admin_ext_t must be 52B");

// Wire length to send for an admin-ext carrying `n` channels (header + n).
static inline size_t pig_admin_ext_len(uint8_t n) {
  return (size_t)(sizeof(pig_admin_ext_t) - PIG_ADMIN_EXT_MAXCH) + (size_t)n;
}

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
