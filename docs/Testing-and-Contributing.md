# Testing & Contributing

## Host unit tests (scheduler)
The scheduler headers are pure and host-testable against a tiny `Arduino.h` shim — no
ESP32 toolchain needed. From `scheduler/tests/`:
```bash
./run.sh          # builds + runs: test_all.cpp, test_dwell_on.cpp, test_pair_on.cpp, replay.cpp
```
On Windows (no host g++) run it under WSL:
```bash
wsl bash -c "cd /mnt/c/Users/skons/Desktop/warhog/scheduler/tests && bash run.sh"
```
Current suite: **175 checks, 0 failures** across three TUs plus the replay smoke test.

### The flag / test convention (important)
- `test_all.cpp` compiles the whole suite with **every `PIG_FEAT_* OFF`** and asserts the
  legacy fall-through. So the `#define … 0` defaults in `scheduler/pig_*.h` **must stay 0**.
- A flag's **ON** path is a compile-time `#if`, so it needs its **own translation unit**
  (`test_dwell_on.cpp` for Phase 2, `test_pair_on.cpp` for Phase 5). Add one for any new
  flag's ON behavior and wire it into `run.sh`.

## Firmware build gates
Before pushing a firmware change, build the envs it affects, ON and OFF:
```bash
pio run -d <dir> -e <env>                                  # ON (release defaults)
PLATFORMIO_BUILD_FLAGS="-U<FLAG>" pio run -d <dir> -e <env>  # OFF path still compiles
```
The **OFF path must stay byte-identical** to legacy — new feature code goes behind its
`#if PIG_FEAT_*`. CI builds all six envs on deploy; the deploy is gated on the build, so a
broken env can't overwrite the live flasher.

## Keeping the C5 header copies in sync
The Arduino sketch `TDongleC5_WarHog/` has no include path, so it keeps **byte-identical
copies** of `pig_proto.h`, `pig_dedup.h`, and `pig_dwell.h`. After editing a scheduler
header:
```bash
cp scheduler/pig_dwell.h TDongleC5_WarHog/pig_dwell.h   # (and proto/dedup as needed)
diff -q scheduler/pig_dwell.h TDongleC5_WarHog/pig_dwell.h   # must be identical
```
TDongle is **not** in CI, so those copies get no build gate — review them by hand.

## Gotchas worth knowing
- **Arduino build artifact:** before committing `warhog_fork`, remove
  `warhog_fork/WarHog.ino.cpp` (a generated artifact; never commit it).
- **S3 flashing** needs esptool `--before usb-reset` (see [Build & Flash](Build-and-Flash.md)).
- A one-off `PLATFORMIO_BUILD_FLAGS` build **overwrites** `firmware.factory.bin` in the
  shared `.pio/build/<env>/` dir — rebuild at release flags before flashing a release image.
- The `build/` directories are Arduino-CLI leftovers and are gitignored — **not** the
  firmware; `.pio/` is the authoritative build output.

## Where state is recorded
- `scheduler/DESIGN.md` — the full technical design (16 sections).
- `scheduler/CLAUDE.md` — current state, flag table, and the "don't relearn these" gotchas.
- `scheduler/PHASE4_PLAN.md` — the core-isolation rollout plan.
- `CHANGELOG.md` — user-facing history.

## License
This repository is **CC BY-NC-SA 4.0** (non-commercial, share-alike). The offensive/dual-use
tools (Marauder, BLE spam, deauth, PMKID) are for authorized testing, research, and CTF use
only — follow your local law and only target systems you're allowed to.
