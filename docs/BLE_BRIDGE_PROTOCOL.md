# Piglet Core ↔ iOS BLE Bridge Protocol

**Version:** 1 (`PROTO_VER = 1`)
**Role:** The Piglet device running as **Mesh Core** acts as a BLE **peripheral** (GATT server). The iOS/iPadOS app is the BLE **central** (client).

## Why BLE (and not ESP-Now or WiFi)

- An iPhone/iPad **cannot** be an ESP-Now peer — iOS exposes no ESP-Now API. ESP-Now is an Espressif link-layer protocol.
- In mesh mode the Piglet firmware deliberately skips the SoftAP window ("ESP-Now owns the WiFi stack"), so there is **no HTTP server** to hit while meshing.
- BLE is a **separate radio stack** on the ESP32 and runs under the chip's WiFi/BLE **coexistence** arbitration, so the Core can keep meshing over ESP-Now **and** stream its aggregated data to the phone over BLE at the same time.

The Core is therefore the **bridge**: nodes → (ESP-Now) → Core → (BLE) → phone.

## Data flow

```
 Piglet Node ──ESP-Now──┐
 Piglet Node ──ESP-Now──┤
 PigletNode  ──ESP-Now──┼──▶  Piglet CORE  ──BLE GATT──▶  iPad / iPhone app
 (Biscuit/JCMK) ─ESPNow─┘      • GPS-stamps records       • live network list
                               • logs WiGLE CSV to SD      • node roster
                               • mirrors each record       • GPS / map
                                 + roster + status over     • counts
                                 BLE                        • start/stop + control
```

The Core's existing behaviour is unchanged: it still writes WiGLE CSV to SD. BLE is an **additive mirror** of the same records, plus live roster/status, plus a small control channel.

## GATT layout

All UUIDs are 128-bit, sharing base `7A1F00NN-9B7E-4C2A-9E1D-9A6F3C5B1E00`.

| Role | UUID | Properties | Payload |
|------|------|-----------|---------|
| **Service** | `7A1F0000-9B7E-4C2A-9E1D-9A6F3C5B1E00` | — | Piglet Mesh Core service |
| **Status** | `7A1F0001-9B7E-4C2A-9E1D-9A6F3C5B1E00` | Read, Notify | Core/GPS/session status (JSON) |
| **Record** | `7A1F0002-9B7E-4C2A-9E1D-9A6F3C5B1E00` | Notify | One scan record per notification (JSON) |
| **Nodes** | `7A1F0003-9B7E-4C2A-9E1D-9A6F3C5B1E00` | Read, Notify | Full node roster (JSON). Read for the whole list; the notify delivers the same payload when the roster changes. |
| **Control** | `7A1F0004-9B7E-4C2A-9E1D-9A6F3C5B1E00` | Write, Write-No-Response | Commands from app → Core (JSON) |

**Advertised name:** `Piglet-Core` (or `Piglet-Core <deviceName>` when `deviceName` is set in `wardriver.cfg`).
The Service UUID is included in the advertisement so the app can filter for Piglet Cores.

**MTU:** The Core requests an ATT MTU of 247. iOS typically negotiates 185–527. The Status and Record payloads are kept under ~180 bytes so they fit a single notification on any negotiated MTU. The Nodes payload can be larger than one notification — it is primarily a **Read** characteristic (CoreBluetooth performs the long-read/read-blob sequence automatically); the Notify on this characteristic is best-effort and the app should re-`read` it whenever `Status.rosterVer` changes.

## Payloads

All payloads are UTF-8 JSON (compact, no whitespace). JSON is used (rather than packed structs) so SSIDs containing commas/tabs are safe and the app parses with `JSONDecoder`.

### Status (Read / Notify, ~1 Hz)

```json
{
  "v": 1,
  "fw": "v2.62",
  "core": true,
  "rx": 10432,
  "nodes": 3,
  "rosterVer": 7,
  "found2g": 812,
  "found5g": 140,
  "fix": true,
  "lat": 32.2746,
  "lon": -90.1842,
  "alt": 87.4,
  "acc": 1.2,
  "upMs": 912333
}
```

| Field | Type | Meaning |
|-------|------|---------|
| `v` | int | Protocol version (`PROTO_VER`) |
| `fw` | string | `FIRMWARE_VERSION` |
| `core` | bool | `meshCoreActive` — true while in Core mode |
| `rx` | uint32 | `coreRecordsRx` — total records received from nodes this session |
| `nodes` | uint8 | `coreNodeCount` — active nodes in the roster |
| `rosterVer` | uint8 | Bumps whenever the roster changes; app re-reads **Nodes** on change |
| `found2g` / `found5g` | uint32 | Core's own `networksFound2G` / `networksFound5G` counters |
| `fix` | bool | `gpsHasFix` |
| `lat` `lon` `alt` `acc` | number | Present only when `fix` is true (`acc` = HDOP) |
| `upMs` | uint32 | `millis()` — device uptime, for liveness/debug |

