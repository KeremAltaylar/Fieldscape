// The morph cells, drawn as the web draws them (index.html cellsDraw, ported line for line): one
// cell per morph of the route that is playing, on the piece's own clock (fs_piece_morphs) - the
// numbers the voices take, so what moves here is what is heard. Seat = which morph, hue = which voice
// on the chord-root wheel, membrane = its shape, the rim = its value. It draws and never decides;
// nothing in the audio path reads it. Not on screen, not drawn; reduced motion gets one frame a
// second (C-6). No labels: the patch names every destination in words.
import SwiftUI

struct CellsView: View {
    let core: Core
    let route: Int      /* the playing route: another one starts the events afresh */
    @Environment(\.accessibilityReduceMotion) private var reduced
    @State private var memory = CellsMemory()

    var body: some View {
        /* 30 fps is plenty for something whose fastest motion takes forty seconds */
        TimelineView(.animation(minimumInterval: reduced ? 1 : 1.0 / 30)) { tl in
            Canvas { g, size in _ = tl.date; draw(&g, size) }
        }
        .frame(width: 136, height: 136)                                  /* the web's 8.5rem on a phone */
        .background(Circle().fill(T.sunk.opacity(0.78)))
        .overlay(Circle().stroke(T.accent.opacity(0.16), lineWidth: 1))  /* --bdr */
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }

    /* the web's rgba(190,205,192,·): the track, the seat ticks, the empty state */
    private static let mist = Color(red: 190 / 255, green: 205 / 255, blue: 192 / 255)
    private static let voiceOffset = [0, 4, 9]
    private static let tau = 2 * Double.pi

    private struct Cell {
        let shape, dest, key: Int
        let seed, v, dv, ph, a0, a1, hx, hy, r: Double
        let hue: Color
        var x = 0.0, y = 0.0
    }

