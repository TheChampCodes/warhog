# Yield-Driven Acquisition — Technical Design

Implementation-ready design for moving a WarHog-class ESP-NOW wardriving mesh from
static even channel-slicing to yield-driven adaptive acquisition. Status markers:
**[D]** documented in this repo's source · **[I]** inferred from code · **[V]** needs verification.

---

## 1. Executive Summary

Five independently shippable changes, in ROI order:

1. **Worker-side rolling BSSID dedup (TTL cache)** — highest ROI, lowest risk,
   pure win even on one node. Today each node re-sends every AP every cycle.
2. **Adaptive dwell/revisit** — fast active first pass builds per-channel yield;
   longer passive revisits weighted to high-yield channels.
3. **Dynamic yield-based channel allocation** at the coordinator, with hysteresis.
4. **FreeRTOS core/task isolation** — RT ESP-NOW/scheduling on core 0, SD/GNSS/UI
   on core 1, joined by bounded lossy queues.
5. **GPS-aware scheduling/timestamping** — stamp at ESP-NOW RX, interpolate GPS,
   modulate dwell by speed/distance.

Dedup and core isolation are robustly beneficial. Dynamic allocation and adaptive
dwell help most in multi-node, mixed-density, mobile deployments; a single
hand-tuned stationary node may gain little. Everything below is instrumented so
you measure rather than assume (§13).

---

## 2. Documented Baseline vs. Proposed Changes

| Baseline fact | Status | Evidence |
|---|---|---|
| ESP-NOW mesh on channel 6 | **[D]** | `JCMK_ESPNOW_CH`; README §5; `WarHogNode.ino` |
| Coordinator assigns channel slices; worker dwells & scans | **[D]** | `coreReassignChannels()` |
| Assignment is a static even split of the 40-channel table | **[D]** | `perNode = JCMK_NUM_CHANNELS / count` (`MeshNode.cpp:644`) |
| Fixed 80 ms/channel dwell | **[D]** | `NODE_SCAN_DWELL_MS = 80` |
| Node cycles range, returns to ch 6, sends results + heartbeat | **[D]** | `WarHogNode/README.md`; `nodeModeTick` |
| Active scan by default | **[D]** | `WiFi.scanNetworks(async,hidden,passive=false,80,ch)` (`:825`) |
| Core timeout → re-search after 30 s | **[D]** | `JCMK_CORE_TIMEOUT_MS = 30000` |
| Records travel as text lines in 212 B packets | **[D]** | `jcmk_text_msg_t`; "Biscuit drops < 212 B" |
| No worker-side BSSID dedup / TTL cache | **[I]** | No cache in node path |
| No adaptive revisit / yield scoring | **[I]** | Assignment is count-based only |
| No explicit RT/non-RT core isolation | **[I]** | Single Arduino `loop()`; `delay(10)` in core path |
| GPS timestamp model (fix time vs RX time) | **[V]** | `GPS.cpp` exists; policy not documented |
| 5 GHz only on C5/C6 silicon | **[D]** | Hardware matrix |

Not claiming upstream Piglet lacks these globally — only that they are absent in
this fork's node path as read. Cross-variant comparisons = "needs verification."

---

## 3. Target Architecture

```
            CORE 1 (non-RT, best-effort)             CORE 0 (RT, deadline-bound)
 tGNSS   : UART GNSS parse 1–10 Hz           tEspNowRx: ESP-NOW RX cb → stamp @ RX
 tStore  : SD WiGLE CSV / PCAP / binary      tSched   : yield scoring, assignment
 tUI     : OLED/TFT, PigWorld                tScanFsm : (worker) async scan FSM
 tUpload : WiGLE/Wardrift opportunistic
         └── qStore (lossy) ── ring queues ── qObs (lossy RT→nonRT) / qCmd (reliable) ──┘
```

**Coordinator:** pairing + peer table + protocol classification (unchanged);
global channel-yield table from worker HBs; dynamic assignment with hysteresis;
stamp records at RX + attach interpolated GPS; stale-HB detection & reclaim;
backpressure authority (push `summary_only` when store saturates).

**Worker:** scan FSM (fast pass → yield-weighted revisits); local BSSID cache;
emit FULL for new/changed, DELTA for repeats; HB carries per-channel yield counters;
honor assignment version + dwell/revisit/scan-policy knobs.

**Timing:** worker *sweep* = one pass over assigned sub-range; *sweep boundary* =
return-to-ch-6 admin window (yield scores update here). Coordinator *epoch* =
`ASSIGN_EPOCH_MS` (8 s) min between reassignments. HB cadence ≤ `HB_MIN_MS` 1500.

