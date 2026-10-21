# scheduler/ — yield-driven acquisition modules (Phase 0/1 groundwork)

Standalone, host-testable building blocks for moving the Piglet mesh from static
even channel-slicing to yield-driven adaptive scanning. **These files are not yet
wired into any firmware project** — they are deliberately isolated so they can be
unit-tested on a host before integration, per the phased rollout.

## Files

| File | Status | Purpose |
|---|---|---|
| `pig_proto.h` | drop-in | ESP-NOW wire structs (`pig_obs_t`, `pig_batch_t`, 212 B Biscuit-safe) + CRC16-CCITT. New message types `0x20–0x23` sit above the legacy `1..5` range so legacy Cores ignore them. |
| `bssid_cache.h` | drop-in | Rolling TTL dedup cache (512 slots ≈ 8 KB, open-addressed, approx-LRU eviction). Classifies each sighting NEW / CHANGED / REPEAT / SUPPRESS. |
| `lossy_ring.h` | drop-in | SP/SC lossy ring. Refuses DELTA records at ≥80% fill, overwrites oldest FULL only when saturated — protects the unique-BSSID metric under load. |
| `yield_sched.h` | requires integration | Pure coordinator scoring + yield-packed assignment + 3-gate hysteresis. No Arduino/WiFi deps (`now` is a parameter) so it compiles and unit-tests on a host. |
| `pig_metrics.h` | drop-in | **Phase 0.** The 1 Hz acquisition metrics line (DESIGN §13). Pure accountant — feed it events, emit one `PIGM,...` CSV line per second. No `Serial` dep; `now` is a parameter. Adds no behavior change, only observes. |
| `pig_replay.h` | drop-in | **Phase 0.** Parser for a captured scan corpus (`t_ms,bssid,rssi,ch,enc,ssid_crc[,ssid_len]`) so A/B runs on identical input, not live RF. Dependency-light hand parser (host + on-target). |
| `tests/replay.cpp` | tool | **Phase 0.** Host A/B runner: replays a corpus through BASELINE (send-every-sighting) vs DEDUP (`BssidCache` classify) transmit models and prints bytes/unique for each. Byte figures are a documented first-order packetization model; absolute on-wire bytes come from the on-target `mesh_tx` counter. |

## Local testing (no ESP32 toolchain required)

```bash
./scheduler/tests/run.sh
```

This compiles the headers against a tiny `tests/stubs/Arduino.h` shim and runs
`tests/test_all.cpp`. It validates **logic only** (classification transitions,
drop policy, scoring/assignment, hysteresis gates, CRC check value, wire sizes),
not on-target FreeRTOS timing or radio behavior. Verified clean under `g++` and
`clang++` at `-O2`.

## Not covered here (needs the real toolchain / hardware)

- On-target build: install PlatformIO and build the per-board projects.
- FreeRTOS task/core isolation, async `WiFi.scanNetworks`, SD/GNSS paths.
- Integration into `PigletNode/` (worker) and `Arduino Files/piglet_fork/`
  (coordinator `MeshNode.cpp`) — that is Phase 1+ of the rollout and should be
  flag-gated (`PIG_FEAT_*`) so regressions isolate to one phase.

See the design document for the full architecture, message schema, scheduling
math, edge cases, benchmarks, and phased rollout.
