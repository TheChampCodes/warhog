# Waveshare ESP32-C6 LCD 1.47″ Piglet Edition

A standalone wardriving and wireless research port configured for the **Waveshare ESP32-C6-LCD-1.47** development board. Combines 2.4 GHz Wi-Fi 6 scanning, real-time telemetry on an elongated high-resolution color TFT, onboard MicroSD storage, and GPS logging into a compact single-board package.

---

## 1. Hardware Specifications

| Component | Detail |
|---|---|
| **SoC** | Espressif ESP32-C6 (32-bit RISC-V @ 160 MHz) |
| **Wireless** | 2.4 GHz Wi-Fi 6 (802.11ax/b/g/n), Bluetooth 5 (LE), Zigbee/Thread (802.15.4) |
| **Display** | 1.47″ ST7789 Color IPS TFT (172×320 pixels, SPI) |
| **Storage** | Onboard MicroSD (TF) card slot (SPI) |
| **GPS Interface** | Dedicated UART header (RX = GPIO 17, TX = GPIO 16) |
| **User Button** | Onboard Key / Button on GPIO 2 (Active LOW, pull-up) |
| **Power & Debug** | USB Type-C with native USB-Serial/JTAG |

---

## 2. Hardware Pinout Reference

| Subsystem | Signal | GPIO Pin | Function |
|---|---|---|---|
| **ST7789 Display** | SCK | GPIO 7 | Hardware SPI Clock (Shared) |
| | MOSI | GPIO 6 | Hardware SPI Data Out (Shared) |
| | CS | GPIO 14 | Display Chip Select |
| | DC | GPIO 15 | Data / Command Select |
| | RST | GPIO 21 | Hardware Reset |
| | BL | GPIO 22 | Backlight Control |
| **MicroSD (TF)** | CS | GPIO 4 | SD Chip Select |
| | SCK | GPIO 7 | Hardware SPI Clock (Shared) |
| | MOSI | GPIO 6 | Hardware SPI Data Out (Shared) |
| | MISO | GPIO 5 | Hardware SPI Data In |
| **UART GPS** | RX | GPIO 17 | ESP32 receive from GPS TX |
| | TX | GPIO 16 | ESP32 transmit to GPS RX |
| **User Input** | BTN | GPIO 2 | Scanning pause / resume toggle |

---

## 3. Key Features

* **Real-Time Color Telemetry:** Tailored UI utilizing LovyanGFX to render high-contrast stats on the 172×320 vertical display:
  * Scanning status, current channel dwell, and detected AP counters.
  * Real-time GPS fix metrics (latitude, longitude, altitude, speed, HDOP precision).
  * Unique network detection alert popups.
* **WiGLE-Compliant Logging:** Writes Wi-Fi network observations directly to `/wardriver_YYYYMMDD_HHMMSS.csv` on the MicroSD card.
* **Modularized Web Interface:** Features [`WebUI_HTML.h`](WebUI_HTML.h) for responsive management over SoftAP (192.168.4.1) or home Wi-Fi station mode.
* **Auto-Upload to WiGLE:** When home Wi-Fi credentials are configured in `/wardriver.cfg`, automatically uploads pending CSV logs on boot or when in range.

---

## 4. Building and Flashing

Builds are managed using PlatformIO via [`platformio.ini`](platformio.ini) with the pinned `pioarduino` 55.03.39 environment.

### PlatformIO (Recommended)
```bash
# Build
pio run

# Flash (specify your serial port or let it auto-detect)
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor
```

> **ESP32-C6 Flashing Note:** The USB-Serial/JTAG connection can disconnect if `esptool` initializes its execution stub. The included build profile automatically specifies `--no-stub` to flash directly via the ROM loader.

### Arduino IDE
1. Board: **ESP32C6 Dev Module**.
2. Flash Size: **4MB**.
3. Partition Scheme: **Huge APP (3MB No OTA)**.
4. Libraries:
   * `LovyanGFX`
   * `TinyGPSPlus`
   * `ArduinoJson` (v6.x)

---

## 5. External GPS Wiring

Connect any standard 3.3V UART GPS module (BN-180, NEO-6M, NEO-8M, or ATGM336H):
* **GPS VCC** $\rightarrow$ **3.3V**
* **GPS GND** $\rightarrow$ **GND**
* **GPS TX**  $\rightarrow$ **GPIO 17 (ESP32 RX)**
* **GPS RX**  $\rightarrow$ **GPIO 16 (ESP32 TX)**

---

## 6. License

Distributed under **CC BY-NC-SA 4.0**. Based on Piglet by Hamspiced.
