package net.keremaltaylar.fieldscape

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.text.DateFormat
import java.time.Instant
import java.util.Date

/*
 * The listener's cards, as on iOS (ios/Cards.swift, the approved mock "Point card" / "Route card"):
 * opened by tapping a point or a route on the map, a point in the walk panel, or a route in Routes.
 * Both fit the sheet without scrolling. Tokens only; the chord colours are the Tokens.md data scale.
 */

@Composable private fun Chip(s: String) = Text(s, Modifier.border(1.dp, T.hairline, RoundedCornerShape(5.dp)).padding(horizontal = 6.dp, vertical = 5.dp),
    color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs, letterSpacing = (T.xs.value * 0.18).sp))
@Composable private fun Note(s: String, lines: Int = Int.MAX_VALUE) = Text(s, color = T.dim, maxLines = lines, overflow = TextOverflow.Ellipsis,
    style = TextStyle(fontFamily = T.body, fontSize = T.sm, lineHeight = 17.sp))
@Composable private fun Title(s: String) = Text(s, color = T.ink, maxLines = 2, style = TextStyle(fontFamily = T.display, fontSize = 24.sp))
private fun metres(d: Double) = if (d < 1000) String.format("%.0f m", d) else String.format("%.1f km", d / 1000)
private fun clock(s: Double) = String.format("%d:%02d", s.toInt() / 60, s.toInt() % 60)

@Composable
fun CardButton(title: String, primary: Boolean, modifier: Modifier = Modifier, glyph: String? = null, onClick: () -> Unit) {
    Box(modifier.heightIn(min = T.target).clip(RoundedCornerShape(10.dp)).background(if (primary) T.accent else T.raised)
        .border(1.dp, T.hairline, RoundedCornerShape(10.dp)).clickable(onClick = onClick).semantics { contentDescription = title },
        contentAlignment = Alignment.Center) {
        Text((glyph?.let { "$it  " } ?: "") + title, color = if (primary) T.sunk else T.ink, style = TextStyle(fontFamily = T.body, fontSize = 15.sp))
    }
}

@Composable
fun PointCard(walk: Walk, map: MapUi, id: String, close: () -> Unit, photos: () -> Unit) {
    val q = walk.pointInfo[id] ?: return
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Round("✕", "Close", close)
            Box(Modifier.size(12.dp).clip(CircleShape).background(if (q.mode == "silent") T.sunk else T.lamp)
                .then(if (q.mode == "silent") Modifier.border(1.5.dp, T.ink, CircleShape) else Modifier))
            Chip(q.mode.uppercase())
            Spacer(Modifier.weight(1f))
            walk.distance(id)?.let { Chip(metres(it) + " away") }
            val none = q.images.isEmpty()
            Box(Modifier.heightIn(min = T.target).widthIn(min = T.target).clip(RoundedCornerShape(50)).background(T.panel)
                .border(1.dp, T.hairline, RoundedCornerShape(50)).clickable(enabled = !none, onClick = photos)
                .semantics { contentDescription = if (none) "No photos yet" else "Photos, ${q.images.size}" }.padding(horizontal = T.s2),
                contentAlignment = Alignment.Center) {
                Text(if (none) "▣" else "▣ ${q.images.size}", color = if (none) T.faint else T.ink, style = TextStyle(fontFamily = T.mono, fontSize = 15.sp))
            }
        }
        Title(q.name)
        if (q.note.isNotEmpty()) Note(q.note, 3)
        if (q.path != null && q.duration > 0) {
            Eyebrow("The recording")
            val mine = walk.rawId == id
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s3)) {
                Box(Modifier.size(T.target).clip(CircleShape).background(T.raised).border(1.dp, T.hairline, CircleShape)
                    .clickable { walk.playRaw(id, q.path) }
                    .semantics { contentDescription = if (mine && walk.rawPlaying) "Pause the recording" else "Play the recording as it was made" },
                    contentAlignment = Alignment.Center) {
                    if (mine && walk.rawLoading) CircularProgressIndicator(Modifier.size(18.dp), color = T.ink, strokeWidth = 2.dp)
                    else Text(if (mine && walk.rawPlaying) "❚❚" else "▶", color = T.ink, style = TextStyle(fontSize = 15.sp))
                }
                Waveform(q.peaks, if (mine) walk.rawPosition / maxOf(q.duration, 1.0) else 0.0, Modifier.weight(1f).height(44.dp)) { if (mine) walk.seekRaw(it) }
                Text(clock(if (mine) walk.rawPosition else 0.0) + " / " + clock(q.duration), color = T.dim, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
            }
        }
        if (q.hits.isNotEmpty()) {
            Eyebrow("The recordings")
            q.hits.chunked(2).forEach { pair ->
                Row(horizontalArrangement = Arrangement.spacedBy(T.s2)) {
                    pair.forEach { (slot, name, path) -> HitButton(walk, "$id#$slot", slot, name, path, Modifier.weight(1f)) }
                    if (pair.size == 1) Spacer(Modifier.weight(1f))
                }
            }
        }
        Eyebrow("Where and when")
        Note(where(q))
        if (q.mode != "silent") {
            val on = walk.solo == id
            Note(if (on) "Listening to this point alone, from wherever you are. The rest waits."
                 else if (q.sounds) "Listen: this point alone, stretched, from wherever you are." else "Listen: this point's ${q.mode} alone, from wherever you are.")
            Row(horizontalArrangement = Arrangement.spacedBy(T.s2)) {
                CardButton(if (on) "Stop listening" else "Listen", !on, Modifier.weight(1f), if (on) "■" else "▶") { walk.listen(if (on) null else id) }
                CardButton("Zoom to", false, Modifier.width(110.dp)) { zoom(map, q) }
            }
        } else {
            Note("A quiet point: it speaks once as you step into its circle.")
            CardButton("Zoom to", false, Modifier.fillMaxWidth()) { zoom(map, q) }
        }
    }
}

