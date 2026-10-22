#include "MeshNode.h"
#include "Globals.h"
#include "Config.h"
#include "BiscuitCompat.h"   // present as a Biscuit Pro to the Biscuit Manager app while in Core mode
#include "IosEspCompat.h"    // or present as a RocketGod iosESP bridge to the Wardrive Go app (toggle in settings)
#include "GPS.h"
#include "SDUtils.h"
#include "BleScanner.h"
#include "ScreenTelemetry.h"
#include "pig_dedup.h"   // Scheduler Phase 1: shared worker dedup (flag PIG_FEAT_DEDUP, default OFF)
#include "pig_dwell.h"   // Scheduler Phase 2: adaptive dwell + DFS passive (flag PIG_FEAT_ADAPTIVE_DWELL, default OFF)
#include <esp_now.h>
#include "esp_wifi.h"

// ================================================================
//  Protocol constants
// ================================================================
const uint8_t  JCMK_ESPNOW_CH       = 6;
static const uint8_t  JCMK_MAGIC[4] = {'E','N','O','W'};
static const uint8_t  JCMK_BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint32_t JCMK_REQ_INIT_MS = 300;
static const uint32_t JCMK_REQ_MAX_MS  = 5000;
static const uint32_t JCMK_HB_MS          = 5000;
static const uint32_t NODE_SCAN_DWELL_MS  = 80;    // ms per channel (JCMK CHANNEL_TIMER)
static const uint32_t NODE_ADMIN_WIN_MS   = 500;   // ch-6 window after each full cycle
#define JCMK_TEXT_MAX 200

// Which phone-app BLE personality this Core-mode session advertises. Latched at
// enterCoreMode() from cfg.blePersonality so begin/stop/tick/push stay symmetric
// even if the setting is toggled mid-session (takes effect on next Core entry).
static bool coreBleIsIosEsp = false;

// Piglet mesh-size extension: the Core stamps the total active-node count behind
// this marker in its broadcast heartbeat so every node can show the true herd
// count. Stock JCMK/Biscuit nodes ignore heartbeat text, so this is harmless.
// Must match the marker in Arduino Files/Piglet and c3_node.
static const char*   PIGLET_MARKER     = "PIGLET1";
static const uint8_t PIGLET_MARKER_LEN = 7;

enum JcmkMsgType : uint8_t {
  JCMK_MSG_CORE_REQUEST = 1,
  JCMK_MSG_CORE_REPLY   = 2,
  JCMK_MSG_HEARTBEAT    = 3,
  JCMK_MSG_TEXT         = 4,
  JCMK_MSG_ADMIN        = 5
};

// Packed structs match JCMK wire layout exactly
typedef struct __attribute__((packed)) {
  char     magic[4];
  uint8_t  type;
  uint32_t counter;
  uint16_t len;
  char     text[JCMK_TEXT_MAX + 1];
} jcmk_text_msg_t;

typedef struct __attribute__((packed)) {
  char    magic[4];
  uint8_t type;
  uint8_t assignment_version;
  uint8_t node_index;
  uint8_t node_count;
  uint8_t start_channel_idx;
  uint8_t end_channel_idx;
} jcmk_admin_msg_t;

typedef struct __attribute__((packed)) {
  char     magic[4];
  uint8_t  type;
  uint32_t counter;
} jcmk_hb_msg_t;

typedef struct __attribute__((packed)) {
  char    magic[4];
  uint8_t type;
} jcmk_req_msg_t;

// JCMK scan-channel table — must match Core exactly
const uint8_t JCMK_CHANNELS[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
  36, 40, 44, 48, 52, 56, 60, 64,
  100, 112, 116, 120, 124, 128, 132, 136, 140, 144,
  149, 153, 157, 161, 165, 169, 173, 177
};
const uint8_t JCMK_NUM_CHANNELS = (uint8_t)(sizeof(JCMK_CHANNELS));

// ================================================================
//  Node state
// ================================================================
bool     meshNodeActive   = false;
bool     jcmkHaveCore     = false;
uint8_t  jcmkCoreMac[6]  = {0};
uint8_t  jcmkStartIdx    = 0;
uint8_t  jcmkEndIdx      = 0;
uint8_t  jcmkAssignVer   = 0;
uint32_t jcmkNetworksFound = 0;
uint32_t jcmkSentCount   = 0;

static uint32_t jcmkHbCounter   = 0;
static uint32_t jcmkLastHbMs    = 0;
static uint32_t jcmkLastReqMs   = 0;
static uint32_t jcmkReqInterval = JCMK_REQ_INIT_MS;

// Per-channel async scan state
static bool     nodeScanActive   = false;
static uint8_t  nodeScanChOffset = 0;
static bool     nodeScanAdminWin = false;
static uint32_t nodeScanAdminMs  = 0;

// Pending core-found event — written in ESP-Now callback, consumed in loop
static volatile bool  jcmkCoreFoundPending = false;
static uint8_t        jcmkCoreMacPending[6] = {0};

// Biscuit plain-reply (encrypt=0) interop. Set when a received CORE_REPLY
// advertises "encrypt=0"; gates the type=5/type=10 config-handshake handling
// and the bracketed-auth scan-result format a plain-reply Biscuit Core
// expects. Never set when paired with a Piglet/JCMK Core, so the existing
// Core<->node path is unchanged.
static volatile bool  biscuitPlainReplyPending = false;
static uint8_t        biscuitChannels[JCMK_NUM_CHANNELS + 2];
static volatile uint8_t biscuitChannelCount = 0;

// ================================================================
//  Core mode state
// ================================================================
bool         meshCoreActive = false;
uint32_t     coreRecordsRx  = 0;
uint8_t      coreNodeCount  = 0;
CoreNodeInfo coreNodes[CORE_MAX_NODES] = {};

static uint8_t  coreAssignVer  = 0;
static uint32_t coreLastHbMs   = 0;
static uint32_t coreHbCounter  = 0;

// Sub-cluster membership remembered by MAC so a node that times out and
// rejoins returns to the group the user put it in (coreNodes[] itself is
// cleared on timeout). Default group is 1 for any MAC not listed here.
struct GroupMem { uint8_t mac[6]; uint8_t group; };
static GroupMem groupMem[CORE_MAX_NODES] = {};
static uint8_t  groupMemCount = 0;

static uint8_t rememberedGroup(const uint8_t* mac) {
  for (uint8_t i = 0; i < groupMemCount; i++)
    if (memcmp(groupMem[i].mac, mac, 6) == 0) return groupMem[i].group;
  return 1;  // default sub-cluster
}

static void rememberGroup(const uint8_t* mac, uint8_t group) {
  for (uint8_t i = 0; i < groupMemCount; i++) {
    if (memcmp(groupMem[i].mac, mac, 6) == 0) { groupMem[i].group = group; return; }
  }
  if (groupMemCount < CORE_MAX_NODES) {
    memcpy(groupMem[groupMemCount].mac, mac, 6);
    groupMem[groupMemCount].group = group;
    groupMemCount++;
  }
}

static const uint32_t CORE_HB_MS        = 5000;
static const uint32_t CORE_NODE_TIMEOUT = 90000;  // 90 s — generous for many-node ESP-Now collisions

// Ring buffers: ESP-Now callback → main loop
#define CORE_REQ_QUEUE  16
#define CORE_TEXT_QUEUE 192  // sized for burst from 12 nodes × ~15 networks each

struct CorReqSlot  { uint8_t mac[6]; bool isBiscuit; bool band24; };

// Dwell (ms) the Core tells nodes to spend per channel. Longer than the old 80 ms
// so a node reliably catches a beacon (APs beacon ~every 100 ms) now that
// band-aware assignment gives each node fewer, better channels.
static const uint32_t MESH_NODE_DWELL_MS = 110;

