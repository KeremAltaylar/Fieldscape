package net.keremaltaylar.fieldscape

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** Design System/Tokens.md, as ios/Theme.swift has it (the web's own OKLCH values, to the digit). */
object T {
    val sunk = Color(0xFF0D1310); val ground = Color(0xFF19211D); val panel = Color(0xFF222B27); val raised = Color(0xFF2E3833); val hairline = Color(0xFF36403B)
    val ink = Color(0xFFE3E7E4); val dim = Color(0xFFB3B9B4); val faint = Color(0xFF9CA49D)
    val accent = Color(0xFFBBCEB5); val lamp = Color(0xFFBAE6B1)

    val display = FontFamily(Font(R.font.cormorant_garamond, FontWeight.SemiBold))
    val body = FontFamily(Font(R.font.newsreader, FontWeight.Normal), Font(R.font.newsreader, FontWeight.Medium))
    val mono = FontFamily(Font(R.font.courier_prime))

    val xs = 10.88.sp; val sm = 12.48.sp; val base = 17.sp; val md = 18.4.sp
    val s1 = 4.dp; val s2 = 8.dp; val s3 = 12.dp; val s4 = 16.dp; val s5 = 24.dp
    val target = 48.dp          // Android's touch target (Material), the M-4 floor here

    /** data scale (Tokens.md): a chord root's colour, hsl(pc x 30, 42%, 62%) - index.html rootColour */
    fun rootHex(pc: Int): String {
        val h = (((pc % 12) + 12) % 12) * 30.0; val sat = 0.42; val l = 0.62
        val c = (1 - Math.abs(2 * l - 1)) * sat; val x = c * (1 - Math.abs((h / 60) % 2 - 1)); val m = l - c / 2
        val (r, g, b) = when { h < 60 -> Triple(c, x, 0.0); h < 120 -> Triple(x, c, 0.0); h < 180 -> Triple(0.0, c, x)
                               h < 240 -> Triple(0.0, x, c); h < 300 -> Triple(x, 0.0, c); else -> Triple(c, 0.0, x) }
        return String.format("#%02x%02x%02x", Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255))
    }
    fun root(pc: Int) = Color(android.graphics.Color.parseColor(rootHex(pc)))
}