### Record (Notify, one per notification)

Emitted once for **every** record the Core logs (same choke point as the WiGLE CSV write), so the app sees exactly what the Core records.

```json
{"b":"A4:B1:C2:D3:E4:F5","s":"HomeNet","a":"[WPA2-PSK-CCMP][ESS]","c":36,"r":-67,"lat":32.2746,"lon":-90.1842,"alt":87.4,"acc":1.2,"t":"2026-10-13T19:58:04Z"}
```

| Field | Type | Meaning |
|-------|------|---------|
| `b` | string | BSSID |
| `s` | string | SSID (may be empty; may contain Unicode) |
| `a` | string | Auth/capabilities string (WiGLE `AUTH` field) |
| `c` | int | Channel |
| `r` | int | RSSI (dBm) |
| `lat` `lon` `alt` `acc` | number | Core's GPS stamp at log time (0 when no fix) |
| `t` | string | ISO-8601 UTC timestamp (`iso8601NowUTC()`) |

> **v1 scope note:** records do not carry the source-node MAC, because the Core's internal text queue stores only the scan line, not the sender. Per-node attribution is available via the **Nodes** roster (counts per node). Adding a `"n"` (node MAC) field is a documented future enhancement — see firmware comments in `MeshNode.cpp`.

### Nodes (Read, and Notify on change)

```json
{"v":1,"rosterVer":7,"nodes":[
  {"mac":"24:6F:28:00:11:22","kind":"piglet","rx":5123,"ch0":14,"ch1":21,"slot":0,"ageMs":1200},
  {"mac":"24:6F:28:33:44:55","kind":"piglet","rx":4087,"ch0":22,"ch1":31,"slot":1,"ageMs":800},
  {"mac":"A0:B1:C2:D3:E4:F5","kind":"biscuit","rx":1222,"ch0":0,"ch1":0,"slot":0,"ageMs":2600}
]}
```

| Field | Type | Meaning |
|-------|------|---------|
| `mac` | string | Node MAC |
| `kind` | string | `"piglet"`, `"biscuit"`, or `"jcmk"` (from `isPiglet` / `isBiscuit`) |
| `rx` | uint32 | `recordsRx` for this node |
| `ch0` / `ch1` | uint8 | Assigned channel-index range (`startIdx`/`endIdx` into `JCMK_CHANNELS[]`) |
| `slot` | uint8 | Transmit slot (meaningful for Piglet nodes) |
| `ageMs` | uint32 | `millis() - lastHbMs` — how long since last heard |

### Control (Write / Write-No-Response)

App → Core. One JSON command per write.

```json
{"cmd":"clearStats"}
{"cmd":"exitMesh"}
{"cmd":"ping"}
```

| `cmd` | Effect |
|-------|--------|
| `clearStats` | Resets `coreRecordsRx` and every node's `recordsRx` to 0 (safe, applied in the main-loop tick). |
| `exitMesh` | Requests the Core to leave mesh mode and return to normal wardriving. Sets a flag consumed by the main loop (`bleBridgeExitRequested`); the firmware owner wires it to the same path as a page-exit. |
| `ping` | No-op; bumps a Status notify so the app can confirm the link round-trips. |

Unknown commands are ignored. Control writes are never processed inside the BLE callback — they set flags/state drained in `bleBridgeTick()` (main loop), consistent with the firmware's existing ISR-safety discipline.

## Threading / safety contract

- `bleBridgePushRecord()` is called from `coreParseAndLogText()` (main loop, via `coreModeTick`). It only **enqueues** into a ring buffer — no BLE calls.
- `bleBridgeTick()` (main loop) drains the ring buffer and sends notifications, builds the roster/status JSON, and applies pending control commands.
- Nothing in this module is called from the ESP-Now receive callback (`jcmkOnRecv`) or any ISR.

## Firmware integration points (canonical `Arduino Files/Piglet` tree)

| File | Change |
|------|--------|
| `BLEBridge.h` / `BLEBridge.cpp` | **New module** (this bridge). |
| `MeshNode.cpp` → `enterCoreMode()` | After `meshCoreActive = true;` call `bleBridgeBegin();` |
| `MeshNode.cpp` → `exitCoreMode()` | At the top call `bleBridgeStop();` |
| `MeshNode.cpp` → `coreParseAndLogText()` | After `coreRecordsRx++;` call `bleBridgePushRecord(...)`. |
| `MeshNode.cpp` → `coreModeTick()` | At the end call `bleBridgeTick();` |
| `Piglet.ino` (optional) | If `bleBridgeExitRequested` is set, run the normal "leave mesh page" path. |

**Library:** NimBLE-Arduino by h2zero, **v1.4.x** (lightweight; ~1/3 the RAM of Bluedroid). Pinned for the widely-installed 1.4 API. On NimBLE-Arduino **v2.x** the only change needed is the `onWrite` / `onConnect` callback signatures (they gain a `NimBLEConnInfo&` parameter) — noted inline in `BLEBridge.cpp`.
