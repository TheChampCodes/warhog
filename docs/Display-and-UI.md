# Display & UI

## Display stacks
The firmware supports several panels behind a small abstraction so the app code draws the
same way regardless of hardware.

| Board | Driver | Notes |
|---|---|---|
| Freenove S3 CYD | TFT_eSPI (ILI9341, SPI) | shares the SPI bus with the SD card |
| Guition JC8048W550 | LovyanGFX (800×480 RGB) | renders to a RAM sprite, then scale-blits |
| XIAO C5 / PigletNode C3 | SSD1306 OLED (I²C) | small monochrome |
| T-Dongle C5 | ST7735 color TFT | + APA102 status LED |
| Waveshare C6 | ST7789 TFT | 172×320 |

Relevant files: `Gfx.h` (draw abstraction), `LGFX_W550.h` (W550 LovyanGFX config),
`ViewportSprite.h` (the W550 offscreen sprite + scale-blit), `Display.cpp`, `Touch.cpp`
(FT6336 cap touch on the CYD, GT911 on the W550 — both I²C, off the SPI bus).

## Screen UI (`ScreenUI.cpp`) — S3 touch frontend
A page-based touch UI. Key entry points:
- `begin()`, `update(speed)`, `draw()`, `requestRedraw()`, `showSplash()`, `handleTouch()`.
- Header/footer/status chips: `drawHeader()`, `drawFooter()`, `drawStatusChip(...)`.
- Mesh mode menu: `MESH_MODES[] = {none, node, core, off}`; selecting one enters that mode.
  The STATUS card on a Core drills into the cluster view (node list + per-node channels).
- Serial-testable hooks: `triggerHomeUpload()`, `triggerCoreMode()`, `triggerRadioIdle()`
  (mirror the `u` / `c` / `o` serial commands — see [Serial & Telemetry](Serial-and-Telemetry.md)).

> On the CYD the Core screen is drawn from inside `coreModeTick`, i.e. it's a second writer
> on the shared SPI bus; that's why moving UI rendering to another core is treated as risky
> and why `present()` takes the `SpiBusLock` mutex when core isolation is on.

## Telemetry screens (`ScreenTelemetry.cpp`)
`screenTelemetryObserve(ssid, bssid, rssi, ch)` feeds live acquisition data (recent SSIDs,
RSSI, per-channel activity, logged-row count) that the stats/telemetry pages render. On a
Core this is fed from `coreParseAndLogText` (mesh records); in solo from the scanner.

## PigWorld (`PigWorld.cpp`, C5 Core)
The 1-bit pixel-art animation engine that gives the Core personality:
- a sprite animation loop (`pig_sprites.h`),
- **flocking** that scales 1→6 pigs with the live node count (a visual mesh-size readout),
- a 2.4 GHz **channel-occupancy histogram** driven by `SessionStats`.
It is cosmetic — no effect on scanning — but it's the quickest glance-read of mesh health.

## HAM logo / splash
`HamLogo.h` / `drawPigLogo()` / `drawPigFace()` render the boot splash and branding.
