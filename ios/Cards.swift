// The listener's cards (the approved mock, design/app/gen_mock.py "Point card" / "Route card"), opened
// by tapping a point or a route on the map, a point in the walk panel, or a route in Routes. Both fit
// the sheet without scrolling (Kerem: the point card must not scroll). Tokens only; the chord colours
// are the data scale in Tokens.md (index.html rootColour), the same on the map.
import SwiftUI

/* A route's progression as coloured cells, one per chord, the playing one lit. */
struct ChordStrip: View {
    let route: Walk.RouteInfo
    let active: Int
    var body: some View {
        HStack(spacing: 2) {
            ForEach(route.prog.indices, id: \.self) { i in
                RoundedRectangle(cornerRadius: 3)
                    .fill(T.root(route.prog[i].pc).opacity(active < 0 || i == active ? 1 : 0.45))
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(i == active ? T.ink : .clear, lineWidth: 2))
            }
        }
        .accessibilityHidden(true)
    }
}

private func chip(_ s: String) -> some View {
    Text(s).font(T.mono(T.xs)).tracking(T.xs * T.trackMeta).foregroundStyle(T.faint)
        .padding(.horizontal, 6).padding(.vertical, 5)
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(T.hairline))
}
private func note(_ s: String) -> some View {
    Text(s).font(T.body(T.sm)).foregroundStyle(T.dim).lineSpacing(3).fixedSize(horizontal: false, vertical: true)
}
private func metres(_ d: Double) -> String { d < 1000 ? String(format: "%.0f m", d) : String(format: "%.1f km", d / 1000) }
private func clock(_ s: Double) -> String { String(format: "%d:%02d", Int(s) / 60, Int(s) % 60) }

