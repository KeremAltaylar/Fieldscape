package net.keremaltaylar.fieldscape

import android.provider.Settings
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.*

/** The morph cells, drawn as the web draws them (index.html cellsDraw, as ios/Cells.swift has it): one
 *  cell per morph of the route that is playing, on the piece's own clock (fs_piece_morphs) - the
 *  numbers the voices take. Seat = which morph, hue = which voice on the chord-root wheel, membrane =
 *  its shape, the rim = its value. It draws and never decides. Not on screen, not drawn; animations
 *  off (reduced motion, C-6) gets one frame a second. Sizes are the web's CSS px, drawn in dp. */
@Composable
fun CellsView(route: Int) {
    val ctx = LocalContext.current
    val reduced = remember { Settings.Global.getFloat(ctx.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f }
    val memory = remember(route) { CellsMemory() }      /* another patch: stale events would fire */
    var frame by remember { mutableStateOf(0L) }
    LaunchedEffect(reduced) {
        var last = 0L
        while (true) withFrameNanos { now ->
            /* 30 fps is plenty for something whose fastest motion takes forty seconds */
            if (now - last >= (if (reduced) 1_000_000_000L else 33_000_000L)) { last = now; frame = now }
        }
    }
    val text = rememberTextMeasurer()
    Canvas(Modifier.size(136.dp)                                          /* the web's 8.5rem on a phone */
        .background(T.sunk.copy(alpha = 0.78f), CircleShape)
        .border(1.dp, T.accent.copy(alpha = 0.16f), CircleShape)         /* --bdr */
        .graphicsLayer(compositingStrategy = CompositingStrategy.Offscreen) /* light adds within the disc, as on the web canvas */
        .clearAndSetSemantics {}) {
        frame.let { }
        scale(density, Offset.Zero) { drawCells(Core.pieceMorphs(), memory, size.width / density, size.height / density) { s, at ->
            val m = text.measure(s, TextStyle(fontFamily = T.mono, fontSize = 12.sp, color = MIST.copy(alpha = 0.85f)))
            scale(1 / density, Offset.Zero) { drawText(m, topLeft = Offset(at.x * density - m.size.width / 2, at.y * density - m.size.height / 2)) }
        } }
    }
}

/** per-morph render memory, so an event in the generator can be drawn as an event */
class CellsMemory {
    class E { var lastP: Double? = null; val waves = mutableListOf<Double>(); var flash = 0.0 }
    val cells = HashMap<Int, E>()
    var lastT: Double? = null
}

/** the web's rgba(190,205,192,·): the track, the seat ticks, the empty state */
private val MIST = Color(190, 205, 192)
private val VOICE_OFFSET = intArrayOf(0, 4, 9)
private const val TAU = 2 * PI

private class Cell(val shape: Int, val dest: Int, val key: Int, val seed: Double, val v: Double, val dv: Double, val ph: Double,
                   val a0: Double, val a1: Double, val hx: Double, val hy: Double, val r: Double, val hue: Color) { var x = hx; var y = hy }

private fun DrawScope.drawCells(o: DoubleArray, m: CellsMemory, w: Float, h: Float, label: DrawScope.(String, Offset) -> Unit) {
    val t = o[0]; val pc = o[1].toInt(); val n = o[3].toInt()
    val dt = m.lastT?.let { max(0.0, min(1.0, t - it)) } ?: 0.0
    m.lastT = t
    val cx = w / 2.0; val cy = h / 2.0; val R = min(w, h) / 2.0 - 1
    /* neighbouring seats are rSeat apart at any count, so a body reaches half of it and no more */
    val rGauge = R * 0.93; val Rf = R * 0.82; val rSeat = Rf * 0.52

    /* the chord, tinted into the disc */
    val ground = T.root(pc)
    drawCircle(Brush.radialGradient(0f to ground.a(0.075), 0.72f to ground.a(0.028), 1f to ground.a(0.0), center = off(cx, cy), radius = R.toFloat()),
               R.toFloat(), off(cx, cy))

    /* empty (C-5): an empty track reads as idle, a blank disc as broken */
    if (n <= 0) {
        drawCircle(MIST.a(0.13), rGauge.toFloat(), off(cx, cy), style = Stroke(1f))
        drawCircle(MIST.a(0.13), rSeat.toFloat(), off(cx, cy), style = Stroke(1f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(2f, 5f))))
        label(if (n < 0) "no route" else "no morphs", off(cx, cy))
        return
    }

    val span = TAU / n; val gap = min(0.10, span * 0.14)
    val pts = ArrayList<Cell>(n)
    val groups = LinkedHashMap<Int, MutableList<Int>>()
    for (i in 0 until n) {
        val b = 4 + 7 * i; val v = o[b + 4]; val seat = -PI / 2 + (i + 0.5) * span
        val p = Cell(o[b].toInt(), o[b + 2].toInt(), i, o[b + 3], v, o[b + 5], o[b + 6],
                     -PI / 2 + i * span + gap * 0.5, -PI / 2 + (i + 1) * span - gap * 0.5,
                     cx + cos(seat) * rSeat, cy + sin(seat) * rSeat, Rf * (0.075 + 0.185 * v),
                     T.root(pc + VOICE_OFFSET[o[b + 1].toInt().coerceIn(0, 2)]))
        groups.getOrPut(p.dest) { mutableListOf() }.add(pts.size)
        pts.add(p)

        /* a wrap in the phase is a real edge in the generator: pulse rings, ramp flashes */
        val e = m.cells.getOrPut(i) { CellsMemory.E() }
        val wrapped = p.ph - floor(p.ph)
        e.lastP?.let { if (wrapped < it) { if (p.shape == 3) e.flash = 1.0; if (p.shape == 2) e.waves.add(t) } }
        e.lastP = wrapped
        if (e.flash > 0) e.flash = max(0.0, e.flash - dt / 1.2)
        e.waves.removeAll { t - it >= 3.2 }
    }

    /* gathering: morphs on one destination move toward their mean while they agree */
    val agree = HashMap<Int, Double>(); val centre = HashMap<Int, Pair<Double, Double>>()
    for ((k, grp) in groups) {
        agree[k] = 0.0
        if (grp.size < 2) continue
        val mean = grp.sumOf { pts[it].v } / grp.size
        val gx = grp.sumOf { pts[it].hx } / grp.size; val gy = grp.sumOf { pts[it].hy } / grp.size
        val spread = grp.maxOf { abs(pts[it].v - mean) }
        val a = max(0.0, 1 - spread / 0.20)
        agree[k] = a; centre[k] = gx to gy
        for (q in grp) { pts[q].x = pts[q].hx + (gx - pts[q].hx) * a * 0.88; pts[q].y = pts[q].hy + (gy - pts[q].hy) * a * 0.88 }
    }

    /* bodies, in light */
    for ((k, grp) in groups) {
        val a = agree[k] ?: 0.0
        if (grp.size < 2 || a == 0.0) continue
        for (i in grp.indices) for (j in i + 1 until grp.size) {
            val p = pts[grp[i]]; val q = pts[grp[j]]
            for (s in 1 until 6) {
                val f = s / 6.0
                blob(p.x + (q.x - p.x) * f, p.y + (q.y - p.y) * f, (p.r + (q.r - p.r) * f) * 0.60 * a, p.hue, 0.17 * a * min(p.v, q.v), BlendMode.Plus)
            }
        }
    }
    for (p in pts) {
        blob(p.x, p.y, p.r, p.hue, 0.14 + 0.42 * p.v, BlendMode.Plus)
        val fl = m.cells[p.key]?.flash ?: 0.0
        if (p.shape == 3 && fl > 0) blob(p.x, p.y, p.r * 1.5, p.hue, 0.30 * fl, BlendMode.Plus)
    }

    /* membranes, and the events the taut ones carry instead */
    for (p in pts) {
        drawPath(membrane(p, t), p.hue.a(0.34 + 0.46 * p.v), style = Stroke(1.25f))
        when (p.shape) {
            4 -> {   /* tide: two rates beating */
                val b1 = sin(TAU * p.ph); val b2 = sin(TAU * p.ph * 1.6180339887)
                drawCircle(p.hue.a(0.20 + 0.30 * p.v), (p.r * (1 + 0.20 * b1)).toFloat(), off(p.x, p.y), style = Stroke(1f))
                drawCircle(p.hue.a(0.16 + 0.26 * p.v), max(1.0, p.r * (1 - 0.20 * b2)).toFloat(), off(p.x, p.y), style = Stroke(1f))
            }
            2 -> for (born in m.cells[p.key]?.waves ?: emptyList()) {   /* pulse: its shockwaves */
                val age = (t - born) / 3.2
                drawCircle(p.hue.a(0.34 * (1 - age)), (p.r + Rf * 0.30 * age).toFloat(), off(p.x, p.y), style = Stroke(1f))
            }
            3 -> {   /* ramp: a clock hand */
                val pr = p.ph - floor(p.ph); val rr = p.r * 0.82
                drawArc(p.hue.a(0.16 + 0.16 * p.v), -90f, (360 * pr).toFloat(), true, off(p.x - rr, p.y - rr),
                        androidx.compose.ui.geometry.Size((2 * rr).toFloat(), (2 * rr).toFloat()))
            }
            1 -> if (p.dv > 0) blob(p.x, p.y, p.r * 0.42, p.hue, 0.30, BlendMode.SrcOver)   /* breath, on its rise */
        }
    }

    /* while they agree, they are one body */
    for ((k, grp) in groups) {
        val a = agree[k] ?: 0.0
        val (gx, gy) = centre[k] ?: continue
        if (grp.size < 2 || a < 0.18) continue
        val far = grp.maxOf { hypot(pts[it].x - gx, pts[it].y - gy) + pts[it].r }
        drawCircle(pts[grp[0]].hue.a(0.10 + 0.26 * a), (far + 3).toFloat(), off(gx, gy),
                   style = Stroke(1f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(1.5f, 4f))))
    }

    /* the gauge: where a value is read rather than guessed */
    val ring = Rect(off(cx, cy), rGauge.toFloat())
    for (p in pts) {
        drawArc(p.hue.a(0.13), deg(p.a0), deg(p.a1 - p.a0), false, ring.topLeft, ring.size, style = Stroke(1.5f))
        val end = p.a0 + (p.a1 - p.a0) * p.v
        drawArc(p.hue.a(0.42 + 0.48 * p.v), deg(p.a0), deg(end - p.a0), false, ring.topLeft, ring.size, style = Stroke(3f))
        drawCircle(p.hue.a(0.92), 2.2f, off(cx + cos(end) * rGauge, cy + sin(end) * rGauge))
        /* the fixed edge of this morph's seat */
        val d0 = p.a0 - gap * 0.5
        drawLine(MIST.a(0.16), off(cx + cos(d0) * (rGauge - 5), cy + sin(d0) * (rGauge - 5)),
                 off(cx + cos(d0) * (rGauge + 4), cy + sin(d0) * (rGauge + 4)), 1f)
    }
}