    private func draw(_ g: inout GraphicsContext, _ size: CGSize) {
        var o = [Double](repeating: 0, count: 7 * 24)
        var clock = 0.0, root: Int32 = 0, shown: Int32 = 0
        let n = Int(fs_piece_morphs(core.piece, &o, 24, &clock, &root, &shown))
        let t = clock, m = memory
        let dt = m.lastT.map { max(0, min(1, t - $0)) } ?? 0
        m.lastT = t

        let cx = Double(size.width) / 2, cy = Double(size.height) / 2, R = Double(min(size.width, size.height)) / 2 - 1
        /* neighbouring seats are rSeat apart at any count, so a body reaches half of it and no more */
        let rGauge = R * 0.93, Rf = R * 0.82, rSeat = Rf * 0.52
        let pc = Int(root), c = CGPoint(x: cx, y: cy)

        /* the chord, tinted into the disc */
        let ground = T.root(pc)
        g.fill(circle(cx, cy, R), with: .radialGradient(Gradient(stops: [
            .init(color: ground.opacity(0.075), location: 0), .init(color: ground.opacity(0.028), location: 0.72),
            .init(color: ground.opacity(0), location: 1)]), center: c, startRadius: 0, endRadius: R))

        /* empty (C-5): an empty track reads as idle, a blank disc as broken */
        if n <= 0 {
            g.stroke(circle(cx, cy, rGauge), with: .color(Self.mist.opacity(0.13)), lineWidth: 1)
            g.stroke(circle(cx, cy, rSeat), with: .color(Self.mist.opacity(0.13)), style: StrokeStyle(lineWidth: 1, dash: [2, 5]))
            g.draw(Text(n < 0 ? "no route" : "no morphs").font(T.mono(12)).foregroundColor(Self.mist.opacity(0.85)), at: c)
            return
        }
        if m.route != route { m.cells = [:]; m.route = route }   /* another patch: stale events would fire */

        let span = Self.tau / Double(n), gap = min(0.10, span * 0.14)
        var pts: [Cell] = []
        var groups: [Int: [Int]] = [:], keys: [Int] = []
        for i in 0..<n {
            let b = 7 * i, v = o[b + 4], seat = -Double.pi / 2 + (Double(i) + 0.5) * span
            var p = Cell(shape: Int(o[b]), dest: Int(o[b + 2]), key: i, seed: o[b + 3], v: v, dv: o[b + 5], ph: o[b + 6],
                         a0: -Double.pi / 2 + Double(i) * span + gap * 0.5, a1: -Double.pi / 2 + Double(i + 1) * span - gap * 0.5,
                         hx: cx + cos(seat) * rSeat, hy: cy + sin(seat) * rSeat, r: Rf * (0.075 + 0.185 * v),
                         hue: T.root(pc + Self.voiceOffset[min(2, max(0, Int(o[b + 1])))]))
            p.x = p.hx; p.y = p.hy
            if groups[p.dest] == nil { groups[p.dest] = []; keys.append(p.dest) }
            groups[p.dest]!.append(pts.count)
            pts.append(p)

            /* a wrap in the phase is a real edge in the generator: pulse rings, ramp flashes */
            var e = m.cells[i] ?? (lastP: nil, waves: [], flash: 0)
            let wrapped = p.ph - floor(p.ph)
            if let last = e.lastP, wrapped < last {
                if p.shape == 3 { e.flash = 1 }
                if p.shape == 2 { e.waves.append(t) }
            }
            e.lastP = wrapped
            if e.flash > 0 { e.flash = max(0, e.flash - dt / 1.2) }
            e.waves = e.waves.filter { t - $0 < 3.2 }
            m.cells[i] = e
        }

        /* gathering: morphs on one destination move toward their mean while they agree */
        var agree: [Int: Double] = [:], centre: [Int: (Double, Double)] = [:]
        for k in keys {
            let grp = groups[k]!
            agree[k] = 0
            if grp.count < 2 { continue }
            let mean = grp.map { pts[$0].v }.reduce(0, +) / Double(grp.count)
            let gx = grp.map { pts[$0].hx }.reduce(0, +) / Double(grp.count), gy = grp.map { pts[$0].hy }.reduce(0, +) / Double(grp.count)
            let spread = grp.map { abs(pts[$0].v - mean) }.max() ?? 0
            let a = max(0, 1 - spread / 0.20)
            agree[k] = a; centre[k] = (gx, gy)
            for q in grp {
                pts[q].x = pts[q].hx + (gx - pts[q].hx) * a * 0.88
                pts[q].y = pts[q].hy + (gy - pts[q].hy) * a * 0.88
            }
        }

        /* bodies, in light */
        var light = g
        light.blendMode = .plusLighter
        for k in keys {
            let grp = groups[k]!, a = agree[k] ?? 0
            if grp.count < 2 || a == 0 { continue }
            for i in 0..<grp.count { for j in (i + 1)..<grp.count {
                let p = pts[grp[i]], q = pts[grp[j]]
                for s in 1..<6 {
                    let f = Double(s) / 6
                    blob(&light, p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, (p.r + (q.r - p.r) * f) * 0.60 * a, p.hue, 0.17 * a * min(p.v, q.v))
                }
            } }
        }
        for p in pts {
            blob(&light, p.x, p.y, p.r, p.hue, 0.14 + 0.42 * p.v)
            if p.shape == 3, let fl = m.cells[p.key]?.flash, fl > 0 { blob(&light, p.x, p.y, p.r * 1.5, p.hue, 0.30 * fl) }
        }

        /* membranes, and the events the taut ones carry instead */
        for p in pts {
            g.stroke(membrane(p, t), with: .color(p.hue.opacity(0.34 + 0.46 * p.v)), lineWidth: 1.25)
            switch p.shape {
            case 4:   /* tide: two rates beating */
                let b1 = sin(Self.tau * p.ph), b2 = sin(Self.tau * p.ph * 1.6180339887)
                g.stroke(circle(p.x, p.y, p.r * (1 + 0.20 * b1)), with: .color(p.hue.opacity(0.20 + 0.30 * p.v)), lineWidth: 1)
                g.stroke(circle(p.x, p.y, max(1, p.r * (1 - 0.20 * b2))), with: .color(p.hue.opacity(0.16 + 0.26 * p.v)), lineWidth: 1)
            case 2:   /* pulse: its shockwaves */
                for born in m.cells[p.key]?.waves ?? [] {
                    let age = (t - born) / 3.2
                    g.stroke(circle(p.x, p.y, p.r + Rf * 0.30 * age), with: .color(p.hue.opacity(0.34 * (1 - age))), lineWidth: 1)
                }
            case 3:   /* ramp: a clock hand */
                let pr = p.ph - floor(p.ph)
                var w = Path()
                w.move(to: CGPoint(x: p.x, y: p.y))
                w.addArc(center: CGPoint(x: p.x, y: p.y), radius: p.r * 0.82, startAngle: .radians(-Double.pi / 2),
                         endAngle: .radians(-Double.pi / 2 + Self.tau * pr), clockwise: false)
                w.closeSubpath()
                g.fill(w, with: .color(p.hue.opacity(0.16 + 0.16 * p.v)))
            case 1 where p.dv > 0:   /* breath, on its rise */
                blob(&g, p.x, p.y, p.r * 0.42, p.hue, 0.30)
            default: break
            }
        }

        /* while they agree, they are one body */
        for k in keys {
            let grp = groups[k]!, a = agree[k] ?? 0
            guard grp.count >= 2, a >= 0.18, let (gx, gy) = centre[k] else { continue }
            let far = grp.map { hypot(pts[$0].x - gx, pts[$0].y - gy) + pts[$0].r }.max() ?? 0
            g.stroke(circle(gx, gy, far + 3), with: .color(pts[grp[0]].hue.opacity(0.10 + 0.26 * a)), style: StrokeStyle(lineWidth: 1, dash: [1.5, 4]))
        }

        /* the gauge: where a value is read rather than guessed */
        for p in pts {
            g.stroke(arc(c, rGauge, p.a0, p.a1), with: .color(p.hue.opacity(0.13)), lineWidth: 1.5)
            let end = p.a0 + (p.a1 - p.a0) * p.v
            g.stroke(arc(c, rGauge, p.a0, end), with: .color(p.hue.opacity(0.42 + 0.48 * p.v)), lineWidth: 3)
            g.fill(circle(cx + cos(end) * rGauge, cy + sin(end) * rGauge, 2.2), with: .color(p.hue.opacity(0.92)))
            /* the fixed edge of this morph's seat */
            let d0 = p.a0 - gap * 0.5
            var tick = Path()
            tick.move(to: CGPoint(x: cx + cos(d0) * (rGauge - 5), y: cy + sin(d0) * (rGauge - 5)))
            tick.addLine(to: CGPoint(x: cx + cos(d0) * (rGauge + 4), y: cy + sin(d0) * (rGauge + 4)))
            g.stroke(tick, with: .color(Self.mist.opacity(0.16)), lineWidth: 1)
        }
    }

