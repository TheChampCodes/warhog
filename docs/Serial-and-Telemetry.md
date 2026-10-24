# Serial & Telemetry

Serial is 115200 baud. On ESP32-S3/C5 it's the native USB-CDC (VID `303A`). To read
without rebooting the board, open the port with DTR/RTS deasserted (see
[Build & Flash](Build-and-Flash.md#reading-serial-without-rebooting-the-board)).

## Serial commands (single keypress, no newline)
Handled by `processSerialCommands()` in `main.cpp`:

| Key | Action |
|---|---|
| `v` | version + diagnostics: `FW <ver> heap=<bytes> battPin/battAvail/battV/raw_mV` |
| `u` | trigger a home-network upload (`displayTriggerHomeUpload`) |
| `b` | Biscuit reporter state (`cfg/active/linked/sent`) |
| `c` | toggle Core mode (`displayToggleCore`) — solo→core triggers a clean reboot; core→solo is in-place |
| `o` | OFF / radio-idle (tear down all radio consumers) |

> `heap=` from `v` is **internal** free RAM on the S3 (PSRAM isn't in the default pool).
> Core mode is tight on internal RAM; SD/Wi-Fi DMA draws from this pool, so a low number
> here predicts `sdmmc … err=0x101` (ENOMEM) under load.

## Live metric lines
### `PIGM,` — Phase-0 acquisition metrics (piglet_fork only)
A 1 Hz CSV accountant line (DESIGN §13), hard-wired via `PigMetricsHook`:
```
PIGM, t_ms, uniq_bssid_total, uniq_per_min, gross_per_min, mesh_tx_bytes,
      mesh_tx_bytes_per_uniq, qObs_drops, qStore_drops, sd_lat_ewma_ms,
      assign_count, reassign_rejected_by_gate, cache_fill_pct, nodes_active,
      gps_fix_type_hist, pos_age_ms_p50/p95, gross_total, supp_pct
```
Use it to A/B the dedup win (`mesh_tx_bytes_per_uniq`) and watch cache fill.

### `[CORE-ISO]` — Phase-4 core-isolation health (S3 Core, every 5 s)
```
[CORE-ISO] SD store q: depth=N drops=M | rx_drop txt=T req=R txtq_max=Q
           | rt_us max=A gt5ms=B | loop_us max=X gt5ms=Y | nodes=Z
```
- `depth`/`drops` — the core-0 SD queue occupancy / lossy drops (want low / 0).
- `rx_drop txt=/req=` — ESP-NOW records/requests dropped because the RX ring was full
  (a real RX deadline miss; want 0). `txtq_max` — peak ring occupancy.
- `rt_us max`/`gt5ms` — the RX-drain task's cadence and the count of ticks over the 5 ms
  jitter target (the acceptance metric; want `gt5ms=0`).
- `loop_us max`/`gt5ms` — the main-loop period. Expected to be large (~100–220 ms) and
  **harmless** once Step 5 decouples RX from the loop — don't chase it.
- `nodes` — active node count.

### `[PAIR]` — Phase-5 antenna-pair overlap (S3 Core, every 5 s, per group)
```
[PAIR] grp=g overlap both=B either=E pct=P%
```
`pct` = how often a group's two nodes saw the same BSSID within 1 s. **Lower is better**
(complementary coverage). This is the dual-node field-test's key metric.

### Other notable lines
- `[SCAN] Async scan started (dwell=100 ms)` — solo scanner (not mesh).
- `[CORE] cfg -> <mac>: channels=…;dwell=…` — a per-node channel assignment the Core sent.
- `[CORE] Sub-cluster assign: K node(s) across G group(s), vN` — an assignment pass.
- `[CORE] New Biscuit node …` / `Node N timed out` — roster churn.
- `[MESH] Core: <mac>` — a node attached to a Core.
