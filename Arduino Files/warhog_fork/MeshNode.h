#pragma once
#include <Arduino.h>

// ================================================================
//  MeshNode — JCMK-compatible ESP-Now wardriving node (page 5)
//  Compatible with justcallmekoko/ESP32DualBandWardriver core role.
// ================================================================

// JCMK channel table (shared so Display can show range labels)
extern const uint8_t JCMK_ESPNOW_CH;
extern const uint8_t JCMK_NUM_CHANNELS;
extern const uint8_t JCMK_CHANNELS[];

// Node state (read from Display.cpp to render page 5)
extern bool     meshNodeActive;
extern bool     jcmkHaveCore;
extern uint8_t  jcmkCoreMac[6];
extern uint8_t  jcmkAssignVer;
extern uint8_t  jcmkStartIdx;
extern uint8_t  jcmkEndIdx;
extern uint32_t jcmkNetworksFound;
extern uint32_t jcmkSentCount;
// Count of ESP-Now sends that the radio/driver itself reported as failed
// (via the send-status callback) -- distinguishes "attempted" from
// "actually transmitted" since broadcast frames have no delivery ACK at all.
extern uint32_t jcmkSendFailCount;
// Mesh size (total active nodes) as last reported by the Core's heartbeat.
// A node's only view of the whole mesh; drives the herd count on screen.
extern uint8_t  jcmkMeshHerd;

// Lifecycle — called from WarHog.ino on page enter/exit
void enterNodeMode();
void exitNodeMode();

// Loop tick — call every loop() iteration while on page 5
void nodeModeTick();

// OLED page renderer — called from Display.cpp updateOLED() (handles both modes)
void drawPageMeshNode();

// ================================================================
//  MeshCore — Core (coordinator) role
//  On the mesh page / PigWorld view, a long-press cycles the device role
//  Solo -> Mesh Core -> Mesh Node -> Solo. The mesh page defaults to Solo
//  (it no longer auto-joins as Node); leaving the page returns to Solo.
// ================================================================

#define CORE_MAX_NODES 12

struct CoreNodeInfo {
  bool     active;
  uint8_t  mac[6];
  uint8_t  startIdx;   // index into JCMK_CHANNELS[]
  uint8_t  endIdx;
  uint32_t lastHbMs;
  uint32_t recordsRx;
  bool     isBiscuit;  // true = Biscuit Node protocol (requires full-size 212-byte packets)
  bool     isWarHog;   // true = confirmed genuine WarHog node (via marker handshake) --
                        // only these participate in deterministic transmit-slot scheduling
  uint8_t  slotIndex;  // this node's transmit slot among other WarHog nodes (valid when isWarHog)
};

// Core state (read from Display.cpp for page 5 rendering)
extern bool          meshCoreActive;
extern uint32_t      coreRecordsRx;
extern uint8_t       coreNodeCount;
extern CoreNodeInfo  coreNodes[CORE_MAX_NODES];

void enterCoreMode();
void exitCoreMode();
void coreModeTick();
