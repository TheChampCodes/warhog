# Companion Apps & Bridges

WarHog streams live data to phone apps over BLE GATT. The two bridges share the single
NimBLE stack with the BLE scanner/detectors (see [BLE & Detectors](BLE-and-Detectors.md) for
ownership rules). The active bridge is chosen by `cfg.blePersonality`
(`biscuit` → Biscuit Manager, `wardrivego` → Wardrive Go).

> **Fork note:** upstream Piglet reports to a Biscuit *Core* over ESP-NOW; the BLE/GATT
> phone bridges and the solo-mode bridge are WarHog additions. See
> [Upstream Differences](Upstream-Differences.md).

## When a bridge runs
- **Core mode:** the bridge is mandatory — the app is the only live window into mesh
  traffic — so `enterCoreMode()` owns it.
- **Solo mode:** `phoneBridgeSoloTick()` (in `MeshNode.cpp`) brings the same bridge up and
  feeds it from the local scanner, so one app works in both roles.
- **Node mode:** no bridge (the node owns the radio for scanning).

## Biscuit Manager (`BiscuitCompat.cpp`)
A GATT server presenting as a Biscuit Pro to the Biscuit Manager app.
- Lifecycle: `biscuitCompatAttach(server)` / `biscuitCompatBeginStandalone(name)` /
  `biscuitCompatStopStandalone(freeStack)` / `biscuitCompatDetach()`, `biscuitCompatTick()`.
- Data: `biscuitCompatPushAp(bssid, ssid, …)` and `biscuitCompatPushGps(lat, lon, alt, acc)`
  stream each sighting and ~1 Hz position.
- **Node roster** (Biscuit-only): the Node Management screen is served on request
  (`biscuitCompatConsumeSnapshotRequest()`) or every ~3 s via
  `biscuitCompatSnapshotBegin/Record/Complete()`, which stream per-node MAC + record counts.

## Wardrive Go (`IosEspCompat.cpp`)
The iOS "Wardrive Go" bridge (the `iosesp` protocol).
- Lifecycle mirrors Biscuit: `iosEspCompatBeginStandalone(name)` / `StopStandalone` / `Tick`.
- Data: `iosEspCompatPushAp(...)`, `iosEspCompatPushGps(lat, lon, alt, sats)`.
- `iosEspCompatSetGpsPresent(present)` tells the app whether a GPS module is *wired* (so a
  phone that connects before lock isn't told `gps=0` for the whole session). The WD:CAPS
  line is sent once on subscribe; TX honours notify() backpressure.

## Core BLE Bridge (`BLEBridge.cpp`, C5 Core)
On the C5 Core, `BLEBridge` forwards BLE *records* (not just Wi-Fi) to the companion app,
so BLE wardriving results reach the phone alongside AP sightings.

## Interop notes
- Records keep the **212-byte** packet size so Biscuit Pro accepts them.
- A WarHog node identifies itself to a Biscuit Core as `"WarHog:<version>"` during the
  ch-6 admin handshake, which is how a Core categorizes it as a "Biscuit node" while still
  routing its records through the WarHog log path.