/** One of a rhythm point's originals: its slot, its file, and a play button. */
@Composable
fun HitButton(walk: Walk, key: String, slot: String, name: String, path: String, modifier: Modifier) {
    val mine = walk.rawId == key; val on = mine && walk.rawPlaying
    val tidy = name.substringBeforeLast('.').substringAfterLast("__")      /* "50549__broumbroum__hit-low.wav" -> "hit-low" */
    Row(modifier.heightIn(min = T.target).clip(RoundedCornerShape(10.dp)).background(if (on) T.raised else T.sunk)
        .border(1.dp, if (on) T.accent else T.hairline, RoundedCornerShape(10.dp)).clickable { walk.playRaw(key, path) }
        .semantics { contentDescription = (if (on) "Pause " else "Play ") + slot + ", " + tidy }.padding(horizontal = T.s3, vertical = T.s1),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
        if (mine && walk.rawLoading) CircularProgressIndicator(Modifier.size(14.dp), color = T.ink, strokeWidth = 2.dp)
        else Text(if (on) "❚❚" else "▶", color = T.ink, style = TextStyle(fontSize = 12.sp))
        Column {
            Text(if (slot == "rand") "RANDOM" else slot.uppercase(), color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs, letterSpacing = (T.xs.value * 0.18).sp))
            Text(tidy, color = T.ink, maxLines = 1, overflow = TextOverflow.Ellipsis, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        }
    }
}

/** A point's photos, full screen, swiped sideways; a tap closes. */
@Composable
fun PhotoViewer(walk: Walk, id: String, close: () -> Unit) {
    val paths = walk.pointInfo[id]?.images ?: emptyList()
    val pager = androidx.compose.foundation.pager.rememberPagerState { paths.size }
    Box(Modifier.fillMaxSize().background(T.sunk).clickable(onClick = close)) {
        androidx.compose.foundation.pager.HorizontalPager(pager, Modifier.fillMaxSize()) { i ->
            val bmp = androidx.compose.runtime.produceState<android.graphics.Bitmap?>(null, paths[i]) {
                value = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    runCatching { android.graphics.BitmapFactory.decodeFile(walk.recording(paths[i], "img:" + paths[i]).path) }.getOrNull()
                }
            }.value
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                if (bmp != null) androidx.compose.foundation.Image(bmp.asImageBitmap(), "Photo ${i + 1} of ${paths.size}", Modifier.fillMaxWidth())
                else CircularProgressIndicator(color = T.ink)
            }
        }
        Row(Modifier.align(Alignment.TopEnd).statusBarsPadding().padding(T.s4), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            if (paths.size > 1) Text("${pager.currentPage + 1} / ${paths.size}", color = T.dim, style = TextStyle(fontFamily = T.mono, fontSize = T.sm))
            Round("✕", "Close the photos", close)
        }
    }
}