// A node declares itself 2.4 GHz-only by putting the marker "B24" in its
// CORE_REQUEST / HEARTBEAT text; the Core then never assigns it 5 GHz.
static bool jcmkPacketIsBand24(const uint8_t* data, int len) {
  if (len < (int)sizeof(jcmk_text_msg_t)) return false;
  const jcmk_text_msg_t* tm = (const jcmk_text_msg_t*)data;
  uint16_t slen = (tm->len < JCMK_TEXT_MAX) ? tm->len : JCMK_TEXT_MAX;
  for (uint16_t i = 0; i + 3 <= slen; i++)
    if (tm->text[i] == 'B' && tm->text[i + 1] == '2' && tm->text[i + 2] == '4') return true;
  return false;
}
struct CorTextSlot { char    line[JCMK_TEXT_MAX + 1]; };

static CorReqSlot          coreReqBuf[CORE_REQ_QUEUE];
static volatile uint8_t    coreReqHead = 0, coreReqTail = 0;

static CorTextSlot         coreTextBuf[CORE_TEXT_QUEUE];
static volatile uint8_t    coreTextHead = 0, coreTextTail = 0;

// ================================================================
//  Local helpers
// ================================================================
static String meshAuthModeToString(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPAWPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2EAP";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2WPA3";
    default: return "UNKNOWN";
  }
}

// ================================================================
//  ESP-Now helpers
// ================================================================
static void jcmkSetChannel(uint8_t ch) {
  esp_wifi_set_ps(WIFI_PS_NONE);
  // Do NOT use promiscuous mode here: on IDF 5.x, disabling promiscuous after
  // a prior STA connection causes the driver to revert to the home router channel.
  // Direct esp_wifi_set_channel() works correctly when the STA is not connected.
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

static bool jcmkAddPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // follow current home channel
  peer.encrypt = false;
  return (esp_now_add_peer(&peer) == ESP_OK);
}

// Node-mode sends always use full jcmk_text_msg_t (212 bytes).
// Biscuit Pro drops any packet < 212 bytes; JCMK Cores only check len >= 5
// so full-size packets are backward-compatible with both coordinators.

// A non-C5 board is 2.4 GHz-only: declare "B24" so the Core never assigns it
// 5 GHz channels it can't tune. C5 boards send nothing (default = dual-band).
static void jcmkStampBand(jcmk_text_msg_t& msg) {
  if (!wardriverIsC5()) { memcpy(msg.text, "B24", 3); msg.text[3] = '\0'; msg.len = 3; }
  else                  { msg.len = 0; }
}

static void jcmkSendCoreRequest() {
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_CORE_REQUEST;
  msg.counter = 0;
  jcmkStampBand(msg);
  esp_now_send(JCMK_BCAST, (uint8_t*)&msg, sizeof(msg));
}

static void jcmkSendHeartbeat() {
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_HEARTBEAT;
  msg.counter = ++jcmkHbCounter;
  jcmkStampBand(msg);
  esp_now_send(JCMK_BCAST, (uint8_t*)&msg, sizeof(msg));
}

static void jcmkSendText(const String& s) {
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_TEXT;
  msg.counter = jcmkHbCounter;
  uint16_t slen = (uint16_t)((s.length() < JCMK_TEXT_MAX) ? s.length() : JCMK_TEXT_MAX);
  msg.len = slen;
  memcpy(msg.text, s.c_str(), slen);
  msg.text[slen] = '\0';
  // Always send full struct size — Biscuit Pro drops variable-length packets < 212 bytes.
  esp_now_send(JCMK_BCAST, (uint8_t*)&msg, sizeof(msg));
}

// ================================================================
//  Biscuit Reporter state (solo mode)
// ================================================================
bool     biscuitReporterActive = false;
bool     biscuitLinked         = false;
uint32_t biscuitRecordsSent    = 0;

static bool     biscuitInited     = false;
static uint32_t biscuitLastHbMs   = 0;
static uint32_t biscuitLastReqMs  = 0;

#define BISCUIT_FWD_QUEUE 96
static char biscuitFwd[BISCUIT_FWD_QUEUE][JCMK_TEXT_MAX + 1];
static int  biscuitFwdCount = 0;

// ================================================================
//  Core mode helpers (forward declarations used in jcmkOnRecv)
// ================================================================
static void coreSendReply(const uint8_t* mac) {
  // Use full-size jcmk_text_msg_t (212 bytes) instead of jcmk_req_msg_t (5 bytes).
  // Biscuit Node drops any packet < sizeof(enow_text_msg_t) = 212 bytes.
  // JCMK nodes only check len >= 5, so this is fully backward-compatible.
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_CORE_REPLY;
  msg.counter = 0;
  msg.len     = 0;
  esp_now_send(mac, (uint8_t*)&msg, sizeof(msg));
}

// Send Biscuit Node a role assignment (MSG_ROLE_ASSIGN, type=5) then a channel
// config (MSG_CONFIG_UPDATE, type=10). Both packets must be full size (212 bytes).
// Called from coreReassignChannels / coreResendAdminToAll for isBiscuit nodes.
static void coreSendBiscuitChannels(const uint8_t* mac, const uint8_t* chans, uint8_t n) {
  if (n == 0) return;
  // Step 1: role assignment — text[0] = 1 (ROLE_WIFI). Biscuit MSG_ROLE_ASSIGN
  // = type 5. After receiving this the node transitions to STATE_SCANNING.
  jcmk_text_msg_t roleMsg = {};
  memcpy(roleMsg.magic, JCMK_MAGIC, 4);
  roleMsg.type    = JCMK_MSG_ADMIN;
  roleMsg.counter = 0;
  roleMsg.len     = 1;
  roleMsg.text[0] = 1;  // ROLE_WIFI
  esp_now_send(mac, (uint8_t*)&roleMsg, sizeof(roleMsg));
  delay(10);

  // Step 2: channel config — "channels=<explicit list>;dwell=N" (MSG_CONFIG_UPDATE,
  // type 10). The list is the exact set the node should scan — sparse (e.g. 1,6,11)
  // for 2.4-only nodes, a 5 GHz slice for dual-band nodes.
  jcmk_text_msg_t cfgMsg = {};
  memcpy(cfgMsg.magic, JCMK_MAGIC, 4);
  cfgMsg.type    = 10;
  cfgMsg.counter = 0;

  String chList = "channels=";
  for (uint8_t i = 0; i < n; i++) {
    if (i) chList += ",";
    chList += String(chans[i]);
  }
  chList += ";dwell=";
  chList += String((uint32_t)MESH_NODE_DWELL_MS);

  uint16_t slen = (chList.length() < JCMK_TEXT_MAX)
                ? (uint16_t)chList.length() : (uint16_t)JCMK_TEXT_MAX;
  cfgMsg.len = slen;
  memcpy(cfgMsg.text, chList.c_str(), slen);
  cfgMsg.text[slen] = '\0';
  esp_now_send(mac, (uint8_t*)&cfgMsg, sizeof(cfgMsg));

  Serial.printf("[CORE] cfg -> %02X%02X: %s\n", mac[4], mac[5], chList.c_str());
}

// ---- Biscuit plain-reply (encrypt=0) node-side interop helpers ----

// True if a received CORE_REPLY indicates a Biscuit Core running in
// unencrypted-reply mode (carries "encrypt=0" somewhere in its text field).
static bool biscuitHasPlainReply(const void* data, int len) {
  if (len < (int)sizeof(jcmk_text_msg_t)) return false;
  const jcmk_text_msg_t* msg = (const jcmk_text_msg_t*)data;
  size_t n = msg->len < JCMK_TEXT_MAX ? msg->len : JCMK_TEXT_MAX;
  for (size_t i = 0; i + 9 <= n; i++) {
    if (memcmp(msg->text + i, "encrypt=0", 9) == 0) return true;
  }
  return false;
}

