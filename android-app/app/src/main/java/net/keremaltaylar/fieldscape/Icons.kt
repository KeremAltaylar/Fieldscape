package net.keremaltaylar.fieldscape

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp

/*
 * Test 16 (the Android tester's report): the button symbols drawn rather than typed. A "✕" or "▶" set in a font sits
 * where that font's metrics put it - low in a round button, as the tester saw - and a drawing sits where it is put.
 * The point markers too: a small badge per kind of point, so the plain dot means only you.
 */

enum class Icon { Close, Play, Pause, Stop, Layers, Account }

/** A symbol drawn in the middle of its box: strokes of 1/12 of it, round caps. */
@Composable
fun Glyph(icon: Icon, color: Color, size: Dp = 18.dp, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val s = this.size.minDimension; val w = s / 12f
        val st = Stroke(width = w * 1.15f, cap = StrokeCap.Round)
        when (icon) {
            Icon.Close -> { val a = s * 0.26f; val b = s * 0.74f
                drawLine(color, Offset(a, a), Offset(b, b), w * 1.15f, StrokeCap.Round); drawLine(color, Offset(b, a), Offset(a, b), w * 1.15f, StrokeCap.Round) }
            /* a triangle's weight is left of its box's middle: nudged right by a sixth of its width so it reads centred */
            Icon.Play -> drawPath(Path().apply { val x0 = s * 0.30f; val x1 = s * 0.78f; val t = s * 0.22f; val bt = s * 0.78f
                moveTo(x0 + s * 0.04f, t); lineTo(x1 + s * 0.04f, s / 2); lineTo(x0 + s * 0.04f, bt); close() }, color)
            Icon.Pause -> { drawRoundRect(color, Offset(s * 0.28f, s * 0.22f), androidx.compose.ui.geometry.Size(s * 0.14f, s * 0.56f), androidx.compose.ui.geometry.CornerRadius(w / 2))
                drawRoundRect(color, Offset(s * 0.58f, s * 0.22f), androidx.compose.ui.geometry.Size(s * 0.14f, s * 0.56f), androidx.compose.ui.geometry.CornerRadius(w / 2)) }
            Icon.Stop -> drawRoundRect(color, Offset(s * 0.27f, s * 0.27f), androidx.compose.ui.geometry.Size(s * 0.46f, s * 0.46f), androidx.compose.ui.geometry.CornerRadius(w / 2))
            Icon.Layers -> for (k in 0..2) { val y = s * (0.30f + 0.20f * k)
                drawPath(Path().apply { moveTo(s * 0.18f, y); lineTo(s / 2, y - s * 0.12f); lineTo(s * 0.82f, y); lineTo(s / 2, y + s * 0.12f); close() }, color, style = st) }
            Icon.Account -> { drawCircle(color, s * 0.15f, Offset(s / 2, s * 0.36f), style = st)
                drawArc(color, 200f, 140f, false, Offset(s * 0.22f, s * 0.58f), androidx.compose.ui.geometry.Size(s * 0.56f, s * 0.44f), style = st) }
        }
    }
}

/** A label set in the middle of its button: the font's own padding off and the line trimmed to the letters, so "Zoom to"
 *  sits where "Listen" does (the tester saw it high) */
fun label(size: TextUnit, family: androidx.compose.ui.text.font.FontFamily = T.body) = TextStyle(fontFamily = family, fontSize = size,
    platformStyle = PlatformTextStyle(includeFontPadding = false),
    /* measured: Newsreader's letters sit ~1 dp above the middle of their own line (3 px at 2.6x), so the baseline drops a little */
    baselineShift = androidx.compose.ui.text.style.BaselineShift(-0.12f),
    lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both))

/** The map's point markers: a badge per kind - stretch (a wave), rhythm (beats), grains (a scatter), a quiet point
 *  (a ring) - in the walk's lamp colour, a quiet one dark; drawn at the screen's density. */
object Markers {
    val ids = listOf("mk-stretch", "mk-rhythm", "mk-grains", "mk-silent")
    fun bitmap(id: String, density: Float): android.graphics.Bitmap {
        val px = (26 * density).toInt(); val b = android.graphics.Bitmap.createBitmap(px, px, android.graphics.Bitmap.Config.ARGB_8888)
        val c = android.graphics.Canvas(b); val r = px / 2f
        val quiet = id == "mk-silent"
        val fill = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply { color = if (quiet) 0xFF0D1310.toInt() else 0xFFBAE6B1.toInt() }
        val ring = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply { style = android.graphics.Paint.Style.STROKE; strokeWidth = 1.6f * density
            color = if (quiet) 0xFFE3E7E4.toInt() else 0xFF0D1310.toInt() }
        c.drawCircle(r, r, r - 1.2f * density, fill); c.drawCircle(r, r, r - 1.2f * density, ring)
        val ink = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply { color = if (quiet) 0xFFE3E7E4.toInt() else 0xFF0D1310.toInt()
            style = android.graphics.Paint.Style.STROKE; strokeWidth = 1.7f * density; strokeCap = android.graphics.Paint.Cap.ROUND }
        val u = px / 26f
        when (id) {
            "mk-stretch" -> { val p = android.graphics.Path(); p.moveTo(6 * u, 13 * u)
                p.cubicTo(8.5f * u, 7 * u, 10.5f * u, 7 * u, 13 * u, 13 * u); p.cubicTo(15.5f * u, 19 * u, 17.5f * u, 19 * u, 20 * u, 13 * u); c.drawPath(p, ink) }
            "mk-rhythm" -> for ((k, h) in listOf(4f, 8f, 5.5f).withIndex()) { val x = (9 + 4 * k) * u; c.drawLine(x, (13 + h / 2) * u, x, (13 - h / 2) * u, ink) }
            "mk-grains" -> { ink.style = android.graphics.Paint.Style.FILL
                for ((x, y) in listOf(9f to 10f, 14f to 8.5f, 17f to 13f, 11f to 15.5f, 15.5f to 17.5f)) c.drawCircle(x * u, y * u, 1.5f * u, ink) }
            else -> c.drawCircle(r, r, 3.4f * u, ink)
        }
        return b
    }
}
