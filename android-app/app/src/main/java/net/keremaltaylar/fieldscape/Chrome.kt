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
    var zones by mutableStateOf(false)
    var sections by mutableStateOf(false)
    /** a request to frame w, s, e, n; a new serial each time so the same place can be asked twice */
    var frame by mutableStateOf<Pair<DoubleArray, Long>?>(null)
    private var serial = 0L
    fun show(box: DoubleArray) { frame = box to ++serial }
}

enum class Sheet { Walk, Places, Layers, Account }


@Composable
fun TopBar(walk: Walk, onSheet: (Sheet) -> Unit) {
    Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = T.s4, vertical = T.s2),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
        Row(Modifier.heightIn(min = T.target).clip(RoundedCornerShape(50)).background(T.panel)
            .border(1.dp, T.hairline, RoundedCornerShape(50)).clickable { onSheet(Sheet.Places) }
            .semantics { contentDescription = "Places and routes" }.padding(horizontal = T.s4),
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

@Composable
fun PlacesSheet(walk: Walk, map: MapUi, onSheet: (Sheet) -> Unit) {
    var q by remember { mutableStateOf("") }
    val places = walk.placeList.filter { q.isEmpty() || it.name.contains(q, ignoreCase = true) }
    val routes = walk.routeList.filter { q.isEmpty() || it.name.contains(q, ignoreCase = true) }
    Column(verticalArrangement = Arrangement.spacedBy(T.s3)) {
        SheetTitle("Places") { onSheet(Sheet.Walk) }
        Box(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp)).background(T.sunk)
            .border(1.dp, T.hairline, RoundedCornerShape(10.dp)).padding(horizontal = T.s3), contentAlignment = Alignment.CenterStart) {
            if (q.isEmpty()) Text("Search parks and routes", color = T.faint, style = TextStyle(fontFamily = T.body, fontSize = 15.sp))
            BasicTextField(q, { q = it }, Modifier.fillMaxWidth().semantics { contentDescription = "Search parks and routes" },
                textStyle = TextStyle(fontFamily = T.body, fontSize = 15.sp, color = T.ink), cursorBrush = SolidColor(T.ink), singleLine = true)
        }
        LazyColumn(Modifier.heightIn(max = 420.dp), verticalArrangement = Arrangement.spacedBy(T.s1)) {
            if (places.isEmpty() && routes.isEmpty()) item {
                Text("Nothing matches “$q”.", color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            }
            if (places.isNotEmpty()) item { Eyebrow("Parks") }
            items(places) { p ->
                Column(Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(10.dp))
                    .background(if (p.name == walk.place) T.raised else T.panel)
                    .clickable { map.show(p.box); onSheet(Sheet.Walk) }.padding(horizontal = T.s3, vertical = T.s2),
                    verticalArrangement = Arrangement.Center) {
                    Text(p.name, color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = 17.sp))
                    Text(p.detail, color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
                }
            }
            if (routes.isNotEmpty()) item { Box(Modifier.padding(top = T.s2)) { Eyebrow("Routes") } }
            items(routes) { r ->
                Row(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp))
                    .clickable { map.show(r.box); onSheet(Sheet.Walk) }.semantics { contentDescription = "${r.name}, ${r.length}" }
                    .padding(horizontal = T.s3), verticalAlignment = Alignment.CenterVertically) {
                    Text(r.name, Modifier.weight(1f), color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = 16.sp))
                    Text(r.length, color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = 11.5.sp))
                }
            }
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
        Text("Signing in as a setter comes in a coming test: placing points, recording and shaping the sound.",
            color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        Eyebrow("About")
        Text("Fieldscape · Test $TEST_BUILD · recordings © their authors · map imagery © Esri, © OpenStreetMap contributors, © OpenTopoMap",
            color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
        Eyebrow("Developer")
        Text(engineLine, color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs))
    }
}

/** The test number of this build (docs/TESTS.md), shown in Account. */
const val TEST_BUILD = 5