// Parses a Biscuit "channels=1,2,...;dwell=N" config message into
// biscuitChannels[]/biscuitChannelCount, restarting the scan cycle from the
// new list. Only used for plain-reply Biscuit pairings.
static void biscuitSetChannels(const jcmk_text_msg_t* msg) {
  if (msg->len < 9 || msg->len > JCMK_TEXT_MAX || memcmp(msg->text, "channels=", 9)) {
    biscuitChannelCount = 0;
    return;
  }
  char text[JCMK_TEXT_MAX + 1];
  memcpy(text, msg->text, msg->len);
  text[msg->len] = '\0';
  char* end = strchr(text, ';');
  if (end) *end = '\0';
  uint8_t channels[JCMK_NUM_CHANNELS + 2];
  uint8_t count = 0;
  for (char* token = text + 9; *token && count < JCMK_NUM_CHANNELS + 2;) {
    char* next;
    long channel = strtol(token, &next, 10);
    if (next == token || channel < 1 || channel > 255 || (*next && *next != ',')) {
      count = 0;
      break;
    }
    if (channel == 104 || channel == 108) {
      channels[count++] = (uint8_t)channel;
    } else {
      for (uint8_t i = 0; i < JCMK_NUM_CHANNELS; i++) {
        if (JCMK_CHANNELS[i] == channel) { channels[count++] = (uint8_t)channel; break; }
      }
    }
    if (!*next) break;
    token = next + 1;
  }
  memcpy(biscuitChannels, channels, count);
  biscuitChannelCount = count;
  nodeScanChOffset = 0;
}

// Generic Biscuit protocol-control reply, always unicast to the Core. Used
// for the type=15 (admin ack) and type=11 (config ack) handshake messages a
// plain-reply Biscuit Core expects during node registration.
static void biscuitSendControl(uint8_t type, uint32_t counter = 0, const char* text = nullptr) {
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type = type;
  msg.counter = counter;
  if (text) {
    msg.len = strlen(text);
    memcpy(msg.text, text, msg.len);
  }
  esp_now_send(jcmkCoreMac, (uint8_t*)&msg, sizeof(msg));
}

// ESP-Now receive callback — handles both Node and Core roles
static void jcmkOnRecv(const esp_now_recv_info_t* info,
                        const uint8_t* data, int len) {
  if (len < 5) return;
  if (data[0] != 'E' || data[1] != 'N' || data[2] != 'O' || data[3] != 'W') return;
  uint8_t type = data[4];

  if (meshCoreActive) {
    // ---- Core role: handle requests from nodes ----
  if (type == JCMK_MSG_CORE_REQUEST) {
      Serial.printf("[CORE] RX CORE_REQUEST from %02X:%02X:%02X:%02X:%02X:%02X len=%d\n",
        info->src_addr[0],info->src_addr[1],info->src_addr[2],
        info->src_addr[3],info->src_addr[4],info->src_addr[5], len);
      coreSendReply(info->src_addr);  // reply immediately (safe from callback)
      uint8_t next = (coreReqTail + 1) % CORE_REQ_QUEUE;
      if (next != coreReqHead) {
        memcpy(coreReqBuf[coreReqTail].mac, info->src_addr, 6);
        // Biscuit Node always sends full-size packets (212 bytes); JCMK sends 5 bytes.
        coreReqBuf[coreReqTail].isBiscuit = (len >= (int)sizeof(jcmk_text_msg_t));
        coreReqBuf[coreReqTail].band24    = jcmkPacketIsBand24(data, len);
        coreReqTail = next;
      }
    } else if (type == JCMK_MSG_TEXT && len >= 11) {
      const jcmk_text_msg_t* tm = (const jcmk_text_msg_t*)data;
      uint8_t next = (coreTextTail + 1) % CORE_TEXT_QUEUE;
      if (next != coreTextHead) {
        uint16_t slen = (tm->len < JCMK_TEXT_MAX) ? tm->len : JCMK_TEXT_MAX;
        memcpy(coreTextBuf[coreTextTail].line, tm->text, slen);
        coreTextBuf[coreTextTail].line[slen] = '\0';
        coreTextTail = next;
      }
      // Update heartbeat for known node; queue unknown node for registration.
      // This handles reconnect: node has jcmkHaveCore=true and skips CORE_REQUEST,
      // but the Core can register it from any received packet (JCMK touchNode pattern).
      {
        bool found = false;
        for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
          if (coreNodes[i].active && memcmp(coreNodes[i].mac, info->src_addr, 6) == 0) {
            coreNodes[i].lastHbMs = millis();
            coreNodes[i].recordsRx++;
            found = true; break;
          }
        }
        if (!found) {
          uint8_t nxt = (coreReqTail + 1) % CORE_REQ_QUEUE;
          if (nxt != coreReqHead) {
            memcpy(coreReqBuf[coreReqTail].mac, info->src_addr, 6);
            coreReqBuf[coreReqTail].isBiscuit = (len >= (int)sizeof(jcmk_text_msg_t));
            coreReqBuf[coreReqTail].band24    = jcmkPacketIsBand24(data, len);
            coreReqTail = nxt;
          }
        }
      }
    } else if (type == JCMK_MSG_HEARTBEAT) {
      // Update heartbeat for known node; queue unknown node for registration.
      bool found = false;
      for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
        if (coreNodes[i].active && memcmp(coreNodes[i].mac, info->src_addr, 6) == 0) {
          coreNodes[i].lastHbMs = millis();
          found = true; break;
        }
      }
      if (!found) {
        uint8_t nxt = (coreReqTail + 1) % CORE_REQ_QUEUE;
        if (nxt != coreReqHead) {
          memcpy(coreReqBuf[coreReqTail].mac, info->src_addr, 6);
          coreReqBuf[coreReqTail].isBiscuit = (len >= (int)sizeof(jcmk_text_msg_t));
          coreReqBuf[coreReqTail].band24    = jcmkPacketIsBand24(data, len);
          coreReqTail = nxt;
        }
      }
    }
  } else {
    // ---- Node role (existing) ----
    if (type == JCMK_MSG_CORE_REPLY && !jcmkHaveCore && !jcmkCoreFoundPending) {
      memcpy(jcmkCoreMacPending, info->src_addr, 6);
      biscuitPlainReplyPending = biscuitHasPlainReply(data, len);
      jcmkCoreFoundPending = true;
    } else if (biscuitPlainReplyPending && jcmkHaveCore
               && memcmp(info->src_addr, jcmkCoreMac, 6) == 0
               && len >= (int)sizeof(jcmk_text_msg_t) && type == JCMK_MSG_ADMIN) {
      // Plain-reply Biscuit admin handshake step: ack with type=15, echoing
      // back the first text byte (or low byte of counter as a fallback).
      // Fully gated on biscuitPlainReplyPending so a Piglet/JCMK Core's own
      // 212-byte role message still falls through to the binary-admin branch
      // below exactly as before.
      const jcmk_text_msg_t* msg = (const jcmk_text_msg_t*)data;
      biscuitSendControl(15, msg->len ? (uint8_t)msg->text[0] : msg->counter & 0xFF);
    } else if (biscuitPlainReplyPending && jcmkHaveCore
               && memcmp(info->src_addr, jcmkCoreMac, 6) == 0
               && len >= (int)sizeof(jcmk_text_msg_t) && type == 10) {
      // Plain-reply Biscuit channel-config message (MSG_CONFIG_UPDATE) --
      // parse the assigned channel list, then ack with type=11.
      biscuitSetChannels((const jcmk_text_msg_t*)data);
      biscuitSendControl(11);
    } else if (type == JCMK_MSG_ADMIN && len >= (int)sizeof(jcmk_admin_msg_t)) {
      const jcmk_admin_msg_t* adm = (const jcmk_admin_msg_t*)data;
      if (adm->assignment_version != jcmkAssignVer) {
        jcmkAssignVer = adm->assignment_version;
        jcmkStartIdx  = adm->start_channel_idx;
        jcmkEndIdx    = adm->end_channel_idx;
      }
    }
  }
}

