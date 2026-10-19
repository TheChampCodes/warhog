# Session handoff — yield-driven scheduler work

**Read this first.** It captures a prior Claude (cloud) chat so a fresh Claude Code
session in VS Code can continue with full context. The companion file
[`DESIGN.md`](./DESIGN.md) holds the complete technical design; the code in this
folder is the Phase-0 groundwork already written and tested.

## Where things stand

- **Branch:** `yield-scheduling-phase0` (cut from `claude/vibrant-davinci-e193f7`).
- **Committed, NOT pushed.** One commit: `scheduler: add host-testable yield-driven
  acquisition modules (Phase 0)`.
- **No existing firmware was modified.** Everything lives under `scheduler/`.
- **Tests pass:** `./scheduler/tests/run.sh` → 52 checks, 0 failures, clean under
  `g++` and `clang++` at `-O2`. (Host-only logic tests; no ESP32 toolchain was
  available in the cloud container.)

## What this work is

Moving the Piglet mesh from **static even channel-slicing** (today:
`perNode = JCMK_NUM_CHANNELS / count` in `Arduino Files/piglet_fork/MeshNode.cpp`)
to **yield-driven adaptive acquisition**: worker-side BSSID dedup, adaptive
dwell/revisit, dynamic yield-based channel assignment, FreeRTOS core isolation,
and GPS-aware scheduling. Full rationale, schemas, algorithms, and the phased
rollout are in `DESIGN.md`.

## Grounded baseline (verified against this repo's source)

- ESP-NOW mesh on **channel 6** (`JCMK_ESPNOW_CH`); 5 message types
  (`JCMK_MSG_CORE_REQUEST/REPLY/HEARTBEAT/TEXT/ADMIN`).
- **Static even split** assignment in `coreReassignChannels()` (`MeshNode.cpp:635`).
- Fixed **80 ms** dwell (`NODE_SCAN_DWELL_MS`); active scan
  (`WiFi.scanNetworks(async,hidden,passive=false,80,ch)`, `MeshNode.cpp:825`).
- 40-entry channel table (14× 2.4 GHz + 26× 5 GHz); 5 GHz only on C5/C6 silicon.
- Records travel as **212-byte** text packets (`jcmk_text_msg_t`); Biscuit Pro
  drops anything < 212 B — new binary packets keep the 212 B size.
- **No** worker-side dedup / TTL cache / yield scoring / explicit core isolation
  exists in the node path today (this is what we're adding).

## Files in this folder

| File | Status | What |
|---|---|---|
| `pig_proto.h` | drop-in | ESP-NOW structs (`pig_obs_t`, `pig_batch_t` = 212 B) + CRC16-CCITT. New msg types `0x20–0x23`. |
| `bssid_cache.h` | drop-in | 512-slot TTL dedup cache → NEW/CHANGED/REPEAT/SUPPRESS. |
| `lossy_ring.h` | drop-in | SP/SC ring; drops DELTA before FULL under backpressure. |
| `yield_sched.h` | needs integration | Coordinator scoring + yield-packed assignment + 3-gate hysteresis (host-testable; `now` is a param). |
| `tests/` | — | `Arduino.h` shim + `test_all.cpp` + `run.sh`. |
| `DESIGN.md` | — | Full 16-section technical design. |

## Next steps (phased rollout — do them in order, each flag-gated)

0. **Instrumentation only** — add the 1 Hz metrics line (see DESIGN §13) + a scan
   replay harness. No behavior change. Capture baselines.
1. **Worker dedup** — wire `BssidCache` into the worker scan path behind
   `PIG_FEAT_DEDUP`. In `PigletNode/PigletNode.ino` (headless C5 worker) and the
   node path of `Arduino Files/piglet_fork/MeshNode.cpp`, replace "send every
   found record each cycle" with classify→FULL/DELTA/SUPPRESS. Target: mesh
   bytes/unique-BSSID −40%, unique/min ≥ parity.
2. **Adaptive dwell/revisit** — fast active first pass + yield-weighted passive
   revisit over the assigned range (`PIG_FEAT_ADAPTIVE_DWELL`).
3. **Dynamic assignment** — replace the even split in `coreReassignChannels()`
   with `sched_assign()` from `yield_sched.h`; send explicit channel lists via a
   new `PIG_MSG_ADMIN_EXT` (`PIG_FEAT_DYNAMIC_ASSIGN`). Keep legacy contiguous
   ADMIN for non-Piglet peers (detect via existing marker handshake / `isPiglet`).
4. **Core isolation** — split RT (ESP-NOW/sched/scan on core 0) from non-RT
   (SD/GNSS/UI/upload on core 1) with the bounded queues (`PIG_FEAT_CORE_ISO`).
   Note: ESP32-C3 is single-core → priority-only fallback (DESIGN §7.3).

## How to build/test locally in VS Code

- **Module logic tests (fast, no hardware):** `./scheduler/tests/run.sh`
- **On-target firmware (needs PlatformIO + a board):**
  - Worker:      `cd PigletNode && pio run -t upload`
  - C3 node:     `cd c3_node && pio run -t upload`
  - XIAO C5 Core: `cd "Arduino Files/piglet_fork" && pio run -t upload`
  - Monitor:     `pio device monitor -b 115200`

## Hard invariants (do not break)

- Keep ESP-NOW record packets **212 B** (Biscuit compatibility).
- **DFS 5 GHz channels (52–144) must be passive-only** — compliance, not a flag.
- Never drop a NEW/unique BSSID before a DELTA under backpressure.
- New message types stay **≥ 0x20** so legacy Cores ignore them.
- Legacy JCMK/Biscuit peers keep working: binary batches are Piglet↔Piglet only.

## Open decisions for the user

- Whether to push `yield-scheduling-phase0` to origin (not yet pushed).
- Whether to rename the branch to the `claude/…` convention.
- Tuning constants in `yield_sched.h` `SchedCfg` are first-guess defaults — field-tune.
