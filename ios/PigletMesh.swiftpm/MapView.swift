import SwiftUI
import MapKit
import CoreLocation

struct MapView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var region = MKCoordinateRegion(
        center: CLLocationCoordinate2D(latitude: 32.2988, longitude: -90.1848), // Jackson, MS default
        span: MKCoordinateSpan(latitudeDelta: 0.05, longitudeDelta: 0.05))
    @State private var didCenter = false

    // Networks that carry a GPS stamp, plus the Core itself.
    private var pins: [MapPin] {
        var out = ble.networks.compactMap { n -> MapPin? in
            guard let c = n.coordinate else { return nil }
            return MapPin(id: n.id, coord: c, kind: .network(rssi: n.lastRssi, open: n.isOpen))
        }
        if let c = ble.status?.coordinate {
            out.append(MapPin(id: "__core__", coord: c, kind: .core))
        }
        return out
    }

    var body: some View {
        Map(coordinateRegion: $region, annotationItems: pins) { pin in
            MapAnnotation(coordinate: pin.coord) { PinGlyph(kind: pin.kind) }
        }
        .ignoresSafeArea(edges: .bottom)
        .overlay(alignment: .bottomTrailing) { recenterButton }
        .overlay(alignment: .top) {
            if pins.isEmpty {
                Text("No GPS-stamped records yet — needs a fix on the Core.")
                    .font(.caption).padding(8)
                    .background(.ultraThinMaterial, in: Capsule()).padding(.top, 8)
            }
        }
        .onChange(of: ble.status?.lat) { _ in centerOnCoreOnce() }
    }

    private var recenterButton: some View {
        Button { centerOnCore(force: true) } label: {
            Image(systemName: "location.fill").padding(10)
                .background(.ultraThinMaterial, in: Circle())
        }.padding(16).tint(.pink)
    }

    private func centerOnCoreOnce() { if !didCenter { centerOnCore(force: false); } }
    private func centerOnCore(force: Bool) {
        guard let c = ble.status?.coordinate else { return }
        didCenter = true
        withAnimation { region.center = c }
    }
}

struct MapPin: Identifiable {
    enum Kind { case core; case network(rssi: Int, open: Bool) }
    let id: String
    let coord: CLLocationCoordinate2D
    let kind: Kind
}

struct PinGlyph: View {
    let kind: MapPin.Kind
    var body: some View {
        switch kind {
        case .core:
            Image(systemName: "shippingbox.fill")
                .foregroundStyle(.white).padding(6)
                .background(Color.pink, in: Circle())
                .overlay(Circle().stroke(.white, lineWidth: 2))
                .shadow(radius: 3)
        case .network(let rssi, let open):
            Image(systemName: open ? "lock.open.fill" : "wifi")
                .font(.system(size: 9, weight: .bold))
                .foregroundStyle(.black)
                .padding(4)
                .background(rssiColor(rssi), in: Circle())
        }
    }
}
