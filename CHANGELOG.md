# Changelog — olosnoks/piglet (fork)

This fork tracks [Hamspiced/piglet](https://github.com/Hamspiced/piglet) upstream and adds
multi-board support, a Marauder/BLE security suite, PigWorld graphics, and a web flasher.
See the upstream [CHANGELOG](https://github.com/Hamspiced/piglet/blob/main/CHANGELOG.md) for
the base firmware history.

## Unreleased

### CYD mesh: sub-clusters, cluster UI & wardriving profiles
On-device cluster management for the **Freenove S3 CYD (HAM UI4)** firmware, in Core mode:
- **Cluster view** — the Home STATUS card (in Core mode) drills into the live node list.
  Each worker card is tappable and shows its sub-cluster tag and the exact channels it is
  scanning.
- **Sub-clusters** — tap a node to open a per-node editor and move it between sub-clusters
  (`CL1…CL9`). The Core now splits channels *within* each sub-cluster, so co-located nodes
  sharing one antenna never scan the same channel at once, while separate clusters each
  cover the spectrum independently. Splitting stays band-aware (2.4 GHz-only nodes get
  1/6/11; dual-band nodes share the 5 GHz space).
- **Wardriving profiles** — a built-in **DEFAULT** (even band-aware split across all nodes)
  plus **3 named custom slots** saved to the SD card (`/mesh_profiles.cfg`). Save the
  current grouping, then recall a setup with one tap instead of re-grouping each session;
  groupings are remembered by MAC so a node that drops and rejoins returns to its cluster.
- All CSV logs across the fork firmwares now rotate under a hard 30 MB ceiling (byte-based).

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
