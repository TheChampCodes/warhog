# Hardware & Firmware Projects

Each board has its own firmware project and PlatformIO environment. Several share source
(the two S3 display boards share one `src/` tree; the C5 Core/node/dongle share headers).

## Board matrix
| Project (dir) | PlatformIO env | SoC | Display / input | Bands | GPS / SD | Mesh role |
|---|---|---|---|---|---|---|
| `WarHog_Freenove_S3_CYD_HAM_UI4/` | `freenove-s3-cyd-ham-ui4` | ESP32-S3 (dual-core, PSRAM) | 2.4″ 240×320 SPI TFT (ILI9341), cap touch | 2.4 GHz | UART GPS, 4-bit SDIO | Core / Solo / Security |
| `WarHog_Freenove_S3_CYD_HAM_UI4/` | `guition-jc8048w550` | ESP32-S3 N16R8 | 5.0″ 800×480 RGB IPS, GT911 touch | 2.4 GHz | UART GPS, SPI SD | Core / Solo / Dashboard |
| `warhog_fork/` | `xiao_c5` | ESP32-C5 (dual-band) | 0.96″ SSD1306 OLED, button | 2.4 + 5 GHz | UART GPS, SPI SD | Core / Solo |
| `WarHogNode/` | `warhognode_c5` | ESP32-C5 | headless | 2.4 + 5 GHz | — | Worker node |
| `c3_node/` | `esp32c3_oled` | ESP32-C3 (single-core) | 0.42″ 72×40 SSD1306 OLED, button | 2.4 GHz | headless | Worker node |
| `TDongleC5_WarHog/` | *(Arduino sketch, not in CI)* | ESP32-C5 | 0.96″ ST7735 TFT, APA102 LED | 2.4 + 5 GHz | UART, SPI SD | Dongle / Node / Core |
| `waveshareDisplayMiniWarHog/` | `waveshare_c6` | ESP32-C6 | 1.47″ 172×320 ST7789 TFT, button | 2.4 GHz | UART, SPI SD | Solo wardriver |

> **5 GHz** is only available on the C5/C6 silicon. On 2.4-only boards the scheduler
> drops unscannable 5 GHz channels from a node's assignment at build time.

## The two S3 display boards share one tree
`WarHog_Freenove_S3_CYD_HAM_UI4/src/` builds **both** the Freenove CYD (SPI ILI9341 via
TFT_eSPI) and the Guition W550 (800×480 RGB via LovyanGFX). All Wi-Fi/BLE/GPS/mesh code
is identical; the envs differ only in the display stack and the SD backend (SDIO vs SPI).
This is why a change to `MeshNode.cpp` affects both envs at once.

- Display abstraction: `Gfx.h`, `LGFX_W550.h`, `ViewportSprite.h` (the W550 renders to a
  RAM sprite then scale-blits to the panel).
- **Shared-SPI hazard (CYD):** the SD card and the ILI9341 TFT are on the *same* physical
  SPI bus (pins 10–13). Any code that writes SD and TFT from different tasks must take the
  `SpiBusLock.h` recursive mutex — see [Adaptive Scheduler → Core isolation](Adaptive-Scheduler.md#phase-4--core-isolation).

## The C5 family shares headers
`warhog_fork` (Core/solo), `WarHogNode` (worker), and `TDongleC5_WarHog` (dongle) all run
on the ESP32-C5 and share the scheduler headers. Because the Arduino-IDE sketches
(`TDongleC5_WarHog`) have no include path, they keep **byte-identical copies** of
`pig_proto.h`, `pig_dedup.h`, and `pig_dwell.h` in the sketch folder. If you change a
scheduler header, sync those copies (see [Testing & Contributing](Testing-and-Contributing.md)).

## Chip / toolchain notes
- Platform: pinned `pioarduino` **55.03.39** (ESP-IDF 5.x / Arduino 3.x) across every env.
- ESP32-S3 native USB enumerates as VID `303A` PID `1001`. It uses the hardware
  USB-Serial-JTAG, which needs esptool `--before usb-reset` (PlatformIO's default
  `default_reset` fails with *"Wrong boot mode detected (0x2a)"*). See [Build & Flash](Build-and-Flash.md).
- ESP32-C3 is single-core → no FreeRTOS core affinity; `PIG_FEAT_CORE_ISO` there is a
  priority-only fallback.