private fun zoom(map: MapUi, q: Walk.PointInfo) { val d = 0.0012; map.show(doubleArrayOf(q.lon - d, q.lat - d, q.lon + d, q.lat + d)) }
private fun where(q: Walk.PointInfo): String {
    val parts = mutableListOf(String.format("%.4f %s, %.4f %s", Math.abs(q.lat), if (q.lat >= 0) "N" else "S", Math.abs(q.lon), if (q.lon >= 0) "E" else "W"))
    q.recorded?.let { r -> runCatching { Date.from(Instant.parse(r)) }.getOrNull()?.let { parts += DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(it) } }
    if (q.duration > 0) parts += if (q.duration < 60) String.format("%.0f s", q.duration) else String.format("%d min %d s", q.duration.toInt() / 60, q.duration.toInt() % 60)
    return parts.joinToString(" · ")
}

/** The recording's peaks as bars, the played part in the accent; a tap seeks. */
@Composable
fun Waveform(peaks: DoubleArray, played: Double, modifier: Modifier, seek: (Double) -> Unit) {
    val top = maxOf(peaks.maxOrNull() ?: 1.0, 0.05)
    Canvas(modifier.pointerInput(Unit) { detectTapGestures { seek((it.x / size.width).toDouble().coerceIn(0.0, 1.0)) } }
        .semantics { contentDescription = String.format("The recording, %.0f percent played", played * 100) }) {
        val n = maxOf(1, minOf(peaks.size, (size.width / 3.dp.toPx()).toInt()))
        val w = size.width / n
        for (i in 0 until n) {
            val v = if (peaks.isEmpty()) 0.3 else peaks[minOf(peaks.size - 1, (i * peaks.size.toDouble() / n).toInt())]
            val h = maxOf(2.dp.toPx(), (v / top).toFloat() * size.height)
            drawRoundRect(if (i.toDouble() / n < played) T.accent else T.faint.copy(alpha = 0.55f), Offset(i * w, (size.height - h) / 2),
                Size(maxOf(1f, w - 1.dp.toPx()), h), CornerRadius(1.dp.toPx()))
        }
    }
}

@Composable
fun RouteCard(walk: Walk, map: MapUi, index: Int, close: () -> Unit) {
    val r = walk.routeList.getOrNull(index) ?: return
    val here = if (walk.playingRoute == index) walk.chordStep else -1
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Round("✕", "Close", close)
            Chip("ROUTE")
            Spacer(Modifier.weight(1f))
            Chip(r.length)
        }
        Title(r.name)
        if (r.note.isNotEmpty()) Note(r.note, 3)
        Eyebrow(if (here >= 0) "The progression — you are at ${here + 1} of ${r.prog.size}" else "The progression — ${r.prog.size} chords along the route")
        ChordStrip(r, here, Modifier.fillMaxWidth().height(28.dp))
        if (r.prog.isNotEmpty()) Row(Modifier.fillMaxWidth()) {
            Text(r.prog.first().second, color = T.dim, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
            Spacer(Modifier.weight(1f))
            if (here >= 0) Text(r.prog[here].second + " — now", color = T.ink, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
            Spacer(Modifier.weight(1f))
            Text(r.prog.last().second, color = T.dim, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Stat("Key", if (r.key2.isEmpty() || r.key2 == r.key) r.key else "${r.key} · ${r.key2}", Modifier.weight(1f))
            Stat("Tempo", "${r.tempo} bpm", Modifier.weight(1f))
            Stat("Sections", "${r.sections}", Modifier.weight(1f))
        }
        CardButton("Show whole route", false, Modifier.fillMaxWidth()) { map.show(r.box) }
    }
}

@Composable
private fun Stat(k: String, v: String, modifier: Modifier) {
    Column(modifier.clip(RoundedCornerShape(10.dp)).background(T.sunk).border(1.dp, T.hairline, RoundedCornerShape(10.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(T.s1)) {
        Eyebrow(k)
        Text(v, color = T.ink, maxLines = 1, style = TextStyle(fontFamily = T.body, fontSize = 18.sp))
    }
}