private fun off(x: Double, y: Double) = Offset(x.toFloat(), y.toFloat())
private fun deg(r: Double) = Math.toDegrees(r).toFloat()
private fun Color.a(alpha: Double) = copy(alpha = alpha.coerceIn(0.0, 1.0).toFloat())

private fun DrawScope.blob(x: Double, y: Double, r: Double, hue: Color, alpha: Double, mode: BlendMode) {
    if (r <= 0.5) return
    drawCircle(Brush.radialGradient(0f to hue.a(alpha), 0.55f to hue.a(alpha * 0.45), 1f to hue.a(0.0), center = off(x, y), radius = r.toFloat()),
               r.toFloat(), off(x, y), blendMode = mode)
}

/** the membrane: drift wobbles on the noise that makes it, breath leans into its rise; the rest stay taut */
private fun membrane(p: Cell, t: Double) = Path().apply {
    for (i in 0..72) {
        val th = i / 72.0 * 6.2832
        val f = when (p.shape) {
            0 -> 1 + 0.30 * (pnoise(th, p.seed, 7, t / 240) - 0.5)
            1 -> 1 + 0.24 * (p.dv * 26).coerceIn(-1.0, 1.0) * cos(th + PI / 2)
            else -> 1.0
        }
        val x = (p.x + cos(th) * p.r * f).toFloat(); val y = (p.y + sin(th) * p.r * f).toFloat()
        if (i == 0) moveTo(x, y) else lineTo(x, y)
    }
    close()
}
private fun hash01(n: Double, seed: Double): Double { val x = sin(n * 127.1 + seed * 311.7) * 43758.5453; return x - floor(x) }
/** periodic value noise round the rim: the knot index wraps at K so the loop closes */
private fun pnoise(theta: Double, seed: Double, K: Int, rot: Double): Double {
    var x = theta / 6.283185307 + rot
    x = (x - floor(x)) * K
    val i = floor(x).toInt(); val f = x - i
    val a = hash01((((i % K) + K) % K).toDouble(), seed); val b = hash01(((((i + 1) % K) + K) % K).toDouble(), seed)
    return a + (b - a) * (f * f * (3 - 2 * f))
}
