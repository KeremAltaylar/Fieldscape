// The app's frame around the map (the approved mock, design/app/gen_mock.py): the top bar - place
// picker, layers, account - and the sheets it opens: Places (search, parks, routes) and Layers (base
// map, park boundary, zones, sections, fit all). Tokens only (Theme.swift); targets >= 44 pt (M-4).
import SwiftUI
import CoreLocation

/* What the map shows and where it should go; the sheets write it, MapView reads it. */
final class MapState: ObservableObject {
    enum Base: String, CaseIterable { case map = "Map", topo = "Topo", satellite = "Satellite", virtual = "Virtual" }
    @Published var base: Base = .satellite
    @Published var boundary = true
    @Published var zones = false
    @Published var sections = false
    /* a request to frame these corners (sw, ne); a new value each time, so the same place can be asked twice */
    @Published var frame: (sw: CLLocationCoordinate2D, ne: CLLocationCoordinate2D, id: UUID)? = nil
    func show(_ sw: CLLocationCoordinate2D, _ ne: CLLocationCoordinate2D) { frame = (sw, ne, UUID()) }
}

enum Sheet: Equatable { case walk, places, layers, account }

struct TopBar: View {
    @ObservedObject var walk: Walk
    @Binding var sheet: Sheet
    var body: some View {
        HStack(spacing: T.s2) {
            Button { sheet = .places } label: {
                HStack(spacing: T.s2) {
                    Text(walk.place ?? "Open world").font(T.display(20)).foregroundStyle(T.ink).lineLimit(1)
                    Text("▾").foregroundStyle(T.faint)
                }
                .padding(.horizontal, T.s4).frame(minHeight: T.target)
                .background(T.panel, in: Capsule()).overlay(Capsule().stroke(T.hairline))
            }
            .accessibilityLabel("Places and routes")
            Spacer(minLength: T.s2)
            RoundButton(system: "square.3.layers.3d", label: "Map layers") { sheet = .layers }
            RoundButton(system: "person", label: "Account") { sheet = .account }
        }
        .padding(.horizontal, T.s4)
    }
}

struct RoundButton: View {
    let system: String, label: String, action: () -> Void
    var body: some View {
        Button(action: action) {
            Image(systemName: system).font(.system(size: 17, weight: .regular)).foregroundStyle(T.ink)
                .frame(width: T.target, height: T.target)
                .background(T.panel, in: Circle()).overlay(Circle().stroke(T.hairline))
        }
        .accessibilityLabel(label)
    }
}

/* a row of choices, one on (the mock's segmented control) */
struct Segmented<V: Hashable>: View {
    let items: [(V, String)]
    @Binding var value: V
    var body: some View {
        HStack(spacing: 2) {
            ForEach(items, id: \.0) { v, name in
                Button { value = v } label: {
                    Text(name).font(T.body(14)).foregroundStyle(value == v ? T.ink : T.dim)
                        .frame(maxWidth: .infinity, minHeight: 40)
                        .background(value == v ? T.raised : .clear, in: RoundedRectangle(cornerRadius: 8))
                }
                .accessibilityAddTraits(value == v ? .isSelected : [])
            }
        }
        .padding(3).background(T.sunk, in: RoundedRectangle(cornerRadius: 11)).overlay(RoundedRectangle(cornerRadius: 11).stroke(T.hairline))
    }
}

