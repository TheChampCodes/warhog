import SwiftUI

/// JCMK scan-channel table — mirrors JCMK_CHANNELS[] in the firmware so we can
/// translate the roster's ch0/ch1 indices into real channel numbers.
enum JCMK {
    static let channels: [Int] = [
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        36,40,44,48,52,56,60,64,
        100,112,116,120,124,128,132,136,140,144,
        149,153,157,161,165,169,173,177
    ]
    static func channel(_ idx: Int) -> Int {
        (idx >= 0 && idx < channels.count) ? channels[idx] : idx
    }
}

struct NodesView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        NavigationStack {
            List {
                Section {
                    ForEach(ble.roster) { NodeRow(n: $0) }
                } header: {
                    Text(ble.roster.isEmpty ? "No nodes linked" : "\(ble.roster.count) node(s) linked")
                }

                Section("Core") {
                    if let s = ble.status {
                        LabeledRow("Firmware", s.fw)
                        LabeledRow("Records received", "\(s.rx)")
                        LabeledRow("2.4 GHz / 5 GHz", "\(s.found2g) / \(s.found5g)")
                        LabeledRow("Uptime", uptime(s.upMs))
                    }
                    Button(role: .destructive) { ble.send(command: "exitMesh") } label: {
                        Label("Exit mesh mode on Core", systemImage: "xmark.circle")
                    }
                }
            }
            .overlay { if ble.roster.isEmpty { waiting } }
            .navigationTitle("Nodes")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    private var waiting: some View {
        ContentUnavailableViewCompat(
            title: "Waiting for nodes",
            system: "point.3.connected.trianglepath.dotted",
            message: "Piglet nodes appear here once they link to this Core over ESP-Now.")
        .allowsHitTesting(false)
    }

    private func uptime(_ ms: UInt32) -> String {
        let s = Int(ms / 1000)
        let h = s / 3600, m = (s % 3600) / 60, sec = s % 60
        return h > 0 ? String(format: "%dh %02dm", h, m) : String(format: "%dm %02ds", m, sec)
    }
}

struct NodeRow: View {
    let n: NodeInfo
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                KindBadge(kind: n.kind)
                Text(n.mac).font(.system(.subheadline, design: .monospaced)).bold()
                Spacer()
                Text("\(n.rx)").font(.system(.subheadline, design: .monospaced)).bold()
                    .foregroundStyle(.pink)
            }
            HStack(spacing: 12) {
                Label("ch \(JCMK.channel(n.ch0))–\(JCMK.channel(n.ch1))", systemImage: "dot.radiowaves.right")
                if n.kind == "piglet" { Label("slot \(n.slot)", systemImage: "square.grid.3x1.below.line.grid.1x2") }
                Spacer()
                Text(age(n.ageMs)).foregroundStyle(freshness(n.ageMs))
            }
            .font(.caption2).foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
    }
    private func age(_ ms: UInt32) -> String {
        ms < 1000 ? "now" : (ms < 60000 ? "\(ms/1000)s ago" : "\(ms/60000)m ago")
    }
    private func freshness(_ ms: UInt32) -> Color {
        ms < 8000 ? .green : (ms < 20000 ? .yellow : .red)
    }
}

struct KindBadge: View {
    let kind: String
    var body: some View {
        Text(kind.uppercased())
            .font(.system(size: 9, weight: .heavy))
            .padding(.horizontal, 6).padding(.vertical, 3)
            .background(color.opacity(0.2), in: Capsule())
            .foregroundStyle(color)
    }
    private var color: Color {
        switch kind { case "piglet": return .pink; case "biscuit": return .orange; default: return .gray }
    }
}

struct LabeledRow: View {
    let k: String; let v: String
    init(_ k: String, _ v: String) { self.k = k; self.v = v }
    var body: some View {
        HStack { Text(k); Spacer(); Text(v).foregroundStyle(.secondary)
            .font(.system(.body, design: .monospaced)) }
    }
}
