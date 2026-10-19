# Changelog — olosnoks/piglet (fork)

This fork tracks [Hamspiced/piglet](https://github.com/Hamspiced/piglet) upstream and adds
multi-board support, a Marauder/BLE security suite, PigWorld graphics, and a web flasher.
See the upstream [CHANGELOG](https://github.com/Hamspiced/piglet/blob/main/CHANGELOG.md) for
the base firmware history.

## Unreleased

### Upstream sync — now at v2.7
Brought the shared firmware up from the v2.62 fork point to upstream **v2.7**, keeping all
fork additions (BLE bridge, PigWorld, herd-count display, PlatformIO builds):

- **Piglet Core** (`Arduino Files/piglet_fork`): Wardrift upload (3rd upload service),
  SSID whitelist (`ssidWhitelist1–10`), home-network auto-return + `uploadOnNetworkSeen`,
  per-file WDGoWars upload fix, upload size logging, and the GPIO0 boot-settle guard.
  Biscuit handling keeps the fork's BLE-bridge mirror and herd-count branch **and** adds
  upstream's Biscuit plain-reply (encrypt=0) handshake.
- **PigletNode C5** and **T-Dongle C5**: synced to upstream v2.7 (clean drop-in).
- `wardriver.cfg` sample gained `wardriftApiKey`, `ssidWhitelist1–10`, `uploadOnNetworkSeen`,
  and `sdMaxSpiHz`.
- *Waveshare C6*: no change (upstream had none since v2.62).

### Web flasher
- Re-added the **LilyGo T-Dongle C5** card (official build — flashes upstream's prebuilt
  `TDongleC5_Piglet.ino.merged.bin`, latest).
- The Piglet Core (XIAO C5) and PigletNode C5 cards carry a **skon's fork / official**
  toggle that tracks ham's latest GitHub release; the fork side is now synced to v2.7.

### In progress
- Porting the v2.63–v2.7 feature set into the fork-only **CYD/Guition** suite and
  **c3_node** (separate branch; these trees are full rewrites with no upstream lineage).
