// The walk panel under the map (the mock Kerem approved on 2026-09-25): the place and the GPS fix,
// then what is heard - nearest first, a level bar, the distance - or why nothing is. Five states:
// hearing, loading, nothing in range, weak GPS (holding), location off. Tokens only (Theme.swift);
// the one accent is the level bars; rows are at least 44 pt (M-4). The Sound test and the CPU
// line sit under Developer, opened by a long-press on the place name.
import SwiftUI

struct WalkPanel: View {
    @ObservedObject var walk: Walk
    @ObservedObject var core: Core
    var onLongPress: () -> Void = {}
    /* a point in the list opens its card */
    var open: (String) -> Void = { _ in }

    var body: some View {
        VStack(alignment: .leading, spacing: T.s3) {
            header
            if let why = core.paused {
                note(Text("Paused. ").foregroundColor(T.ink) + Text(why))
                Button { core.resume() } label: {
                    Text("Resume").font(T.body(T.sm, .medium)).foregroundStyle(T.ink)
                        .frame(maxWidth: .infinity, minHeight: T.target)
                        .background(T.raised, in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
                }
            }
            loading
            switch walk.mode {
            case .off: off
            case .denied: denied
            case .waiting: note(Text("Finding where you are… Or press and hold the map to put yourself there, then drag to walk."))
            case .live, .holding, .byHand: hearing
            }
            if let f = walk.failure { note(Text(f)) }
        }
    }

    private var header: some View {
        HStack(alignment: .firstTextBaseline, spacing: T.s3) {
            Text(walk.mode == .denied || walk.mode == .off ? "Location is off" : (walk.place ?? "Fieldscape"))
                .font(T.display(T.md)).foregroundStyle(T.ink).lineLimit(1)
                .onLongPressGesture(perform: onLongPress)
            Spacer(minLength: T.s2)
            SoundButton(walk: walk, core: core)
            switch walk.mode {
            case .live(let a): chip(String(format: "±%.0f m", a))
            case .holding(let a): chip(String(format: "±%.0f m · HOLDING", a))
            case .byHand: chip("BY HAND")
            default: EmptyView()
            }
        }
    }

    /* Test 16: the recordings on their way - how many of this batch are in, as a bar */
    @ViewBuilder private var loading: some View {
        if walk.pending > 0 {
            let f = Double(walk.batchDone) / Double(max(1, walk.batchDone + walk.pending))
            VStack(alignment: .leading, spacing: T.s1) {
                note(Text(core.soundOn ? "Getting the sounds around you · \(walk.pending) to go" : "Getting the sounds ready · \(walk.pending) to go - Sound starts when they are in"))
                GeometryReader { g in
                    ZStack(alignment: .leading) {
                        Capsule().fill(T.raised)
                        Capsule().fill(T.accent).frame(width: g.size.width * max(0.04, f))
                    }
                }.frame(height: 4)
            }
        }
    }
    /* Test 16: Location Services off on the phone - say so, and take the listener to Settings */
    private var off: some View {
        VStack(alignment: .leading, spacing: T.s3) {
            note(Text("This phone's Location Services are turned off, so Fieldscape cannot tell where you are."))
            Button { if let u = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(u) } } label: {
                Text("Turn on location").font(T.body(T.sm, .medium)).foregroundStyle(T.sunk)
                    .frame(maxWidth: .infinity, minHeight: T.target)
                    .background(T.accent, in: RoundedRectangle(cornerRadius: 10))
            }
            note(Text("Settings → Privacy & Security → Location Services. Or press and hold the map to put yourself there, then drag to walk."))
        }
    }

