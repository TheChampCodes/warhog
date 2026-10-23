# Context for Claude sessions working in scheduler/

This folder is the **yield-driven mesh scheduler**: the move from static even
channel-slicing to yield-driven acquisition. Full technical design in
[`DESIGN.md`](./DESIGN.md); Phase-4 plan in [`PHASE4_PLAN.md`](./PHASE4_PLAN.md).
[`HANDOFF.md`](./HANDOFF.md) is the original design chat, kept as an **archive** —
it predates all firmware wiring, so don't read it as current state.

## Where things stand
Phases 0–3 and Phase 4 Step 1 are merged to `main` and **ON by default** in the
released builds (each board's `platformio.ini` `build_flags` set `-DPIG_FEAT_*=1`).
Every node firmware's channel sweep now runs through the Phase-2 `PigSweep` FSM.

| Flag | Phase | What it does | Boards |
|------|-------|--------------|--------|
| `PIG_FEAT_METRICS` | 0 | 1 Hz `PIGM,` metrics line | piglet_fork (hard-on in `PigMetricsHook.h`) |
| `PIG_FEAT_DEDUP` | 1 | worker BSSID dedup (~29% airtime saved, validated) | all mesh nodes |
| `PIG_FEAT_ADAPTIVE_DWELL` | 2 | two-pass sweep: 50 ms active fast pass, then yield-weighted passive revisits; DFS 5 GHz passive | all mesh nodes |
| `PIG_FEAT_DYNAMIC_ASSIGN` | 3 | yield-weighted channel assignment (`PIG_MSG_ADMIN_EXT`) | Core: piglet_fork, CYD, W550 |
| `PIG_FEAT_CORE_ISO` | 4 Step 1 | SD logging → own FreeRTOS task + SPI-bus mutex | piglet_fork, CYD, W550 |

**Do NOT change the `#define … 0` defaults in `pig_*.h`** — `tests/test_all.cpp`
asserts the OFF-default legacy behavior. A flag's ON path gets its own test TU
instead (see `tests/test_dwell_on.cpp`).

## How the firmwares use the Phase-2 sweep
Each node firmware holds one `static PigSweep nodeSweep;` and drives it from its
scan tick (`nodeDoScanTick()` / `scanTick()`):

1. `!pig_sweep_valid(&nodeSweep)` → build the channel list with
   `pig_sweep_build()` (explicit Phase-3/Biscuit list if there is one, else the
   contiguous table slice; unscannable channels dropped) and `pig_sweep_begin()`.
2. `pig_sweep_next()` yields the next channel + `PigScanPlan`; `false` means the
   sweep is done → ch-6 admin window.
3. After each async scan completes, `pig_sweep_report()` records that channel's
   yield (everything Phase-1 dedup did **not** suppress) and advances.

Every site that changes a node's assignment calls `pig_sweep_invalidate()` so the
next tick rebuilds. A zero-initialized `PigSweep` is already invalid, so a static
one is born needing a build.

## Build / test
```bash
# Host unit tests — 156 checks, 2 TUs + the replay smoke test.
./scheduler/tests/run.sh
# No host g++ on Windows; run it under WSL:
wsl bash -c "cd /mnt/c/Users/skons/Desktop/piglet/scheduler/tests && bash run.sh"

# Firmware (Windows, Git Bash):
/c/Users/skons/.platformio/penv/Scripts/pio.exe run -d "<dir>" -e "<env>"
# turn a flag off for a one-off build without editing the ini:
PLATFORMIO_BUILD_FLAGS="-DPIG_FEAT_CORE_ISO=0" pio.exe run -d <dir> -e <env>
```

CI matrix (`.github/workflows/build-and-deploy.yml`): `xiao_c5` (`Arduino Files/piglet_fork`),
`freenove-s3-cyd-ham-ui4` + `guition-jc8048w550` (`Piglet_Freenove_S3_CYD_HAM_UI4`),
`esp32c3_oled` (`c3_node`), `pigletnode_c5` (`PigletNode`). The `waveshare_c6` solo
wardriver is not a mesh node and has no scheduler.

`TDongleC5_Piglet/` is an Arduino sketch, **not in CI** — Arduino has no `-I`, so it
keeps **copies** of `pig_proto.h`/`pig_dedup.h`/`pig_dwell.h`. Keep those copies
byte-identical to this folder's, and remember changes there get no build gate.

## Hard invariants (do not break)
- Record packets stay **212 B** (Biscuit Pro drops anything smaller).
- **DFS 5 GHz (ch 52–144) is passive-only** — compliance, not a tunable.
- Never drop a NEW/unique BSSID before a DELTA under backpressure.
- New message types stay **≥ 0x20** so legacy Cores ignore them.
- `pig_proto.h` must be `#include`d **unconditionally** in each firmware — the
  `PIG_MSG_ADMIN_EXT` worker parse needs it even with `PIG_FEAT_DEDUP` off.

## What's left (all bench-gated — needs hardware)
1. **Validate the ON paths on a live multi-node mesh**, especially `CORE_ISO`
   (dual-core threading, never run on hardware) and the Phase-2 revisit pass's
   effect on unique/min. Watch `[CORE-ISO] SD store q: depth=N drops=M` (5 s) and
   the 1 Hz `PIGM,` line. Disable a suspect board with `-DPIG_FEAT_CORE_ISO=0`.
2. **Phase 4 Steps 2–5** (UI/GNSS/RT-trio isolation). On piglet_fork the Core
   screen is drawn inside `coreModeTick` (a 2nd I2C writer), so a blind UI→core
   move is racy; the C5 is single-core, so Step 1 there is priority-only.
3. **CYD node-mode reboot** — the CYD crashes when switched to node mode; needs a
   backtrace over a stable serial link (unresolved).

## Gotchas (don't relearn these)
- Before committing piglet_fork, `rm -f "Arduino Files/piglet_fork/Piglet.ino.cpp"`
  (Arduino build artifact; regenerates, never commit it).
- `COM13` = a Black Magic Probe (VID 1D50:6018), **not** an ESP. Real ESP32 native
  USB = 303A:1001. No-reset serial monitor: `$TEMP/readport.py` (dtr/rts=False).
- No `gh` CLI here. Open PRs / dispatch workflows via the GitHub REST API with the
  git credential token: `printf "protocol=https\nhost=github.com\n\n" | git credential fill`.
- CYD SD + ILI9341 TFT share the SAME physical SPI pins (10–13); `sdSpi(HSPI)` is a
  separate driver on the same wires → that's why `CORE_ISO` needs `SpiBusLock.h`.
  CYD touch is I2C (FT6336), off that bus.
- The CYD's 64 GB SD card: 64 GB cards usually ship **exFAT** but the ESP32 SD stack
  wants **FAT32** — confirm the format before chasing an SD firmware bug.
