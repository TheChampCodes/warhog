# WarHog Web Flasher

A browser-based firmware installer for the WarHog wardriving ecosystem. Powered by **ESP Web Tools** (`web-serial`), it allows users to flash pre-compiled WarHog firmware directly to any supported ESP32 board over a USB cable—without installing Python, Arduino IDE, PlatformIO, or vendor drivers.

> **Firmware note:** the fork-built targets (every card except the upstream-tracking T-Dongle C5) now ship with the [yield-driven mesh scheduler](../scheduler/) **on by default** — worker BSSID dedup, adaptive dwell, dynamic yield-weighted channel assignment, and (on the dual-band/dual-core Core boards) core-isolated SD logging.

---

## 1. Supported Firmware Targets & Manifests

The flasher automatically serves manifest configurations and binaries from [`manifests/`](manifests/):

| Board Name | Chip Target | Description | Manifest File |
|---|---|---|---|
| **Freenove S3 CYD** | ESP32-S3 | 2.4 GHz scanning, GPS, SD logging, 2.8″ touch display (HAM UI4 suite) | `manifests/freenove-s3-cyd-ham-ui4.json` |
| **Guition JC8048W550** | ESP32-S3 | 5.0″ 800×480 RGB IPS panel, GT911 touch, GPS, SD logging | `manifests/guition-jc8048w550.json` |
| **WarHogNode C3** | ESP32-C3 | Minimal 2.4 GHz ESP-NOW mesh worker node with 0.42″ OLED & animated PigWorld | `manifests/esp32c3_oled.json` |
| **WarHog Core (XIAO C5)** | ESP32-C5 | Dual-band 2.4/5 GHz Wi-Fi 6, GPS, SD, OLED, BLE Bridge | `manifests/xiao_c5.json` (fork) · `manifests/xiao_c5-ham.json` (official) |
| **WarHogNode C5** | ESP32-C5 | Headless dual-band 2.4/5 GHz ESP-NOW mesh scanning node (no screen/SD) | `manifests/warhognode_c5.json` (fork) · `manifests/pigletnode_c5-ham.json` (official) |
| **Waveshare C6 Mini** | ESP32-C6 | 1.47″ ST7789 vertical color TFT, GPS, SD logging | `manifests/waveshare_c6.json` |
| **LilyGo T-Dongle C5** | ESP32-C5 | USB-A dongle, 0.96″ ST7735 TFT, APA102 LED, GPS, SD, dual-band — *official build* | `manifests/tdongle_c5-ham.json` |

### Fork vs. official firmware toggle

The two XIAO **C5** cards (WarHog Core and WarHogNode C5) carry a **skon's fork / official** switch. The switch repoints the installer at upstream [`Hamspiced/piglet`](https://github.com/Hamspiced/piglet)'s **latest** GitHub release (`Piglet.ino.merged.bin` / `PigletNode.ino.merged.bin`) via the `releases/latest/download/…` URL, so it always tracks ham's newest release with no version pinning. The fork side is now synced to upstream **v2.7**.

The **T-Dongle C5** card is official-only: the fork's T-Dongle firmware tracks upstream exactly, so it flashes ham's prebuilt `TDongleC5_Piglet.ino.merged.bin` (latest) directly. The CYD, Guition, Waveshare, and ESP32-C3 cards stay fork-only — upstream ships no build for those boards.

---

## 2. Interactive Features

* **Zero Toolchain Required:** Runs entirely client-side in Chromium-based browsers (Google Chrome, Microsoft Edge, Brave, Opera) supporting the Web Serial API.
* **Animated Device Previews:** Built with [`screens.js`](screens.js) and [`pig.js`](pig.js) to render interactive canvas animations of the real device displays:
  * 1-bit trotting pixel-art pig simulating the WarHogNode C3 OLED screen.
  * Live status dashboard simulation of the Freenove CYD / Guition touch interface.
  * Real-time telemetry feed mockup of the Waveshare C6 display.
* **Fallback Command Generator:** Displays copy-pasteable `esptool.py` CLI flashing commands for users flashing from non-WebSerial browsers or terminal environments.

---

## 3. How to Use

1. Connect your ESP32 board to your computer via a data-capable USB cable.
2. Open `index.html` in a supported browser (Chrome, Edge, or Brave).
3. Find your board in the grid.
4. Click the **Install** button on the card.
5. Select your board's serial port from the browser prompt and click **Connect**.
6. Follow the on-screen installation prompts.

> **Note on ESP32-C5 / ESP32-C6 Boards:** The native USB-Serial/JTAG controller on some C5 and C6 boards may re-enumerate when the browser flasher attempts to load the execution stub. If flashing fails to start in the browser, use the terminal command shown on the card with the `--no-stub` argument:
> ```bash
> esptool --chip esp32c5 --no-stub write-flash 0x0 firmware.bin
> ```

---

## 4. Local Development & Hosting

To serve the web flasher locally:
```bash
# Using Python
python -m http.server 8000

# Using Node.js npx
npx serve .
```
Navigate to `http://localhost:8000` in Google Chrome or Microsoft Edge.

To host on **GitHub Pages**, ensure GitHub Pages is enabled on the repository branch pointing to the `web-flasher/` directory or root.

---

## 5. License

Open source under CC BY-NC-SA 4.0. Built with [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