---

## 4. ESP-NOW Message Schema

Constraint: ESP-NOW payload ≤ 250 B. Keep 212 B on-wire for record/admin traffic
(Biscuit Pro drops < 212 B). Little-endian, `__attribute__((packed))`, preserve the
4 B magic + 1 B type prefix so legacy Cores still parse `type`/`len`.

New types (above legacy 1..5 so old Cores ignore them):
`PIG_MSG_OBS_BATCH=0x20`, `PIG_MSG_DELTA_BATCH=0x21`, `PIG_MSG_YIELD_HB=0x22`,
`PIG_MSG_ADMIN_EXT=0x23`.

Binary observation record (22 B fixed; one 212 B batch carries ~9 deltas / ~7 full
records vs. 1–2 text records today) and the 212 B batch envelope are defined in
`pig_proto.h` (`pig_obs_t`, `pig_batch_t`, `static_assert(sizeof==212)`).

- **Yield heartbeat** (`PIG_MSG_YIELD_HB`): header + `cache_fill`, `drops`, then
  `(ch, uniq, gross)` triples — 12 B header + 3 B/channel; 40 ch = 132 B, fits 212.
- **Extended admin** (`PIG_MSG_ADMIN_EXT`): `assign_ver`, `slot_index/count`,
  `fast_dwell`, `base_revisit`, `scan_policy` bits (active_allowed / passive_only_5g
  / summary_only), and an **explicit channel list** (not a contiguous start/end
  range) — this is what unlocks non-contiguous yield-packed assignment. Legacy
  ADMIN keeps the contiguous range for old nodes.

Negotiate binary batches via the existing WarHog marker handshake; non-WarHog peers
keep getting legacy TEXT.

---

## 5. Coordinator Scheduling Algorithm

Per-channel EWMA state: `yield` (unique new BSSIDs/s), `density` (gross obs/s),
`last_scan_ms`. See `yield_sched.h` for the exact implementation.

**Score:** `score = (yield + EPS) * freshness * bandWeight / sqrt(1 + density)`
- `EPS=0.05` exploration floor (never-scanned stays eligible).
- `freshness = 1 + STARVE_K*min(1,(now-last_scan)/STARVE_MS)`, `STARVE_K=0.5`,
  `STARVE_MS=20000` — revisit every channel within ~20 s regardless of yield.
- `bandWeight` 1.15 for 2.4 GHz, 1.0 for 5 GHz (tunable).
- `/sqrt(1+density)` deprioritizes noisy-but-no-new channels.
- EWMA α = 0.3.

**Cost:** `cost = clamp(base_revisit + K*density, fast_dwell, MAX_DWELL)`,
`MAX_DWELL=300`.

**Assignment** (yield-packed, replaces even split): sort channels by score desc;
first-fit-decreasing into per-worker dwell budgets (`BUDGET_MS=900`); coverage
floor forces any unplaced channel onto the worker with most budget; optional
redundancy places hot channels on a 2nd worker if budget remains.

**Hysteresis (all three gates must pass):**
- A time: `now - last_assign ≥ ASSIGN_EPOCH_MS (8000)`
- B churn: Jaccard distance(new, current) `> CHURN_MIN (0.25)`
- C benefit: predicted gain `> BENEFIT_MIN (0.15) × current total score`
Plus per-channel dwell-band debounce (promote fast, demote slow) to kill oscillation.

**GPS knobs:** stationary (`v<0.5`) → `BUDGET×1.6`, `MAX_DWELL=400` (depth);
highway (`v>8`) → `BUDGET×0.6`, `fast_dwell=40` (breadth); force an epoch eval when
distance since last assign `> REASSIGN_DIST_M (150)`.

**Stale HB:** `HB_STALE_MS=6000` → suspect (exclude from benefit math);
`HB_DEAD_MS=15000` → reclaim channels; `HB_EVICT_MS=30000` keeps legacy peer eviction.

---

## 6. Worker Deduplication Algorithm

Implemented in `bssid_cache.h`. 512-slot open-addressed cache (≈8 KB), FNV-1a hash,
linear probe (`PROBE_MAX=8`), approx-LRU eviction.

- **TTL:** `BSSID_TTL_MS=180000` (after this an AP is "new" again → full record);
  `BSSID_REFRESH_MS=20000` (periodic summary for still-present APs);
  `RSSI_DELTA_DB=8`. Scale TTL **down** with speed: `TTL = base/(1+v/4)`.
