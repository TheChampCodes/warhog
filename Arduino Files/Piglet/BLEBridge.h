#pragma once
#include <Arduino.h>

// ================================================================
//  BLEBridge — streams Mesh Core data to an iOS/iPadOS app over BLE
//
//  The Core is the only device that sees all mesh traffic. In mesh
//  mode ESP-Now owns the WiFi stack (no SoftAP / HTTP), and a phone
//  cannot be an ESP-Now peer, so BLE is the bridge: nodes -> ESP-Now
//  -> Core -> BLE GATT -> phone.
//
//  BLE runs under the chip's WiFi/BLE coexistence arbitration, so it
//  runs alongside the active ESP-Now mesh without disturbing it.
//
//  See docs/BLE_BRIDGE_PROTOCOL.md for the GATT layout and payloads.
//
//  Threading contract:
//    - bleBridgePushRecord() only ENQUEUES (safe to call from the
//      Core's main-loop record path). It performs no BLE calls.
//    - bleBridgeTick() (main loop) drains the queue, sends all
//      notifications, and applies pending control commands.
//    - Nothing here is called from the ESP-Now RX callback or an ISR.
//
//  Library: NimBLE-Arduino by h2zero (v2.x). Lightweight vs Bluedroid.
//  v2.x is required by the Arduino-ESP32 3.x / IDF 5 core this firmware
//  builds against; v1.4.x only compiles on the older 2.x core (IDF 4).
// ================================================================

#define BLE_BRIDGE_PROTO_VER 1

// Lifecycle — called from MeshNode.cpp Core lifecycle.
void bleBridgeBegin();   // start GATT server + advertising (call once Core is up)
void bleBridgeStop();    // stop advertising + deinit BLE (call when leaving Core mode)
bool bleBridgeActive();  // true while the bridge is running

// Per-record mirror — call from coreParseAndLogText() AFTER coreRecordsRx++.
// Only enqueues; the actual BLE notify happens in bleBridgeTick().
void bleBridgePushRecord(const char* bssid, const char* ssid, const char* auth,
                         int ch, int rssi,
                         double lat, double lon, double alt, double acc,
                         const char* isoTime);

// Main-loop tick — call at the end of coreModeTick().
void bleBridgeTick();

// Set true by the Control characteristic when the app sends {"cmd":"exitMesh"}.
// Consumed by the main loop (Piglet.ino) to leave Core mode cleanly — never
// act on it inside coreModeTick(), which is mid-drain when it runs.
extern volatile bool bleBridgeExitRequested;
