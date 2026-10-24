# Marauder Security Tools

A port of the ESP32 Marauder offensive/defensive toolset, orchestrated by
`MarauderTools.cpp`. These are **dual-use** tools — use them only where you are
authorized (your own lab, an authorized pentest, a CTF). They are available on the S3
boards and the C5 Core; they are not part of the mesh worker role.

## Orchestration (`MarauderTools`)
A single `AttackMode` state machine owns the radio while a tool runs:
`marauderStartAttack(mode, channel)` tears down conflicting consumers (BLE scanner, mesh
mode, AP window), reconfigures the radio for the mode, and installs the promiscuous RX
callback; `marauderStopAttack()` restores the prior Wi-Fi state. Live counters:
`marauderFramesSent/Failed/PerSec/Captured()`. The promiscuous callback `mgmtRxCb()` and
the enqueue path are `IRAM_ATTR` (they run in interrupt context — so their buffers must be
**internal** RAM, never PSRAM).

### AttackMode
| Mode | Module | What it does |
|---|---|---|
| `BeaconSpam` | MarauderTools | floods crafted beacon frames (source list or custom SSID, optional channel hop) |
| `Deauth` | MarauderTools | deauth/disassoc injection against a target |
| `ProbeSniff` | MarauderTools | logs probe requests |
| `PcapCapture` | `RawCapture` | promiscuous frame capture to `.pcap` on SD |
| `RawCapture` | `RawCapture` | raw frame capture with a type filter |
| `DeauthDetect` | `DeauthDetector` | *defensive* — counts deauth/disassoc frames and alerts |
| `Pmkid` | `PmkidCapture` | association-based PMKID capture → Hashcat 22000 |

## Raw / PCAP capture (`RawCapture.cpp`)
`rawCaptureStart(filter, channel)` begins promiscuous capture; `rawCaptureBreakdown()`
reports counts by frame class (`B:` beacon / `P:` probe / `D:` data / `E:` EAPOL).
`rawCaptureWantsData()` tells the filter installer whether to pass data frames.

## Deauth detection (`DeauthDetector.cpp`) — defensive
Counts deauth/disassoc frames per channel, tracks distinct source MACs
(`deauthDetectorUniqueSources()`), exposes a status line, and raises
`deauthDetectorAlert()` when a recent-rate threshold is exceeded. Runs as a tick from
`marauderToolsTick()`.

## PMKID capture (`PmkidCapture.cpp`)
`pmkidStart(targetBssid, targetSsid, channel)` rebuilds the driver, sends association
requests, and extracts PMKIDs from the first EAPOL message; `pmkidAttempts()` /
`pmkidCaptures()` track progress and `pmkidCurrentPath()` is the open `.hc22000` file.
Pairs with the **Hashcat writer** (`HashcatWriter.cpp`), which observes EAPOL frames
(`hashcatWriterObserve`) and writes both handshakes and PMKIDs in the Hashcat **22000**
format for offline analysis.

## WPS vulnerability check (`WpsCheck.cpp`)
`wpsScanRun()` scans and flags APs advertising WPS; iterate with `wpsGet()` /
`wpsCheckAp()` and persist with `wpsLogResults()`.
