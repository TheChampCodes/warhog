# Scanning & Logging

## Solo Wi-Fi scanning (`Scanner.cpp`)
Solo mode runs `doScanOnce()` each loop: an **asynchronous** all-channel scan
(`WiFi.scanNetworks(async=true, show_hidden=true, passive=false, dwellMs)`), 100 ms dwell
(200 ms in powersave). On completion `processScanResults(n)` iterates the results and for
each AP:
1. builds the auth string (`meshAuthModeToString`),
2. writes a WiGLE row (`appendWigleRow`),
3. mirrors it to the phone bridge (`phoneBridgeSoloPushAp`),
4. updates session counters (`SessionStats`).

A stuck radio (3 consecutive empty scans) triggers a `WIFI_OFF`/`WIFI_STA` recovery cycle.

> Solo scanning does **not** use the Phase-2 `PigSweep` FSM or the Phase-5 per-channel
> table — those drive the **mesh node** path (`nodeDoScanTick`). Solo is a plain
> all-channel sweep. Serial shows `[SCAN] Async scan started (dwell=100 ms)` in solo.

## WiGLE CSV logging (`SDUtils.cpp`)
Rows are written to `/logs/WiGLE_<rand>_<id>.csv` with the standard WiGLE header. A row is
`MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,Lat,Lon,AltMeters,AccuracyMeters,Type`.
- `openLogFile()` creates/rotates the file with headers; empty (header-only) files are
  pruned (`deleteEmptyCsvs`).
- BLE sightings go through `appendWigleBleRow()` (type column distinguishes them).
- Timestamps come from `iso8601NowUTC()` (GPS-disciplined when a fix is present).
- Path safety: `normalizeSdPath`, `isAllowedDataPath`, and `moveToUploaded` keep writes and
  post-upload archival inside the allowed `/logs` and `/uploaded` trees.

### SD card format
64 GB cards usually ship **exFAT**, but the ESP32 SD stack wants **FAT32** — if logging
silently fails, check the format first. On the CYD the SD is 4-bit SDIO; on the Guition/C5
it's SPI (and on the CYD it shares the SPI bus with the TFT — see
[Adaptive Scheduler → Phase 4](Adaptive-Scheduler.md#phase-4--core-isolation)).

## Raw PCAP logging
Promiscuous-mode frame capture to `.pcap` is part of the Marauder suite
(`RawCapture.cpp`) — see [Marauder Security Tools](Marauder-Security-Tools.md).

## Session statistics (`SessionStats.cpp`)
Maintains per-auth-mode tallies (`statsRecordWifiAuth`), a rolling Wi-Fi/BLE history for
the stats screen (`statsHistoryTick/Get`), and a live acquisition rate
(`radiosPerMinute`). On the C5 Core these feed the PigWorld histogram and the `PIGM`
metrics line.
