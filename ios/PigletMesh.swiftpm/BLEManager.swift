import Foundation
import CoreBluetooth

// ================================================================
//  GATT UUIDs — must match firmware (Arduino Files/Piglet/BLEBridge.cpp)
// ================================================================
enum PigletGATT {
    static let service = CBUUID(string: "7A1F0000-9B7E-4C2A-9E1D-9A6F3C5B1E00")
    static let status  = CBUUID(string: "7A1F0001-9B7E-4C2A-9E1D-9A6F3C5B1E00")
    static let record  = CBUUID(string: "7A1F0002-9B7E-4C2A-9E1D-9A6F3C5B1E00")
    static let nodes   = CBUUID(string: "7A1F0003-9B7E-4C2A-9E1D-9A6F3C5B1E00")
    static let control = CBUUID(string: "7A1F0004-9B7E-4C2A-9E1D-9A6F3C5B1E00")
}

enum LinkState: Equatable {
    case bluetoothOff
    case unauthorized
    case scanning
    case connecting(String)
    case connected(String)

    var label: String {
        switch self {
        case .bluetoothOff:      return "Bluetooth is off"
        case .unauthorized:      return "Bluetooth permission needed"
        case .scanning:          return "Searching for a Piglet Core…"
        case .connecting(let n): return "Connecting to \(n)…"
        case .connected(let n):  return n
        }
    }
    var isConnected: Bool { if case .connected = self { return true }; return false }
}

@MainActor
final class BLEManager: NSObject, ObservableObject {
    // Published UI state
    @Published var link: LinkState = .scanning
    @Published var status: CoreStatus?
    @Published var roster: [NodeInfo] = []
    @Published var networks: [Network] = []        // deduped, newest activity first
    @Published var recordCount: Int = 0            // raw records seen this session
    @Published var discovered: [DiscoveredCore] = [] // cores seen while scanning

    struct DiscoveredCore: Identifiable {
        let id: UUID
        let name: String
        var rssi: Int
    }

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var peripheralsById: [UUID: CBPeripheral] = [:]   // discovered, pre-connect
    private var controlChar: CBCharacteristic?
    private var nodesChar: CBCharacteristic?
    private var lastRosterVer: Int = -1

    private var byBssid: [String: Int] = [:]       // BSSID -> index in networks
    private let decoder = JSONDecoder()

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Public actions
    func startScan() {
        guard central.state == .poweredOn else { return }
        discovered.removeAll()
        link = .scanning
        central.scanForPeripherals(withServices: [PigletGATT.service],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    func connect(_ core: DiscoveredCore) {
        guard let p = peripheralsById[core.id] else { return }
        central.stopScan()
        peripheral = p
        p.delegate = self
        link = .connecting(core.name)
        central.connect(p, options: nil)
    }

    func disconnect() {
        if let p = peripheral { central.cancelPeripheralConnection(p) }
    }

    func clearNetworks() {
        networks.removeAll(); byBssid.removeAll(); recordCount = 0
    }

    func send(command: String) {
        guard let p = peripheral, let c = controlChar,
              let data = "{\"cmd\":\"\(command)\"}".data(using: .utf8) else { return }
        let type: CBCharacteristicWriteType =
            c.properties.contains(.write) ? .withResponse : .withoutResponse
        p.writeValue(data, for: c, type: type)
    }

    // MARK: - Record aggregation
    private func ingest(_ rec: ScanRecord) {
        recordCount += 1
        if let idx = byBssid[rec.b] {
            var n = networks[idx]
            n.lastRssi = rec.r
            n.bestRssi = max(n.bestRssi, rec.r)
            n.lastSeen = Date()
            n.hits += 1
            if n.ssid.isEmpty, !rec.s.isEmpty { n.ssid = rec.s }
            if n.coordinate == nil { n.coordinate = rec.coordinate }
            networks.remove(at: idx)
            networks.insert(n, at: 0)
            reindex()
        } else {
            let n = Network(id: rec.b, ssid: rec.s, auth: rec.a, channel: rec.c,
                            band: rec.band, bestRssi: rec.r, lastRssi: rec.r,
                            coordinate: rec.coordinate, lastSeen: Date(), hits: 1)
            networks.insert(n, at: 0)
            reindex()
        }
    }

    private func reindex() {
        byBssid.removeAll(keepingCapacity: true)
        for (i, n) in networks.enumerated() { byBssid[n.id] = i }
    }
}

// ================================================================
//  CoreBluetooth delegates (queue is main; safe to touch @Published)
// ================================================================
extension BLEManager: CBCentralManagerDelegate, CBPeripheralDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        Task { @MainActor in
            switch central.state {
            case .poweredOn:     startScan()
            case .unauthorized:  link = .unauthorized
            case .poweredOff:    link = .bluetoothOff
            default:             link = .bluetoothOff
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name ?? "Piglet Core"
        let id = peripheral.identifier
        Task { @MainActor in
            peripheralsById[id] = peripheral
            if let i = discovered.firstIndex(where: { $0.id == id }) {
                discovered[i].rssi = RSSI.intValue
            } else {
                discovered.append(.init(id: id, name: name, rssi: RSSI.intValue))
            }
            // Auto-connect to the first Core found while still scanning.
            if case .scanning = link, let first = discovered.first {
                connect(first)
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        Task { @MainActor in
            link = .connected(peripheral.name ?? "Piglet Core")
            peripheral.discoverServices([PigletGATT.service])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                                    error: Error?) {
        Task { @MainActor in
            controlChar = nil; nodesChar = nil; status = nil; roster = []
            lastRosterVer = -1
            startScan()               // auto-reconnect
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = peripheral.services?.first(where: { $0.uuid == PigletGATT.service }) else { return }
        peripheral.discoverCharacteristics(
            [PigletGATT.status, PigletGATT.record, PigletGATT.nodes, PigletGATT.control], for: svc)
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                                error: Error?) {
        for c in service.characteristics ?? [] {
            switch c.uuid {
            case PigletGATT.status, PigletGATT.record:
                peripheral.setNotifyValue(true, for: c)
            case PigletGATT.nodes:
                peripheral.setNotifyValue(true, for: c)
                peripheral.readValue(for: c)                 // pull the full roster now
                Task { @MainActor in self.nodesChar = c }
            case PigletGATT.control:
                Task { @MainActor in self.controlChar = c }
            default: break
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                                error: Error?) {
        guard let data = characteristic.value, !data.isEmpty else { return }
        let uuid = characteristic.uuid
        Task { @MainActor in
            switch uuid {
            case PigletGATT.status:
                if let s = try? decoder.decode(CoreStatus.self, from: data) {
                    status = s
                    // Roster version bumped → re-read the full (possibly long) node list.
                    if s.rosterVer != lastRosterVer, let nc = nodesChar {
                        peripheral.readValue(for: nc)
                    }
                }
            case PigletGATT.record:
                if let r = try? decoder.decode(ScanRecord.self, from: data) { ingest(r) }
            case PigletGATT.nodes:
                if let roster = try? decoder.decode(NodeRoster.self, from: data) {
                    self.roster = roster.nodes
                    self.lastRosterVer = roster.rosterVer
                }
            default: break
            }
        }
    }
}
