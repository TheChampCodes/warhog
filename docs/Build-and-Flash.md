# Build & Flash

## Prerequisites
- **PlatformIO** (CLI or the pioarduino-based envs in each `platformio.ini`). On this
  project the CLI lives at `~/.platformio/penv/Scripts/pio.exe`.
- A host C++ toolchain only for the scheduler **host unit tests** (`g++`/`clang++`).

## Building a firmware
```bash
pio run -d "<project dir>" -e "<env>"
```
Examples:
```bash
pio run -d WarHog_Freenove_S3_CYD_HAM_UI4 -e freenove-s3-cyd-ham-ui4
pio run -d WarHog_Freenove_S3_CYD_HAM_UI4 -e guition-jc8048w550
pio run -d WarHogNode -e warhognode_c5
pio run -d c3_node   -e esp32c3_oled
pio run -d "warhog_fork" -e xiao_c5
```
Build artifacts land in `<project>/.pio/build/<env>/` — `firmware.bin`, `bootloader.bin`,
`partitions.bin`, and the merged `firmware.factory.bin` (bootloader + partitions +
boot_app0 + app at offset `0x0`).

### Toggling a feature flag for a one-off build
Flags default to their `platformio.ini` values. Override without editing the ini:
```bash
PLATFORMIO_BUILD_FLAGS="-DPIG_FEAT_CORE_ISO=0" pio run -d <dir> -e <env>   # force a flag off
PLATFORMIO_BUILD_FLAGS="-UPIG_FEAT_PAIR_SCHED" pio run -d <dir> -e <env>   # undefine → header default (0)
```
> A one-off build writes into the **same** `.pio/build/<env>/` dir, so it overwrites
> `firmware.factory.bin`. Rebuild at the normal flags before flashing a release image.

## Flashing

### Normal path
```bash
pio run -d <dir> -e <env> -t upload --upload-port <PORT>
```

### ESP32-S3 USB-Serial-JTAG gotcha
The S3 display boards expose the **hardware USB-Serial-JTAG**, which PlatformIO's default
`default_reset` can't drop into the ROM bootloader — you get
`A fatal error occurred: Wrong boot mode detected (0x2a)!`. Flash directly with esptool
using `--before usb-reset`:
```bash
python <.platformio>/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port COM3 --baud 460800 \
  --before usb-reset --after hard-reset \
  write-flash --flash-mode keep --flash-freq keep --flash-size keep \
  0x0 .pio/build/freenove-s3-cyd-ham-ui4/firmware.factory.bin
```
(The merged `firmware.factory.bin` at `0x0` avoids juggling per-partition offsets.)

### Reading serial without rebooting the board
Opening a serial monitor normally asserts DTR and **resets** the board. To attach without
a reboot, construct the port with DTR/RTS deasserted *before* opening:
```python
import serial, time, sys
s = serial.Serial(); s.port = sys.argv[1]; s.baudrate = 115200; s.timeout = 0.3
s.dtr = False; s.rts = False
s.open()
while True:
    line = s.readline()
    if line: print(line.decode('utf-8','replace').rstrip())
```
Even so, budget one boot banner per capture; don't mistake it for a crash.

## The CI / web-flasher pipeline
`.github/workflows/build-and-deploy.yml` builds **every** board env, merges each into a
single flashable image with `esptool merge_bin`, assembles the web-flasher with the built
`firmware/*.bin`, stamps the version (`<ref>+<sha7>`), and deploys to **GitHub Pages**.

- Trigger: `workflow_dispatch` (manual) or a `v*` tag push.
- The **deploy job needs the build job** — if any env fails to build, the deploy is
  skipped, so a broken env never overwrites the live flasher.
- Matrix: `xiao_c5`, `freenove-s3-cyd-ham-ui4`, `guition-jc8048w550`, `esp32c3_oled`,
  `warhognode_c5`, `waveshare_c6`. `TDongleC5_WarHog` is an Arduino sketch and is **not**
  in CI (its header copies get no build gate).
- Live flasher: **https://olosnoks.github.io/warhog** — see [Web Flasher](Web-Flasher.md).

## Partitions & flash
16 MB flash, `default_16MB.csv` partition table, `qio_opi` memory type on the S3 boards
(`BOARD_HAS_PSRAM`). Note `ESP.getFreeHeap()` on the S3 reports **internal** RAM (PSRAM is
not in the default malloc pool); DMA-using subsystems (SD, Wi-Fi) draw from that internal
pool, which is why internal-RAM headroom matters on a Core.