struct Toggles: View {                /* independent on/off chips (Show: zones, sections) */
    let items: [(String, Binding<Bool>)]
    var body: some View {
        HStack(spacing: T.s2) {
            ForEach(items.indices, id: \.self) { i in
                let (name, on) = items[i]
                Button { on.wrappedValue.toggle() } label: {
                    Text(name).font(T.body(14)).foregroundStyle(on.wrappedValue ? T.ink : T.dim)
                        .padding(.horizontal, T.s3).frame(minHeight: 40)
                        .background(on.wrappedValue ? T.raised : T.sunk, in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
                }
                .accessibilityAddTraits(on.wrappedValue ? .isSelected : [])
            }
        }
    }
}

func eyebrow(_ s: String) -> some View {
    Text(s.uppercased()).font(T.mono(T.xs)).tracking(T.xs * T.trackEyebrow).foregroundStyle(T.faint)
}

struct SheetTitle: View {
    let title: String, close: () -> Void
    var body: some View {
        HStack {
            Text(title).font(T.display(T.lg)).foregroundStyle(T.ink)
            Spacer()
            RoundButton(system: "xmark", label: "Close", action: close)
        }
    }
}

struct PlacesSheet: View {
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState
    @Binding var sheet: Sheet
    @State private var q = ""
    var body: some View {
        let places = walk.placeList.filter { q.isEmpty || $0.name.localizedCaseInsensitiveContains(q) }
        let routes = walk.routeList.filter { q.isEmpty || $0.name.localizedCaseInsensitiveContains(q) }
        VStack(alignment: .leading, spacing: T.s3) {
            SheetTitle(title: "Places") { sheet = .walk }
            HStack(spacing: T.s2) {
                Image(systemName: "magnifyingglass").foregroundStyle(T.faint)
                TextField("", text: $q, prompt: Text("Search parks and routes").foregroundColor(T.faint))
                    .font(T.body(15)).foregroundStyle(T.ink).autocorrectionDisabled()
            }
            .padding(.horizontal, T.s3).frame(minHeight: T.target)
            .background(T.sunk, in: RoundedRectangle(cornerRadius: 10)).overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
            ScrollView {
                VStack(alignment: .leading, spacing: T.s2) {
                    if places.isEmpty && routes.isEmpty {
                        Text("Nothing matches “\(q)”.").font(T.body(T.sm)).foregroundStyle(T.dim).padding(.vertical, T.s3)
                    }
                    if !places.isEmpty { eyebrow("Parks") }
                    ForEach(places) { p in
                        Button { map.show(p.sw, p.ne); sheet = .walk } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(p.name).font(T.body(17)).foregroundStyle(T.ink)
                                Text(p.detail).font(T.mono(11.5)).foregroundStyle(T.faint)
                            }
                            .frame(maxWidth: .infinity, minHeight: 52, alignment: .leading).padding(.horizontal, T.s3)
                            .background(p.name == walk.place ? T.raised : .clear, in: RoundedRectangle(cornerRadius: 10))
                        }
                    }
                    if !routes.isEmpty { eyebrow("Routes").padding(.top, T.s2) }
                    ForEach(routes) { r in
                        Button { map.show(r.sw, r.ne); sheet = .walk } label: {
                            HStack(spacing: T.s3) {
                                Image(systemName: "point.topleft.down.to.point.bottomright.curvepath").foregroundStyle(T.dim)
                                Text(r.name).font(T.body(16)).foregroundStyle(T.ink)
                                Spacer()
                                Text(r.length).font(T.mono(11.5)).foregroundStyle(T.faint)
                            }
                            .frame(minHeight: T.target).padding(.horizontal, T.s3)
                        }
                    }
                }
            }
        }
    }
}

struct LayersSheet: View {
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState
    @Binding var sheet: Sheet
    var body: some View {
        VStack(alignment: .leading, spacing: T.s3) {
            SheetTitle(title: "Map") { sheet = .walk }
            eyebrow("Base map")
            Segmented(items: MapState.Base.allCases.map { ($0, $0.rawValue) }, value: $map.base)
            eyebrow("Park boundary")
            HStack(spacing: T.s2) {
                Segmented(items: [(false, "Off"), (true, "Frame")], value: $map.boundary)
                Button { if let b = walk.allBounds { map.show(b.sw, b.ne) } } label: {
                    Text("Fit all").font(T.body(15, .medium)).foregroundStyle(T.ink).frame(width: 96).frame(minHeight: T.target)
                        .background(T.raised, in: RoundedRectangle(cornerRadius: 10)).overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
                }
            }
            eyebrow("Show")
            Toggles(items: [("Zones", $map.zones), ("Sections", $map.sections)])
        }
    }
}

struct AccountSheet: View {
    @ObservedObject var core: Core
    @Binding var sheet: Sheet
    @State private var developer = false
    var body: some View {
        VStack(alignment: .leading, spacing: T.s3) {
            SheetTitle(title: "Account") { sheet = .walk }
            eyebrow("Setter")
            Text("Signing in as a setter comes in a coming test: placing points, recording and shaping the sound.")
                .font(T.body(T.sm)).foregroundStyle(T.dim).fixedSize(horizontal: false, vertical: true)
            eyebrow("About")
            Text("Fieldscape · Test \(TEST_BUILD) · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors, © OpenTopoMap")
                .font(T.body(T.sm)).foregroundStyle(T.dim).fixedSize(horizontal: false, vertical: true)
            DisclosureGroup(isExpanded: $developer) {
                ScrollView { DeveloperPanel(core: core, open: true) }.frame(maxHeight: 300)
            } label: { eyebrow("Developer") }
            .tint(T.faint)
        }
    }
}
