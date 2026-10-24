# Naming & Lineage

This project was renamed **Piglet → WarHog**, and it sits several layers down a lineage of
ESP-NOW wardriving firmware. If you are reading the code and wondering why half the
identifiers still say `pig`, this page is the answer. For the behavioral fork diff see
[Upstream Differences](Upstream-Differences.md).

---

## Lineage

```
JCMK Wardriver (ESP-NOW protocol, WiGLE logging)
      └─▶ Biscuit Pro (mesh Core/node ecosystem, 212-byte record, GATT manager app)
             └─▶ Hamspiced/piglet (the direct upstream this repo forks)
                    └─▶ olosnoks/warhog (this fork — formerly olosnoks/piglet)
```

- **JCMK** is where the ESP-NOW message family (`JCMK_MSG_*`) and the WiGLE text record
  come from. It is still the base protocol on channel 6.
- **Biscuit Pro** is the mesh ecosystem WarHog stays interoperable with — hence the
  212-byte record invariant, the `BiscuitCompat` GATT layer, and the `"WarHog:<version>"`
  identify handshake.
- **Hamspiced/piglet** is the firmware this repo forks and still syncs (currently v2.7).
- **WarHog** is this fork: upstream's mesh firmware plus the BLE layer, the Marauder suite,
  the extra boards, the adaptive scheduler, and the web flasher.

---

## The Piglet → WarHog rename

The rename touched three layers. What changed, and what deliberately did not:

### Renamed (human-facing + on the wire)
- **Product/UI name**, docs, README, the repo (`olosnoks/piglet` → `olosnoks/warhog`) and
  the Pages flasher URL (`olosnoks.github.io/piglet` → `/warhog`).
- **Board folders & sketches**: `piglet_fork/` → `warhog_fork/`, `PigletNode/` →
  `WarHogNode/`, `Piglet_Freenove_…` → `WarHog_Freenove_…`, `TDongleC5_Piglet/` →
  `TDongleC5_WarHog/`, `waveshareDisplayMiniPiglet/` → `waveshareDisplayMiniWarHog/`.
- **PlatformIO env** `pigletnode_c5` → `warhognode_c5` (and its manifest + firmware `.bin`).
- **On the wire** (an intentional compatibility break — see
  [Upstream Differences §5](Upstream-Differences.md#5-wire--on-disk-compatibility)):
  - ESP-NOW mesh marker `PIGLET_MARKER "PIGLET1"` → `WARHOG_MARKER "WARHOG1"` (still 7
    bytes, so `WARHOG_MARKER_LEN` is unchanged).
  - BLE advertised names (`Piglet-Core` → `WarHog-Core`), the DIS manufacturer string, and
    the Biscuit identify string (`"Piglet:…"` → `"WarHog:…"`).
  - AP SSID default, WiGLE `device=`/`brand=` fields, and the SD filename prefix.

### Deliberately **not** renamed
These keep the `pig` name on purpose — renaming them would break compatibility, tooling, or
attribution:

| Kept as-is | Why |
|---|---|
| `PIG_FEAT_*` build flags, `PIG_MSG_*` message types | wire/ABI identifiers and documented knobs; renaming churns every `platformio.ini` and the scheduler tests for no functional gain |
| `pig_proto.h`, `pig_dwell.h`, `pig_dedup.h`, `yield_sched.h` etc. | the `scheduler/` headers are the stable, host-tested module; the `pig_` prefix is the module name used throughout the tests |
| `PIGM,` metrics line | parsed by external telemetry tooling — a format identifier, not a label |
| `PigWorld`, `PigSweep`, `PigScanPlan`, `PigMetricsHook` | internal class/type names; cosmetic only, and `PigWorld` is literally a pig-themed animation |
| `Hamspiced/piglet` URLs, upstream release `*.bin`, `web-flasher/ham/`, `*-ham.json`, credit lines | these name **upstream's** project, not this fork |

> **Rule of thumb when editing:** `piglet` (the old product/repo) → `warhog`. `pig` /
> `PIG_` (a code identifier or wire constant) → **leave it**, unless you are doing a
> deliberate, test-backed identifier migration. When in doubt, grep for the string in
> `scheduler/tests/` first — if a test asserts on it, it is an identifier, not a label.

---

## Where the names live

| Name | Meaning | File(s) |
|---|---|---|
| `WARHOG1` | ESP-NOW mesh marker (fleet identity) | `MeshNode.cpp`, `c3_node/src/main.cpp` |
| `WarHog-Core` | BLE advertised name | `BLEBridge.cpp`, `BiscuitCompat.cpp` |
| `"WarHog:<ver>"` | Biscuit identify string | `MeshNode.cpp` |
| `PIG_FEAT_*` | scheduler feature flags | `scheduler/pig_*.h`, each `platformio.ini` |
| `PIG_MSG_*` (≥0x20) | fork mesh message types | `scheduler/pig_proto.h` |
| `PIGM,` | Phase-0 metrics line | `scheduler/pig_metrics.h`, `PigMetricsHook.cpp` |

### A note on "WarHog" the scheduler phase
"WarHog" also names the **Phase-5 dual-node antenna-pair scheduler**
(`PIG_FEAT_PAIR_SCHED`) in [Adaptive Scheduler](Adaptive-Scheduler.md#phase-5--warhog-dual-node-pair-scheduler).
That predates the product rename, so the word now does double duty — the product *and* one
of its scheduler phases. When reading `scheduler/` docs, "WarHog pair scheduler" means the
Phase-5 feature specifically, not the project as a whole.
