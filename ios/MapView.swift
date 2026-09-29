// The map: the web app's style (index.html, "Map") on MapLibre Native, so the two read the same.
// Satellite base (the web's default), routes as a white line on a dark casing, points filled
// "lamp" when they carry a recording and hollow when not. Published features come from the same
// public view the web reads, with the anon key the web ships.
import MapLibre
import SwiftUI

enum Supa {
    static let url = "https://ujdygmcpqsbyeysggypc.supabase.co"
    static let anon = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVqZHlnbWNwcXNieWV5c2dneXBjIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODkwNDE4NDIsImV4cCI6MjEwNDYxNzg0Mn0.SJlrNKQftKxdM0G6f18e6PsRCvdhIH8fco5tW3CktwM"

    /* Published features as a GeoJSON FeatureCollection, id and properties flattened the way the
       web's `features` source has them. */
    static func published() async throws -> [String: Any] {
        var req = URLRequest(url: URL(string: url + "/rest/v1/public_features?select=id,kind,geometry,properties")!)
        req.setValue(anon, forHTTPHeaderField: "apikey")
        req.setValue("Bearer " + anon, forHTTPHeaderField: "Authorization")
        let (data, _) = try await URLSession.shared.data(for: req)
        let rows = (try JSONSerialization.jsonObject(with: data) as? [[String: Any]]) ?? []
        let features: [[String: Any]] = rows.map { r in
            var p = (r["properties"] as? [String: Any]) ?? [:]
            p["id"] = r["id"]
            p["kind"] = r["kind"]                  /* route / point: the walk tells them apart by it */
            return ["type": "Feature", "geometry": r["geometry"] ?? NSNull(), "properties": p]
        }
        return ["type": "FeatureCollection", "features": features]
    }
}

/* Studio tokens (Design System/Tokens.md), as the web resolves them from OKLCH. */
enum Ink {
    static let sunk = "#0d1310", ink = "#e3e7e4", lamp = "#bae6b1", accent = "#bbceb5"
}

struct MapView: UIViewRepresentable {
    let features: [String: Any]
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState
    @Binding var sheet: Sheet
    @Binding var collapsed: Bool

