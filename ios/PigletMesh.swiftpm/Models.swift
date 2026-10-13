import Foundation
import CoreLocation

// ================================================================
//  Wire models — match docs/BLE_BRIDGE_PROTOCOL.md (PROTO_VER 1)
// ================================================================

/// Status characteristic (7A1F0001). Notified ~1 Hz; also readable.
struct CoreStatus: Decodable {
    var v: Int
    var fw: String
    var core: Bool
    var rx: UInt32
    var nodes: Int
    var rosterVer: Int
    var found2g: UInt32
    var found5g: UInt32
    var fix: Bool
    var lat: Double
    var lon: Double
    var alt: Double
    var acc: Double
    var upMs: UInt32

    var coordinate: CLLocationCoordinate2D? {
        guard fix, lat != 0 || lon != 0 else { return nil }
        return CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }
}

/// Record characteristic (7A1F0002). One scan record per notification.
struct ScanRecord: Decodable {
    var b: String          // BSSID
    var s: String          // SSID
    var a: String          // auth/caps
    var c: Int             // channel
    var r: Int             // RSSI (dBm)
    var lat: Double
    var lon: Double
    var alt: Double
    var acc: Double
    var t: String          // ISO-8601 UTC

    var coordinate: CLLocationCoordinate2D? {
        guard lat != 0 || lon != 0 else { return nil }
        return CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }
    var band: String { c >= 32 ? "5 GHz" : "2.4 GHz" }
}

/// One entry in the Nodes characteristic (7A1F0003) roster array.
struct NodeInfo: Decodable, Identifiable {
    var mac: String
    var kind: String       // "piglet" | "biscuit" | "jcmk"
    var rx: UInt32
    var ch0: Int
    var ch1: Int
    var slot: Int
    var ageMs: UInt32

    var id: String { mac }
}

struct NodeRoster: Decodable {
    var v: Int
    var rosterVer: Int
    var nodes: [NodeInfo]
}

// ================================================================
//  View model — a deduplicated network, aggregated from records
// ================================================================
struct Network: Identifiable {
    let id: String          // BSSID
    var ssid: String
    var auth: String
    var channel: Int
    var band: String
    var bestRssi: Int
    var lastRssi: Int
    var coordinate: CLLocationCoordinate2D?
    var lastSeen: Date
    var hits: Int

    var displaySsid: String { ssid.isEmpty ? "〈hidden〉" : ssid }
    var isOpen: Bool {
        let a = auth.uppercased()
        return !a.contains("WPA") && !a.contains("WEP") && !a.contains("PSK") && !a.contains("SAE")
    }
}