    @ViewBuilder private var hearing: some View {
        /* the route's own sound and the rhythm points in reach (the piece) */
        if walk.route != nil || !walk.rhythms.isEmpty {
            HStack(spacing: T.s2) {
                note((walk.route.map { Text("Route ") + Text($0).foregroundColor(T.ink) + Text(walk.chord.map { " · " + $0 } ?? "") } ?? Text(""))
                     + (walk.rhythms.isEmpty ? Text("") : Text(walk.route == nil ? "Rhythm " : " · rhythm ") + Text(walk.rhythms.joined(separator: ", ")).foregroundColor(T.ink)))
                if walk.route != nil {
                    Spacer(minLength: T.s2)
                    /* the morph cells over the map: one cell per morph, breathing on its own clock */
                    Button { withAnimation(.easeOut(duration: 0.18)) { walk.cells.toggle() } } label: {
                        Text("Cells").font(T.body(T.sm, .medium)).foregroundStyle(walk.cells ? T.ink : T.dim)
                            .padding(.horizontal, T.s3).frame(minHeight: T.target)
                            .background(walk.cells ? T.raised : .clear, in: RoundedRectangle(cornerRadius: 10))
                            .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
                    }
                    .accessibilityLabel("Morph cells")
                    .accessibilityAddTraits(walk.cells ? .isSelected : [])
                }
            }
        }
        if let s = walk.solo, let q = walk.pointInfo[s] {
            HStack(spacing: T.s2) {
                note(Text("Listening to ") + Text(q.name).foregroundColor(T.ink) + Text(" alone."))
                Spacer(minLength: T.s2)
                Button { walk.listen(nil) } label: {
                    Text("Everything").font(T.body(T.sm, .medium)).foregroundStyle(T.ink)
                        .padding(.horizontal, T.s3).frame(minHeight: T.target)
                        .background(T.raised, in: RoundedRectangle(cornerRadius: 10))
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
                }
                .accessibilityLabel("Listen to everything again")
            }
        }
        if walk.rows.isEmpty {
            if let n = walk.nearest {
                note(Text("Nothing in range here. ").foregroundColor(T.ink)
                     + Text("The nearest recording is ") + Text(n.name).foregroundColor(T.ink)
                     + Text(", ") + Text(n.dist < 1000 ? String(format: "%.0f m", n.dist) : String(format: "%.1f km", n.dist / 1000)).font(T.mono()) + Text(" \(n.direction)."))
            } else {
                note(Text("No recordings are published yet."))
            }
        } else {
            VStack(spacing: 0) {
                ForEach(Array(walk.rows.enumerated()), id: \.element.id) { i, r in
                    if i > 0 { Rectangle().fill(T.hairline).frame(height: 1) }
                    Button { open(r.id) } label: { row(r) }.buttonStyle(.plain)
                }
            }
            if let n = walk.nearest, walk.solo == nil {
                note(Text("Nothing in range here. ").foregroundColor(T.ink) + Text("Walk toward \(n.name), \(n.direction)."))
            }
            if walk.rows.contains(where: { if case .downloading = $0.phase { return true }; return false }) {
                note(Text("First time here: each recording downloads once, then plays offline."))
            }
        }
        if walk.mode == .byHand {
            note(Text("Listening from where you put yourself. Drag your dot, or press and hold anywhere, to walk."))
            Button { walk.useLocation() } label: {
                Text("Use my location").font(T.body(T.sm, .medium)).foregroundStyle(T.ink)
                    .frame(maxWidth: .infinity, minHeight: T.target)
                    .background(T.raised, in: RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
            }
        }
        if case .holding = walk.mode {
            note(Text("GPS is too rough here, so the sound holds where you last were until the fix is better than ")
                 + Text("40 m").font(T.mono()) + Text("."))
        }
    }

    private func row(_ r: Walk.Row) -> some View {
        VStack(spacing: T.s1) {
            HStack(spacing: T.s2) {
                Circle().fill(T.lamp).frame(width: 9, height: 9)
                Text(r.name).font(T.body()).foregroundStyle(T.ink).lineLimit(1)
                Spacer(minLength: T.s2)
                Text(detail(r)).font(T.mono()).foregroundStyle(T.dim)
                Image(systemName: "chevron.right").font(.system(size: 12, weight: .semibold)).foregroundStyle(T.faint)
            }
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    Capsule().fill(T.raised)
                    Capsule().fill(r.phase == .playing ? T.accent : T.faint.opacity(0.6))
                        .frame(width: g.size.width * CGFloat(fill(r)))
                        .animation(.easeOut(duration: 0.18), value: fill(r))
                }
            }.frame(height: 5)
        }
        .frame(minHeight: T.target)
        .padding(.vertical, T.s2)
        .accessibilityElement(children: .combine)
    }

    private func detail(_ r: Walk.Row) -> String {
        switch r.phase {
        case .playing: return r.dist < 1000 ? String(format: "%.0f m", r.dist) : String(format: "%.1f km", r.dist / 1000)
        case .decoding: return "Preparing"
        case .downloading(_, let b): return b > 0 ? String(format: "Downloading · %.1f MB", Double(b) / 1_048_576) : "Downloading"
        }
    }
    private func fill(_ r: Walk.Row) -> Double {
        switch r.phase {
        case .playing: return max(0, min(1, r.level))
        case .downloading(let f, _): return f
        case .decoding: return 1
        }
    }

    private var denied: some View {
        VStack(alignment: .leading, spacing: T.s3) {
            note(Text("Fieldscape plays the recordings placed around you, so it needs to know where you are while the app is open."))
            Button { if let u = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(u) } } label: {
                Text("Open Settings").font(T.body(T.sm, .medium)).foregroundStyle(T.ink)
                    .frame(maxWidth: .infinity, minHeight: T.target)
                    .background(T.raised, in: RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
            }
            note(Text("Settings → Fieldscape → Location → ") + Text("While Using the App").foregroundColor(T.ink))
        }
    }

    /* Units keep their case: an uppercased "m" reads as mega. */
    private func chip(_ s: String) -> some View {
        Text(s).font(T.mono(T.xs)).tracking(T.xs * T.trackMeta).foregroundStyle(T.faint)
            .padding(.horizontal, 6).padding(.vertical, 5)
            .overlay(RoundedRectangle(cornerRadius: 5).stroke(T.hairline))
    }

    private func note(_ t: Text) -> some View {
        t.font(T.body(T.sm)).foregroundStyle(T.dim).lineSpacing(3).fixedSize(horizontal: false, vertical: true)
    }
}

