// The piece's side of the walk (core/piece.cpp): which route and how far along it, the section
// underfoot, the plain points' zones, the character of the recordings in reach, and the rhythm
// points. index.html's worldMove, sectorUpdate, pacerCheckZones, localCharacter and updateRhythms,
// over the core's place functions; the piece makes every sound choice itself.
import Foundation

final class RouteSound {
    private struct Spot {
        let icon: String, lon: Double, lat: Double, radius: Double, zoneR: Double
        let centroid: Double, onsets: Double, audio: Bool, plain: Bool, id: String
        var zone = fs_zone_state()
        var compIn = false
    }
    private struct Beat { let id: String, name: String, lon: Double, lat: Double, radius: Double, gain: Double, grains: Bool, json: String, paths: [String?], tracks: [String?] }
    /* 6b: each route's sampler voices - a voice's sample and its analysis (patch.<voice>.sample), nil none */
    private var routeRoles: [[(path: String, ana: String?)?]] = []
    /* the route whose samples were asked for, its generation (a route change), voices holding a sample - under vlock: a sample
       is prepared off the main thread (final review I3) */
    private var played = -2, gen = 0, roleOn = [false, false, false]
    private let vlock = NSLock()
    private var voiceTimer: Timer?

    private let core: Core
    private var piece: OpaquePointer { core.piece }
    private var routes: [OpaquePointer?] = []
    private var routeNames: [String] = []
    private var spots: [Spot] = []
    private var zonesSent = ""                          /* the inside-set last given to the piece */
    private var beats: [Beat] = []
    private var handle: [String: Int32] = [:]          /* rhythm point id -> the piece's handle */
    private var sections: OpaquePointer? = nil
    private let t0 = Date()
    /* what the walk panel shows */
    private(set) var routeName: String? = nil
    /* every route's line (lon, lat pairs) and length, for the Places list and framing */
    private(set) var routeLines: [(name: String, coords: [[Double]], metres: Double)] = []
    /* every point's zone (the zoneR circle), for the Zones layer */
    var zoneCircles: [(lon: Double, lat: Double, r: Double)] { spots.map { ($0.lon, $0.lat, $0.zoneR) } }
    /* the sections of the park underfoot and the one the walker is in, for the Sections layer */
    func sectionCells() -> (cells: [[[Double]]], active: Int) {
        guard let s = sections else { return ([], -1) }
        var out: [[[Double]]] = []
        for i in 0..<Int(fs_sections_count(s)) {
            var buf = [Double](repeating: 0, count: 512)
            let k = Int(fs_sections_cell(s, Int32(i), &buf, 256))
            if k >= 3 { out.append((0..<k).map { [buf[2 * $0], buf[2 * $0 + 1]] }) }
        }
        return (out, Int(fs_piece_sector_now(piece)))
    }
    private(set) var rhythmNames: [String] = []
    private var soloHandle: Int32 = -1

