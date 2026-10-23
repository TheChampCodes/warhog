# Changelog — olosnoks/piglet (fork)

This fork tracks [Hamspiced/piglet](https://github.com/Hamspiced/piglet) upstream and adds
multi-board support, a Marauder/BLE security suite, PigWorld graphics, and a web flasher.
See the upstream [CHANGELOG](https://github.com/Hamspiced/piglet/blob/main/CHANGELOG.md) for
the base firmware history.

## Unreleased

### Yield-driven mesh scheduler — now LIVE (on by default)
A staged rework of mesh acquisition toward yield-driven scheduling. The feature flags are now
**enabled by default in the released flasher builds** (set per board in `platformio.ini`
`build_flags`); each still compiles to the legacy behavior when its `-DPIG_FEAT_*=0`, so any
one phase can be disabled in isolation. Host-tested (123 checks: `./scheduler/tests/run.sh`);
full design in `scheduler/DESIGN.md`, Phase-4 plan in `scheduler/PHASE4_PLAN.md`.

- **Phase 0 — instrumentation** (`PIG_FEAT_METRICS`): a 1 Hz `PIGM,…` metrics line (DESIGN §13)
  on the XIAO C5 Core + a host A/B replay harness. Validated on hardware (212 B/unique baseline).
- **Phase 1 — worker BSSID dedup** (`PIG_FEAT_DEDUP`, all node firmwares): suppresses redundant
  repeat sightings before a node transmits (`scheduler/pig_dedup.h`). **~29% mesh airtime saved**
  (stationary) measured on hardware; surfaces as `supp_pct` on the Core's PIGM line.
- **Phase 2 — adaptive dwell + DFS-passive** (`PIG_FEAT_ADAPTIVE_DWELL`, all node firmwares):
  faster active dwell + DFS 5 GHz (ch 52–144) forced passive for compliance
  (`scheduler/pig_dwell.h`). The two-pass yield-weighted **revisit FSM** (`PigSweep`) is now a
  pure, host-tested module; wiring it into the live scan loops stays a follow-up.
- **Phase 3 — dynamic yield assignment** (`PIG_FEAT_DYNAMIC_ASSIGN`, Core: piglet_fork + CYD):
  tracks per-channel yield and hands each node an explicit yield-ordered channel list
  (`PIG_MSG_ADMIN_EXT`); stays band-aware so 2.4 GHz-only nodes keep 1/6/11.
- **Phase 4 Step 1 — core-isolated SD logging** (`PIG_FEAT_CORE_ISO`, piglet_fork + CYD/W550):
  the Core's per-record WiGLE SD write moves to a FreeRTOS task (core 0 on the dual-core S3) so
  the ESP-NOW RX drain keeps running; on the CYD the SD card and TFT share one SPI bus, so a
  recursive `gSpiBusMutex` (`SpiBusLock.h`) serializes the display flush against the SD task.
  Instrumented via a 5 s `[CORE-ISO] SD store q: depth/drops` line.

**Caution:** `PIG_FEAT_CORE_ISO` is dual-core threading whose ON path has not been validated on
hardware; it was enabled by owner decision. Watch the `[CORE-ISO]` queue line under live node
load and disable the flag if a board becomes unstable.

### CYD Core-mode screen lag fix
Channel reassignment ran a blocking `delay(10)`-per-node pass on *every* `CORE_REQUEST`, stalling
the loop; now coalesced to at most one reassign per 800 ms and only on an actual roster change.

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