/* The engineering view: the Sound test (bundled recording on slot 0) and the CPU line. */
struct DeveloperPanel: View {
    @ObservedObject var core: Core
    @State var open: Bool
    var body: some View {
        DisclosureGroup(isExpanded: $open) {
            VStack(alignment: .leading, spacing: T.s4) {
                Text(core.line).font(T.mono(T.xs)).foregroundStyle(T.faint)
                Toggle("Sound test (bundled recording)", isOn: $core.test).font(T.body(T.sm))
                ForEach(core.params) { p in
                    let v = core.values[p.id] ?? p.min
                    if p.max == 1 && (p.key == "freeze" || p.key == "shape") {
                        Toggle(p.key == "shape" ? "Hann window" : p.name, isOn: Binding(get: { v > 0.5 }, set: { core.set(p.id, $0 ? 1 : 0) }))
                            .font(T.body(T.sm))
                    } else {
                        VStack(alignment: .leading, spacing: T.s1) {
                            Text(p.key == "stretch" ? String(format: "%@  %.1f×", p.name, pow(1024, v)) : String(format: "%@  %.2f %@", p.name, v, p.unit))
                                .font(T.mono(T.xs)).foregroundStyle(T.dim)
                            Slider(value: Binding(get: { v }, set: { core.set(p.id, $0) }), in: p.min...p.max).tint(T.accent)
                        }
                    }
                }
            }.padding(.top, T.s3)
        } label: {
            Text("DEVELOPER").font(T.mono(T.xs)).tracking(T.xs * T.trackEyebrow).foregroundStyle(T.faint)
        }.tint(T.faint)
    }
}

/* Sound / Stop. Test 16: while the first recordings are still arriving it waits, saying so - started then, the walk would
   come in piece by piece (the Android tester's lag) */
struct SoundButton: View {
    @ObservedObject var walk: Walk
    @ObservedObject var core: Core
    var body: some View {
        let waiting = !core.soundOn && walk.pending > 0
        Button { core.setSound(!core.soundOn) } label: {
            Text(waiting ? "Loading…" : core.soundOn ? "Stop" : "Sound").font(T.body(T.sm, .medium))
                .foregroundStyle(waiting ? T.faint : core.soundOn ? T.ink : T.sunk)
                .frame(minWidth: 64, minHeight: T.target).padding(.horizontal, waiting ? T.s2 : 0)
                .background(core.soundOn || waiting ? T.raised : T.accent, in: RoundedRectangle(cornerRadius: 10))
                .overlay(RoundedRectangle(cornerRadius: 10).stroke(T.hairline))
        }
        .disabled(waiting)
        .accessibilityLabel(waiting ? "Sound, waiting for the recordings" : core.soundOn ? "Stop the sound" : "Start the sound")
    }
}