- **Classification:** MISS/expired → NEW (FULL); SSID-crc/enc change or
  `|Δrssi|≥delta` → CHANGED (FULL); age ≥ refresh → REPEAT (DELTA); else → SUPPRESS
  (count only).
- **FULL** carries SSID bytes; **DELTA** has `ssid_len=0`; suppressed shows only in
  the per-channel `gross` counter.
- `cache_fill%` in HB → coordinator narrows a saturated worker's channel set.

---

## 7. FreeRTOS Task/Core Isolation Plan

| Task | Core | Prio | Stack | Queue |
|---|---|---|---|---|
| tEspNowRx | 0 | 6 | 4 KB | fills qObs |
| tSched | 0 | 5 | 6 KB | drains cmd, fills qCmd |
| tScanFsm (worker) | 0 | 4 | 4 KB | fills qObs |
| tGNSS | 1 | 4 | 3 KB | fills qGps |
| tStore | 1 | 3 | 6 KB | drains qObs |
| tUI | 1 | 2 | 4 KB | snapshots |
| tUpload | 1 | 1 | 6 KB | reads store |

Queues: `qObs` 128×`pig_obs_t` lossy (overwrite-oldest, deltas refused @80%);
`qCmd` 16 reliable; `qGps` length-1 mailbox (latest-wins).

**Drop retention order (keep top):** ADMIN/commands → heartbeats (coalesce) →
NEW/CHANGED fulls → DELTA/summaries → suppressed. See `lossy_ring.h`.

**§7.3 ESP32-C3 single-core fallback:** no affinity; strict priority preemption,
`tEspNowRx` prio 6 preempts all; SD writes in ≤512 B yielded chunks; scan stays
async. A long SD stall can still add a few ms ESP-NOW jitter — acceptable, measured.

---

## 8. GPS-Aware Timing and Positioning

- **Stamp at RX on the coordinator**, not at worker scan time (workers are headless).
- **Interpolate** between the last two fixes; dead-reckon up to `EXTRAP_MAX_MS=1500`
  from last velocity; else mark `fix_type=STALE`.
- Store `pos_age_ms` and `fix_type` (0 none,1 2D,2 3D,3 interp,4 dead-reckoned) on
  every row so positional accuracy is auditable.
- **No fix:** log with `fix=0` + monotonic time; buffer ≤ `GPS_BUFFER_MS` for a late
  first fix. Never drop observations for lack of GPS.

---

## 9. Active vs. Passive Scan Policy

| Phase | Default | Why |
|---|---|---|
| Fast first pass | Active 40–60 ms | surfaces hidden/low-beacon APs fast |
| Hot-channel revisit | Passive 110–300 ms | depth without added probe traffic |
| 5 GHz DFS (52–144) | **Passive only (hard-gated)** | compliance: no active probe before radar detect |
| Operator/stealth set | Passive, configurable | honor local rules |

`is_dfs(ch) ⇒ passive` is a firmware invariant, not a tunable. Discovery/measurement
only — no deauth/injection/credential capture. Co-resident Marauder/BLE must
time-share the radio via a single `radioMutex`.

---

## 10. Representative Firmware Code

The drop-in headers in this folder ARE the representative code:
`pig_proto.h`, `bssid_cache.h`, `lossy_ring.h` (drop-in) and `yield_sched.h`
(requires integration). Integration-only snippets (worker `onScanComplete`,
coordinator wiring to `CoreNodeInfo`/`JCMK_CHANNELS`, FreeRTOS task creation with
`xTaskCreatePinnedToCore`) are in the chat transcript / to be written in Phase 1–4.

---

## 11. Queueing, Backpressure, and Drop Policy

`qObs` refuses deltas at 80% fill, overwrites oldest full at 100% (preserves the
unique-BSSID metric). `qCmd` reliable + coalesce newest. Heartbeats coalesce by
`seq`. Coordinator sets `summary_only` when `tStore.drops()` rises or SD latency
EWMA > `SD_LAT_WARN_MS=250`. Mesh airtime target ≤ `TX_DUTY_MAX=15%`.

---

## 12. Edge Cases and Failure Handling

No GPS → `fix=0`, never drop. Stationary → deeper revisit, dedup suppresses flood.
High speed → breadth-first, TTL scaled down. Dense env → narrow channel set,
density penalty, deltas drop first. Empty channel → freshness floor forces revisit.
Queue overload → §11 order. SD spike → isolated to tStore. Packet loss → `seq` gap
marks yield partial (reuse `jcmkSendFailCount` send-status cb). Worker reboot →
`assign_ver` mismatch forces resync; reclaim at 15 s. Active disabled → all passive;
DFS always passive. Marauder/BLE co-resident → `radioMutex` time-share.
Mixed mesh → legacy TEXT + contiguous ADMIN for non-WarHog peers.