// ================================================================
//  Core mode helpers (main loop only — not ISR-safe)
// ================================================================
static void coreFindOrAddNode(const uint8_t* mac, bool isBiscuit, bool band24) {
  for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
    if (coreNodes[i].active && memcmp(coreNodes[i].mac, mac, 6) == 0) {
      coreNodes[i].lastHbMs = millis();
      coreNodes[i].band24Only = band24;  // pick up a (re)declared band; caller reassigns
      return;  // already registered
    }
  }
  for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
    if (!coreNodes[i].active) {
      coreNodes[i].active    = true;
      coreNodes[i].lastHbMs  = millis();
      coreNodes[i].recordsRx = 0;
      coreNodes[i].isBiscuit = isBiscuit;
      coreNodes[i].band24Only = band24;
      coreNodes[i].group     = rememberedGroup(mac);
      coreNodes[i].chanCount = 0;
      memcpy(coreNodes[i].mac, mac, 6);
      coreNodeCount++;
      jcmkAddPeer(mac);
      // Re-send CORE_REPLY now that the peer is registered.
      // The initial reply from jcmkOnRecv() may have failed because the node's
      // MAC wasn't yet in the peer list when esp_now_send() was called.
      coreSendReply(mac);
      Serial.printf("[CORE] New %s node %d: %02X:%02X:%02X:%02X:%02X:%02X\n",
        isBiscuit ? "Biscuit" : "JCMK",
        i, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      return;
    }
  }
  Serial.println("[CORE] Max nodes reached");
}

// Record the exact channel list a node is scanning (for the on-device UI) and
// push it to the node.
static void coreApplyNodeChannels(CoreNodeInfo& nd, const uint8_t* list, uint8_t nl) {
  nd.chanCount = (nl <= sizeof(nd.chans)) ? nl : (uint8_t)sizeof(nd.chans);
  memcpy(nd.chans, list, nd.chanCount);
  coreSendBiscuitChannels(nd.mac, list, nl);
}

// Band-aware channel assignment for ONE sub-cluster. 2.4 GHz-only nodes
// (declared via "B24") scan the productive 2.4 set {1,6,11}; dual-band (C5)
// nodes split the 5 GHz space. If the group has no dual nodes, 5 GHz simply
// isn't covered (nothing can scan it); if it has no 2.4-only nodes, the first
// dual node also takes {1,6,11} so 2.4 stays covered. Splitting happens inside
// the group so co-located nodes never scan the same channel at once; a group
// of one node therefore scans everything. `idx`/`n` are indices into
// coreNodes[] of the active members of this group. All of the user's nodes are
// Biscuit-type (full-size packets), so every node gets an explicit
// "channels=..." string config.
static void coreAssignGroup(const uint8_t* idx, uint8_t n) {
  if (n == 0) return;

  uint8_t dual[CORE_MAX_NODES], only24[CORE_MAX_NODES];
  uint8_t nDual = 0, n24 = 0;
  for (uint8_t t = 0; t < n; t++) {
    uint8_t i = idx[t];
    if (coreNodes[i].band24Only) only24[n24++] = i;
    else                         dual[nDual++] = i;
  }

  static const uint8_t CH24[3] = {1, 6, 11};
  const uint8_t* ch5 = &JCMK_CHANNELS[14];                 // ch 36..177
  const uint8_t  n5  = (uint8_t)(JCMK_NUM_CHANNELS - 14);  // 26 channels

  // 2.4-only nodes: each scans 1/6/11.
  for (uint8_t k = 0; k < n24; k++) {
    CoreNodeInfo& nd = coreNodes[only24[k]];
    nd.startIdx = 0;  nd.endIdx = 10;  // display only
    coreApplyNodeChannels(nd, CH24, 3);
    delay(10);
  }

  // Dual-band nodes: split the 5 GHz channels into contiguous slices.
  if (nDual > 0) {
    uint8_t per = (nDual <= n5) ? (uint8_t)(n5 / nDual) : 1;
    if (per == 0) per = 1;
    for (uint8_t k = 0; k < nDual; k++) {
      CoreNodeInfo& nd = coreNodes[dual[k]];
      uint8_t list[32];
      uint8_t nl = 0;
      if (n24 == 0 && k == 0) { list[nl++] = 1; list[nl++] = 6; list[nl++] = 11; }
      uint8_t s = (uint8_t)(k * per);
      if (s < n5) {
        uint8_t e = (k == nDual - 1) ? (uint8_t)(n5 - 1) : (uint8_t)(s + per - 1);
        if (e >= n5) e = (uint8_t)(n5 - 1);
        for (uint8_t j = s; j <= e && nl < sizeof(list); j++) list[nl++] = ch5[j];
        nd.startIdx = (uint8_t)(14 + s);
        nd.endIdx   = (uint8_t)(14 + e);
      }
      if (nl == 0) { list[nl++] = 1; list[nl++] = 6; list[nl++] = 11; }  // safety
      coreApplyNodeChannels(nd, list, nl);
      delay(10);
    }
  }
}

// Assign channels across every sub-cluster. Each group is balanced on its own
// (see coreAssignGroup), so groups overlap in coverage but members of the same
// group never do.
static void coreAssignAndSend(bool bumpVersion) {
  if (coreNodeCount == 0) return;
  if (bumpVersion) coreAssignVer++;

  uint8_t nGroups = 0;
  for (uint8_t g = 1; g <= CORE_MAX_GROUPS; g++) {
    uint8_t idx[CORE_MAX_NODES];
    uint8_t n = 0;
    for (uint8_t i = 0; i < CORE_MAX_NODES; i++)
      if (coreNodes[i].active && coreNodes[i].group == g) idx[n++] = i;
    if (n == 0) continue;
    coreAssignGroup(idx, n);
    nGroups++;
  }

  Serial.printf("[CORE] Sub-cluster assign: %u node(s) across %u group(s), v%d\n",
                (unsigned)coreNodeCount, (unsigned)nGroups, coreAssignVer);
}

static void coreReassignChannels() { coreAssignAndSend(true);  }

// Re-send the current assignment to every node (nodes that missed an update
// mid-scan recover). Does not bump the version.
static void coreResendAdminToAll() { if (coreNodeCount) coreAssignAndSend(false); }

// ================================================================
//  Sub-cluster control + saved wardriving profiles (public API)
// ================================================================
uint8_t coreGroupMemberCount(uint8_t group) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < CORE_MAX_NODES; i++)
    if (coreNodes[i].active && coreNodes[i].group == group) n++;
  return n;
}

void coreSetNodeGroup(uint8_t nodeIdx, uint8_t group) {
  if (nodeIdx >= CORE_MAX_NODES || !coreNodes[nodeIdx].active) return;
  if (group < 1) group = 1;
  if (group > CORE_MAX_GROUPS) group = CORE_MAX_GROUPS;
  if (coreNodes[nodeIdx].group == group) return;
  coreNodes[nodeIdx].group = group;
  rememberGroup(coreNodes[nodeIdx].mac, group);
  coreReassignChannels();   // bump version + resend to every node
}

// ---- Wardriving profiles: named node→sub-cluster presets, saved on SD so a
// recurring physical setup (same boards, same antennas) can be recalled with
// one tap instead of re-grouping every session. Stored as a small text file. --
static const char* MESH_PROFILE_PATH = "/mesh_profiles.cfg";

struct MeshProfileEntry { uint8_t mac[6]; uint8_t group; };
struct MeshProfile {
  bool            used;
  char            name[MESH_PROFILE_NAME_MAX + 1];
  uint8_t         count;
  MeshProfileEntry entry[CORE_MAX_NODES];
};
static MeshProfile meshProfiles[MESH_NUM_PROFILES] = {};
static bool        meshProfilesLoaded = false;