    private func circle(_ x: Double, _ y: Double, _ r: Double) -> Path { Path(ellipseIn: CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r)) }
    private func arc(_ c: CGPoint, _ r: Double, _ a0: Double, _ a1: Double) -> Path {
        var p = Path(); p.addArc(center: c, radius: r, startAngle: .radians(a0), endAngle: .radians(a1), clockwise: false); return p
    }
    private func blob(_ g: inout GraphicsContext, _ x: Double, _ y: Double, _ r: Double, _ hue: Color, _ alpha: Double) {
        guard r > 0.5 else { return }
        let a = max(0, min(1, alpha))
        g.fill(circle(x, y, r), with: .radialGradient(Gradient(stops: [
            .init(color: hue.opacity(a), location: 0), .init(color: hue.opacity(a * 0.45), location: 0.55),
            .init(color: hue.opacity(0), location: 1)]), center: CGPoint(x: x, y: y), startRadius: 0, endRadius: r))
    }

    /* the membrane: drift wobbles on the noise that makes it, breath leans into its rise; the rest stay taut */
    private func membrane(_ p: Cell, _ t: Double) -> Path {
        var path = Path()
        for i in 0...72 {
            let th = Double(i) / 72 * 6.2832
            var f = 1.0
            if p.shape == 0 { f = 1 + 0.30 * (Self.pnoise(th, p.seed, 7, t / 240) - 0.5) }
            if p.shape == 1 { f = 1 + 0.24 * max(-1, min(1, p.dv * 26)) * cos(th + Double.pi / 2) }
            let pt = CGPoint(x: p.x + cos(th) * p.r * f, y: p.y + sin(th) * p.r * f)
            if i == 0 { path.move(to: pt) } else { path.addLine(to: pt) }
        }
        path.closeSubpath()
        return path
    }
    private static func hash01(_ n: Double, _ seed: Double) -> Double { let x = sin(n * 127.1 + seed * 311.7) * 43758.5453; return x - floor(x) }
    /* periodic value noise round the rim: the knot index wraps at K so the loop closes */
    private static func pnoise(_ theta: Double, _ seed: Double, _ K: Int, _ rot: Double) -> Double {
        var x = theta / 6.283185307 + rot
        x = (x - floor(x)) * Double(K)
        let i = Int(floor(x)), f = x - Double(i)
        let a = hash01(Double(((i % K) + K) % K), seed), b = hash01(Double((((i + 1) % K) + K) % K), seed)
        return a + (b - a) * (f * f * (3 - 2 * f))
    }
}

/* per-morph render memory, so an event in the generator can be drawn as an event */
final class CellsMemory {
    var cells: [Int: (lastP: Double?, waves: [Double], flash: Double)] = [:]
    var lastT: Double? = nil
    var route = -2
}