---

## 13. Benchmark and Acceptance Criteria

Phase 0 emits a 1 Hz metrics line:
`t_ms, uniq_bssid_total, uniq_per_min, gross_per_min, mesh_tx_bytes,
mesh_tx_bytes_per_uniq, qObs_drops, qStore_drops, sd_lat_ewma_ms, assign_count,
reassign_rejected_by_gate, cache_fill_pct, nodes_active, gps_fix_type_hist,
pos_age_ms_p50/p95`.

| Metric (priority order) | Target |
|---|---|
| Unique BSSIDs/min | ≥ +15% multi-node mobile; ≥ parity single-node |
| Mesh airtime / unique BSSID | ≥ −40% (dedup) |
| Coordinator stability under load | 0 RX deadline misses; RX jitter p95 < 5 ms |
| Scaling with node count | ≥ 0.7× linear to 8 nodes |
| Positional accuracy | p95 `pos_age_ms` < 800; interp error < 5 m urban |

Use a **replay harness** (captured scan corpus fed to workers) so A/B is on
identical input, not live RF. Regression guard: any build that cuts single-node
uniq/min > 5% or introduces an RX deadline miss fails and reverts.

---

## 14. Phased Rollout Plan

| Phase | Ships | Gate to next |
|---|---|---|
| 0 Instrumentation | metrics line + replay harness, no behavior change | baselines recorded |
| 1 Worker dedup | `bssid_cache.h` + FULL/DELTA + binary batch (WarHog↔WarHog) | bytes/uniq −40%, uniq/min ≥ parity |
| 2 Adaptive dwell | fast pass + yield-weighted passive revisit | uniq/min ≥ parity, DFS passive verified |
| 3 Dynamic assign | `sched_assign()` + `PIG_MSG_ADMIN_EXT` + hysteresis | +≥15% multi-node, no thrash, no starvation |
| 4 Core isolation | task/core split + bounded queues + backpressure | 0 RX deadline misses @12 nodes |

Dedup first (biggest win, reduces load before adding scheduling complexity);
isolation last (preventive — protects phases 1–3 under scale).

---

## 15. Risks, Tradeoffs, When This Is Not Better

- Dynamic assignment may only match a hand-tuned static map, and EWMA warm-up can
  be worse for the first ~2 epochs. Seed `yield[]` from a stored profile; keep a
  `PIG_STATIC_OVERRIDE` escape hatch.
- Adaptive revisit trades breadth for depth — can reduce sparse-channel coverage;
  freshness/EPS floors bound it, but uniform rural sweeps may prefer round-robin.
- Dedup TTL too long at speed under-reports distinct geo-sightings (WiGLE wants
  multiple). Speed-scaled TTL mitigates; verify against post-processed tracks.
- Hysteresis adds reaction latency; distance-trigger is the counterweight.
- Core isolation shows no throughput gain if the coordinator wasn't bottlenecked —
  it's robustness, don't over-claim.
- C3 single-core can't fully isolate.
- Binary batch breaks naive text parsers; 212 B padding wastes airtime on small
  batches — negotiate per-peer, keep TEXT fallback.
- 512-entry cache ≈ 8 KB/worker — verify heap after BLE stacks load.

---

## 16. Final Implementation Checklist

Protocol: add `PIG_MSG_*` 0x20–0x23; `pig_obs_t`/`pig_batch_t` (212 B assert); CRC16;
TEXT fallback; marker negotiation.
Worker: integrate `BssidCache`; fast+passive revisit FSM over channel list; DFS hard
gate; emit `pig_yield_hb_t`; honor `pig_admin_ext_t` knobs.
Coordinator: per-channel EWMA; yield-packed assign + coverage floor; 3-gate
hysteresis + dwell-bands; RX-time GPS stamp + interp/dead-reckon + fix_type/pos_age;
speed/distance knobs; stale(6 s)/dead(15 s) HB + assign_ver resync.
Runtime: pin tasks per §7; `LossyRing qObs` + reliable `qCmd`; backpressure
`summary_only`; C3 fallback; `radioMutex` hook.
Validation: Phase 0 metrics + replay harness; ship 1→4 behind flags; per-phase gates;
auto-revert on regression.
