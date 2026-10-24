# WarHog Core Firmware (Fork Edition)

An enhanced build of the official Piglet wardriving core, configured for the **Seeed Studio XIAO ESP32-C5** (and compatible XIAO ESP32-S3 / C6 / C3 boards). This version introduces multi-protocol mesh capabilities, direct BLE bridging to companion apps, Biscuit Pro coordinator compatibility, animated PigWorld graphics, and automated PlatformIO builds.

---

## 1. Hardware Overview

| Component | Specification |
|---|---|
| **Primary SoC** | Seeed Studio XIAO ESP32-C5 (Dual-Band Wi-Fi 6: 2.4 GHz + 5 GHz, RISC-V) |
| **Compatible SoCs** | XIAO ESP32-S3, XIAO ESP32-C6, XIAO ESP32-C3 |
| **Display** | 0.96″ or 0.91″ SSD1306 I²C OLED (128×64 or 128×32) |
| **GPS** | UART GPS module (NEO-6M / NEO-8M / ATGM336H) via standard RX/TX |
| **Storage** | MicroSD card via SPI slot |
| **Power** | USB-C or single-cell LiPo battery (onboard charger) |

---

## 2. Key Enhancements Over Upstream

### A. Core BLE Bridge (`BLEBridge.cpp` / `BLEBridge.h`)
* Integrated via `NimBLE-Arduino` 2.x (compatible with ESP-IDF 5.x / Arduino 3.x).
* Enables WarHog Core to bridge live wardriving telemetry and newly discovered networks over Bluetooth Low Energy to companion mobile apps and clients.
* Seamlessly merges BLE device observations with Wi-Fi network records into WiGLE-compliant CSV streams.

### B. Biscuit Pro Protocol Compatibility (`BiscuitCompat.cpp` / `BiscuitCompat.h`)
* Provides protocol interoperability with Hedge's **Biscuit Pro** / Biscuit Shop ecosystem.
* Allows WarHog Core to act as a coordinator or peer alongside Biscuit nodes, synchronizing channel assignments and scan payloads.

### C. Animated PigWorld Scene (`PigWorld.cpp`, `pig_sprites.h`)
* Integrates the custom pixel-art **PigWorld** animated scene into the OLED display loop.
* The pig trots, sniffs out networks, and reacts dynamically to newly logged BSSIDs and mesh events.

### D. WiGLE CSV Multi-Type Logging (`SDUtils.cpp`)
* Extends the WiGLE CSV writer to record observation types (`WIFI` vs `BLE`) dynamically, preserving standard WiGLE headers while enabling hybrid wireless logging.

---

## 3. Building and Flashing

This project supports both **PlatformIO** and **Arduino IDE**.

### Option 1: PlatformIO (Recommended)
Configuration is managed in [`platformio.ini`](platformio.ini) with pinned toolchains (`pioarduino` 55.03.39 / Arduino 3.3.9).

```bash
# Build
pio run

# Flash to XIAO C5 (auto-detects port)
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor
```

> **Note on ESP32-C5 Flashing:** The ESP32-C5's native USB-Serial/JTAG controller re-enumerates when `esptool` attempts to load its execution stub, which can drop the connection. The configuration passes `--no-stub` to flash directly via the ROM bootloader without requiring manual button sequences.

### Option 2: Arduino IDE
1. Install **ESP32 Arduino core** (v3.x) from Espressif.
2. Select Board: **XIAO_ESP32C5**.
3. Partition Scheme: **Huge APP (3MB No OTA)**.
4. Required Libraries (install via Library Manager):
   * `Adafruit SSD1306` & `Adafruit GFX Library`
   * `TinyGPSPlus`
   * `ArduinoJson` (v6.x)
   * `NimBLE-Arduino` (v2.x)

---

## 4. Operation Modes

Press the user button to cycle through active OLED display pages:
1. **Status:** Core status, satellites in fix, active channel range, battery voltage, and total unique AP count.
2. **Networks:** Live stream of recent BSSID, SSID, RSSI, and security flags.
3. **PigWorld:** Pixel-art animated status display.
4. **Mesh Core:** Coordinator dashboard showing connected worker nodes, forwarded records, and channel assignment health.

---

## 5. Configuration (`wardriver.cfg`)

Saved on the root of the SD card:
```ini
wifiSSID=YourHomeWiFi
wifiPassword=YourWiFiPassword
wigleAPIKey=AID:TOKEN
autoUpload=true
meshModeOnBoot=core   # 'none', 'core' (coordinator), or 'node' (worker)
rotateScreen180=false
```
