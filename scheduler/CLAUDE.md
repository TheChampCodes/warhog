# Context for Claude sessions working in scheduler/

Before doing anything in this folder, read [`HANDOFF.md`](./HANDOFF.md) — it is the
handoff from the design chat that created this work (what's done, what's next, the
grounded baseline, and hard invariants). The full technical design is in
[`DESIGN.md`](./DESIGN.md).

Quick facts:
- Branch `yield-scheduling-phase0`; pushed to origin; no existing firmware modified.
  Rollout Step 0 (instrumentation) host modules landed; on-target wiring + baseline
  capture still pending (needs a board).
- Logic tests: `./scheduler/tests/run.sh` → 85 checks (host-only, no ESP32 toolchain
  needed). On Windows with no host g++, run via WSL/Ubuntu g++ against `-I stubs`.
- Invariants: 212 B record packets (Biscuit compat); DFS 5 GHz passive-only;
  never drop a unique BSSID before a delta; new msg types ≥ 0x20.
