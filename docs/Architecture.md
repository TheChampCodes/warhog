# Architecture

## What Piglet is
A Piglet device scans the RF environment and records what it finds. Each scan result
(an AP or BLE device) is tagged with a GPS position and a UTC timestamp and written to
microSD as a **WiGLE-compliant CSV row**; raw 802.11 frames can additionally be logged
as **PCAP**. Devices can run alone (**solo wardriving**) or join an **ESP-NOW mesh** in
one of two roles.

## The three run modes
A board boots into one of these based on `cfg.meshModeOnBoot` (`solo`/`none`, `node`,
`core`, `off`), and most display boards can switch at runtime from the touch UI.

| Mode | What it does |
|---|---|
| **Solo** | The device scans all channels itself (`Scanner.cpp`) and logs locally. Optionally streams to a phone app over BLE. |
| **Node** (worker) | Joins a Core over ESP-NOW (channel 6), scans only the channels the Core assigns, and sends each record to the Core as a text/binary packet. Does not log to its own SD. |
| **Core** (coordinator) | Owns the mesh: registers nodes, assigns channels, receives every node's records, logs them to SD, and mirrors them to a companion phone app. A Core does not scan APs itself. |

## Boot and the main loop
Every firmware follows the same shape (see each project's `main.cpp` / `*.ino`):

1. **setup()** — bring up Serial/USB-CDC, load config from SD (`loadConfigFromSD`),
   init display, GPS, SD log file, BLE scanner, the Marauder tools, then enter the
   configured mesh mode (`enterCoreMode` / `enterNodeMode`) or stay solo.
2. **loop()** — a cooperative super-loop. It services the web server, serial commands,
   GPS, the display, the BLE/Marauder ticks, and then the active mode's tick
   (`coreModeTick` / `nodeModeTick`) or the solo scanner. Nothing blocks for long; scans
   are asynchronous (`WiFi.scanNetworks(async=true, …)`).

The super-loop is **cooperative and single-threaded on core 1** by default. The only
work deliberately moved off it is on dual-core S3 Cores under `PIG_FEAT_CORE_ISO`: SD
logging and the ESP-NOW record drain each run on a pinned **core-0 FreeRTOS task** so a
slow UI/web frame can't stall ESP-NOW servicing (see [Adaptive Scheduler](Adaptive-Scheduler.md)).

## Data flow: from a scan to a logged row
**Solo:**
```
WiFi.scanNetworks(async) ─▶ processScanResults() ─▶ appendWigleRow() ─▶ /logs/WiGLE_*.csv (SD)
                                     └─▶ phoneBridgeSoloPushAp() ─▶ BLE companion app
```
**Mesh:**
```
Node:  assigned-channel scan ─▶ (Phase-1 dedup) ─▶ jcmkSendText()  ──ESP-NOW──▶ Core
Core:  jcmkOnRecv() ─▶ coreTextBuf ─▶ coreParseAndLogText() ─▶ appendWigleRow() (SD)
                                               └─▶ iosEsp/BiscuitCompat push ─▶ phone app
```
On a Core with `PIG_FEAT_CORE_ISO`, the parse/SD-enqueue half runs on the core-0 drain
task and the phone-app/telemetry half runs on the loop, joined by a small ring.

## Subsystem map
| Area | Modules | Page |
|---|---|---|
| Wi-Fi scan + logging | `Scanner`, `SDUtils`, `SessionStats`, `HashcatWriter` | [Scanning & Logging](Scanning-and-Logging.md) |
| Mesh | `MeshNode`, `scheduler/pig_proto.h` | [Mesh Protocol](Mesh-Protocol.md) |
| Scheduler | `scheduler/pig_*.h`, `yield_sched.h` | [Adaptive Scheduler](Adaptive-Scheduler.md) |
| BLE | `BleScanner`, `BleDetectors`, `AirTagMonitor`, `FlipperSniff`, `PwnagotchiDetect`, `BleSpam`, `AppleBleSpam` | [BLE & Detectors](BLE-and-Detectors.md) |
| Security tools | `MarauderTools`, `RawCapture`, `DeauthDetector`, `PmkidCapture`, `WpsCheck` | [Marauder Security Tools](Marauder-Security-Tools.md) |
| Companion apps | `BiscuitCompat`, `IosEspCompat`, `BLEBridge` | [Companion Apps & Bridges](Companion-Apps-and-Bridges.md) |
| Config/data/upload | `Config`, `WigleUpload`, `WiFiManager`, `WebUI` | [Data & Uploads](Data-and-Uploads.md) |
| Display/UI | `Display`, `ScreenUI`, `ScreenTelemetry`, `Gfx`, `LGFX_W550`, `Touch`, `PigWorld` | [Display & UI](Display-and-UI.md) |
| GPS / power | `GPS`, `BatteryMonitor` | [Module Reference](Module-Reference.md) |

## Key invariants (don't break these)
- ESP-NOW record packets stay **212 bytes** (Biscuit Pro drops anything smaller).
- **DFS 5 GHz (ch 52–144) is passive-only** — a compliance rule, not a tunable.
- New mesh message types stay **≥ 0x20** so legacy Cores ignore them.
- Scheduler feature flags (`PIG_FEAT_*`) compile to **byte-identical legacy behavior when `0`**; host tests assert the OFF default.