static void meshParseMac(const String& s, uint8_t out[6]) {
  for (uint8_t i = 0; i < 6; i++)
    out[i] = (uint8_t)strtol(s.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
}

static void meshProfilesLoad() {
  memset(meshProfiles, 0, sizeof(meshProfiles));
  meshProfilesLoaded = true;
  if (!sdOk || !SD.exists(MESH_PROFILE_PATH)) return;
  File f = SD.open(MESH_PROFILE_PATH, FILE_READ);
  if (!f) return;
  int slot = -1;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    if (line[0] == 'P') {                              // "P <slot> <name...>"
      int sp1 = line.indexOf(' ');
      int sp2 = line.indexOf(' ', sp1 + 1);
      if (sp1 < 0) { slot = -1; continue; }
      slot = line.substring(sp1 + 1, sp2 < 0 ? line.length() : sp2).toInt();
      if (slot < 0 || slot >= MESH_NUM_PROFILES) { slot = -1; continue; }
      String nm = (sp2 < 0) ? String("") : line.substring(sp2 + 1);
      meshProfiles[slot].used  = true;
      meshProfiles[slot].count = 0;
      strncpy(meshProfiles[slot].name, nm.c_str(), MESH_PROFILE_NAME_MAX);
      meshProfiles[slot].name[MESH_PROFILE_NAME_MAX] = '\0';
    } else if (line[0] == 'N' && slot >= 0) {          // "N <AABBCCDDEEFF> <group>"
      int sp1 = line.indexOf(' ');
      int sp2 = line.indexOf(' ', sp1 + 1);
      if (sp1 < 0 || sp2 < 0) continue;
      String macStr = line.substring(sp1 + 1, sp2);
      uint8_t g = (uint8_t)line.substring(sp2 + 1).toInt();
      MeshProfile& p = meshProfiles[slot];
      if (macStr.length() >= 12 && p.count < CORE_MAX_NODES) {
        meshParseMac(macStr, p.entry[p.count].mac);
        p.entry[p.count].group = (g < 1) ? 1 : (g > CORE_MAX_GROUPS ? CORE_MAX_GROUPS : g);
        p.count++;
      }
    }
  }
  f.close();
}

static void meshProfilesSaveToSD() {
  if (!sdOk) return;
  SD.remove(MESH_PROFILE_PATH);
  File f = SD.open(MESH_PROFILE_PATH, FILE_WRITE);
  if (!f) return;
  f.println("# piglet mesh profiles v1");
  for (uint8_t s = 0; s < MESH_NUM_PROFILES; s++) {
    if (!meshProfiles[s].used) continue;
    f.printf("P %u %s\n", (unsigned)s, meshProfiles[s].name);
    for (uint8_t e = 0; e < meshProfiles[s].count; e++) {
      const uint8_t* m = meshProfiles[s].entry[e].mac;
      f.printf("N %02X%02X%02X%02X%02X%02X %u\n",
               m[0], m[1], m[2], m[3], m[4], m[5],
               (unsigned)meshProfiles[s].entry[e].group);
    }
  }
  f.close();
}

void meshProfilesInit() { if (!meshProfilesLoaded) meshProfilesLoad(); }

// DEFAULT profile: put every node back into sub-cluster 1 so the Core splits
// channels evenly across all connected nodes (band-aware). Also forgets saved
// per-MAC groupings so nodes that reconnect default to the even split too.
void meshApplyDefault() {
  groupMemCount = 0;
  for (uint8_t i = 0; i < CORE_MAX_NODES; i++)
    if (coreNodes[i].active) coreNodes[i].group = 1;
  if (coreNodeCount) coreReassignChannels();
}

bool        meshProfileUsed(uint8_t slot)      { return slot < MESH_NUM_PROFILES && meshProfiles[slot].used; }
const char* meshProfileName(uint8_t slot)      { return (slot < MESH_NUM_PROFILES) ? meshProfiles[slot].name : ""; }
uint8_t     meshProfileNodeCount(uint8_t slot) { return (slot < MESH_NUM_PROFILES) ? meshProfiles[slot].count : 0; }

// Capture the current node→sub-cluster grouping (all connected nodes) into a
// slot and persist it.
void meshSaveProfile(uint8_t slot, const char* name) {
  if (slot >= MESH_NUM_PROFILES) return;
  if (!meshProfilesLoaded) meshProfilesLoad();
  MeshProfile& p = meshProfiles[slot];
  memset(&p, 0, sizeof(p));
  p.used = true;
  if (name && *name) {
    strncpy(p.name, name, MESH_PROFILE_NAME_MAX);
    p.name[MESH_PROFILE_NAME_MAX] = '\0';
  } else {
    snprintf(p.name, sizeof(p.name), "Profile %u", (unsigned)(slot + 1));
  }
  for (uint8_t i = 0; i < CORE_MAX_NODES && p.count < CORE_MAX_NODES; i++) {
    if (!coreNodes[i].active) continue;
    memcpy(p.entry[p.count].mac, coreNodes[i].mac, 6);
    p.entry[p.count].group = coreNodes[i].group;
    p.count++;
  }
  meshProfilesSaveToSD();
}

// Apply a saved profile: set the sub-cluster of every matching connected node
// (and remember it by MAC so nodes not yet connected land in the right group
// when they join), then recompute + push assignments. Returns how many
// connected nodes matched.
uint8_t meshApplyProfile(uint8_t slot) {
  if (slot >= MESH_NUM_PROFILES || !meshProfiles[slot].used) return 0;
  MeshProfile& p = meshProfiles[slot];
  uint8_t applied = 0;
  for (uint8_t e = 0; e < p.count; e++) {
    rememberGroup(p.entry[e].mac, p.entry[e].group);
    for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
      if (coreNodes[i].active && memcmp(coreNodes[i].mac, p.entry[e].mac, 6) == 0) {
        coreNodes[i].group = p.entry[e].group;
        applied++;
      }
    }
  }
  if (coreNodeCount) coreReassignChannels();
  return applied;
}

void meshDeleteProfile(uint8_t slot) {
  if (slot >= MESH_NUM_PROFILES) return;
  memset(&meshProfiles[slot], 0, sizeof(meshProfiles[slot]));
  meshProfilesSaveToSD();
}

static void coreSendHeartbeatToAll() {
  // Broadcast a single heartbeat instead of per-node unicast.
  // At 12 nodes, unicast was 12 × 212-byte sends with 10 ms gaps = 120+ ms of
  // blocking channel time. One broadcast reaches all nodes simultaneously and
  // keeps ch 6 clear for incoming TEXT bursts.
  jcmk_text_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_HEARTBEAT;
  msg.counter = ++coreHbCounter;
  // Piglet extension: carry the authoritative mesh size (total active nodes)
  // so every node can display the real herd count. Marker keeps it invisible
  // to stock JCMK/Biscuit nodes, which ignore heartbeat text.
  memcpy(msg.text, PIGLET_MARKER, PIGLET_MARKER_LEN);
  msg.text[PIGLET_MARKER_LEN] = (char)coreNodeCount;
  msg.len     = PIGLET_MARKER_LEN + 1;
  esp_now_send(JCMK_BCAST, (uint8_t*)&msg, sizeof(msg));
}

static void coreParseAndLogText(const char* line) {
  // JCMK format: BSSID,SSID,AUTH,CHANNEL,RSSI,W
  String s(line);
  int p0 = s.indexOf(',');                if (p0 < 0) return;
  int p1 = s.indexOf(',', p0 + 1);       if (p1 < 0) return;
  int p2 = s.indexOf(',', p1 + 1);       if (p2 < 0) return;
  int p3 = s.indexOf(',', p2 + 1);       if (p3 < 0) return;

  String bssid = s.substring(0,      p0);
  String ssid  = s.substring(p0 + 1, p1);
  String auth  = s.substring(p1 + 1, p2);
  int    ch    = s.substring(p2 + 1, p3).toInt();
  int    rssi  = s.substring(p3 + 1).toInt();  // toInt() stops at next comma

  double lat = 0, lon = 0, altM = 0, accM = 0;
  if (gpsHasFix) {
    lat  = gps.location.lat();
    lon  = gps.location.lng();
    altM = gps.altitude.meters();
    accM = gps.hdop.hdop();
  }
  appendWigleRow(bssid, ssid, auth, iso8601NowUTC(), ch, rssi, lat, lon, altM, accM);
  // Mirror each AP to whichever phone app this Core is paired with, so it plots
  // in real time on the map (personality chosen by the CORE BLE APP setting).
  if (coreBleIsIosEsp) iosEspCompatPushAp(bssid.c_str(), ssid.c_str(), ch, rssi, auth.c_str());
  else                 biscuitCompatPushAp(bssid.c_str(), ssid.c_str(), ch, rssi, auth.c_str());
  coreRecordsRx++;

  // Roll the cluster's finds into the same counters solo wardriving uses, so the
  // HOME/STAT/TOOL screens reflect the whole mesh instead of showing zeros.
  if (ch >= 1 && ch <= 14)        networksFound2G++;
  else if (ch >= 32 && ch <= 177) networksFound5G++;
  else                            devicesFoundBle++;   // ch 0 => BLE record
  screenTelemetryObserve(ssid, bssid, rssi, ch);
  screenTelemetry.loggedRows++;
}

