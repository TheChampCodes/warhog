import SwiftUI

struct NetworksView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var query = ""
    @State private var openOnly = false

    var filtered: [Network] {
        ble.networks.filter { n in
            (!openOnly || n.isOpen) &&
            (query.isEmpty
             || n.ssid.localizedCaseInsensitiveContains(query)
             || n.id.localizedCaseInsensitiveContains(query))
        }
    }

    var body: some View {
        NavigationStack {
            List {
                ForEach(filtered) { n in NetworkRow(n: n) }
            }
            .listStyle(.plain)
            .overlay { if ble.networks.isEmpty { EmptyHint() } }
            .searchable(text: $query, prompt: "SSID or BSSID")
            .navigationTitle("Networks")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Toggle(isOn: $openOnly) { Text("Open").font(.caption) }
                        .toggleStyle(.button).tint(.pink)
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Menu {
                        Button(role: .destructive) { ble.clearNetworks() } label: {
                            Label("Clear list", systemImage: "trash")
                        }
                        Button { ble.send(command: "clearStats") } label: {
                            Label("Reset Core counters", systemImage: "arrow.counterclockwise")
                        }
                    } label: { Image(systemName: "ellipsis.circle") }
                }
            }
        }
    }
}

struct NetworkRow: View {
    let n: Network
    var body: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 6) {
                    Text(n.displaySsid).font(.body).bold().lineLimit(1)
                    if n.isOpen {
                        Image(systemName: "lock.open").font(.caption2).foregroundStyle(.orange)
                    }
                }
                Text(n.id).font(.system(.caption2, design: .monospaced)).foregroundStyle(.secondary)
                Text("ch \(n.channel) · \(n.band) · \(n.auth)")
                    .font(.caption2).foregroundStyle(.secondary).lineLimit(1)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 3) {
                HStack(spacing: 4) {
                    Image(systemName: "wifi").font(.caption2)
                    Text("\(n.lastRssi)").font(.system(.subheadline, design: .monospaced)).bold()
                }.foregroundStyle(rssiColor(n.lastRssi))
                Text("best \(n.bestRssi)").font(.system(size: 9, design: .monospaced))
                    .foregroundStyle(.secondary)
                if n.coordinate != nil {
                    Image(systemName: "mappin.circle.fill").font(.caption2).foregroundStyle(.pink)
                }
            }
        }
        .padding(.vertical, 2)
    }
}

struct EmptyHint: View {
    var body: some View {
        ContentUnavailableViewCompat(
            title: "Waiting for records",
            system: "wifi.exclamationmark",
            message: "Records appear here as nodes forward scans to the Core.")
    }
}

/// iOS 16-safe stand-in for ContentUnavailableView (iOS 17+).
struct ContentUnavailableViewCompat: View {
    let title: String; let system: String; let message: String
    var body: some View {
        VStack(spacing: 10) {
            Image(systemName: system).font(.system(size: 40)).foregroundStyle(.secondary)
            Text(title).font(.headline)
            Text(message).font(.caption).foregroundStyle(.secondary)
                .multilineTextAlignment(.center).padding(.horizontal, 40)
        }
    }
}
