package net.keremaltaylar.fieldscape

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/*
 * The app's frame around the map, as on iOS (ios/Chrome.swift, the approved mock design/app/gen_mock.py):
 * the top bar - place picker, layers, account - and the sheets it opens: Places (search, parks, routes)
 * and Layers (base map, park boundary, zones, sections, fit all). Tokens only (Theme.kt), targets >= 44 dp.
 */

/** What the map shows and where it should go; the sheets write it, the map reads it. */
class MapUi {
    enum class Base(val label: String) { Map("Map"), Topo("Topo"), Satellite("Satellite"), Virtual("Virtual") }
    var base by mutableStateOf(Base.Satellite)
    var boundary by mutableStateOf(true)
    /* on from the start: a listener should see where the zones and sections are (Kerem, 2026-09-26) */
    var zones by mutableStateOf(true)
    var sections by mutableStateOf(true)
    /** a request to frame w, s, e, n; a new serial each time so the same place can be asked twice */
    var frame by mutableStateOf<Pair<DoubleArray, Long>?>(null)
    private var serial = 0L
    fun show(box: DoubleArray) { frame = box to ++serial }
}

sealed interface Sheet {
    data object Walk : Sheet; data object Places : Sheet; data object Layers : Sheet; data object Account : Sheet
    data class Point(val id: String) : Sheet
    data class Route(val index: Int) : Sheet
}


@Composable
fun TopBar(walk: Walk, onSheet: (Sheet) -> Unit) {
    Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = T.s4, vertical = T.s2),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
        Row(Modifier.heightIn(min = T.target).clip(RoundedCornerShape(50)).background(T.panel)
            .border(1.dp, T.hairline, RoundedCornerShape(50)).clickable { onSheet(Sheet.Places) }
            .semantics { contentDescription = "Routes" }.padding(horizontal = T.s4),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Text(walk.place ?: "Open world", color = T.ink, style = TextStyle(fontFamily = T.display, fontSize = 20.sp), maxLines = 1)
            Text("▾", color = T.faint)
        }
        Spacer(Modifier.weight(1f))
        Round("≋", "Map layers") { onSheet(Sheet.Layers) }
        Round("◯", "Account") { onSheet(Sheet.Account) }
    }
}

@Composable
fun Round(glyph: String, label: String, onClick: () -> Unit) {
    Box(Modifier.size(T.target).clip(CircleShape).background(T.panel).border(1.dp, T.hairline, CircleShape)
        .clickable(onClick = onClick).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        Text(glyph, color = T.ink, style = TextStyle(fontSize = 18.sp))
    }
}

@Composable
fun <V> Segmented(items: List<Pair<V, String>>, value: V, onPick: (V) -> Unit, modifier: Modifier = Modifier) {
    Row(modifier.clip(RoundedCornerShape(11.dp)).background(T.sunk).border(1.dp, T.hairline, RoundedCornerShape(11.dp)).padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        items.forEach { (v, name) ->
            val on = v == value
            Box(Modifier.weight(1f).heightIn(min = 40.dp).clip(RoundedCornerShape(8.dp)).background(if (on) T.raised else T.sunk)
                .clickable { onPick(v) }.semantics { selected = on }, contentAlignment = Alignment.Center) {
                Text(name, color = if (on) T.ink else T.dim, style = TextStyle(fontFamily = T.body, fontSize = 14.sp))
            }
        }
    }
}

@Composable
fun Toggle(name: String, on: Boolean, onClick: () -> Unit) {
    Box(Modifier.heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp)).background(if (on) T.raised else T.sunk)
        .border(1.dp, T.hairline, RoundedCornerShape(10.dp)).clickable(onClick = onClick).semantics { selected = on }
        .padding(horizontal = T.s3), contentAlignment = Alignment.Center) {
        Text(name, color = if (on) T.ink else T.dim, style = TextStyle(fontFamily = T.body, fontSize = 14.sp))
    }
}

@Composable
fun Eyebrow(s: String) = Text(s.uppercase(), color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs, letterSpacing = (T.xs.value * 0.28).sp))

@Composable
fun SheetTitle(title: String, onClose: () -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(title, Modifier.weight(1f), color = T.ink, style = TextStyle(fontFamily = T.display, fontSize = 24.sp))
        Round("✕", "Close", onClose)
    }
}