struct CardButton: View {
    let title: String, primary: Bool, action: () -> Void
    var system: String? = nil
    var body: some View {
        Button(action: action) {
            HStack(spacing: T.s2) {
                if let system { Image(systemName: system).font(.system(size: 14, weight: .semibold)) }
                Text(title).font(T.body(15, .medium))
            }
            .foregroundStyle(primary ? T.sunk : T.ink)
            .frame(maxWidth: .infinity, minHeight: T.target)
            .background(primary ? T.accent : T.raised, in: RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
        }
    }
}

struct PointCard: View {
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState
    @Binding var sheet: Sheet
    let id: String
    var body: some View {
        if let q = walk.pointInfo[id] {
            VStack(alignment: .leading, spacing: T.s3) {
                HStack(spacing: T.s2) {
                    RoundButton(system: "chevron.left", label: "Back to the walk") { sheet = .walk }
                    Circle().fill(q.mode == "silent" ? T.sunk : T.lamp).overlay(Circle().stroke(T.ink, lineWidth: q.mode == "silent" ? 1.5 : 0))
                        .frame(width: 12, height: 12)
                    chip(q.mode.uppercased())
                    Spacer()
                    if let d = walk.distance(to: id) { chip(metres(d) + " away") }
                }
                Text(q.name).font(T.display(T.lg)).foregroundStyle(T.ink).lineLimit(2)
                if !q.note.isEmpty { note(q.note).lineLimit(3) }
                if q.path != nil && q.duration > 0 {
                    eyebrow("The recording")
                    HStack(spacing: T.s3) {
                        let mine = walk.rawId == id
                        Button { walk.playRaw(id) } label: {
                            Group {
                                if mine && walk.rawLoading { ProgressView().tint(T.ink) }
                                else { Image(systemName: mine && walk.rawPlaying ? "pause.fill" : "play.fill").font(.system(size: 16)) }
                            }
                            .foregroundStyle(T.ink).frame(width: T.target, height: T.target)
                            .background(T.raised, in: Circle()).overlay(Circle().stroke(T.hairline))
                        }
                        .accessibilityLabel(mine && walk.rawPlaying ? "Pause the recording" : "Play the recording as it was made")
                        Waveform(peaks: q.peaks, played: mine ? walk.rawPosition / max(q.duration, 1) : 0) { f in
                            if mine { walk.seekRaw(f) }
                        }
                        .frame(height: 44)
                        Text(clock(mine ? walk.rawPosition : 0) + " / " + clock(q.duration)).font(T.mono(11.5)).foregroundStyle(T.dim)
                            .fixedSize()
                    }
                }
                eyebrow("Where and when")
                note(where_(q))
                if q.mode != "silent" {
                    let on = walk.solo == id
                    note(on ? "Listening to this point alone, from wherever you are. The rest waits."
                            : q.sounds ? "Listen: this point alone, stretched, from wherever you are." : "Listen: this point's \(q.mode) alone, from wherever you are.")
                    HStack(spacing: T.s2) {
                        CardButton(title: on ? "Stop listening" : "Listen", primary: !on, action: { walk.listen(on ? nil : id) }, system: on ? "stop.fill" : "play.fill")
                        CardButton(title: "Zoom to", primary: false, action: { zoom(q) }).frame(width: 110)
                    }
                } else {
                    note("A quiet point: it speaks once as you step into its circle.")
                    CardButton(title: "Zoom to", primary: false, action: { zoom(q) })
                }
            }
        }
    }
    private func zoom(_ q: Walk.PointInfo) {
        let d = 0.0012
        map.show(.init(latitude: q.lat - d, longitude: q.lon - d), .init(latitude: q.lat + d, longitude: q.lon + d))
    }
    private func where_(_ q: Walk.PointInfo) -> String {
        var parts = [String(format: "%.4f %@, %.4f %@", abs(q.lat), q.lat >= 0 ? "N" : "S", abs(q.lon), q.lon >= 0 ? "E" : "W")]
        if let r = q.recorded, let d = ISO8601DateFormatter.frac.date(from: r) ?? ISO8601DateFormatter().date(from: r) {
            parts.append(DateFormatter.localizedString(from: d, dateStyle: .medium, timeStyle: .short))
        }
        if q.duration > 0 { parts.append(q.duration < 60 ? String(format: "%.0f s", q.duration) : String(format: "%d min %d s", Int(q.duration) / 60, Int(q.duration) % 60)) }
        return parts.joined(separator: " · ")
    }
}

extension ISO8601DateFormatter {
    static let frac: ISO8601DateFormatter = { let f = ISO8601DateFormatter(); f.formatOptions = [.withInternetDateTime, .withFractionalSeconds]; return f }()
}

/* The recording's peaks as bars, the played part in the accent; a tap or a drag seeks. */
struct Waveform: View {
    let peaks: [Double], played: Double
    let seek: (Double) -> Void
    var body: some View {
        GeometryReader { g in
            let n = max(1, min(peaks.count, Int(g.size.width / 3)))
            let step = Double(peaks.count) / Double(n)
            let top = max(peaks.max() ?? 1, 0.05)
            Canvas { ctx, size in
                let w = size.width / CGFloat(n)
                for i in 0..<n {
                    let v = peaks.isEmpty ? 0.3 : peaks[min(peaks.count - 1, Int(Double(i) * step))]
                    let h = max(2, CGFloat(v / top) * size.height)
                    let r = CGRect(x: CGFloat(i) * w, y: (size.height - h) / 2, width: max(1, w - 1), height: h)
                    ctx.fill(Path(roundedRect: r, cornerRadius: 1), with: .color(Double(i) / Double(n) < played ? T.accent : T.faint.opacity(0.55)))
                }
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onEnded { v in seek(max(0, min(1, v.location.x / g.size.width))) })
        }
        .accessibilityLabel(String(format: "The recording, %.0f percent played", played * 100))
    }
}

struct RouteCard: View {
    @ObservedObject var walk: Walk
    @ObservedObject var map: MapState
    @Binding var sheet: Sheet
    let index: Int
    var body: some View {
        if index < walk.routeList.count {
            let r = walk.routeList[index]
            let here = walk.playingRoute == index ? walk.chordStep : -1
            VStack(alignment: .leading, spacing: T.s3) {
                HStack(spacing: T.s2) {
                    RoundButton(system: "chevron.left", label: "Back to the walk") { sheet = .walk }
                    chip("ROUTE")
                    Spacer()
                    chip(r.length)
                }
                Text(r.name).font(T.display(T.lg)).foregroundStyle(T.ink).lineLimit(2)
                if !r.note.isEmpty { note(r.note).lineLimit(3) }
                eyebrow(here >= 0 ? "The progression — you are at \(here + 1) of \(r.prog.count)" : "The progression — \(r.prog.count) chords along the route")
                ChordStrip(route: r, active: here).frame(height: 28)
                if let first = r.prog.first, let last = r.prog.last {
                    HStack {
                        Text(first.label).font(T.mono(11.5)).foregroundStyle(T.dim)
                        Spacer()
                        if here >= 0 { Text(r.prog[here].label + " — now").font(T.mono(11.5)).foregroundStyle(T.ink) }
                        Spacer()
                        Text(last.label).font(T.mono(11.5)).foregroundStyle(T.dim)
                    }
                }
                HStack(spacing: T.s2) {
                    stat("Key", r.key2.isEmpty || r.key2 == r.key ? r.key : r.key + " · " + r.key2)
                    stat("Tempo", "\(r.tempo) bpm")
                    stat("Sections", "\(r.sections)")
                }
                CardButton(title: "Show whole route", primary: false, action: { map.show(r.sw, r.ne) })
            }
        }
    }
    private func stat(_ k: String, _ v: String) -> some View {
        VStack(alignment: .leading, spacing: T.s1) {
            eyebrow(k)
            Text(v).font(T.body(18)).foregroundStyle(T.ink).lineLimit(1).minimumScaleFactor(0.8)
        }
        .frame(maxWidth: .infinity, alignment: .leading).padding(10)
        .background(T.sunk, in: RoundedRectangle(cornerRadius: 10)).overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
    }
}
