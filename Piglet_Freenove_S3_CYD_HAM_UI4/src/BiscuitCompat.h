#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

// ================================================================
//  BiscuitCompat — makes the Piglet present itself to the
//  "Biscuit Manager" phone app as a Biscuit Pro, while the GAP
//  name still reads "Piglet...". Identify-only (minimal) build:
//  it stands up the Biscuit GATT service + Device Information,
//  answers info / node-list / ACK / TIME, and replies RSP:...:OK
//  to any command so the app connects, classifies the device as a
//  Pro, and stays happy. It does NOT yet map command verbs to real
//  Piglet features — see biscuitHandleCommand() for where to add
//  that later.
//
//  Protocol (reverse-engineered from Biscuit Manager 1.0.21.1):
//    - Service 4FAFC201-... with Command(write)/Response(notify)/
//      Status(notify)/Settings(read)/Security(read) characteristics.
//    - ASCII line protocol, one message per write/notify:
//        TYPE:SUBTYPE:PAYLOAD   (split on ':', max 3 fields)
//    - Tier is read from Device Information Model Number (0x2A24);
//      exactly "Biscuit Pro" => BISCUIT_PRO in the app. The GAP name
//      is a separate field, so it can stay "Piglet".
//
//  Library: NimBLE-Arduino v2.x (same as BLEBridge).
// ================================================================

// Advertised service UUID the Biscuit app's scan filter requires.
// Put THIS in the primary advertisement (see wiring notes).
extern const char* BISCUIT_SERVICE_UUID;   // "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"

// What the app will classify us as. "Biscuit Pro" unlocks the node
// manager + HID automation menus. Change to "Biscuit Ultra" for Ultra.
#ifndef BISCUIT_MODEL_STRING
#define BISCUIT_MODEL_STRING "Biscuit Ultra"
#endif

// Reported firmware revision (DIS 0x2A26). Must parse as vMAJOR.MINOR.PATCH
// and be > 1.5.60 to clear the app's HID-automation gate. Node-manager
// needs only the model string, not this.
#ifndef BISCUIT_FW_REVISION
#define BISCUIT_FW_REVISION "1.6.2"
#endif

// ---- Shared-server path (Arduino tree: ride the existing BLE bridge) -------
// Create the Biscuit service + Device Information on an already-created
// NimBLEServer, and install the Command write handler. Call once, after you
// create your own service(s) and BEFORE advertising starts. Does NOT init
// NimBLE or start advertising — the caller owns those (and must add
// BISCUIT_SERVICE_UUID to the advertisement; see BLEBridge.cpp).
void biscuitCompatAttach(NimBLEServer* server);

// ---- Standalone path (S3 tree: no existing Core-mode BLE server) -----------
// Init NimBLE (if needed), create a server, attach the Biscuit service, and
// start advertising BISCUIT_SERVICE_UUID with the given GAP name.
void biscuitCompatBeginStandalone(const char* gapName);
// freeStack=true (default, mesh Core path) also deinits the BT controller to
// hand its RAM back to ESP-NOW. Pass false when another BLE consumer may own
// the stack — in solo mode the wardriving BLE scanner and the Marauder
// detectors share it, and deinit would pull the radio out from under them.
void biscuitCompatStopStandalone(bool freeStack = true);
bool biscuitCompatActive();

// Shared-server path only: drop references + go inactive WITHOUT any NimBLE
// calls. Call this from your bridge's stop path right before/after it deinits
// NimBLE, so a later tick() won't touch freed characteristics.
void biscuitCompatDetach();

// Drain queued inbound commands and emit responses. Call from the main loop
// (bleBridgeTick in the Arduino tree, coreModeTick in the S3 tree). Safe to
// call when inactive (no-op).
void biscuitCompatTick();

// ---- Live telemetry push (so the app plots APs on its map in real time) ----
// Both are cheap no-ops unless a phone is connected AND subscribed to the
// Response characteristic, so they're safe to call from the hot record path.
// Enqueue one AP sighting -> "DATA:AP:bssid,ssid,channel,rssi,authmode".
// authMode should be WiGLE bracket form (e.g. "[WPA2-PSK-CCMP][ESS]"); a blank
// one is coerced to "[OPEN][ESS]". ch < 1 (BLE/unknown) is dropped (not an AP).
void biscuitCompatPushAp(const char* bssid, const char* ssid,
                         int channel, int rssi, const char* authMode);
// Enqueue the device position -> "DATA:GPS:lat,lon,alt,accuracy". Call ~1 Hz;
// the app pins the APs it has seen to the latest GPS fix.
void biscuitCompatPushGps(double lat, double lon, double altMeters, double accuracy);

// ---- Node roster snapshot (populates the app's Node Management screen) ------
// The firmware drives this from its core tick using its real node table. A
// snapshot is a 3-part stream: header -> one record per node -> completion.
//   header : DATA:NODESNAPSHOT:1,id,enabled,encrypt,maxActive,mode,30,known,count
//   record : DATA:NODESTATE:id,index,saved,MAC,state,rssi,scan,role,fw,type,caps
//   done   : RSP:nodesnapshot:id,OK,recordCount   (3 fields; app rejects 2)
// Returns true once when the app has asked via CMD:getnodesnapshot (clears it).
bool biscuitCompatConsumeSnapshotRequest();
// Emit the header for `nodeCount` nodes (mode=manager, maxActive=6, encrypt=1,
// known=count=nodeCount). Returns the snapshot id to pass to the record/complete
// calls. nodeCount is clamped to the registry capacity (30).
uint16_t biscuitCompatSnapshotBegin(uint16_t nodeCount);
// Emit one node record. macUpper must be "AA:BB:CC:DD:EE:FF" (uppercase).
// nodeType: 1=biscuit(C5), 2=pro, 3=ultra. saved+connected is assumed.
void biscuitCompatSnapshotRecord(uint16_t snapId, uint16_t index,
                                 const char* macUpper, int scanCount,
                                 int rssi, int nodeType);
// Emit the completion line that commits the snapshot in the app.
void biscuitCompatSnapshotComplete(uint16_t snapId);
