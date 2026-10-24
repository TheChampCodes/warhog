# Data & Uploads

> **Fork note:** the config model and WiGLE upload come from upstream; the WDGoWars and
> Wardrift upload targets (and the chunked/non-blocking upload helpers) are WarHog
> additions. See [Upstream Differences](Upstream-Differences.md).

## Configuration file (`/wardriver.cfg`)
Settings live in a `key=value` file on the SD card, loaded at boot by
`loadConfigFromSD()` and parsed line-by-line (`parseKeyValueLine` → `cfgAssignKV`).
`cfgEnforceMutualExclusion()` resolves conflicting options; `saveConfigToSD()` persists
changes made from the web/touch UI. The config struct carries ~30 fields. Common keys:

| Key | Meaning |
|---|---|
| `meshModeOnBoot` | `core` / `node` / `none` (solo) / `off` |
| `blePersonality` | `biscuit` or `wardrivego` — which phone bridge to run |
| `wardriverSsid` / `wardriverPsk` | the device's own AP credentials |
| `homeSsid` / `homePsk` | a home network to auto-return to for uploads |
| `gpsBaud` | GPS UART baud (e.g. 9600) |
| `scanMode` | e.g. `aggressive` |
| `wigle token` / service keys | upload credentials |
| `speedUnits` | `mph` / `kmph` |
| `battPin` | ADC pin for battery sensing |

Board pins are auto-detected by chip (`detectPinsByChip`, `pickPinsFromConfig`,
`wardriverIsC5`) so one firmware adapts across the hardware it supports.

## Uploads (`WigleUpload.cpp`)
Batch-uploads the `/logs/*.csv` files to WiGLE and two WiGLE-compatible services. Each
service has a *test* call (validate the key), a *single-file* upload, and a *batch* helper
that archives uploaded files via `moveToUploaded()`:

| Service | Test | Single | Batch |
|---|---|---|---|
| WiGLE | `wigleTestToken()` | `uploadFileToWigle(path)` | `uploadAllCsvsToWigle(max)` |
| WDGoWars | `wdgwarsTestKey()` | `uploadFileToWdgwars(path)` | `uploadAllCsvsToWdgwars(max)` |
| Wardrift | `wardriftTestKey()` | `uploadFileToWardrift(path)` | `uploadAllCsvsToWardrift(max)` |

WDGoWars uploads can be chunked across loops to avoid blocking: `wdgwarsServicePendingJobs()`
(incremental) and `wdgwarsDrainPendingJobs(budgetMs)` (bounded flush before a radio mode
change). `deleteEmptyCsvs()` prunes header-only files so they don't get uploaded.

## Connectivity (`WiFiManager.cpp`, `WebUI.cpp`)
- `WiFiManager` handles STA/AP transitions and **home-network auto-return** — the device
  can rejoin a known network opportunistically to upload, then resume wardriving.
- `WebUI::startWebServer()` serves the on-device HTTP UI/API (status, config, triggering
  an upload). The web UI is only active when a mesh role isn't holding the radio exclusively.

## On-device data layout (SD)
```
/wardriver.cfg            device config
/logs/WiGLE_*.csv         wardrive logs (WiGLE format)
/logs/*.pcap              raw capture (Marauder)
/logs/*.hc22000           PMKID / handshake (Hashcat)
/uploaded/                files moved here after a successful upload
```
