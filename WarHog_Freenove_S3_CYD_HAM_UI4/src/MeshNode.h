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

// Lifecycle — called from WarHog.ino on page enter/exit
void enterNodeMode();
void exitNodeMode();

// Loop tick — call every loop() iteration while on page 5
void nodeModeTick();

// OLED page renderer — called from Display.cpp updateOLED() (handles both modes)
void drawPageMeshNode();

// ================================================================
//  MeshCore — Core (coordinator) role
//  Long-press on page 5 while in Node mode activates Core mode;
//  long-press again returns to Node mode.
// ================================================================

#define CORE_MAX_NODES 12

// Sub-clusters: nodes sharing one antenna / location are grouped so the Core
// splits channels *within* the group (no two members ever scan the same
// channel at once); different groups each cover the spectrum independently.
// Group IDs are 1-based; every node defaults to group 1 (one global split,
// i.e. the original behaviour) until the user moves it to another sub-cluster.
#define CORE_MAX_GROUPS 9

struct CoreNodeInfo {
  bool     active;
  uint8_t  mac[6];
  uint8_t  startIdx;   // index into JCMK_CHANNELS[] (display range only)
  uint8_t  endIdx;
  uint32_t lastHbMs;
  uint32_t recordsRx;
  bool     isBiscuit;  // true = Biscuit Node protocol (requires full-size 212-byte packets)
  bool     band24Only; // node declared itself 2.4 GHz-only (gets 1/6/11, never a 5 GHz assignment)
  uint8_t  group;      // sub-cluster this node belongs to (1..CORE_MAX_GROUPS)
  uint8_t  chanCount;  // number of channels actually assigned (for the UI)
  uint8_t  chans[32];  // exact channel list this node is scanning (for the UI)
};

// Core state (read from Display.cpp for page 5 rendering)
extern bool          meshCoreActive;

// ---- Solo-mode phone bridge (cfg.blePhoneSolo) --------------------------
// Keeps the same phone-app GATT bridge the Core uses running while the device
// is SOLO wardriving, so the app sees this unit's own scan results. Call the
// tick every loop from the non-mesh path; push each scanned AP from the solo
// scanner. Both are no-ops unless the bridge is up.
void phoneBridgeSoloTick();
void phoneBridgeSoloPushAp(const char* bssid, const char* ssid,
                           const char* auth, int ch, int rssi);
bool phoneBridgeSoloActive();
extern uint32_t      coreRecordsRx;
extern uint8_t       coreNodeCount;
extern CoreNodeInfo  coreNodes[CORE_MAX_NODES];

void enterCoreMode();
void exitCoreMode();
void coreModeTick();

// Move a worker node into a sub-cluster (1..CORE_MAX_GROUPS) and immediately
// recompute + push channel assignments to every node. The grouping is also
// remembered by MAC so a node that drops and rejoins returns to its cluster.
// Called from the on-device Nodes UI.
void coreSetNodeGroup(uint8_t nodeIdx, uint8_t group);

// Count active worker nodes currently in a given sub-cluster (for the UI).
uint8_t coreGroupMemberCount(uint8_t group);

// ---- Wardriving profiles --------------------------------------------------
// There is one built-in DEFAULT profile plus MESH_NUM_PROFILES user slots.
//  - DEFAULT: every node in one sub-cluster, so channels are split evenly
//    across however many nodes are connected, band-aware (2.4-only nodes get
//    1/6/11, dual-band nodes share the 5 GHz space). This is the base
//    behaviour; meshApplyDefault() forces every node back to it.
//  - User slots: named node→sub-cluster presets saved to the SD card, so a
//    recurring physical setup can be recalled instead of re-grouping each time.
#define MESH_NUM_PROFILES     3
#define MESH_PROFILE_NAME_MAX 15

void        meshProfilesInit();                      // load from SD (idempotent)
void        meshApplyDefault();                      // reset all nodes to one even split
bool        meshProfileUsed(uint8_t slot);           // slot occupied?
const char* meshProfileName(uint8_t slot);           // display name
uint8_t     meshProfileNodeCount(uint8_t slot);      // nodes stored in the slot
void        meshSaveProfile(uint8_t slot, const char* name);  // capture current grouping
uint8_t     meshApplyProfile(uint8_t slot);          // apply; returns nodes matched
void        meshDeleteProfile(uint8_t slot);

// ================================================================
//  Biscuit Reporter — solo-mode telemetry to a Biscuit Manager/Core
//  When cfg.biscuitReport is on and the device is wardriving solo, it
//  announces itself and forwards found networks over ESP-NOW (ch 6) so
//  a Biscuit Manager lists it as a biscuit. Local SD logging continues.
// ================================================================
extern bool     biscuitReporterActive;  // ESP-NOW up and reporting
extern bool     biscuitLinked;          // a Core/manager has replied
extern uint32_t biscuitRecordsSent;

void biscuitForwardNetwork(const String& bssid, const String& ssid,
                           const String& auth, int ch, int rssi);  // buffer one row (from scanner)
void biscuitReporterFlush();  // send buffered rows + heartbeat on ch 6 (radio-idle point)
void biscuitReporterReset();  // relinquish the ESP-NOW driver to Core/Node mode