/** The routes a setter published; the map already opens on the open world, so this is the only list. */
@Composable
fun PlacesSheet(walk: Walk, map: MapUi, onSheet: (Sheet) -> Unit) {
    var q by remember { mutableStateOf("") }
    val routes = walk.routeList.filter { q.isEmpty() || it.name.contains(q, ignoreCase = true) }
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        SheetTitle("Routes") { onSheet(Sheet.Walk) }
        if (walk.routeList.size > 6) Box(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp)).background(T.sunk)
            .border(1.dp, T.hairline, RoundedCornerShape(10.dp)).padding(horizontal = T.s3), contentAlignment = Alignment.CenterStart) {
            if (q.isEmpty()) Text("Search routes", color = T.faint, style = TextStyle(fontFamily = T.body, fontSize = 15.sp))
            BasicTextField(q, { q = it }, Modifier.fillMaxWidth().semantics { contentDescription = "Search routes" },
                textStyle = TextStyle(fontFamily = T.body, fontSize = 15.sp, color = T.ink), cursorBrush = SolidColor(T.ink), singleLine = true)
        }
        if (walk.routeList.isEmpty()) Text("No routes are published yet. Walk anywhere: the points sound wherever you are.",
            color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        else if (routes.isEmpty()) Text("Nothing matches “$q”.", color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        LazyColumn(Modifier.heightIn(max = 420.dp), verticalArrangement = Arrangement.spacedBy(T.s1)) {
            items(routes) { r ->
                Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(10.dp))
                    .background(if (r.index == walk.playingRoute) T.raised else T.panel)
                    .clickable { map.show(r.box); onSheet(Sheet.Route(r.index)) }.semantics { contentDescription = "${r.name}, ${r.length}" }
                    .padding(horizontal = T.s3), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s3)) {
                    ChordStrip(r, -1, Modifier.width(44.dp).height(10.dp))
                    Text(r.name, Modifier.weight(1f), color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = 17.sp))
                    Text(r.length, color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
                }
            }
        }
    }
}

/** A route's progression as coloured cells, one per chord, the playing one lit. */
@Composable
fun ChordStrip(r: Walk.RouteInfo, active: Int, modifier: Modifier = Modifier) {
    Row(modifier, horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        r.prog.forEachIndexed { i, (pc, _) ->
            Box(Modifier.weight(1f).fillMaxHeight().clip(RoundedCornerShape(3.dp))
                .background(T.root(pc).copy(alpha = if (active < 0 || i == active) 1f else 0.45f))
                .then(if (i == active) Modifier.border(2.dp, T.ink, RoundedCornerShape(3.dp)) else Modifier))
        }
    }
}

@Composable
fun LayersSheet(walk: Walk, map: MapUi, onSheet: (Sheet) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        SheetTitle("Map") { onSheet(Sheet.Walk) }
        Eyebrow("Base map")
        Segmented(MapUi.Base.entries.map { it to it.label }, map.base, { map.base = it }, Modifier.fillMaxWidth())
        Eyebrow("Park boundary")
        Row(horizontalArrangement = Arrangement.spacedBy(T.s2), verticalAlignment = Alignment.CenterVertically) {
            Segmented(listOf(false to "Off", true to "Frame"), map.boundary, { map.boundary = it }, Modifier.weight(1f))
            Box(Modifier.width(96.dp).heightIn(min = T.target).clip(RoundedCornerShape(10.dp)).background(T.raised)
                .border(1.dp, T.hairline, RoundedCornerShape(10.dp)).clickable { walk.allBounds?.let { map.show(it) } },
                contentAlignment = Alignment.Center) {
                Text("Fit all", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = 15.sp))
            }
        }
        Eyebrow("Show")
        Row(horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Toggle("Zones", map.zones) { map.zones = !map.zones }
            Toggle("Sections", map.sections) { map.sections = !map.sections }
        }
    }
}

@Composable
fun AccountSheet(onSheet: (Sheet) -> Unit, engineLine: String) {
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        SheetTitle("Account") { onSheet(Sheet.Walk) }
        Eyebrow("Setter")
        Text("The app is for listening. Setters place points, record and shape the sound on the Fieldscape website.",
            color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        Eyebrow("About")
        Text("Fieldscape · Test $TEST_BUILD · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors, © OpenTopoMap",
            color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        Eyebrow("Developer")
        Text(engineLine, color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs))
    }
}

/** The test number of this build (docs/TESTS.md), shown in Account. */
const val TEST_BUILD = 10

