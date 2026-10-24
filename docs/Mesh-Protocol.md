# Mesh Protocol

Piglet nodes coordinate over **ESP-NOW on channel 6** (`JCMK_ESPNOW_CH`). The base
protocol is JCMK (the Wardriver/Biscuit lineage); Piglet adds extension message types
for the adaptive scheduler that legacy Cores safely ignore.

## Roles & lifecycle
- A **Node** that has no Core broadcasts `CORE_REQUEST` with exponential backoff
  (`JCMK_REQ_INIT_MS` → `JCMK_REQ_MAX_MS`). It only scans **while it has a Core**
  (`nodeModeTick` gates the scan on `jcmkHaveCore`), so a lone node never wardrives.
- A **Core** replies immediately (`coreSendReply`), registers the node
  (`coreFindOrAddNode`), assigns it channels, and tracks liveness via heartbeats.
  `CORE_NODE_TIMEOUT` = 90 s (generous for many-node ESP-NOW collisions).
- The Core supports up to **`CORE_MAX_NODES` = 12** nodes across **`CORE_MAX_GROUPS` = 9**
  sub-clusters. Beyond 12, extra nodes don't register.

## Message types
Legacy JCMK/Biscuit types are `1..~10`; Piglet extensions are `≥ 0x20` so stock Cores
drop them.

| Value | Name | Direction | Purpose |
|---|---|---|---|
| 1 | `JCMK_MSG_CORE_REQUEST` | Node → Core | "I need a Core" |
| 2 | `JCMK_MSG_CORE_REPLY` | Core → Node | "I am your Core" |
| 3 | `JCMK_MSG_HEARTBEAT` | both | liveness; Piglet carries mesh size in the heartbeat text |
| 4 | `JCMK_MSG_TEXT` | Node → Core | a scan record: `BSSID,SSID,AUTH,CH,RSSI,W` (212 B) |
| 5 | `JCMK_MSG_ADMIN` | Core → Node | role assignment |
| 10 | config update | Core → Node | `channels=<list>;dwell=<ms>` (Biscuit node channel config) |
| `0x20` | `PIG_MSG_OBS_BATCH` | Node → Core | binary observation batch (new/changed) |
| `0x21` | `PIG_MSG_DELTA_BATCH` | Node → Core | compact deltas / repeat-summaries |
| `0x22` | `PIG_MSG_YIELD_HB` | Node → Core | heartbeat + per-channel yield counters |
| `0x23` | `PIG_MSG_ADMIN_EXT` | Core → Node | explicit yield-ordered channel list + knobs |

All structs are defined in [`scheduler/pig_proto.h`](https://github.com/olosnoks/piglet/blob/main/scheduler/pig_proto.h),
carry the 4-byte magic `"ENOW"`, and are CRC16-CCITT protected (`crc16_ccitt`).

### The 212-byte invariant
Scan records travel as 212-byte `jcmk_text_msg_t` packets. **Biscuit Pro drops anything
smaller than 212 B**, so every new binary batch type keeps the 212 B size. Don't shrink it.

### `PIG_MSG_ADMIN_EXT` (0x23) — explicit channel assignment
```c
typedef struct __attribute__((packed)) {
  char     magic[4];        // "ENOW"
  uint8_t  type;            // 0x23
  uint8_t  assign_ver;      // node resyncs on change
  uint8_t  slot_index;      // this node's deterministic TX slot
  uint8_t  slot_count;      // total nodes in the slot schedule
  uint8_t  fast_dwell;      // ms, Phase-2 fast active pass (0 = node default)
  uint8_t  base_revisit;    // ms, Phase-2 passive revisit floor (0 = node default)
  uint8_t  scan_policy;     // PigScanPolicyBits: ACTIVE_ALLOWED / PASSIVE_5G / SUMMARY_ONLY
  uint8_t  n;               // number of channels
  uint8_t  channels[40];    // explicit list (first n valid)
} pig_admin_ext_t;          // 52 B max on the wire
```
A flag-on Core sends each confirmed Piglet node `{1,6,11}` plus a yield-ranked slice of
5 GHz via 0x23; legacy peers keep the contiguous-range ADMIN. **All** workers parse 0x23
unconditionally (so `pig_proto.h` must be `#include`d even with dedup off) and fold it
into their explicit-list scan path.

## Sub-clusters (groups) = antenna pairs
A node's `group` (1..9) is its sub-cluster. `coreAssignGroup()` balances channels **within
a group** so co-located nodes never scan the same channel at once. The Phase-5 pair
scheduler treats a group as **one antenna's two receivers** and hands its members
complementary A/B channel lists — see [Adaptive Scheduler → Phase 5](Adaptive-Scheduler.md#phase-5--warhog-dual-node-pair-scheduler).
Group membership is set from the touch UI or a saved mesh profile and remembered by MAC
(`rememberGroup` / `rememberedGroup`).

## Core RX path
`jcmkOnRecv()` (ESP-NOW RX callback) is kept short: it replies to requests and enqueues
records into SPSC rings (`coreReqBuf`, `coreTextBuf`). The main loop (or, under
`PIG_FEAT_CORE_ISO`, a core-0 task) drains `coreTextBuf` → `coreParseAndLogText()` →
`appendWigleRow()` + companion-app mirror. Ring overflow is a dropped record — the
`[CORE-ISO]` telemetry line counts those (`rx_drop`). See [Serial & Telemetry](Serial-and-Telemetry.md).
