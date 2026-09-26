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
    static let sunk = "#0d1310", ink = "#e3e7e4", lamp = "#bae6b1"
}

struct MapView: UIViewRepresentable {
    let features: [String: Any]
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState

    func makeUIView(context: Context) -> MLNMapView {
        let v = MLNMapView(frame: .zero, styleURL: styleURL())
        v.logoView.isHidden = true                 /* the attribution stays: a condition of the tiles */
        v.attributionButtonPosition = .topLeft      /* above the walk panel, never under it */
        v.compassViewPosition = .topRight
        v.attributionButton.tintColor = UIColor(T.faint)
        v.delegate = context.coordinator
        v.showsUserLocation = true
        /* walking by hand: a tap puts the walker there, a press-and-drag walks it (the web's
           draggable walker) - so a place can be heard from anywhere, and a point or the route
           line is reached by tapping it */
        /* MapLibre has its own tap and press recognisers on the view, which won over these (Kerem's
           iPhone: taps did nothing): the delegate lets ours recognise alongside them. */
        let tap = UITapGestureRecognizer(target: context.coordinator, action: #selector(Frame.tapped(_:)))
        tap.delegate = context.coordinator
        v.gestureRecognizers?.filter { ($0 as? UITapGestureRecognizer)?.numberOfTapsRequired == 2 }.forEach { tap.require(toFail: $0) }
        v.addGestureRecognizer(tap)
        let drag = UILongPressGestureRecognizer(target: context.coordinator, action: #selector(Frame.dragged(_:)))
        drag.minimumPressDuration = 0.25
        drag.delegate = context.coordinator
        v.addGestureRecognizer(drag)
        return v
    }

    func updateUIView(_ v: MLNMapView, context: Context) {
        context.coordinator.walk = walk
        context.coordinator.map = map
        context.coordinator.apply(map, walk, on: v)
        context.coordinator.show(walk.mode == .byHand ? walk.here : nil, on: v)
        if let g = walk.goTo, context.coordinator.wentTo.map({ $0.latitude != g.latitude || $0.longitude != g.longitude }) ?? true {
            context.coordinator.wentTo = g
            v.setCenter(g, zoomLevel: 16.5, animated: true)
        }
    }

    func makeCoordinator() -> Frame { let f = Frame(bounds: bounds()); f.walk = walk; return f }

    /* Framing needs the view's real size, which it only has once the style has loaded. */
    final class Frame: NSObject, MLNMapViewDelegate, UIGestureRecognizerDelegate {
        func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith o: UIGestureRecognizer) -> Bool { true }
        let bounds: MLNCoordinateBounds?
        weak var walk: Walk?
        weak var map: MapState?
        var wentTo: CLLocationCoordinate2D?
        private var style: MLNStyle?
        private var framed: UUID?
        private var zonesDone = false
        private var sectionsKey = ""

        /* The Layers sheet's choices, the Places sheet's framing, and the zone / section overlays. */
        func apply(_ m: MapState, _ w: Walk, on v: MLNMapView) {
            if let f = m.frame, f.id != framed {
                framed = f.id
                v.setVisibleCoordinateBounds(MLNCoordinateBounds(sw: f.sw, ne: f.ne), edgePadding: UIEdgeInsets(top: 120, left: 40, bottom: 320, right: 40), animated: true, completionHandler: nil)
            }
            guard let s = style else { return }
            for (id, base) in [("base-osm", MapState.Base.map), ("base-topo", .topo), ("base-sat", .satellite)] { s.layer(withIdentifier: id)?.isVisible = m.base == base }
            s.layer(withIdentifier: "park-line")?.isVisible = m.boundary
            s.layer(withIdentifier: "zones-line")?.isVisible = m.zones
            s.layer(withIdentifier: "sections-fill")?.isVisible = m.sections
            s.layer(withIdentifier: "sections-line")?.isVisible = m.sections
            if m.zones && !zonesDone, let src = s.source(withIdentifier: "zones") as? MLNShapeSource {
                zonesDone = true
                src.shape = MapView.shape(w.zoneCircles.map { MapView.circle($0.lon, $0.lat, $0.r) }, active: -1)
            }
            if m.sections, let src = s.source(withIdentifier: "sections") as? MLNShapeSource {
                let (cells, active) = w.sectionCells()
                let key = "\(cells.count) \(active) \(cells.first?.first ?? [])"
                if key != sectionsKey { sectionsKey = key; src.shape = MapView.shape(cells, active: active) }
            }
        }
        private var walker: MLNPointAnnotation?
        private var lastDrag: CLLocationCoordinate2D?
        init(bounds: MLNCoordinateBounds?) { self.bounds = bounds }
        private var centred = false

        @objc func tapped(_ g: UITapGestureRecognizer) {
            guard let v = g.view as? MLNMapView else { return }
            let c = v.convert(g.location(in: v), toCoordinateFrom: v)
            walk?.walkBy(lon: c.longitude, lat: c.latitude)
        }
        @objc func dragged(_ g: UILongPressGestureRecognizer) {
            guard let v = g.view as? MLNMapView else { return }
            let c = v.convert(g.location(in: v), toCoordinateFrom: v)
            /* while the walker is dragged the map holds still */
            if g.state == .began { lastDrag = nil; v.isScrollEnabled = false }
            if g.state == .ended || g.state == .cancelled || g.state == .failed { v.isScrollEnabled = true }
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
            view.frame = CGRect(x: 0, y: 0, width: 22, height: 22)
            view.layer.cornerRadius = 11
            view.backgroundColor = UIColor(T.lamp)
            view.layer.borderColor = UIColor(T.sunk).cgColor
            view.layer.borderWidth = 3
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
                "parks": ["type": "geojson", "data": MapView.parks()],
                "zones": ["type": "geojson", "data": ["type": "FeatureCollection", "features": []]],
                "sections": ["type": "geojson", "data": ["type": "FeatureCollection", "features": []]]
            ],
            "layers": [
                ["id": "ground", "type": "background", "paint": ["background-color": Ink.sunk]],
                ["id": "base-osm", "type": "raster", "source": "base-osm", "layout": ["visibility": "none"]],
                ["id": "base-topo", "type": "raster", "source": "base-topo", "layout": ["visibility": "none"]],
                ["id": "base-sat", "type": "raster", "source": "base-sat"],
                ["id": "park-line", "type": "line", "source": "parks", "paint": ["line-color": Ink.ink, "line-opacity": 0.75, "line-width": 1.5]],
                ["id": "sections-fill", "type": "fill", "source": "sections", "layout": ["visibility": "none"],
                 "paint": ["fill-color": Ink.lamp, "fill-opacity": ["case", ["==", ["get", "active"], 1], 0.14, 0.03]]],
                ["id": "sections-line", "type": "line", "source": "sections", "layout": ["visibility": "none"],
                 "paint": ["line-color": Ink.lamp, "line-opacity": 0.5, "line-width": 1, "line-dasharray": [3, 3]]],
                ["id": "zones-line", "type": "line", "source": "zones", "layout": ["visibility": "none"],
                 "paint": ["line-color": Ink.lamp, "line-opacity": 0.85, "line-width": 1.6, "line-dasharray": [2, 2]]],
                ["id": "route-casing", "type": "line", "source": "features",
                 "filter": ["==", ["geometry-type"], "LineString"],
                 "paint": ["line-color": Ink.sunk, "line-opacity": 0.8, "line-width": 8],
                 "layout": ["line-cap": "round", "line-join": "round"]],
                ["id": "route-line", "type": "line", "source": "features",
                 "filter": ["==", ["geometry-type"], "LineString"],
                 "paint": ["line-color": "#ffffff", "line-width": 3],
                 "layout": ["line-cap": "round", "line-join": "round"]],
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

    /* the park boundaries (places.geojson, bundled) */
    static func parks() -> Any {
        guard let u = Bundle.main.url(forResource: "places", withExtension: "geojson"), let d = try? Data(contentsOf: u),
              let j = try? JSONSerialization.jsonObject(with: d) else { return ["type": "FeatureCollection", "features": []] }
        return j
    }
    /* a circle of r metres as a ring of 48 corners */
    static func circle(_ lon: Double, _ lat: Double, _ r: Double) -> [[Double]] {
        let dLat = r / 111_320, dLon = r / (111_320 * cos(lat * .pi / 180))
        return (0...48).map { i in let a = Double(i) / 48 * 2 * .pi; return [lon + dLon * cos(a), lat + dLat * sin(a)] }
    }
    /* rings -> an MLNShape (polygons, the active one marked) */
    static func shape(_ rings: [[[Double]]], active: Int) -> MLNShape {
        let fc: [String: Any] = ["type": "FeatureCollection", "features": rings.enumerated().map { i, r -> [String: Any] in
            let closed = r.first == r.last ? r : r + [r[0]]
            return ["type": "Feature", "properties": ["active": i == active ? 1 : 0], "geometry": ["type": "Polygon", "coordinates": [closed]]]
        }]
        let data = (try? JSONSerialization.data(withJSONObject: fc)) ?? Data()
        return (try? MLNShape(data: data, encoding: String.Encoding.utf8.rawValue)) ?? MLNShapeCollectionFeature(shapes: [])
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
