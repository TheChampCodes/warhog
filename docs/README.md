# WarHog — Developer Wiki

Multi-board ESP32 wardriving & wireless-research suite: Wi-Fi (2.4 + 5 GHz) and BLE
scanning, GPS-tagged WiGLE/PCAP logging to microSD, an ESP-NOW coordinator/worker mesh
with a yield-driven adaptive scheduler, a Marauder security-tool port, touch + web UIs,
and a browser flasher.

This wiki documents the code: how the firmware is organized, what each subsystem and
module does, the mesh wire protocol, the adaptive scheduler (Phases 1–5), and how to
build, flash, and extend the project. For a product-level overview and the hardware
matrix, see the repository [`README.md`](https://github.com/olosnoks/warhog/blob/main/README.md).

## Start here
- **[Architecture](Architecture.md)** — the big picture: boards, roles, the boot/loop model, and how a scan becomes a logged row.
- **[Upstream Differences](Upstream-Differences.md)** — what the fork adds, changes, and keeps vs [Hamspiced/piglet](https://github.com/Hamspiced/piglet), subsystem by subsystem.
- **[Naming & Lineage](Naming-and-Lineage.md)** — the JCMK→Biscuit→Piglet→WarHog lineage and the Piglet→WarHog rename (why `PIG_*` names persist).
- **[Hardware & Firmware Projects](Hardware-and-Firmware-Projects.md)** — every board, its SoC, display/SD/GPS wiring, and which PlatformIO env builds it.
- **[Build & Flash](Build-and-Flash.md)** — PlatformIO envs, the CI/web-flasher pipeline, and the ESP32-S3 USB-JTAG flashing gotcha.

## Subsystems
- **[Module Reference](Module-Reference.md)** — every firmware source module and its public functions.
- **[Scanning & Logging](Scanning-and-Logging.md)** — the Wi-Fi scanner, WiGLE/PCAP writers, SD paths, session stats.
- **[Mesh Protocol](Mesh-Protocol.md)** — ESP-NOW roles, JCMK/Biscuit message types, `PIG_MSG_*` extensions, sub-clusters.
- **[Adaptive Scheduler](Adaptive-Scheduler.md)** — Phases 1–5: dedup, adaptive dwell, dynamic assignment, core isolation, the pair scheduler.
- **[BLE & Detectors](BLE-and-Detectors.md)** — BLE wardriving, AirTag/Flipper/Pwnagotchi detection, BLE spam.
- **[Marauder Security Tools](Marauder-Security-Tools.md)** — raw capture, beacon/deauth, deauth detection, PMKID, WPS, Hashcat export.
- **[Companion Apps & Bridges](Companion-Apps-and-Bridges.md)** — Biscuit Manager and Wardrive Go BLE/GATT bridges.
- **[Data & Uploads](Data-and-Uploads.md)** — WiGLE, WiGLE-compatible services, config file format.
- **[Display & UI](Display-and-UI.md)** — the TFT/OLED frontends, PigWorld, touch, telemetry screens.
- **[Web Flasher](Web-Flasher.md)** — the browser installer, manifests, and screen previews.

## Working on the code
- **[Serial & Telemetry](Serial-and-Telemetry.md)** — serial commands and the live metric lines (`PIGM`, `[CORE-ISO]`, `[PAIR]`).
- **[Testing & Contributing](Testing-and-Contributing.md)** — host unit tests, the flag/byte-identical invariants, and the firmware-copy rules.

---
*This wiki describes `main`. Where a feature is flag-gated, the flag name (`PIG_FEAT_*`)
is given so you can trace it to [`scheduler/`](https://github.com/olosnoks/warhog/tree/main/scheduler).*
