import SwiftUI

@main
struct PigletMeshApp: App {
    @StateObject private var ble = BLEManager()
    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(ble)
                .preferredColorScheme(.dark)
                .tint(.pink)
        }
    }
}

struct RootView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        VStack(spacing: 0) {
            StatusHeader()
            Divider().overlay(Color.pink.opacity(0.3))
            if ble.link.isConnected {
                TabView {
                    NetworksView()
                        .tabItem { Label("Networks", systemImage: "wifi") }
                    MapView()
                        .tabItem { Label("Map", systemImage: "map") }
                    NodesView()
                        .tabItem { Label("Nodes", systemImage: "point.3.connected.trianglepath.dotted") }
                }
            } else {
                ConnectGate()
            }
        }
        .background(Color.black.ignoresSafeArea())
    }
}

// MARK: - Header shown on every screen
struct StatusHeader: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        let s = ble.status
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: 2) {
                Text("PIGLET MESH")
                    .font(.system(.caption, design: .monospaced)).bold()
                    .foregroundStyle(.pink)
                Text(ble.link.label)
                    .font(.caption2).foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer()
            if let s {
                Stat(value: "\(s.rx)", label: "records")
                Stat(value: "\(s.nodes)", label: "nodes")
                Stat(value: "\(ble.networks.count)", label: "nets")
                GpsPill(fix: s.fix)
            }
        }
        .padding(.horizontal, 16).padding(.vertical, 10)
    }
}

struct Stat: View {
    let value: String; let label: String
    var body: some View {
        VStack(spacing: 1) {
            Text(value).font(.system(.headline, design: .monospaced)).bold()
            Text(label).font(.system(size: 9)).foregroundStyle(.secondary)
        }
    }
}

struct GpsPill: View {
    let fix: Bool
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: fix ? "location.fill" : "location.slash")
            Text(fix ? "GPS" : "no fix").font(.system(size: 10, weight: .semibold))
        }
        .padding(.horizontal, 8).padding(.vertical, 5)
        .background((fix ? Color.green : Color.orange).opacity(0.18), in: Capsule())
        .foregroundStyle(fix ? .green : .orange)
    }
}

// MARK: - Not-connected screen
struct ConnectGate: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        VStack(spacing: 20) {
            Spacer()
            Image(systemName: iconName)
                .font(.system(size: 54)).foregroundStyle(.pink)
                .opacity(isSearching ? 0.55 : 1)
                .animation(isSearching ? .easeInOut(duration: 0.9).repeatForever(autoreverses: true) : .default,
                           value: isSearching)
            Text(ble.link.label).font(.headline)

            if case .unauthorized = ble.link {
                Text("Enable Bluetooth for PigletMesh in Settings → Privacy → Bluetooth.")
                    .font(.caption).foregroundStyle(.secondary)
                    .multilineTextAlignment(.center).padding(.horizontal, 40)
            }

            if !ble.discovered.isEmpty {
                VStack(spacing: 8) {
                    ForEach(ble.discovered) { core in
                        Button { ble.connect(core) } label: {
                            HStack {
                                Image(systemName: "dot.radiowaves.left.and.right")
                                Text(core.name).bold()
                                Spacer()
                                Text("\(core.rssi) dBm")
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundStyle(.secondary)
                            }
                            .padding().background(Color.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 12))
                        }.buttonStyle(.plain)
                    }
                }.padding(.horizontal, 30)
            }
            Spacer()
            Text("Put a Piglet into Mesh Core mode to connect.")
                .font(.caption2).foregroundStyle(.secondary).padding(.bottom, 24)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    private var isSearching: Bool { if case .scanning = ble.link { return true }; return false }
    private var iconName: String {
        switch ble.link {
        case .bluetoothOff, .unauthorized: return "antenna.radiowaves.left.and.right.slash"
        default: return "antenna.radiowaves.left.and.right"
        }
    }
}

// Shared RSSI → color helper
func rssiColor(_ r: Int) -> Color {
    if r >= -60 { return .green }
    if r >= -75 { return .yellow }
    return .red
}
