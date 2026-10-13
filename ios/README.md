# PigletMesh — iOS / iPadOS app

Connects to a Piglet running as **Mesh Core** over BLE and shows live wardriving
data: the deduplicated network list, a GPS map of stamped records, the node
roster, and Core controls. See [`../docs/BLE_BRIDGE_PROTOCOL.md`](../docs/BLE_BRIDGE_PROTOCOL.md)
for the wire contract it speaks.

`PigletMesh.swiftpm` is a Swift Package **App** project — it opens and builds in
**Swift Playgrounds** (iPad/Mac, free) *and* in **Xcode**. No Mac required to build.

## Build & run on an iPad (no Mac, no dev account)
1. Install **Swift Playgrounds** from the App Store.
2. Copy `PigletMesh.swiftpm` to the iPad (AirDrop / iCloud Drive / Files) and open it.
3. Tap **▶ Run**. The Bluetooth-usage permission is already declared via the
   package's `.bluetoothAlways` capability.
4. Put a Piglet into **Mesh Core** mode — the app auto-connects to `Piglet-Core`.

The app runs on the iPad itself. For wardriving, an iPad in a mount makes a fine console.

## Getting it onto an iPhone
Swift Playgrounds can't cable-sideload to a *different* device, so an iPhone build
goes through a paid Apple Developer account ($99/yr) — including a buddy's:
- **TestFlight:** build/upload to App Store Connect (from Playgrounds if your Apple
  ID is on the team, or hand the project to the account holder), add your Apple ID
  as a tester, install via the TestFlight app. Builds last 90 days.
- **Ad hoc / development:** register the iPhone's UDID and install a signed build
  (easiest from Xcode).

## Notes
- **Deployment target: iOS 16** for broad Swift Playgrounds compatibility. The map
  uses the `Map(coordinateRegion:annotationItems:)` API intentionally for that reason.
- **Background BLE** (receiving with the screen locked) needs the `bluetooth-central`
  background mode, which Swift Playgrounds doesn't expose — open the project once in
  Xcode and enable it if you want that. Foreground use needs nothing extra.
- UUIDs in `BLEManager.swift` (`PigletGATT`) must stay in sync with
  `Arduino Files/Piglet/BLEBridge.cpp`.

## Files
| File | Role |
|------|------|
| `Package.swift` | App package manifest + Bluetooth capability |
| `Models.swift` | Codable wire models + the `Network` view model |
| `BLEManager.swift` | CoreBluetooth central: scan, connect, decode, control |
| `PigletMeshApp.swift` | App entry, status header, connect screen |
| `NetworksView.swift` | Live deduplicated network list + filters |
| `MapView.swift` | GPS map of stamped records + Core position |
| `NodesView.swift` | Node roster + Core info + exit-mesh control |