    init(core: Core) {
        self.core = core
        /* 6b: the route the piece plays is taken on the audio thread, a moment after the walk names it - so asked on a timer,
           not only on a fix (a walker standing still gets no new fix) */
        voiceTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.voices() }
    }
    deinit { voiceTimer?.invalidate(); routes.forEach { fs_route_destroy($0) }; if let s = sections { fs_sections_destroy(s) } }

    /* 6b: the playing route changed - its sampler voices' samples (the web's routeSamples): the last route's voices silenced
       first, so its sample never plays on this one; each sample prepared as the website does (fs_piece_role_sample) */
    private func voices() {
        let pr = Int(fs_piece_route(piece))
        vlock.lock()
        guard pr != played else { vlock.unlock(); return }
        played = pr; gen += 1
        for q in 0..<3 where roleOn[q] { silence(q) }
        let g = gen
        vlock.unlock()
        guard pr >= 0, pr < routeRoles.count else { return }
        for (q, spec) in routeRoles[pr].enumerated() { if let spec { loadVoice(q, g, spec) } }
    }
    /* one voice's sample; a failed fetch asked again in 10 s while its route still plays (final review I4) - signal comes and
       goes on a walk */
    private func loadVoice(_ q: Int, _ g: Int, _ spec: (path: String, ana: String?)) {
        let sr = core.sampleRate, piece = self.piece
        let retry = { [weak self] in DispatchQueue.main.asyncAfter(deadline: .now() + 10) { if let self, self.gen == g { self.loadVoice(q, g, spec) } } }
            walk?.loadStarted()
            Task { @MainActor [weak self] in
                defer { self?.walk?.loadEnded() }     /* Test 16 */
                guard let walk = self?.walk, let data = try? await walk.recording(spec.path, for: "route#\(q)") else { retry(); return }
                var anaData: Data? = nil
                if let a = spec.ana { anaData = try? await walk.recording(a, for: "route#\(q)#analysis"); if anaData == nil { retry(); return } }
                guard let pcm = try? await Decode.pcm16(data, sampleRate: sr) else { retry(); return }
                defer { pcm.free() }
                guard let self, self.gen == g, pcm.frames > 0 else { return }
                let n = pcm.frames, c = min(pcm.planar.count, 2), planar = pcm.planar
                let js = anaData.map { String(decoding: $0, as: UTF8.self) } ?? "{}"
                /* prepared off the main thread; handed over only if the route has not changed since (its generation) */
                await Task.detached { [weak self] in
                    guard let self else { return }
                    let f = (0..<c).map { k -> UnsafeMutablePointer<Float> in
                        let p = UnsafeMutablePointer<Float>.allocate(capacity: n); for i in 0..<n { p[i] = Float(planar[k][i]) / 32768 }; return p }
                    defer { f.forEach { $0.deallocate() } }
                    var ptrs: [UnsafePointer<Float>?] = f.map { UnsafePointer($0) }
                    let t0 = Date()
                    self.vlock.lock(); defer { self.vlock.unlock() }
                    guard self.gen == g else { return }
                    fs_piece_role_sample(piece, Int32(q), Int32(c), Int64(n), &ptrs, js)
                    self.roleOn[q] = true
                    NSLog("6b voice %d sample: generation %d, %d frames, prepared off the main thread in %.0f ms", q, g, n, Date().timeIntervalSince(t0) * 1000)
                }.value
            }
    }
    private func silence(_ q: Int) {
        var z = [Float](repeating: 0, count: 128)
        z.withUnsafeMutableBufferPointer { b in var ptrs: [UnsafePointer<Float>?] = [UnsafePointer(b.baseAddress!)]; fs_piece_role_sample(piece, Int32(q), 1, 128, &ptrs, nil) }
        roleOn[q] = false
    }

    func start(features: [[String: Any]]) {
        for f in features {
            guard let g = f["geometry"] as? [String: Any], let p = f["properties"] as? [String: Any] else { continue }
            if g["type"] as? String == "LineString", p["kind"] as? String == "route",
               let c = g["coordinates"] as? [[Double]], !c.isEmpty {
                var flat = c.flatMap { [$0[0], $0[1]] }
                guard let r = fs_route_create(&flat, Int32(c.count)), fs_route_length(r) > 0 else { continue }
                routes.append(r)
                routeNames.append(p["name"] as? String ?? "Route")
                routeLines.append((p["name"] as? String ?? "Route", c, fs_route_length(r)))
                _ = fs_piece_add_route(piece, RouteSound.json(p["patch"]))
                let patch = p["patch"] as? [String: Any] ?? [:]
                routeRoles.append(["voice", "sect", "v3"].map { k in
                    guard let v = patch[k] as? [String: Any], (v["synth"] as? String ?? "").hasPrefix("s-"),
                          let sm = v["sample"] as? [String: Any], let path = sm["path"] as? String, !path.isEmpty else { return nil }
                    return (path, sm["analysis_path"] as? String)
                })
            } else if g["type"] as? String == "Point", let c = g["coordinates"] as? [Double], c.count >= 2 {
                let q = p["sound"] as? [String: Any] ?? [:], a = p["audio"] as? [String: Any] ?? [:]
                let mode = p["audio_mode"] as? String
                let hits = p["hits"] as? [String: Any] ?? [:]
                let slots = ["low", "mid", "high", "rand"].map { ((hits[$0] as? [String: Any])?["storage_path"] as? String) }
                let hitTracks = ["low", "mid", "high", "rand"].map { ((hits[$0] as? [String: Any])?["track_path"] as? String) }   /* 6b */
                let hasHits = slots.contains { $0 != nil }
                let hasAudio = p["has_audio"] as? Bool ?? false
                let radius = (q["radius"] as? Double) ?? 140, gain = (q["gain"] as? Double) ?? 0.9
                spots.append(Spot(icon: p["icon"] as? String ?? "", lon: c[0], lat: c[1], radius: radius, zoneR: (q["zoneR"] as? Double) ?? 25,
                                  centroid: (a["centroid_hz"] as? Double) ?? 0, onsets: (a["onset_rate"] as? Double) ?? -1,
                                  audio: hasAudio, plain: !hasAudio && !hasHits && mode != "hits" && mode != "grains",
                                  id: p["id"] as? String ?? ""))
                let id = p["id"] as? String ?? UUID().uuidString, name = p["name"] as? String ?? "Unnamed point"
                if mode == "hits" && hasHits {
                    beats.append(Beat(id: id, name: name, lon: c[0], lat: c[1], radius: radius, gain: gain, grains: false, json: RouteSound.json(p["rhythm"]), paths: slots, tracks: hitTracks))
                } else if mode == "grains" && hasAudio {
                    beats.append(Beat(id: id, name: name, lon: c[0], lat: c[1], radius: radius, gain: gain, grains: true, json: RouteSound.json(p["rhythm"]), paths: [p["storage_path"] as? String], tracks: [q["track_path"] as? String]))
                }
            }
        }
    }

    /* How many soundscape points the playing patch lets sound (bed.on / bed.voices). */
    var bedVoices: Int { Int(fs_piece_bed_voices(piece)) }

    /* The park underfoot changed: its sections, with the playing patch's count (sectorGeometry). */
    func placeChanged(rings: [[Double]]?, area: Double) {
        if let s = sections { fs_sections_destroy(s); sections = nil }
        if let rings, !rings.isEmpty {
            let n = rings.map { $0.count / 2 }
            var out = [Double](repeating: 0, count: (n.max() ?? 0) * 2 + 8), which: Int32 = 0
            let k = withPtrs(rings) { fs_place_frame($0, n.map { Int32($0) }, Int32(rings.count), area, &out, &which) }
            if k >= 3 { sections = fs_sections_create(out, k, fs_piece_sect_n(piece)) }
        }
        fs_piece_sector(piece, -1)
    }

    /* One position (worldMove). */
    func step(lon: Double, lat: Double, solo: String? = nil) {
        /* the route: the nearest, held by sectorHold's margin */
        var proj = fs_projection()
        let r = fs_nearest_route(routes, Int32(routes.count), lon, lat, fs_piece_route(piece), fs_sections_hold(sections), &proj)
        fs_piece_walk(piece, r, proj.t, r >= 0 ? proj.dist : .infinity)
        /* the route underfoot (its sound crossfades in over 1.5 s), named only within the leash */
        routeName = r >= 0 && Int(r) < routeNames.count && proj.dist <= Double(FS_GPS_LEASH) ? routeNames[Int(r)] : nil

        /* the section underfoot (sectorUpdate) */
        if let s = sections {
            let cur = fs_piece_sector_now(piece), next = fs_sections_step(s, cur, lon, lat)
            if next != cur { fs_piece_sector(piece, next) }
        }

        /* zones (zonesNear + pacerCheckZones): a plain point speaks as you enter it */
        let now = Date().timeIntervalSince(t0) * 1000
        var dist = [Double](), radius = [Double](), centroid = [Double](), onsets = [Double]()
        var inside = [String](), entered = ""
        for i in spots.indices {
            let d = fs_geo_distance(lon, lat, spots[i].lon, spots[i].lat)
            if spots[i].audio { dist.append(d); radius.append(spots[i].radius); centroid.append(spots[i].centroid); onsets.append(spots[i].onsets) }
            let wasIn = spots[i].compIn   /* composition: geometry only, no chime cooldown */
            spots[i].compIn = d <= spots[i].zoneR || (wasIn && d <= spots[i].zoneR * Double(FS_ZONE_MARGIN))
            if spots[i].compIn { inside.append(spots[i].id); if !wasIn { entered = spots[i].id } }
            if d > spots[i].radius * Double(FS_ZONE_MARGIN) { spots[i].zone = fs_zone_state(); continue }
            let ev = fs_zone_step(&spots[i].zone, d, spots[i].zoneR, now, Double(FS_ZONE_MARGIN), Double(FS_ZONE_COOLDOWN_MS))
            if ev == 1 && spots[i].plain { fs_piece_zone(piece, spots[i].icon) }
        }
        let ins = inside.joined(separator: ",")
        if ins != zonesSent || !entered.isEmpty { fs_piece_zones(piece, ins, entered); zonesSent = ins }
        fs_piece_character(piece, Int32(dist.count), dist, radius, centroid, onsets)

        /* rhythm points (updateRhythms): within their radius, the nearest few */
        var bd = beats.map { fs_geo_distance(lon, lat, $0.lon, $0.lat) }
        var br = beats.map { $0.radius }
        var el = [UInt8](repeating: 1, count: beats.count)
        var picked = [Int32](repeating: 0, count: max(beats.count, 1))
        let k = Int(fs_pick_voices(&bd, &br, &el, Int32(beats.count), Int32(min(Int(FS_MAX_VOICES), max(bedVoices, 1))), 1, &picked))
        /* Listen on a rhythm point: it joins at its full level from wherever the walker is */
        let soloJ = solo.flatMap { id in beats.firstIndex { $0.id == id } }
        var chosen = picked.prefix(k).map { Int($0) }
        if let j = soloJ, !chosen.contains(j) { chosen = Array(chosen.prefix(Int(FS_MAX_VOICES) - 1)) + [j] }
        let want = Set(chosen.map { beats[$0].id })
        for (id, h) in handle where !want.contains(id) { fs_piece_rhythm_remove(piece, h); handle[id] = nil }
        for j in chosen {
            let b = beats[j]
            if handle[b.id] == nil {
                let h = fs_piece_rhythm_add(piece, b.json, b.grains ? 1 : 0)
                if h < 0 { continue }
                handle[b.id] = h
                load(b, h)
            }
            if let h = handle[b.id] { fs_piece_rhythm_gain(piece, h, Float(j == soloJ ? b.gain : fs_point_gain(bd[j], b.radius, b.gain))) }
        }
        let h = soloJ.flatMap { handle[beats[$0].id] } ?? -1
        if h != soloHandle { soloHandle = h; fs_piece_solo(piece, h) }
        rhythmNames = chosen.map { beats[$0].name }
    }

    /* Each recording of a rhythm point: fetched and cached as the soundscape's are, decoded at the
       engine's rate, handed to the piece as interleaved 16-bit (it owns and frees the memory). */
    private func load(_ b: Beat, _ h: Int32) {
        let sr = core.sampleRate
        for (slot, path) in b.paths.enumerated() {
            guard let path else { continue }
            walk?.loadStarted()
            Task { @MainActor [weak self] in
                defer { self?.walk?.loadEnded() }     /* Test 16 */
                guard let walk = self?.walk, let data = try? await walk.recording(path, for: b.id + "#\(slot)") else { return }
                /* interleaved 16-bit off the main thread; copied into the piece's own memory here */
                guard let pcm = try? await Decode.pcm16(data, sampleRate: sr) else { return }
                guard let self, pcm.frames > 0, self.handle[b.id] == h else { pcm.free(); return }
                /* interleaved into the piece's own memory (it frees it), the planar copy let go at once */
                let c = pcm.planar.count, n = pcm.frames
                let mem = fs_alloc_i16(n * c)!
                for k in 0..<c { let src = pcm.planar[k]; for i in 0..<n { mem[i * c + k] = src[i] } }
                pcm.free()
                fs_piece_rhythm_source(self.piece, h, Int32(slot), Int32(c), Int64(n), mem)
                /* 6b: its pitch track after it - a recording arriving resets the track (final review C2) */
                guard slot < b.tracks.count, let t = b.tracks[slot], let tj = try? await walk.recording(t, for: b.id + "#\(slot)#track"), self.handle[b.id] == h else { return }
                fs_piece_rhythm_track(self.piece, h, Int32(slot), String(decoding: tj, as: UTF8.self))
                NSLog("6b rhythm track: handle %d slot %d", h, slot)
            }
        }
    }

    weak var walk: Walk?

    private static func json(_ v: Any?) -> String {
        guard let v, JSONSerialization.isValidJSONObject(v), let d = try? JSONSerialization.data(withJSONObject: v) else { return "{}" }
        return String(data: d, encoding: .utf8) ?? "{}"
    }

    private func withPtrs<R>(_ rings: [[Double]], _ body: ([UnsafePointer<Double>?]) -> R) -> R {
        let bufs = rings.map { r -> UnsafeMutablePointer<Double> in
            let p = UnsafeMutablePointer<Double>.allocate(capacity: max(r.count, 1)); p.initialize(from: r, count: r.count); return p
        }
        defer { bufs.forEach { $0.deallocate() } }
        return body(bufs.map { UnsafePointer($0) })
    }
}