// ================================================================
//  Per-channel async scan (JCMK startNextNodeAssignedScan pattern).
//  Scans one assigned channel per call at NODE_SCAN_DWELL_MS dwell,
//  cycles through the full assigned range, then enters a ch-6 admin
//  window (heartbeat + NODE_ADMIN_WIN_MS) before the next cycle.
// ================================================================
static void nodeDoScanTick() {
  uint8_t numCh = biscuitChannelCount ? biscuitChannelCount :
                  ((jcmkEndIdx >= jcmkStartIdx) ? (jcmkEndIdx - jcmkStartIdx + 1) : 0);
  if (numCh == 0) return;

  // Admin window: radio is on ch 6, heartbeat already sent, just waiting
  if (nodeScanAdminWin) {
    if (millis() - nodeScanAdminMs >= NODE_ADMIN_WIN_MS) {
      nodeScanAdminWin = false;
      nodeScanChOffset = 0;  // begin next cycle
    }
    return;
  }

  if (!nodeScanActive) {
    // Full cycle complete — enter admin window
    if (nodeScanChOffset >= numCh) {
      jcmkSetChannel(JCMK_ESPNOW_CH);
      while (GPSSerial.available()) gps.encode(GPSSerial.read());
      if (jcmkHaveCore) { jcmkSendHeartbeat(); jcmkLastHbMs = millis(); }
      nodeScanAdminWin = true;
      // Random jitter (0-200 ms) staggers admin windows so nodes with similar
      // channel counts don't all transmit on ch 6 at the same instant.
      nodeScanAdminMs  = millis() + (uint32_t)(esp_random() % 200);
      return;
    }

    uint8_t chIdx = jcmkStartIdx + nodeScanChOffset;
    if (!biscuitChannelCount && chIdx >= JCMK_NUM_CHANNELS) { nodeScanChOffset++; return; }
    uint8_t channel = biscuitChannelCount ? biscuitChannels[nodeScanChOffset] : JCMK_CHANNELS[chIdx];

    // Skip 5 GHz channels on 2.4 GHz-only hardware (S3, C6).
    // wardriverIsC5() returns true only for C5 (dual-band); all others are 2.4 GHz only.
    if (channel > 14 && !wardriverIsC5()) {
      nodeScanChOffset++;  // hop over without attempting the scan
      return;
    }

    // Async scan of this single channel only (no blocking).
    // Phase 2: adaptive dwell + DFS passive when enabled (no-op unless flag).
    PigScanPlan plan = pig_scan_plan(channel, PIG_PASS_FAST, 0);
    int16_t rc = WiFi.scanNetworks(/*async*/true, /*hidden*/true,
                                    plan.passive, plan.dwell_ms, channel);
    if (rc == WIFI_SCAN_RUNNING || rc == 0) {
      nodeScanActive = true;
    } else {
      nodeScanChOffset++;  // skip failed channel
    }
    return;
  }

  // Scan in progress — poll for completion
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;

  if (n > 0) {
    jcmkNetworksFound += (uint32_t)n;
    // Return to ch 6 before any ESP-Now send (JCMK sendBroadcastStringPlain pattern).
    // The scan left the radio on the scan channel; Core listens only on ch 6.
    jcmkSetChannel(JCMK_ESPNOW_CH);
    for (int i = 0; i < n; i++) {
      if (pigDedupSkip(i)) continue;   // Phase 1 dedup: skip redundant repeat (no-op unless flag)
      String bssid = WiFi.BSSIDstr(i);
      String ssid  = WiFi.SSID(i);
      String auth  = meshAuthModeToString(WiFi.encryptionType(i));
      // A plain-reply Biscuit Core expects the bracketed AuthMode token set
      // its own nodes emit; a Piglet/JCMK Core takes the bare form unchanged.
      if (biscuitPlainReplyPending) {
        if      (auth == "OPEN")     auth = "[OPEN]";
        else if (auth == "WEP")      auth = "[WEP]";
        else if (auth == "WPA")      auth = "[WPA_PSK]";
        else if (auth == "WPA2")     auth = "[WPA2_PSK]";
        else if (auth == "WPA3")     auth = "[WPA3_PSK]";
        else if (auth == "WPAWPA2")  auth = "[WPA_WPA2_PSK]";
        else if (auth == "WPA2EAP")  auth = "[WPA2_ENTERPRISE]";
        else if (auth == "WPA2WPA3") auth = "[WPA2_WPA3_PSK]";
        else                         auth = "[UNDEFINED]";
      }
      int    ch    = WiFi.channel(i);
      int    rssi  = WiFi.RSSI(i);
      String line  = bssid + "," + ssid + "," + auth + ","
                   + String(ch) + "," + String(rssi) + ",W";
      jcmkSendText(line);
      jcmkSentCount++;
    }
  }
  WiFi.scanDelete();
  nodeScanActive = false;
  nodeScanChOffset++;
}

// ================================================================
//  Lifecycle
// ================================================================
void enterNodeMode() {
  Serial.println("[MESH] Entering node mode");
  biscuitReporterReset();  // release ESP-NOW ownership from the solo reporter
  bleScannerShutdown();    // free the BT controller so ESP-NOW has clean 2.4 GHz airtime
  meshNodeActive        = false;
  jcmkHaveCore          = false;
  jcmkCoreFoundPending  = false;
  jcmkNetworksFound     = 0;
  jcmkSentCount         = 0;
  jcmkHbCounter         = 0;
  jcmkLastHbMs          = 0;
  jcmkLastReqMs         = 0;
  jcmkReqInterval       = JCMK_REQ_INIT_MS;
  jcmkStartIdx          = 0;
  jcmkEndIdx            = JCMK_NUM_CHANNELS - 1;
  jcmkAssignVer         = 0;
  nodeScanActive        = false;
  nodeScanChOffset      = 0;
  nodeScanAdminWin      = false;
  biscuitPlainReplyPending = false;
  biscuitChannelCount   = 0;

  // Mesh mode owns the WiFi stack — prevent stopAPIfAllowed() from firing
  // WiFi.disconnect(true,true) after esp_now_init() would kill the ESP-Now driver.
  apWindowActive = false;

  // Full WiFi deinit → reinit: clears previous AP channel/state from driver.
  // WiFi.disconnect(wifioff=true) only calls esp_wifi_stop() — it leaves the
  // driver in a tainted state that breaks ESP-Now receive on the next start.
  // WiFi.mode(WIFI_OFF) calls esp_wifi_deinit() for a true clean slate.
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);  // full deinit — clears previous AP channel state
  delay(200);
  WiFi.mode(WIFI_STA);  // clean reinit
  delay(200);

  // Init ESP-Now FIRST, then lock the home channel.
  // Calling setChannel before esp_now_init() risks the driver
  // resetting the channel back during its own initialisation.
  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[MESH] esp_now_init failed: %d\n", (int)err);
    return;
  }
  esp_now_register_recv_cb(jcmkOnRecv);

  // Lock radio to JCMK ESP-Now home channel AFTER init (matches JCMK pattern)
  delay(50);
  jcmkSetChannel(JCMK_ESPNOW_CH);
  // Verify the channel actually stuck
  { uint8_t pri; wifi_second_chan_t sec; esp_wifi_get_channel(&pri, &sec);
    Serial.printf("[MESH] Channel after set: %d (target=%d)%s\n", pri, JCMK_ESPNOW_CH,
      (pri == JCMK_ESPNOW_CH) ? " OK" : " *** MISMATCH — trying again");
    if (pri != JCMK_ESPNOW_CH) { delay(50); jcmkSetChannel(JCMK_ESPNOW_CH);
      esp_wifi_get_channel(&pri, &sec);
      Serial.printf("[MESH] Channel after retry: %d\n", pri); }
  }

  jcmkAddPeer(JCMK_BCAST);

  // Random stagger before first scan (JCMK begin() pattern: delay(random(100,5000))).
  // Prevents multiple nodes starting cycles in sync and flooding simultaneously.
  delay(random(200, 3000));

  meshNodeActive = true;
  Serial.println("[MESH] ESP-Now ready — searching for Core on ch 6...");
}

