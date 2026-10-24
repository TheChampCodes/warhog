# LilyGo T-Dongle C5 WarHog Edition

A self-contained wardriving and wireless mapping firmware tailored for the **LilyGo T-Dongle C5**. Encased in a compact USB-A flash drive form factor, this build provides dual-band 2.4 GHz and 5 GHz Wi-Fi 6 scanning, onboard color TFT telemetry, an RGB status indicator, and autonomous WiGLE CSV logging.

---

## 1. Hardware Specifications

| Component | Detail |
|---|---|
| **SoC** | Espressif ESP32-C5 (32-bit RISC-V @ up to 160 MHz) |
| **Wireless** | Dual-Band Wi-Fi 6 (802.11ax/b/g/n, 2.4 GHz + 5 GHz UNII-1/2/2e/3) |
| **Display** | 0.96″ ST7735 Color TFT LCD (80×160 pixels) |
| **Indicator** | APA102 Addressable RGB LED |
| **Storage** | MicroSD (TF) card slot |
| **Form Factor** | USB-A Dongle with transparent plastic case |
| **User Input** | Side Push Button (GPIO 28) |
| **Expansion Header** | 4-pin Qwiic/JST connector for external UART GPS |

---

## 2. Pin Mapping & Hardware Bus Architecture

The T-Dongle C5 utilizes a multiplexed SPI bus to drive the ST7735 display, MicroSD card, and APA102 LED:

| Subsystem | Signal | GPIO Pin | Notes |
|---|---|---|---|
| **TFT Display** | SCK / MOSI | GPIO 6 / GPIO 2 | Hardware SPI (Shared) |
| | CS / DC | GPIO 10 / GPIO 3 | Chip Select & Data/Command |
| | RST / Backlight | GPIO 1 / GPIO 0 | Hardware reset & Backlight power |
| **MicroSD (TF)** | CS | GPIO 23 | Dedicated SPI Chip Select |
| | SCK / MOSI / MISO | GPIO 6 / GPIO 2 / GPIO 7 | Hardware SPI (Shared) |
| **APA102 RGB LED** | DI / CI | GPIO 2 / GPIO 7 | Bit-banged when LCD and SD are deselected |
| **External GPS** | RX / TX | GPIO 12 / GPIO 11 | UART1 on 4-pin Qwiic / JST connector |
| **User Button** | KEY | GPIO 28 | Active LOW with internal pull-up |

---

## 3. Features & Firmware Capabilities

* **Dual-Band 2.4 GHz + 5 GHz Scanning:** Scans across all 14 standard 2.4 GHz channels plus 5 GHz channels (36–165).
* **Self-Contained Color UI:** Custom ST7735 graphics stack rendering:
  * **Status Page:** Live satellite count, coordinates, current scan channel, total discovered APs, and memory usage.
  * **Networks Page:** Scrolling feed of recently captured SSIDs, BSSIDs, RSSI signal strength, and encryption types.
  * **Navigation Page:** GPS heading, velocity, and fix accuracy.
  * **Pig Animation:** Pixel-art animated trotting pig mascot.
  * **Mesh Node Mode:** Joins a WarHog or JCMK coordinator over ESP-NOW channel 6, forwarding scan bursts in real time.
* **APA102 Multi-Color Telemetry:**
  * **Blue Pulse:** Searching for GPS fix / scanning.
  * **Green Flash:** New unique Wi-Fi network discovered.
  * **Yellow:** SoftAP active / Web UI mode.
  * **Purple:** Mesh Node mode active.
* **WiGLE CSV Logging:** Directly writes WiGLE-formatted CSV logs to the onboard MicroSD card, including observation type distinction (`WIFI` vs `BLE`).
* **Web UI Management:** Integrated SoftAP and STA Web UI for viewing logs, editing `/wardriver.cfg`, and uploading scans directly to WiGLE, WDGoWars, or Wardrift over home Wi-Fi.

---

## 4. Building and Flashing

### Arduino IDE
1. Open Arduino IDE and add the ESP32 Arduino Core URL in Preferences:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Install **ESP32 by Espressif** (version 3.x).
3. Select Board: **XIAO_ESP32C5** or **ESP32C5 Dev Module**.
4. Install Required Libraries via Library Manager:
   * `Adafruit GFX Library`
   * `Adafruit ST7735 and ST7789 Library`
   * `Adafruit BusIO`
   * `TinyGPSPlus`
   * `ArduinoJson` (v6.x)
5. Open `TDongleC5_WarHog.ino` and upload.

> **Flashing Tip (USB-Serial/JTAG):** The ESP32-C5's USB-Serial/JTAG controller may disconnect when `esptool` initializes its execution stub. When flashing via terminal or command line, include `--no-stub`:
> ```bash
> esptool --chip esp32c5 --no-stub write-flash 0x0 firmware.bin
> ```

---

## 5. External GPS Wiring

Connect any standard 3.3V UART GPS module (e.g., Beitian BN-180, NEO-6M/8M, or ATGM336H) to the onboard 4-pin JST-SH connector:

```
[GPS Module]          [T-Dongle C5 JST Connector]
   VCC      ------->     3.3V
   GND      ------->     GND
   TX       ------->     RX (GPIO 12)
   RX       ------->     TX (GPIO 11)
```

---

## 6. License

Distributed under the **CC BY-NC-SA 4.0** license. Based on Piglet by Hamspiced.
