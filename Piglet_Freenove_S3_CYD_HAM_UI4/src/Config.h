#pragma once
#include <Arduino.h>
#include "PinMapDefs.h"

struct Config {
  String wigleBasicToken;
  String homeSsid;
  String homePsk;
  String wardriverSsid = "Piglet-WARDRIVE";
  String wardriverPsk  = "wardrive1234";
  uint32_t gpsBaud     = 9600;
  String scanMode      = "aggressive"; // aggressive | powersaving
  bool bluetoothScan   = true;         // Passively scan BLE devices and log them to the WiGLE CSV (Type=BLE) for extra WDGoWars points.
  bool scan24          = true;         // Log 2.4 GHz Wi-Fi networks while wardriving.
  bool scan5           = true;         // Log 5 GHz Wi-Fi networks (only effective on 5 GHz-capable boards, e.g. XIAO C5).
  bool biscuitReport   = false;        // While solo wardriving, also announce + forward found networks over ESP-NOW (ch 6) so a Biscuit Manager sees this device as a biscuit.
  String blePersonality = "biscuit";   // Core-mode BLE app: "biscuit" (Biscuit Manager) or "wardrivego" (RocketGod Wardrive Go / WarBLE Go iosESP bridge).
  String board = "auto"; // auto | s3 | c5 | c6 | c3 | exp  (pins selected at boot; reboot required after change)
  String speedUnits  = "kmh"; // kmh | mph
  int battPin        = 9;     // GPIO for battery voltage ADC (-1 = disabled). Expects 1:2 voltage divider from LiPo (FNK0104B: GPIO 9).
  bool batteryTest   = false; // Enable battery test (logs elapsed time to /battery_test.csv)
  
  // Boot auto-upload limit:
  //  -1 = upload ALL files at boot (no limit)
  //   0 = disabled (no auto-upload at boot)
  //  1+ = upload up to N files at boot (WiGLE allows 25 API calls/day)
  // IMPORTANT: Requires PSRAM enabled in Arduino IDE for reliable TLS connections.
  int maxBootUploads = 25;

  // WDGoWars API key from https://wdgwars.pl/profile -> "Generate API key".
  // If set, CSVs are uploaded to WDGoWars BEFORE WiGLE at every boot.
  // Leave empty to disable WDGoWars uploads.
  String wdgwarsApiKey;

  // Wardrift API key (wdk_...) from wardrift.net, sent as "X-API-Key".
  // The CSV file is uploaded as-is (raw body, Content-Type: text/csv,
  // X-File-Name header) to POST /v1/ingest/signals -- no multipart wrapper
  // and no parsing needed. If set, CSVs are uploaded to Wardrift AFTER
  // WDGoWars but BEFORE WiGLE at every boot (2nd in the chain). Leave empty
  // to disable Wardrift uploads.
  String wardriftApiKey;

  // Custom SSID broadcast by the Marauder beacon-spam tool when its SSID source
  // is set to "Custom". Empty falls back to a generated name.
  String beaconSsid;

  // Optional device name — appended to WiGLE CSV header and filename so
  // multiple Piglets uploading to the same account can be distinguished.
  // E.g. deviceName=rover1  →  device=Piglet-rover1  /  rover1_Piglet_WiGLE_....csv
  // Leave empty for default ("Piglet-Wardriver" / "WiGLE_....csv").
  String deviceName;

  // Auto-start mesh mode on boot: core, node, or none.
  // core — become the mesh coordinator (receives wardriving records from nodes).
  // node — become a scanning node that forwards records to the Core.
  // none — normal solo wardriving mode.
  // Default is "core" for this Core firmware build; change on the SYS screen.
  String meshModeOnBoot = "core";

  // Rotate the OLED display 180° (true = upside-down mount, false = normal).
  // Requires reboot to take effect.
  bool rotateScreen180 = false;

  // When true: after boot uploads complete, disconnect from home WiFi and
  // begin wardriving immediately instead of staying on the STA connection.
  // The web UI is still reachable if you connect to the Wardriver AP later,
  // but the device will not hold the STA link open. Requires reboot.
  bool autoStartAfterUpload = false;

  // When true (default): if the configured home SSID is seen in a scan
  // result while wardriving, the device stops scanning, connects to it, and
  // uploads pending CSVs automatically. Mutually exclusive with
  // autoStartAfterUpload -- enforced in cfgEnforceMutualExclusion() so the
  // two features can never both be active (one wants to leave home Wi-Fi
  // for good after boot, the other wants to return to it mid-drive).
  bool uploadOnNetworkSeen = true;

  // Network whitelist: up to 10 SSIDs that are scanned normally (still count
  // toward Found totals) but are never written to the CSV log -- e.g. your
  // own home/work network you don't want captured. Exact, case-sensitive
  // match. Empty slots are ignored. Applies to both local scanning and
  // mesh-forwarded records written by this device.
  String ssidWhitelist[10];
};

const PinMap& detectPinsByChip();
PinMap pickPinsFromConfig();
bool wardriverIsC5();

String trimCopy(String s);
bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut);
void cfgAssignKV(const String& k, const String& v);
bool loadConfigFromSD();
bool saveConfigToSD();

// Forces uploadOnNetworkSeen off whenever autoStartAfterUpload is on, so the
// two mutually-exclusive STA-connection behaviors never both run. Call after
// any bulk config change (file load, Web UI save) -- not from cfgAssignKV()
// itself, since key order within a save is not guaranteed.
void cfgEnforceMutualExclusion();