void exitNodeMode() {
  Serial.println("[MESH] Exiting node mode");
  if (meshNodeActive) esp_now_deinit();
  meshNodeActive = false;
  jcmkHaveCore   = false;

  // Full WiFi stack reset: OFF then STA gives a clean state after esp_now_deinit
  WiFi.mode(WIFI_OFF);
  delay(150);
  WiFi.setAutoReconnect(true);  // restore auto-reconnect for normal wardriving
  WiFi.mode(WIFI_STA);
  delay(100);

  // Drain GPS serial buffer accumulated during mesh mode scans
  while (GPSSerial.available()) gps.encode(GPSSerial.read());

  // Re-enable scanning so normal wardriving resumes immediately
  scanningEnabled = true;
}

// ================================================================
//  Core mode lifecycle
// ================================================================
void enterCoreMode() {
  Serial.println("[CORE] Entering Core mode");
  biscuitReporterReset();  // release ESP-NOW ownership from the solo reporter
  bleScannerShutdown();    // free the BT controller so ESP-NOW has clean 2.4 GHz airtime

  meshCoreActive = false;
  coreRecordsRx  = 0;
  coreNodeCount  = 0;
  coreAssignVer  = 0;
  coreHbCounter  = 0;
  coreLastHbMs   = 0;
  coreReqHead = coreReqTail = 0;
  coreTextHead = coreTextTail = 0;
  memset(coreNodes, 0, sizeof(coreNodes));
  meshProfilesInit();  // load saved sub-cluster profiles from SD

  // Mesh mode owns the WiFi stack — prevent stopAPIfAllowed() from firing
  // WiFi.disconnect(true,true) after esp_now_init() would kill the ESP-Now driver.
  apWindowActive = false;

  // Full WiFi deinit → reinit: clears previous AP channel/state from driver.
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);  // full deinit — clears previous AP channel state
  delay(200);
  WiFi.mode(WIFI_STA);  // clean reinit
  delay(200);

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    Serial.printf("[CORE] esp_now_init failed: %d\n", (int)err);
    return;
  }
  esp_now_register_recv_cb(jcmkOnRecv);
  delay(50);
  jcmkSetChannel(JCMK_ESPNOW_CH);
  // Verify the channel actually stuck
  { uint8_t pri; wifi_second_chan_t sec; esp_wifi_get_channel(&pri, &sec);
    Serial.printf("[CORE] Channel after set: %d (target=%d)%s\n", pri, JCMK_ESPNOW_CH,
      (pri == JCMK_ESPNOW_CH) ? " OK" : " *** MISMATCH — trying again");
    if (pri != JCMK_ESPNOW_CH) { delay(50); jcmkSetChannel(JCMK_ESPNOW_CH);
      esp_wifi_get_channel(&pri, &sec);
      Serial.printf("[CORE] Channel after retry: %d\n", pri); }
  }
  jcmkAddPeer(JCMK_BCAST);

  meshCoreActive = true;

  // Bring BLE back up (it was shut down above for clean ESP-NOW airtime) so the
  // Biscuit Manager app sees this unit as a Biscuit Pro named "Piglet-Core...".
  // BLE + ESP-NOW coexist under the radio arbiter, same as the Arduino bridge;
  // expect a small ESP-NOW throughput cost while a phone is connected.
  // Latch the BLE personality for this Core session from the CORE BLE APP
  // setting, then stand up the matching standalone bridge.
  coreBleIsIosEsp = (cfg.blePersonality == "wardrivego");
  if (coreBleIsIosEsp) {
    // Advertises as "iosESP-XXXX" to the RocketGod Wardrive Go / WarBLE Go app.
    iosEspCompatBeginStandalone();
  } else {
    // Keep the Biscuit-facing GAP name SHORT ("Piglet"): it must fit the 31-byte
    // advertisement alongside the 128-bit Biscuit service UUID, or NimBLE drops
    // the local name and the app shows "Unknown".
    biscuitCompatBeginStandalone("Piglet");
  }

  Serial.printf("[CORE] Ready — listening for nodes on ch 6 (BLE app: %s)\n",
                coreBleIsIosEsp ? "Wardrive Go" : "Biscuit Manager");
}

void exitCoreMode() {
  Serial.println("[CORE] Exiting Core mode");
  // Tear down whichever BLE bridge this session brought up, before WiFi/ESP-NOW churn.
  if (coreBleIsIosEsp) iosEspCompatStopStandalone();
  else                 biscuitCompatStopStandalone();
  if (meshCoreActive) esp_now_deinit();
  meshCoreActive = false;
  coreNodeCount  = 0;

  WiFi.mode(WIFI_OFF);
  delay(150);
  WiFi.setAutoReconnect(true);  // restore auto-reconnect for normal wardriving
  WiFi.mode(WIFI_STA);
  delay(100);

  while (GPSSerial.available()) gps.encode(GPSSerial.read());
  scanningEnabled = true;
}

// ================================================================
//  Biscuit Reporter (solo mode)
// ================================================================
static void biscuitOnRecv(const esp_now_recv_info_t* info,
                          const uint8_t* data, int len) {
  (void)info;
  if (len < 5) return;
  if (data[0] != 'E' || data[1] != 'N' || data[2] != 'O' || data[3] != 'W') return;
  if (data[4] == JCMK_MSG_CORE_REPLY) biscuitLinked = true;
}

void biscuitReporterReset() {
  // Core/Node mode is taking the ESP-NOW driver (its WiFi.mode(OFF) deinits it),
  // so just drop our flags — we re-init cleanly next time we're solo.
  biscuitInited = false;
  biscuitReporterActive = false;
  biscuitLinked = false;
  biscuitFwdCount = 0;
}

void biscuitForwardNetwork(const String& bssid, const String& ssid,
                           const String& auth, int ch, int rssi) {
  if (!cfg.biscuitReport || meshCoreActive || meshNodeActive) return;
  if (biscuitFwdCount >= BISCUIT_FWD_QUEUE) return;  // drop overflow; device re-advertises
  String line = bssid + "," + ssid + "," + auth + "," + String(ch) + "," + String(rssi) + ",W";
  uint16_t slen = (line.length() < JCMK_TEXT_MAX) ? line.length() : JCMK_TEXT_MAX;
  memcpy(biscuitFwd[biscuitFwdCount], line.c_str(), slen);
  biscuitFwd[biscuitFwdCount][slen] = '\0';
  biscuitFwdCount++;
}

// Safe to retune to ch 6 only when solo, scanning (STA, not connected), no AP up.
static bool biscuitRadioSafe() {
  if (!cfg.biscuitReport || meshCoreActive || meshNodeActive) return false;
  if (apWindowActive) return false;
  if (WiFi.status() == WL_CONNECTED) return false;  // don't break a home-WiFi link
  const wifi_mode_t m = WiFi.getMode();
  return (m == WIFI_STA || m == WIFI_AP_STA);
}

