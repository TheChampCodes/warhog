// swift-tools-version: 5.9
// PigletMesh — iOS/iPadOS app that connects to a Piglet running as Mesh Core
// over BLE and shows live wardriving data. Opens directly in Swift Playgrounds
// (iPad/Mac) and in Xcode.
import PackageDescription
import AppleProductTypes

let package = Package(
    name: "PigletMesh",
    platforms: [
        .iOS("16.0")
    ],
    products: [
        .iOSApplication(
            name: "PigletMesh",
            targets: ["AppModule"],
            bundleIdentifier: "us.piglet.mesh",
            teamIdentifier: nil,                     // set automatically by the signing team
            displayVersion: "1.0",
            bundleVersion: "1",
            appIcon: .placeholder(icon: .network),
            accentColor: .presetColor(.pink),
            supportedDeviceFamilies: [
                .pad,
                .phone
            ],
            supportedInterfaceOrientations: [
                .portrait,
                .landscapeRight,
                .landscapeLeft,
                .portraitUpsideDown(.when(deviceFamilies: [.pad]))
            ],
            capabilities: [
                // Injects NSBluetoothAlwaysUsageDescription into the generated Info.plist.
                .bluetoothAlways(purposeString: "PigletMesh connects to your Piglet Core over Bluetooth to show live wardriving data from the mesh.")
            ]
        )
    ],
    targets: [
        .executableTarget(
            name: "AppModule",
            path: "."
        )
    ]
)