    func makeUIView(context: Context) -> MLNMapView {
        let v = MLNMapView(frame: .zero, styleURL: styleURL())
        v.logoView.isHidden = true                 /* the attribution stays: a condition of the tiles */
        v.attributionButtonPosition = .topLeft      /* above the walk panel, never under it */
        v.compassViewPosition = .topRight
        v.attributionButton.tintColor = UIColor(T.faint)
        v.delegate = context.coordinator
        v.showsUserLocation = true
        /* A tap opens what is under it - a point's card, a route's card. The walker moves by being
           dragged (Kerem, 2026-09-26: "move the listener by clicking and dragging it, this will allow
           us to click points and routes"): a drag that starts on the dot moves it at once; a press
           and hold anywhere puts it there and keeps dragging.
           MapLibre has its own tap and press recognisers on the view, which won over these (Kerem's
           iPhone: taps did nothing): the delegate lets ours recognise alongside them. */
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Frame.tapped(_:)))
        tap.delegate = context.coordinator
        v.gestureRecognizers?.filter { ($0 as? UITapGestureRecognizer)?.numberOfTapsRequired == 2 }.forEach { tap.require(toFail: $0) }
        v.addGestureRecognizer(tap)
        let drag = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Frame.dragged(_:)))
        drag.minimumPressDuration = 0.25
        drag.delegate = context.coordinator
        v.addGestureRecognizer(drag)
        let grab = UIPanGestureRecognizer(target: context.coordinator, action: #selector(Frame.dragged(_:)))
        grab.delegate = context.coordinator
        context.coordinator.grab = grab
        v.addGestureRecognizer(grab)
        /* a press that became a walk is not also a tap on what lies under it */
        tap.require(toFail: drag); tap.require(toFail: grab)
        return v
    }

    func updateUIView(_ v: MLNMapView, context: Context) {
        context.coordinator.walk = walk
        context.coordinator.map = map
        context.coordinator.pick = { s in sheet = s; if s != .walk { collapsed = false } }
        context.coordinator.carded = sheet != .walk && sheet != .places && sheet != .layers && sheet != .account
        context.coordinator.apply(map, walk, on: v)
        context.coordinator.show(walk.mode == .byHand ? walk.here : nil, on: v)
    }

    func makeCoordinator() -> Frame { let f = Frame(bounds: bounds()); f.walk = walk; return f }

    /* Framing needs the view's real size, which it only has once the style has loaded. */
    final class Frame: NSObject, MLNMapViewDelegate, UIGestureRecognizerDelegate {
        func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith o: UIGestureRecognizer) -> Bool { true }
        /* A touch that lands on the walker's dot drags it, and the map holds still; any other touch
           leaves the map free to pan (asked at touch-down, before any recogniser decides). */
        func gestureRecognizer(_ g: UIGestureRecognizer, shouldReceive t: UITouch) -> Bool {
            guard g === grab, let v = g.view as? MLNMapView else { return true }
            let on = walk?.here.map { h -> Bool in
                let p = v.convert(h, toPointTo: v), q = t.location(in: v)
                return hypot(p.x - q.x, p.y - q.y) < 32
            } ?? false
            v.isScrollEnabled = !on
            return on
        }
        let bounds: MLNCoordinateBounds?
        weak var walk: Walk?
        weak var map: MapState?
        weak var grab: UIPanGestureRecognizer?
        var pick: (Sheet) -> Void = { _ in }
        var carded = false
        private var style: MLNStyle?
        private var framed: UUID?
        private var zonesKey = ""
        private var sectionsKey = ""
        private var progKey = ""
        private var segments: [[String: Any]] = []           /* every route's chord segments, drawn once */

        /* The Layers sheet's choices, the Places sheet's framing, and the zone / section overlays. */
        func apply(_ m: MapState, _ w: Walk, on v: MLNMapView) {
            if let f = m.frame, f.id != framed {
                framed = f.id
                v.setVisibleCoordinateBounds(MLNCoordinateBounds(sw: f.sw, ne: f.ne), edgePadding: UIEdgeInsets(top: 120, left: 40, bottom: 320, right: 40), animated: true, completionHandler: nil)
            }
            guard let s = style else { return }
            for (id, base) in [("base-osm", MapState.Base.map), ("base-topo", .topo), ("base-sat", .satellite)] { s.layer(withIdentifier: id)?.isVisible = m.base == base }
            s.layer(withIdentifier: "park-line")?.isVisible = m.boundary
            for id in ["zones-fill", "zones-line"] { s.layer(withIdentifier: id)?.isVisible = m.zones }
            for id in ["sections-fill", "sections-line"] { s.layer(withIdentifier: id)?.isVisible = m.sections }
            /* zones: the one you stand in lit (drawZones) */
            if m.zones, let src = s.source(withIdentifier: "zones") as? MLNShapeSource {
                let zs = w.zoneCircles
                let inside = zs.map { z in w.here.map { fs_geo_distance($0.longitude, $0.latitude, z.lon, z.lat) <= z.r } ?? false }
                let key = "\(zs.count) " + inside.map { $0 ? "1" : "0" }.joined()
                if key != zonesKey {
                    zonesKey = key
                    src.shape = MapView.shape(zs.indices.map { i in (MapView.circle(zs[i].lon, zs[i].lat, zs[i].r), ["inside": inside[i] ? 1 : 0]) })
                }
            }
            /* sections: each in its mode's root colour from the playing route (drawSectors), the one underfoot lit */
            if m.sections, let src = s.source(withIdentifier: "sections") as? MLNShapeSource {
                let (cells, active) = w.sectionCells()
                let roots = w.playingRoute >= 0 && w.playingRoute < w.routeList.count ? w.routeList[w.playingRoute].sectors : (w.routeList.first?.sectors ?? [])
                let key = "\(cells.count) \(active) \(w.playingRoute) \(cells.first?.first ?? [])"
                if key != sectionsKey {
                    sectionsKey = key
                    src.shape = MapView.shape(cells.indices.map { i in
                        (cells[i], ["active": i == active ? 1 : 0, "colour": roots.isEmpty ? Ink.lamp : T.rootHex(roots[i % roots.count])] as [String: Any])
                    })
                }
            }
            /* the progression along every route, the chord playing lit (drawProgSegments) */
            if let src = s.source(withIdentifier: "progseg") as? MLNShapeSource {
                if segments.isEmpty {
                    for r in w.routeList { for (k, c) in r.prog.enumerated() {
                        let n = Double(r.prog.count)
                        segments.append(["route": r.index, "step": k, "colour": T.rootHex(c.pc), "coords": MapView.segment(r.coords, Double(k) / n, Double(k + 1) / n)])
                    } }
                }
                let key = "\(segments.count) \(w.playingRoute) \(w.chordStep)"
                if key != progKey {
                    progKey = key
                    let fc: [String: Any] = ["type": "FeatureCollection", "features": segments.map { f -> [String: Any] in
                        let lit = f["route"] as? Int == w.playingRoute && f["step"] as? Int == w.chordStep
                        return ["type": "Feature", "properties": ["route": f["route"]!, "step": f["step"]!, "colour": f["colour"]!, "active": lit ? 1 : 0],
                                "geometry": ["type": "LineString", "coordinates": f["coords"]!]]
                    }]
                    if let d = try? JSONSerialization.data(withJSONObject: fc), let sh = try? MLNShape(data: d, encoding: String.Encoding.utf8.rawValue) { src.shape = sh }
                }
            }
        }
        private var walker: MLNPointAnnotation?
        private var lastDrag: CLLocationCoordinate2D?
        init(bounds: MLNCoordinateBounds?) { self.bounds = bounds }
        private var centred = false

        @objc func tapped(_ g: UITapGestureRecognizer) {
            guard let v = g.view as? MLNMapView, let w = walk else { return }
            let p = g.location(in: v), box = CGRect(x: p.x - 22, y: p.y - 22, width: 44, height: 44)   /* M-4: a 44 pt target */
            if let f = v.visibleFeatures(in: box, styleLayerIdentifiers: ["point-dot"]).first, let id = (f.attribute(forKey: "id") as? String) ?? (f.identifier as? String) {
                pick(.point(id)); return
            }
            let rs = v.visibleFeatures(in: box, styleLayerIdentifiers: ["prog-seg", "route-line"])
            if let i = rs.compactMap({ $0.attribute(forKey: "route") as? Int }).first
                ?? rs.compactMap({ f in (f.attribute(forKey: "name") as? String).flatMap { n in w.routeList.first { $0.name == n }?.index } }).first {
                pick(.route(i)); return
            }
            if carded { pick(.walk) }                                    /* a tap on nothing puts a card away */
        }
        @objc func dragged(_ g: UIGestureRecognizer) {
            guard let v = g.view as? MLNMapView else { return }
            let c = v.convert(g.location(in: v), toCoordinateFrom: v)
            /* while the walker is dragged the map holds still */
            if g.state == .began { lastDrag = nil; v.isScrollEnabled = false }
            if g.state == .ended || g.state == .cancelled || g.state == .failed { v.isScrollEnabled = true; return }
            /* a fix every couple of metres, as GPS would give (distanceFilter 2) */
            if let l = lastDrag, fs_geo_distance(l.longitude, l.latitude, c.longitude, c.latitude) < 2, g.state == .changed { return }
            lastDrag = c
            walk?.walkBy(lon: c.longitude, lat: c.latitude)
        }
        /* the walker's dot while walking by hand */
        func show(_ c: CLLocationCoordinate2D?, on v: MLNMapView) {
            guard let c else { if let w = walker { v.removeAnnotation(w); walker = nil }; return }
            if walker == nil { let a = MLNPointAnnotation(); walker = a; a.coordinate = c; v.addAnnotation(a) }
            else { walker?.coordinate = c }
        }
        func mapView(_ v: MLNMapView, viewFor a: MLNAnnotation) -> MLNAnnotationView? {
            guard a === walker else { return nil }
            let view = MLNAnnotationView(reuseIdentifier: "walker")
            /* the walker is not a recording: ink, ringed, larger than any point, so it reads as "you" */
            view.frame = CGRect(x: 0, y: 0, width: 26, height: 26)
            view.layer.cornerRadius = 13
            view.backgroundColor = UIColor(T.ink)
            view.layer.borderColor = UIColor(T.sunk).cgColor
            view.layer.borderWidth = 4
            view.layer.shadowColor = UIColor(T.sunk).cgColor
            view.layer.shadowOpacity = 0.6
            view.layer.shadowRadius = 6
            view.layer.shadowOffset = .zero
            return view
        }
        func mapView(_ v: MLNMapView, didFinishLoading style: MLNStyle) {
            self.style = style
            if let m = map, let w = walk { apply(m, w, on: v) }
            guard let b = bounds, !centred else { return }
            v.setVisibleCoordinateBounds(b, edgePadding: UIEdgeInsets(top: 80, left: 40, bottom: 140, right: 40), animated: false, completionHandler: nil)
        }
        /* On a walk the map is about where you are: the first fix brings it to street level on you,
           once; after that it is yours to pan. */
        func mapView(_ v: MLNMapView, didUpdate u: MLNUserLocation?) {
            guard !centred, let c = u?.location?.coordinate, CLLocationCoordinate2DIsValid(c) else { return }
            centred = true
            v.setCenter(c, zoomLevel: 15.5, animated: true)
        }
    }

    /* The style, written once to a file: the web's sources and layers, features inline. */
    private func styleURL() -> URL {
        let style: [String: Any] = [
            "version": 8,
            "sources": [
                "base-sat": ["type": "raster", "tileSize": 256, "maxzoom": 19, "attribution": "Imagery © Esri",
                             "tiles": ["https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"]],
                /* the web's other base maps (index.html BASEMAPS): OpenStreetMap and OpenTopoMap */
                "base-osm": ["type": "raster", "tileSize": 256, "maxzoom": 19, "attribution": "© OpenStreetMap contributors",
                             "tiles": ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"]],
                "base-topo": ["type": "raster", "tileSize": 256, "maxzoom": 17, "attribution": "© OpenStreetMap contributors, SRTM · © OpenTopoMap (CC-BY-SA)",
                              "tiles": ["https://a.tile.opentopomap.org/{z}/{x}/{y}.png"]],
                "features": ["type": "geojson", "data": features],
                "parks": ["type": "geojson", "data": MapView.parks(features)],
                "zones": ["type": "geojson", "data": ["type": "FeatureCollection", "features": []]],
                "sections": ["type": "geojson", "data": ["type": "FeatureCollection", "features": []]],
                "progseg": ["type": "geojson", "data": ["type": "FeatureCollection", "features": []]]
            ],
            "layers": [
                ["id": "ground", "type": "background", "paint": ["background-color": Ink.sunk]],
                ["id": "base-osm", "type": "raster", "source": "base-osm", "layout": ["visibility": "none"]],
                ["id": "base-topo", "type": "raster", "source": "base-topo", "layout": ["visibility": "none"]],
                ["id": "base-sat", "type": "raster", "source": "base-sat"],
                ["id": "park-line", "type": "line", "source": "parks", "paint": ["line-color": Ink.ink, "line-opacity": 0.75, "line-width": 1.5]],
                /* the web's own sections and zones (index.html sector-* / zone-*): coloured, filled, the
                   one underfoot lit - Kerem asked for them "more distinguishable and present" */
                ["id": "sections-fill", "type": "fill", "source": "sections",
                 "paint": ["fill-color": ["get", "colour"], "fill-opacity": ["case", ["==", ["get", "active"], 1], 0.22, 0.10]]],
                ["id": "sections-line", "type": "line", "source": "sections",
                 "paint": ["line-color": ["get", "colour"], "line-opacity": 0.7, "line-width": ["case", ["==", ["get", "active"], 1], 2, 1]]],
                ["id": "zones-fill", "type": "fill", "source": "zones",
                 "paint": ["fill-color": ["case", ["==", ["get", "inside"], 1], Ink.lamp, Ink.accent],
                           "fill-opacity": ["case", ["==", ["get", "inside"], 1], 0.34, 0.16]]],
                ["id": "zones-line", "type": "line", "source": "zones",
                 "paint": ["line-color": ["case", ["==", ["get", "inside"], 1], Ink.lamp, Ink.accent],
                           "line-width": ["case", ["==", ["get", "inside"], 1], 2.4, 1.4], "line-opacity": 0.9]],
                ["id": "route-casing", "type": "line", "source": "features",
                 "filter": ["==", ["geometry-type"], "LineString"],
                 "paint": ["line-color": Ink.sunk, "line-opacity": 0.8, "line-width": 8],
                 "layout": ["line-cap": "round", "line-join": "round"]],
                ["id": "route-line", "type": "line", "source": "features",
                 "filter": ["==", ["geometry-type"], "LineString"],
                 "paint": ["line-color": "#ffffff", "line-width": 3],
                 "layout": ["line-cap": "round", "line-join": "round"]],
                /* the progression beside the route: sixteen chords, each its root's colour, the one
                   playing thick (index.html prog-seg / prog-active) */
                ["id": "prog-seg", "type": "line", "source": "progseg",
                 "paint": ["line-color": ["get", "colour"], "line-width": 5, "line-opacity": 0.85, "line-offset": 8],
                 "layout": ["line-cap": "butt", "line-join": "round"]],
                ["id": "prog-active", "type": "line", "source": "progseg", "filter": ["==", ["get", "active"], 1],
                 "paint": ["line-color": ["get", "colour"], "line-width": 11, "line-opacity": 1, "line-offset": 8],
                 "layout": ["line-cap": "butt", "line-join": "round"]],
                ["id": "point-halo", "type": "circle", "source": "features",
                 "filter": ["==", ["geometry-type"], "Point"],
                 "paint": ["circle-radius": ["interpolate", ["linear"], ["zoom"],
                                              10, ["case", ["has", "icon"], 8, 6], 13, ["case", ["has", "icon"], 17, 12]],
                           "circle-color": Ink.sunk, "circle-opacity": 0.22, "circle-blur": 0.7]],
                ["id": "point-dot", "type": "circle", "source": "features",
                 "filter": ["==", ["geometry-type"], "Point"],
                 "paint": ["circle-radius": ["interpolate", ["linear"], ["zoom"],
                                              10, ["case", ["has", "icon"], 5.5, 3.5], 13, ["case", ["has", "icon"], 11.5, 6.5]],
                           "circle-color": ["case", ["==", ["get", "has_audio"], true], Ink.lamp, Ink.sunk],
                           "circle-stroke-width": ["interpolate", ["linear"], ["zoom"], 10, 1.2, 13, 2.2],
                           "circle-stroke-color": ["case", ["==", ["get", "has_audio"], true], Ink.sunk, Ink.ink]]]
            ]
        ]
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("fieldscape-style.json")
        try? JSONSerialization.data(withJSONObject: style).write(to: url)
        return url
    }

    /* the park boundaries (places.geojson, bundled), only the parks something is published in (Kerem,
       2026-09-29): each point, and each route's start, lights the first park it falls in (placeAt) */
    static func parks(_ features: [String: Any]) -> Any {
        guard let u = Bundle.main.url(forResource: "places", withExtension: "geojson"), let d = try? Data(contentsOf: u),
              var j = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return ["type": "FeatureCollection", "features": []] }
        let all = (j["features"] as? [[String: Any]]) ?? []
        let rings = all.map { p in (((p["geometry"] as? [String: Any])?["coordinates"] as? [[[[Double]]]]) ?? []).compactMap { $0.first?.flatMap { $0 } } }
        var keep = Set<Int>()
        for f in (features["features"] as? [[String: Any]]) ?? [] {
            let c = (f["geometry"] as? [String: Any])?["coordinates"]
            guard let a = (c as? [Double]) ?? (c as? [[Double]])?.first, a.count >= 2 else { continue }
            if let i = rings.firstIndex(where: { rs in rs.contains { r in r.withUnsafeBufferPointer { fs_point_in_ring(a[0], a[1], $0.baseAddress, Int32(r.count / 2)) != 0 } } }) { keep.insert(i) }
        }
        j["features"] = keep.sorted().map { all[$0] }
        return j
    }
    /* a circle of r metres as a ring of 48 corners */
    static func circle(_ lon: Double, _ lat: Double, _ r: Double) -> [[Double]] {
        let dLat = r / 111_320, dLon = r / (111_320 * cos(lat * .pi / 180))
        return (0...48).map { i in let a = Double(i) / 48 * 2 * .pi; return [lon + dLon * cos(a), lat + dLat * sin(a)] }
    }
    /* rings with their properties -> an MLNShape of polygons */
    static func shape(_ rings: [([[Double]], [String: Any])]) -> MLNShape {
        let fc: [String: Any] = ["type": "FeatureCollection", "features": rings.map { r, props -> [String: Any] in
            let closed = r.first == r.last ? r : r + [r[0]]
            return ["type": "Feature", "properties": props, "geometry": ["type": "Polygon", "coordinates": [closed]]]
        }]
        let data = (try? JSONSerialization.data(withJSONObject: fc)) ?? Data()
        return (try? MLNShape(data: data, encoding: String.Encoding.utf8.rawValue)) ?? MLNShapeCollectionFeature(shapes: [])
    }

    /* the stretch of a line from t0 to t1 of its length (index.html segmentCoords) */
    static func segment(_ c: [[Double]], _ t0: Double, _ t1: Double) -> [[Double]] {
        var cum = [0.0]
        for i in 1..<max(c.count, 1) { cum.append(cum[i - 1] + fs_geo_distance(c[i - 1][0], c[i - 1][1], c[i][0], c[i][1])) }
        let total = cum.last ?? 0
        func at(_ d: Double) -> [Double] {
            guard total > 0, let i = cum.indices.dropFirst().first(where: { cum[$0] >= d }) else { return c.last ?? [0, 0] }
            let f = (d - cum[i - 1]) / max(cum[i] - cum[i - 1], 1e-9)
            return [c[i - 1][0] + (c[i][0] - c[i - 1][0]) * f, c[i - 1][1] + (c[i][1] - c[i - 1][1]) * f]
        }
        let d0 = t0 * total, d1 = t1 * total
        return [at(d0)] + c.indices.filter { cum[$0] > d0 && cum[$0] < d1 }.map { c[$0] } + [at(d1)]
    }

    /* Everything published, framed. */
    private func bounds() -> MLNCoordinateBounds? {
        var lons: [Double] = [], lats: [Double] = []
        func add(_ c: Any?) {
            if let p = c as? [Double], p.count >= 2 { lons.append(p[0]); lats.append(p[1]) }
            else if let a = c as? [Any] { a.forEach(add) }
        }
        for f in (features["features"] as? [[String: Any]]) ?? [] { add((f["geometry"] as? [String: Any])?["coordinates"]) }
        guard let x0 = lons.min(), let x1 = lons.max(), let y0 = lats.min(), let y1 = lats.max() else { return nil }
        return MLNCoordinateBounds(sw: CLLocationCoordinate2D(latitude: y0, longitude: x0), ne: CLLocationCoordinate2D(latitude: y1, longitude: x1))
    }
}
