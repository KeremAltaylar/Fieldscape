// The walk: GPS -> the core's place layer -> which points sound, how loud -> recordings fetched,
// cached, decoded into the Core's slots. The same rules as the web's open world, from the C++ the
// parity tests hold to index.html: points by their own radius, prox^1.5 x gain, the four nearest
// soundscape points (updateBed's order), placeAt for the park underfoot, and a fix worse than
// 40 m holds the walk where it was (A-15). It publishes what the walk screen shows.
import CoreLocation
import Foundation

final class Walk: NSObject, ObservableObject, CLLocationManagerDelegate {
    struct Point {
        let id: String, name: String, lon: Double, lat: Double
        let radius: Double, gain: Double, stretch: Float, windowSamples: Double, grit: Float, freeze: Float, onset: Float
        let brightest: Double          /* the low-pass ceiling: 2.2 x the recording's centroid, 600-14000 Hz */
        let path: String?, sounds: Bool
    }
    enum Phase: Equatable { case playing, downloading(fraction: Double, bytes: Int64), decoding }
    struct Row: Identifiable, Equatable { let id: String, name: String; let level: Double, dist: Double; let phase: Phase }
    /* what a point's card shows (its feature's properties) */
    struct PointInfo {
        let id, name, note, mode: String      /* mode: "stretch", "rhythm", "grains", or "silent" */
        let lon, lat: Double
        let peaks: [Double], duration: Double, recorded: String?
        let path: String?, sounds: Bool
        /* a rhythm point's four originals (low, mid, high, random), and the point's photos */
        let hits: [(slot: String, name: String, path: String)]
        let images: [String]                  /* storage paths */
    }
    /* what a route's card shows: its line, and its patch from the piece (fs_piece_route_info) */
    struct RouteInfo {
        let index: Int, name, note: String, metres: Double, coords: [[Double]]
        let tempo: Int, key: String, key2: String, sections: Int
        let prog: [(pc: Int, label: String)], sectors: [Int]
        var length: String { metres < 1000 ? String(format: "%.0f m", metres) : String(format: "%.1f km", metres / 1000) }
        var sw: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: coords.map { $0[1] }.min() ?? 0, longitude: coords.map { $0[0] }.min() ?? 0) }
        var ne: CLLocationCoordinate2D { CLLocationCoordinate2D(latitude: coords.map { $0[1] }.max() ?? 0, longitude: coords.map { $0[0] }.max() ?? 0) }
    }
    /* byHand: the walker placed on the map by a tap or a drag (the web's draggable walker), for
       listening to a place from anywhere; GPS fixes are ignored until "Use my location". */
    enum Mode: Equatable { case waiting, denied, live(accuracy: Double), holding(accuracy: Double), byHand }

    @Published var mode = Mode.waiting
    @Published var rows: [Row] = []
    @Published var place: String? = nil
    @Published var nearest: (name: String, dist: Double, direction: String)? = nil
    @Published var failure: String? = nil
    /* The route whose patch is playing, and the rhythm points sounding (the piece, RouteSound). */
    @Published var route: String? = nil
    @Published var rhythms: [String] = []
    /* where the walker is, for the map's walker dot */
    @Published var here: CLLocationCoordinate2D? = nil
    /* the chord the route is on, "chord 7 of 16, D#maj7#11" (from the piece, twice a second), and which
       route and step that is - the map lights that chord's stretch of the route */
    @Published var chord: String? = nil
    @Published var playingRoute = -1
    @Published var chordStep = -1
    /* the routes a setter published (the Routes sheet and the route cards), every point's card, and
       everything, framed */
    @Published var routeList: [RouteInfo] = []
    private(set) var pointInfo: [String: PointInfo] = [:]
    @Published var allBounds: (sw: CLLocationCoordinate2D, ne: CLLocationCoordinate2D)? = nil
    var zoneCircles: [(lon: Double, lat: Double, r: Double)] { sound.zoneCircles }
    func sectionCells() -> (cells: [[[Double]]], active: Int) { sound.sectionCells() }

    /* Listen (the point card): that point alone, at its full level from any distance; the other
       points and the route's sound rest until it is let go. Kerem: "it works as solo". */
    @Published var solo: String? = nil
    func listen(_ id: String?) {
        solo = id
        if let pos = lastPos { step(lon: pos.0, lat: pos.1, acc: 0, moved: false) }
        else if let id, let q = pointInfo[id] { step(lon: q.lon, lat: q.lat, acc: 0, moved: false) }
    }
    /* The card's player: a recording as it was made (a point's, or one of a rhythm point's four), keyed
       "id" or "id#slot". The walk rests while it plays. */
    @Published var rawId: String? = nil
    @Published var rawDuration = 0.0
    @Published var rawPlaying = false
    @Published var rawLoading = false
    @Published var rawPosition = 0.0                 /* seconds */
    private var rawTimer: Timer?
    func playRaw(_ id: String, path: String) {
        if rawId == id { if !rawLoading { setRaw(!rawPlaying) }; return }
        stopRaw()
        rawId = id; rawLoading = true
        let sr = core.sampleRate
        Task { @MainActor [weak self] in
            guard let self, let data = try? await self.recording(path, for: id),
                  let pcm = try? await Decode.pcm16(data, sampleRate: sr) else { self?.rawLoading = false; return }
            guard self.rawId == id else { pcm.free(); return }
            self.rawDuration = Double(pcm.frames) / sr
            self.core.loadRaw(pcm)
            self.rawLoading = false
            self.setRaw(true)
        }
    }
    func seekRaw(_ f: Double) { core.seekRaw(f) }
    func stopRaw() {
        setRaw(false)
        if rawId != nil { core.loadRaw(nil) }                  /* frees its memory */
        rawId = nil; rawPosition = 0; rawLoading = false
    }
    private func setRaw(_ on: Bool) {
        rawPlaying = on
        core.playRaw(on)
        rawTimer?.invalidate()
        if on {
            rawTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
                guard let self else { return }
                self.rawPosition = Double(self.core.rawFrames) / self.core.sampleRate
                if self.rawPosition >= self.rawDuration - 0.05 { self.setRaw(false); self.core.seekRaw(0) }
            }
        }
        if let pos = lastPos { step(lon: pos.0, lat: pos.1, acc: 0, moved: false) }
    }
    private var lastPos: (Double, Double)? = nil

    private let core: Core
    private let sound: RouteSound
    private let loc = CLLocationManager()
    private var points: [Point] = []
    private var slotOf: [String: Int] = [:]           /* point id -> slot */
    private var phase: [String: Phase] = [:] { didSet { refreshRows() } }   /* points still arriving */
    private var loaded: Set<String> = [] { didSet { refreshRows() } }
    private var releasedAt: [Int: Date] = [:]          /* slot -> when its point left (fade before reuse) */
    /* The level each point's distance earned at the last fix. Applied again the moment its recording
       is ready: standing still sends no fixes, and a point that finished loading must not wait for
       you to move before it sounds (rulebook A-15). */
    private var earned: [String: Float] = [:]
    private var parks: [(name: String, rings: [[Double]], area: Double)] = []
    private var placeCheckedAt: (Double, Double)? = nil
    private var parkIndex: Int? = nil
    private let log: URL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("walk.log")

    init(core: Core) {
        self.core = core
        self.sound = RouteSound(core: core)
        super.init()
        sound.walk = self
        loc.delegate = self
        loc.desiredAccuracy = kCLLocationAccuracyBest
        loc.distanceFilter = 2
        /* 5.8: keep walking with the screen locked (UIBackgroundModes audio + location); the blue
           status-bar pill tells the walker the app is still listening to where they are. */
        loc.allowsBackgroundLocationUpdates = true
        loc.pausesLocationUpdatesAutomatically = false
        loc.showsBackgroundLocationIndicator = true
        loc.activityType = .fitness
        loadParks()
    }

    func start(features: [String: Any]) {
        points = ((features["features"] as? [[String: Any]]) ?? []).compactMap { f in
            guard let g = f["geometry"] as? [String: Any], g["type"] as? String == "Point",
                  let c = g["coordinates"] as? [Double], c.count >= 2,
                  let p = f["properties"] as? [String: Any] else { return nil }
            let q = p["sound"] as? [String: Any] ?? [:], px = q["px"] as? [String: Any] ?? [:]
            let mode = p["audio_mode"] as? String
            let fft = (px["fft"] as? Double) ?? 0.7                          /* pxDefaults */
            return Point(id: p["id"] as? String ?? UUID().uuidString, name: p["name"] as? String ?? "Unnamed point",
                         lon: c[0], lat: c[1], radius: (q["radius"] as? Double) ?? 140, gain: (q["gain"] as? Double) ?? 0.9,
                         stretch: Float((q["stretch"] as? Double) ?? 0),
                         windowSamples: pow(2, (7 + 10 * max(0, min(1, fft))).rounded()),   /* pxBufsize */
                         grit: Float((q["grit"] as? Double) ?? 0),
                         freeze: (px["freeze"] as? Bool ?? false) ? 1 : 0, onset: Float((px["onset"] as? Double) ?? 0),
                         brightest: max(600, min(14000, (((p["audio"] as? [String: Any])?["centroid_hz"] as? Double) ?? 2000) * 2.2)),
                         path: p["storage_path"] as? String,
                         sounds: (p["has_audio"] as? Bool ?? false) && mode != "hits" && mode != "grains")
        }
        sound.start(features: (features["features"] as? [[String: Any]]) ?? [])

        let fs = (features["features"] as? [[String: Any]]) ?? []
        for f in fs {
            guard let g = f["geometry"] as? [String: Any], g["type"] as? String == "Point", let c = g["coordinates"] as? [Double], c.count >= 2,
                  let p = f["properties"] as? [String: Any], let id = p["id"] as? String else { continue }
            let a = p["audio"] as? [String: Any] ?? [:]
            let m = p["audio_mode"] as? String, has = p["has_audio"] as? Bool ?? false
            let mode = m == "hits" ? "rhythm" : m == "grains" ? "grains" : has ? "stretch" : "silent"
            let hits = p["hits"] as? [String: Any] ?? [:]
            let slots: [(slot: String, name: String, path: String)] = ["low", "mid", "high", "rand"].compactMap { k in
                guard let h = hits[k] as? [String: Any], let sp = h["storage_path"] as? String else { return nil }
                return (k, h["name"] as? String ?? k, sp)
            }
            let imgs = ((p["images"] as? [[String: Any]]) ?? []).compactMap { i -> String? in
                (i["storage_path"] as? String) ?? (i["key"] as? String).map { "\(id)/images/\($0).jpg" }
            }
            pointInfo[id] = PointInfo(id: id, name: p["name"] as? String ?? "Unnamed point", note: p["note"] as? String ?? "", mode: mode,
                                      lon: c[0], lat: c[1], peaks: (a["peaks"] as? [Double]) ?? [], duration: (a["duration_s"] as? Double) ?? 0,
                                      recorded: (a["recorded_at"] as? String) ?? (p["created_at"] as? String),
                                      path: has ? p["storage_path"] as? String : nil, sounds: mode == "stretch", hits: slots, images: imgs)
        }
        let notes = Dictionary(fs.compactMap { f -> (String, String)? in
            guard let p = f["properties"] as? [String: Any], p["kind"] as? String == "route" else { return nil }
            return (p["name"] as? String ?? "", p["note"] as? String ?? "")
        }, uniquingKeysWith: { a, _ in a })
        routeList = sound.routeLines.enumerated().map { i, r in
            var buf = [CChar](repeating: 0, count: 4096)
            _ = fs_piece_route_info(core.piece, Int32(i), &buf, 4096)
            let j = (try? JSONSerialization.jsonObject(with: Data(String(cString: buf).utf8)) as? [String: Any]) ?? [:]
            let prog = ((j["prog"] as? [[Any]]) ?? []).map { (pc: ($0.first as? Int) ?? 0, label: ($0.last as? String) ?? "") }
            return RouteInfo(index: i, name: r.name, note: notes[r.name] ?? "", metres: r.metres, coords: r.coords,
                             tempo: j["tempo"] as? Int ?? 72, key: j["key"] as? String ?? "", key2: j["key2"] as? String ?? "",
                             sections: j["sections"] as? Int ?? 0, prog: prog, sectors: (j["sectors"] as? [Int]) ?? [])
        }
        var all: [[Double]] = points.map { [$0.lon, $0.lat] }
        for r in sound.routeLines { all += r.coords }
        if let x0 = all.map({ $0[0] }).min(), let x1 = all.map({ $0[0] }).max(), let y0 = all.map({ $0[1] }).min(), let y1 = all.map({ $0[1] }).max() {
            allBounds = (CLLocationCoordinate2D(latitude: y0, longitude: x0), CLLocationCoordinate2D(latitude: y1, longitude: x1))
        }
        Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.refreshChord() }
        loc.requestWhenInUseAuthorization()
        loc.startUpdatingLocation()
    }

    func locationManager(_ m: CLLocationManager, didChangeAuthorization s: CLAuthorizationStatus) {
        if s == .denied || s == .restricted { mode = .denied }
        else if mode == .denied { mode = .waiting; loc.startUpdatingLocation() }
    }

    func locationManager(_ m: CLLocationManager, didUpdateLocations locs: [CLLocation]) {
        guard let l = locs.last, l.horizontalAccuracy >= 0, mode != .byHand else { return }
        if l.horizontalAccuracy > Double(FS_GPS_ACC_MAX) { mode = .holding(accuracy: l.horizontalAccuracy); return }
        mode = .live(accuracy: l.horizontalAccuracy)
        step(lon: l.coordinate.longitude, lat: l.coordinate.latitude, acc: l.horizontalAccuracy)
    }

    /* The walker put down on the map by hand. */
    func walkBy(lon: Double, lat: Double) {
        mode = .byHand
        step(lon: lon, lat: lat, acc: 0)
    }
    /* where the walker is now, for the card's distance */
    func distance(to id: String) -> Double? {
        guard let h = here, let q = pointInfo[id] else { return nil }
        return fs_geo_distance(h.longitude, h.latitude, q.lon, q.lat)
    }
    /* Back to the phone's own position. */
    func useLocation() {
        mode = .waiting
        loc.stopUpdatingLocation(); loc.startUpdatingLocation()
    }

    private func refreshChord() {
        var count: Int32 = 0
        var buf = [CChar](repeating: 0, count: 32)
        let i = fs_piece_chord(core.piece, &count, &buf, 32)
        let c = (i >= 0 && route != nil) ? "chord \(i + 1) of \(count), " + String(cString: buf) : nil
        if c != chord { chord = c }
        let r = route != nil ? Int(fs_piece_route(core.piece)) : -1, k = r >= 0 ? Int(i) : -1
        if r != playingRoute { playingRoute = r }
        if k != chordStep { chordStep = k }
    }

    /* One position through the place layer. */
    func step(lon: Double, lat: Double, acc: Double, moved: Bool = true) {
        if moved { here = CLLocationCoordinate2D(latitude: lat, longitude: lon); lastPos = (lon, lat) }
        let n = points.count
        var dist = points.map { fs_geo_distance(lon, lat, $0.lon, $0.lat) }
        var radius = points.map { $0.radius }
        var eligible = points.map { UInt8($0.sounds ? 1 : 0) }
        var picked = [Int32](repeating: 0, count: max(n, 1))
        /* the playing patch's bed: bed.on and how many voices (BED.maxVoices) */
        let voices = min(Core.slots - (core.test ? 1 : 0), sound.bedVoices)
        let k = Int(fs_pick_voices(&dist, &radius, &eligible, Int32(n), Int32(voices), 0, &picked))
        /* Listen: a stretch point here, alone; a rhythm or grains point is soloed inside the piece (RouteSound) */
        let soloAny = solo.flatMap { id in points.firstIndex { $0.id == id } }
        let soloIndex = soloAny.flatMap { points[$0].sounds ? $0 : nil }
        let soloBeat = soloAny != nil && soloIndex == nil
        let chosen = soloIndex.map { [$0] } ?? picked.prefix(k).map { Int($0) }
        /* the card's player rests the walk; Listen rests everything but its point */
        let rest = rawPlaying || soloBeat
        let want = Set(chosen.map { points[$0].id })

        for (id, slot) in slotOf where !want.contains(id) {                /* left: fade, free after the ramp */
            core.gain(slot: slot, 0)
            slotOf[id] = nil; loaded.remove(id); phase[id] = nil; earned[id] = nil; releasedAt[slot] = Date()
        }
        for j in chosen {
            let p = points[j]
            if slotOf[p.id] == nil, let slot = freeSlot() { slotOf[p.id] = slot; begin(p, slot: slot) }
            guard let slot = slotOf[p.id] else { continue }
            let full = soloIndex == j
            earned[p.id] = rest ? 0 : Float(full ? p.gain : fs_point_gain(dist[j], p.radius, p.gain))
            core.gain(slot: slot, loaded.contains(p.id) ? earned[p.id]! : 0)
            /* ensureVoice: the filter opens with proximity, from 300 Hz to the recording's own ceiling */
            core.lowpass(slot: slot, Float(full ? p.brightest : 300 + (p.brightest - 300) * fs_point_proximity(dist[j], p.radius)))
        }
        core.gain(slot: Core.pieceSlot, rawPlaying || soloIndex != nil ? 0 : 1)
        /* the panel: the two nearest points of any kind, each opening its card */
        rows = (0..<n).sorted { dist[$0] < dist[$1] }.prefix(2).map { j in
            let p = points[j]
            return Row(id: p.id, name: p.name, level: soloAny == j ? 1 : soloAny != nil ? 0 : fs_point_gain(dist[j], p.radius, 1), dist: dist[j],
                       phase: slotOf[p.id] == nil || loaded.contains(p.id) ? .playing : (phase[p.id] ?? .decoding))
        }
        if chosen.isEmpty, let j = (0..<n).filter({ points[$0].sounds }).min(by: { dist[$0] < dist[$1] }) {
            nearest = (points[j].name, dist[j], Walk.direction(lon, lat, points[j].lon, points[j].lat))
        } else { nearest = nil }
        updatePlace(lon: lon, lat: lat)
        sound.step(lon: lon, lat: lat, solo: soloBeat ? solo : nil)
        route = sound.routeName
        rhythms = sound.rhythmNames
        append(String(format: "%.6f %.6f %.0f out %.1f dBFS | %@", lon, lat, acc, core.outputDb,
                      rows.map { String(format: "%@ %.0f%%", $0.name, $0.level * 100) }.joined(separator: ", ")))
    }

    /* Progress between fixes: a download moves while the walker stands still. */
    private func refreshRows() {
        rows = rows.map { r in Row(id: r.id, name: r.name, level: r.level, dist: r.dist,
                                   phase: slotOf[r.id] == nil || loaded.contains(r.id) ? .playing : (phase[r.id] ?? .decoding)) }
    }

    /* The park underfoot (placeAt), re-asked only after a few metres (the web's PLACE_CHECK_M). */
    private func updatePlace(lon: Double, lat: Double) {
        if let c = placeCheckedAt, fs_geo_distance(c.0, c.1, lon, lat) < 5 { return }
        placeCheckedAt = (lon, lat)
        let i = parks.firstIndex { park in park.rings.contains { r in r.withUnsafeBufferPointer { fs_point_in_ring(lon, lat, $0.baseAddress, Int32(r.count / 2)) != 0 } } }
        place = i.map { parks[$0].name }
        if i != parkIndex { parkIndex = i; sound.placeChanged(rings: i.map { parks[$0].rings }, area: i.map { parks[$0].area } ?? 0) }
    }

    private func loadParks() {
        guard let url = Bundle.main.url(forResource: "places", withExtension: "geojson"),
              let data = try? Data(contentsOf: url),
              let fc = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }
        for f in (fc["features"] as? [[String: Any]]) ?? [] {
            guard let g = f["geometry"] as? [String: Any], let polys = g["coordinates"] as? [[[[Double]]]] else { continue }
            let rings = polys.compactMap { $0.first?.flatMap { $0 } }
            let p = f["properties"] as? [String: Any]
            parks.append((p?["name"] as? String ?? "", rings, (p?["area_km2"] as? Double) ?? 0))
        }
    }

    static func direction(_ lon1: Double, _ lat1: Double, _ lon2: Double, _ lat2: Double) -> String {
        let r = Double.pi / 180
        let y = sin((lon2 - lon1) * r) * cos(lat2 * r)
        let x = cos(lat1 * r) * sin(lat2 * r) - sin(lat1 * r) * cos(lat2 * r) * cos((lon2 - lon1) * r)
        let deg = (atan2(y, x) / r + 360).truncatingRemainder(dividingBy: 360)
        return ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"][Int((deg + 22.5) / 45) % 8]
    }

    private func freeSlot() -> Int? {
        let used = Set(slotOf.values)
        return (0..<Core.slots).first { s in
            !used.contains(s) && !(core.test && s == 0) && (releasedAt[s].map { Date().timeIntervalSince($0) > 1.2 } ?? true)
        }
    }

    /* The point's own settings, then its recording: cache, else download, decode, hand over. */
    private func begin(_ p: Point, slot: Int) {
        core.param(slot: slot, 0, p.stretch)
        core.param(slot: slot, 1, Float(p.windowSamples / core.sampleRate))
        core.grit(slot: slot, p.grit)
        core.param(slot: slot, 2, p.freeze); core.param(slot: slot, 3, p.onset)   /* px.freeze, px.onset */
        guard let path = p.path else { return }
        let sr = core.sampleRate
        Task { @MainActor [weak self] in
            do {
                guard let data = try await self?.recording(path, for: p.id) else { return }
                self?.phase[p.id] = .decoding
                /* 16-bit, one decode at a time, at most 10 minutes (Decode.capSeconds): four voices
                   stay under ~460 MB on a 2 GB iPhone 8 */
                let decoded = try await Decode.pcm16(data, sampleRate: sr)
                guard let self, self.slotOf[p.id] == slot else { decoded.free(); return }
                self.core.load(slot: slot, pcm: decoded)
                self.loaded.insert(p.id)
                self.core.gain(slot: slot, self.earned[p.id] ?? 0)
                self.phase[p.id] = nil
            } catch {
                self?.failure = "Could not load \(p.name): \(error.localizedDescription)"
                self?.phase[p.id] = nil
            }
        }
    }

    /* Downloaded once, kept in Caches (the web keeps them in IndexedDB the same way); progress
       is reported so the screen can say what is arriving. */
    @MainActor func recording(_ path: String, for id: String) async throws -> Data {
        let dir = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0].appendingPathComponent("recordings")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let file = dir.appendingPathComponent(path.replacingOccurrences(of: "/", with: "__"))
        if let d = try? Data(contentsOf: file) { return d }
        var req = URLRequest(url: URL(string: Supa.url + "/storage/v1/object/authenticated/recordings/" + path)!)
        req.setValue(Supa.anon, forHTTPHeaderField: "apikey")
        req.setValue("Bearer " + Supa.anon, forHTTPHeaderField: "Authorization")
        phase[id] = .downloading(fraction: 0, bytes: 0)
        return try await withCheckedThrowingContinuation { cont in
            var watch: NSKeyValueObservation?
            let task = URLSession.shared.downloadTask(with: req) { tmp, resp, err in
                watch?.invalidate()
                guard let tmp, (resp as? HTTPURLResponse)?.statusCode == 200 else {
                    cont.resume(throwing: err ?? URLError(.badServerResponse)); return
                }
                do { try? FileManager.default.removeItem(at: file); try FileManager.default.moveItem(at: tmp, to: file)
                     cont.resume(returning: try Data(contentsOf: file)) } catch { cont.resume(throwing: error) }
            }
            watch = task.progress.observe(\.fractionCompleted) { [weak self, weak task] pr, _ in
                let bytes = task?.countOfBytesExpectedToReceive ?? 0
                DispatchQueue.main.async { self?.phase[id] = .downloading(fraction: pr.fractionCompleted, bytes: bytes) }
            }
            task.resume()
        }
    }

    private func append(_ line: String) {
        let s = ISO8601DateFormatter().string(from: Date()) + " " + line + "\n"
        if let h = try? FileHandle(forWritingTo: log) { h.seekToEndOfFile(); h.write(s.data(using: .utf8)!); try? h.close() }
        else { try? s.data(using: .utf8)!.write(to: log) }
    }
}