void biscuitReporterFlush() {
  if (!biscuitRadioSafe()) {
    biscuitFwdCount = 0;  // can't send safely — discard this batch
    if (!cfg.biscuitReport && biscuitInited) {
      esp_now_deinit();
      biscuitInited = false;
      biscuitReporterActive = false;
      Serial.println("[BISCUIT] Reporter stopped");
    }
    return;
  }

  if (!biscuitInited) {
    if (esp_now_init() != ESP_OK) { biscuitFwdCount = 0; return; }
    esp_now_register_recv_cb(biscuitOnRecv);
    jcmkAddPeer(JCMK_BCAST);
    biscuitInited = true;
    biscuitReporterActive = true;
    biscuitLinked = false;
    biscuitLastReqMs = 0;  // announce immediately
    Serial.println("[BISCUIT] Reporter online (ch 6)");
  }

  jcmkSetChannel(JCMK_ESPNOW_CH);
  while (GPSSerial.available()) gps.encode(GPSSerial.read());

  const uint32_t now = millis();

  // Announce (CORE_REQUEST) until a manager replies, then only occasionally.
  const uint32_t reqGap = biscuitLinked ? 15000UL : 2000UL;
  if (now - biscuitLastReqMs >= reqGap) { jcmkSendCoreRequest(); biscuitLastReqMs = now; }

  // Keepalive heartbeat.
  if (now - biscuitLastHbMs >= JCMK_HB_MS) { jcmkSendHeartbeat(); biscuitLastHbMs = now; }

  // Forward this batch's networks as biscuit records.
  for (int i = 0; i < biscuitFwdCount; i++) {
    jcmkSendText(String(biscuitFwd[i]));
    biscuitRecordsSent++;
  }
  biscuitFwdCount = 0;
}

void coreModeTick() {
  if (!meshCoreActive) return;
  // Service whichever phone-app bridge this session brought up.
  if (coreBleIsIosEsp) iosEspCompatTick();   // Wardrive Go: commands + WD: telemetry
  else                 biscuitCompatTick();  // Biscuit Manager: commands + telemetry
  uint32_t now = millis();

  // Stream the device position ~1 Hz so the app can pin APs to our location.
  static uint32_t biscuitGpsMs = 0;
  if (gpsHasFix && now - biscuitGpsMs >= 1000) {
    biscuitGpsMs = now;
    if (coreBleIsIosEsp)
      iosEspCompatPushGps(gps.location.lat(), gps.location.lng(),
                          gps.altitude.meters(), (int)gps.satellites.value());
    else
      biscuitCompatPushGps(gps.location.lat(), gps.location.lng(),
                           gps.altitude.meters(), gps.hdop.hdop());
  }

  // Node-roster snapshot is Biscuit-only (Wardrive Go has no node roster concept).
  // Stream it to the Biscuit app's Node Management screen — on request
  // (CMD:getnodesnapshot) or every ~3 s so it stays live. Capture the active node
  // indices first so the header count always matches the records.
  static uint32_t biscuitSnapMs = 0;
  if (!coreBleIsIosEsp && biscuitCompatActive() &&
      (biscuitCompatConsumeSnapshotRequest() || now - biscuitSnapMs >= 3000)) {
    biscuitSnapMs = now;
    uint8_t  act[CORE_MAX_NODES];
    uint16_t n = 0;
    for (uint8_t i = 0; i < CORE_MAX_NODES; i++)
      if (coreNodes[i].active) act[n++] = i;
    uint16_t sid = biscuitCompatSnapshotBegin(n);
    for (uint16_t k = 0; k < n; k++) {
      CoreNodeInfo& nd = coreNodes[act[k]];
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               nd.mac[0], nd.mac[1], nd.mac[2], nd.mac[3], nd.mac[4], nd.mac[5]);
      biscuitCompatSnapshotRecord(sid, k, mac, (int)nd.recordsRx, -50, 1 /*C5/biscuit*/);
    }
    biscuitCompatSnapshotComplete(sid);
  }

  // 1. Process pending CORE_REQUEST queue (new node registrations)
  while (coreReqHead != coreReqTail) {
    uint8_t i = coreReqHead;
    coreReqHead = (coreReqHead + 1) % CORE_REQ_QUEUE;
    coreFindOrAddNode(coreReqBuf[i].mac, coreReqBuf[i].isBiscuit, coreReqBuf[i].band24);
    coreReassignChannels();
  }

  // 2. Process pending TEXT records
  while (coreTextHead != coreTextTail) {
    uint8_t i = coreTextHead;
    coreTextHead = (coreTextHead + 1) % CORE_TEXT_QUEUE;
    coreParseAndLogText(coreTextBuf[i].line);
  }

  // 3. Periodic heartbeat + ADMIN refresh to all connected nodes.
  // Nodes that missed the ADMIN while scanning will recover within one cycle.
  if (now - coreLastHbMs >= CORE_HB_MS) {
    coreLastHbMs = now;
    coreSendHeartbeatToAll();
    coreResendAdminToAll();
  }

  // 3b. If Biscuit Report is on, also announce ourselves to a parent Biscuit
  // Manager with a full-size CORE_REQUEST so it lists this Core as a biscuit.
  // Our own nodes ignore CORE_REQUEST, and any reply the Manager sends is ignored
  // by the Core branch — so this doesn't disturb our mesh.
  static uint32_t coreAnnounceMs = 0;
  if (cfg.biscuitReport && (now - coreAnnounceMs >= 3000)) {
    coreAnnounceMs = now;
    jcmkSendCoreRequest();
  }

  // 4. Node timeout check
  bool changed = false;
  for (uint8_t i = 0; i < CORE_MAX_NODES; i++) {
    if (coreNodes[i].active && (now - coreNodes[i].lastHbMs) > CORE_NODE_TIMEOUT) {
      Serial.printf("[CORE] Node %d timed out\n", i);
      esp_now_del_peer(coreNodes[i].mac);
      memset(&coreNodes[i], 0, sizeof(CoreNodeInfo));
      coreNodeCount--;
      changed = true;
    }
  }
  if (changed) coreReassignChannels();
}

// ================================================================
//  Loop tick
// ================================================================
void nodeModeTick() {
  if (!meshNodeActive) return;
  uint32_t now = millis();

  // Consume pending core-found event from the ESP-Now callback
  if (jcmkCoreFoundPending) {
    jcmkCoreFoundPending = false;
    memcpy(jcmkCoreMac, jcmkCoreMacPending, 6);
    jcmkHaveCore    = true;
    jcmkReqInterval = JCMK_REQ_INIT_MS;
    jcmkAddPeer(jcmkCoreMac);
    if (biscuitPlainReplyPending) {
      // Identify ourselves to the Biscuit Core ("Piglet:<version>") and give
      // it a short ch-6 admin window to send its role/channel-config handshake.
      jcmkSetChannel(JCMK_ESPNOW_CH);
      biscuitSendControl(7, 0, "Piglet:" FIRMWARE_VERSION);
      nodeScanAdminWin = true;
      nodeScanAdminMs  = now;
    }
    Serial.printf("[MESH] Core: %02X:%02X:%02X:%02X:%02X:%02X%s\n",
      jcmkCoreMac[0], jcmkCoreMac[1], jcmkCoreMac[2],
      jcmkCoreMac[3], jcmkCoreMac[4], jcmkCoreMac[5],
      biscuitPlainReplyPending ? " (Biscuit plain-reply)" : "");
  }

  // CORE_REQUEST with backoff (only while radio is free)
  if (!jcmkHaveCore && !nodeScanActive && (now - jcmkLastReqMs >= jcmkReqInterval)) {
    jcmkLastReqMs  = now;
    jcmkSetChannel(JCMK_ESPNOW_CH);
    jcmkSendCoreRequest();
    jcmkReqInterval = (jcmkReqInterval * 2 > JCMK_REQ_MAX_MS)
                      ? JCMK_REQ_MAX_MS : jcmkReqInterval * 2;
  }

  // Heartbeat backup timer (cycle-end heartbeat is primary; fires if scan stalls)
  if (jcmkHaveCore && !nodeScanActive && (now - jcmkLastHbMs >= JCMK_HB_MS)) {
    jcmkLastHbMs = now;
    jcmkSetChannel(JCMK_ESPNOW_CH);
    jcmkSendHeartbeat();
  }

  // Per-channel async scan — runs continuously while connected to Core
  if (jcmkHaveCore) nodeDoScanTick();
}

// ================================================================
//  OLED page renderer (page 5) — handles both Node and Core mode
// ================================================================
void drawPageMeshNode() {
  // Rendering is owned by the Freenove TFT frontend.
}

