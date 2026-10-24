# BLE & Detectors

WarHog adds a full BLE layer (not present upstream): BLE wardriving plus passive
detectors and active advertisement floods. Everything shares **one NimBLE stack**, so
ownership is coordinated carefully.

## Radio ownership (important)
There is exactly one NimBLE controller. The wardriving scanner, the detectors, the phone
bridges, and the spam tools all use it, and ESP-NOW mesh mode wants the Wi-Fi radio. The
rules enforced in code:
- `bleScannerShutdown()` fully releases the BT controller **before** entering the ESP-NOW
  mesh (a BLE↔ESP-NOW transition without it crashes the device).
- A standalone phone bridge calls NimBLE `init()` first; `BleScanner` then sees an
  initialized stack and only starts its scan (so a GATT server + advertising + an active
  scan coexist).
- Teardown only frees the controller when **no other consumer** is using it
  (`bleOwnedElsewhere()` guards `freeStack`).

## BLE wardriving (`BleScanner.cpp`)
`bleScannerTick()` runs scans and drains results, geo-dedups a MAC (re-logs only after it
moves or goes stale), and writes BLE rows via `appendWigleBleRow()`. The dedup table is
PSRAM-backed so it doesn't starve internal RAM. `bleUniqueCount()` reports distinct MACs
this session.

## Passive detectors (`BleDetectors.cpp` + friends)
These classify advertisements and annotate/log matches:
- **AirTag / FindMy** (`AirTagMonitor.h`) — Apple offline-finding beacons.
- **Flipper Zero** (`FlipperSniff.h`) — Flipper BLE advertisements.
- **Pwnagotchi** (`PwnagotchiDetect.h`) — Pwnagotchi presence beacons.
`bleDetectorsActive()` is true while any detector owns the stack.

## Active advertisement floods (`BleSpam.cpp`, `AppleBleSpam.cpp`)
Multi-platform BLE advertisement spam for research/testing:
- `BleSpam` — SwiftPair, Samsung, Google Fast Pair, and others (`BleSpamType`,
  `bleSpamTypeName()`), with `bleSpamStart(type)/Stop/Tick` and a session `bleSpamCount()`.
- `AppleBleSpam` — the Apple-proximity variant.

> These are offensive/dual-use tools. Use them only on networks and devices you are
> authorized to test. See the project license and your local regulations.
