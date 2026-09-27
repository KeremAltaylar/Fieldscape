package net.keremaltaylar.fieldscape

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import org.json.JSONObject
import org.maplibre.android.MapLibre
import org.maplibre.android.camera.CameraUpdateFactory
import org.maplibre.android.geometry.LatLng
import org.maplibre.android.geometry.LatLngBounds
import org.maplibre.android.maps.MapView
import org.maplibre.android.maps.Style
import org.maplibre.android.style.layers.Property
import org.maplibre.android.style.layers.PropertyFactory
import org.maplibre.android.style.sources.GeoJsonSource
import java.io.File

/** The map fills the screen; the walk panel lies over its foot and hugs its content, as on iOS. */
class MainActivity : ComponentActivity() {
    private lateinit var walk: Walk
    private val mapUi = MapUi()
    private var sheet by mutableStateOf<Sheet>(Sheet.Walk)
    /** the panel folded down to a bar, so the map has the screen; a card pops it back up */
    private var collapsed by mutableStateOf(false)
    private var photos by mutableStateOf<String?>(null)
    private fun open(s: Sheet) { sheet = s; if (s != Sheet.Walk) collapsed = false }
    private fun closeCard() { sheet = Sheet.Walk; collapsed = true }
    private var features by mutableStateOf<JSONObject?>(null)
    private var failed by mutableStateOf<String?>(null)
    /** Why the sound is paused, when it is; the panel offers Resume (rulebook M-6). */
    private var paused by mutableStateOf<String?>(null)
    /** The Sound / Stop button: fades the output (2 s in, 1.5 s out, as the web), then pauses the stream. */
    private var soundOn by mutableStateOf(true)
    private val main = android.os.Handler(android.os.Looper.getMainLooper())
    private fun setSound(on: Boolean) {
        soundOn = on
        main.removeCallbacksAndMessages(null)
        if (on) { if (paused == null) Core.resume(); Core.master(1f, 2f) }
        else { Core.master(0f, 1.5f); main.postDelayed({ if (!soundOn) Core.pause() }, 1600) }
    }
    private lateinit var focus: AudioFocusRequest

    /* A call or another player takes the audio: pause, and come back by ourselves when it ends.
       Headphones pulled out: pause and wait for Resume, rather than playing out loud in the street. */
    private val onFocus = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_LOSS, AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> { Core.pause(); if (paused == null) paused = "Another sound took over (a call or another app)." }
            AudioManager.AUDIOFOCUS_GAIN -> if (paused?.startsWith("Another") == true) { paused = null; Core.resume() }
        }
    }
    private val noisy = object : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) { Core.pause(); paused = "Headphones were unplugged." }
    }

    private val ask = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { r ->
        if (Manifest.permission.ACCESS_FINE_LOCATION !in r) return@registerForActivityResult     // only the notification was asked
        if (r[Manifest.permission.ACCESS_FINE_LOCATION] == true || r[Manifest.permission.ACCESS_COARSE_LOCATION] == true) { walk.listen(); WalkService.start(this) }
        else walk.denied()
    }

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        MapLibre.getInstance(this)
        walk = Walk(this, Core.start())
        focus = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(onFocus).build()
        getSystemService(AudioManager::class.java).requestAudioFocus(focus)
        registerReceiver(noisy, IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY), RECEIVER_EXPORTED)
        Thread {
            runCatching { Supa.published() }
                .onSuccess { f -> runOnUiThread { features = f; walk.start(f); locate() } }
                .onFailure { e -> runOnUiThread { failed = "The map could not load: ${e.message}. Check the connection and reopen the app." } }
        }.start()
        setContent { Screen() }
    }

    override fun onDestroy() { WalkService.stop(this); super.onDestroy() }

    private fun locate() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
            walk.listen(); WalkService.start(this)                            // 5.8: on with the screen locked
            if (android.os.Build.VERSION.SDK_INT >= 33) ask.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
        } else ask.launch(buildList {
            add(Manifest.permission.ACCESS_FINE_LOCATION); add(Manifest.permission.ACCESS_COARSE_LOCATION)
            if (android.os.Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)          // the walk's notification
        }.toTypedArray())
    }

    @Composable
    private fun Screen() {
        var developer by remember { mutableStateOf(false) }
        Box(Modifier.fillMaxSize().background(T.ground)) {
            features?.let { Map(it) } ?: Text(failed ?: "Loading the map…", Modifier.align(Alignment.Center), color = T.dim,
                                              style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(T.s3)) {
            /* the morph cells sit on the map just above the panel, and ride up and down with it */
            if (walk.cells && walk.playingRoute >= 0 && sheet == Sheet.Walk) Box(Modifier.padding(start = T.s4)) { CellsView(walk.playingRoute) }
            Column(Modifier.fillMaxWidth()
                .clip(RoundedCornerShape(topStart = 20.dp_, topEnd = 20.dp_)).background(T.panel)
                .navigationBarsPadding().padding(start = T.s4, end = T.s4, bottom = T.s5),
                verticalArrangement = Arrangement.spacedBy(T.s3)) {
                /* the grip folds the panel away and brings it back */
                Box(Modifier.fillMaxWidth().height(T.target).clickable { collapsed = !collapsed }
                    .semantics { contentDescription = if (collapsed) "Show the panel" else "Hide the panel" }, contentAlignment = Alignment.Center) {
                    Box(Modifier.size(38.dp_, 4.dp_).clip(CircleShape).background(T.hairline))
                }
                if (collapsed) CollapsedBar() else {
                paused?.let { why ->
                    Note("Paused. $why")
                    Box(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp_)).background(T.raised)
                        .border(1.dp_, T.hairline, RoundedCornerShape(10.dp_))
                        .clickable { paused = null; getSystemService(AudioManager::class.java).requestAudioFocus(focus); Core.resume() },
                        contentAlignment = Alignment.Center) {
                        Text("Resume", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
                    }
                }
                when (val s = sheet) {
                    Sheet.Places -> PlacesSheet(walk, mapUi) { sheet = it }
                    Sheet.Layers -> LayersSheet(walk, mapUi) { sheet = it }
                    Sheet.Account -> AccountSheet({ sheet = it }, engineLine())
                    is Sheet.Point -> PointCard(walk, mapUi, s.id, ::closeCard) { photos = s.id }
                    is Sheet.Route -> RouteCard(walk, mapUi, s.index, ::closeCard)
                    Sheet.Walk -> {
                        Panel(onLongPress = { developer = !developer })
                        if (developer) Text(engineLine(), color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs))
                    }
                }
                }
            }
            }
            if (features != null) TopBar(walk) { open(it) }
            photos?.let { PhotoViewer(walk, it) { photos = null } }
        }
        /* leaving a point's card stops its player (Listen stays until let go: it is the walk's state) */
        LaunchedEffect(sheet) { val s = sheet; if (walk.rawId != null && !(s is Sheet.Point && walk.rawId!!.startsWith(s.id))) walk.stopRaw() }
    }

    /** The panel folded down: where you are, the sound, and a way back up. */
    @Composable
    private fun CollapsedBar() {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s3)) {
            Column(Modifier.weight(1f).heightIn(min = T.target).clickable { collapsed = false }
                .semantics { contentDescription = "Show the panel" }, verticalArrangement = Arrangement.Center) {
                Text(walk.solo?.let { walk.pointInfo[it]?.name }?.let { "Listening to $it" } ?: walk.place ?: "Fieldscape",
                     color = T.ink, maxLines = 1, style = TextStyle(fontFamily = T.display, fontSize = T.md))
                walk.chord?.let { Text(it, color = T.dim, maxLines = 1, style = TextStyle(fontFamily = T.body, fontSize = T.sm)) }
            }
            Box(Modifier.heightIn(min = T.target).widthIn(min = 64.dp_).clip(RoundedCornerShape(10.dp_)).background(T.raised)
                .border(1.dp_, T.hairline, RoundedCornerShape(10.dp_)).clickable { setSound(!soundOn) }.padding(horizontal = T.s3),
                contentAlignment = Alignment.Center) {
                Text(if (soundOn) "Stop" else "Sound", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            }
        }
    }

    private fun engineLine() = String.format("Fieldscape · Test %d · engine: worst %.2f ms per burst of %d frames · xruns %d · out %.1f dBFS",
        TEST_BUILD, Core.worstMs(), Core.bufferFrames(), Core.xruns(), Core.outputDb())

    @Composable
    private fun Panel(onLongPress: () -> Unit) {
        val mode = walk.mode
        /* the developer long-press lives on the place name alone, clear of the buttons */
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Text(if (mode == Walk.Mode.Denied) "Location is off" else walk.place ?: "Fieldscape",
                 Modifier.weight(1f).pointerInput(Unit) { detectTapGestures(onLongPress = { onLongPress() }) },
                 color = T.ink, style = TextStyle(fontFamily = T.display, fontSize = T.md), maxLines = 1)
            Box(Modifier.heightIn(min = T.target).widthIn(min = 64.dp_).clip(RoundedCornerShape(10.dp_)).background(T.raised)
                .border(1.dp_, T.hairline, RoundedCornerShape(10.dp_)).clickable { setSound(!soundOn) }.padding(horizontal = T.s3),
                contentAlignment = Alignment.Center) {
                Text(if (soundOn) "Stop" else "Sound", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            }
            when (mode) {
                is Walk.Mode.Live -> Chip(String.format("±%.0f m", mode.accuracy))
                is Walk.Mode.Holding -> Chip(String.format("±%.0f m · HOLDING", mode.accuracy))
                Walk.Mode.ByHand -> Chip("BY HAND")
                else -> {}
            }
        }
        when (mode) {
            Walk.Mode.Denied -> {
                Note("Fieldscape plays the recordings placed around you, so it needs to know where you are while the app is open.")
                Box(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp_)).background(T.raised)
                    .border(1.dp_, T.hairline, RoundedCornerShape(10.dp_))
                    .clickable { startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null))) },
                    contentAlignment = Alignment.Center) {
                    Text("Open Settings", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
                }
                Note("Settings → Apps → Fieldscape → Permissions → Location → Allow only while using the app")
            }
            Walk.Mode.Waiting -> Note("Finding where you are… Or press and hold the map to put yourself there, then drag to walk.")
            else -> Hearing()
        }
        walk.failure?.let { Note(it) }
    }

    @Composable
    private fun Hearing() {
        val rows = walk.rows
        /* the route's own sound and the rhythm points in reach (the piece) */
        if (walk.route != null || walk.rhythms.isNotEmpty()) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
            Text(buildAnnotatedString {
                walk.route?.let { append("Route "); withStyle(SpanStyle(color = T.ink)) { append(it) } }
                walk.chord?.let { append(" · $it") }
                if (walk.rhythms.isNotEmpty()) {
                    append(if (walk.route == null) "Rhythm " else " · rhythm ")
                    withStyle(SpanStyle(color = T.ink)) { append(walk.rhythms) }
                }
            }, Modifier.weight(1f), color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            /* the morph cells over the map: one cell per morph, breathing on its own clock */
            if (walk.route != null) Box(Modifier.heightIn(min = T.target).clip(RoundedCornerShape(10.dp_))
                .background(if (walk.cells) T.raised else Color.Transparent).border(1.dp_, T.hairline, RoundedCornerShape(10.dp_))
                .semantics { contentDescription = "Morph cells" }.toggleable(walk.cells) { walk.cells = it }
                .padding(horizontal = T.s3), contentAlignment = Alignment.Center) {
                Text("Cells", color = if (walk.cells) T.ink else T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            }
        }
        walk.solo?.let { walk.pointInfo[it] }?.let { q ->
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(T.s2)) {
                Text(buildAnnotatedString { append("Listening to "); withStyle(SpanStyle(color = T.ink)) { append(q.name) }; append(" alone.") },
                     Modifier.weight(1f), color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
                CardButton("Everything", false, Modifier.width(110.dp_)) { walk.listen(null) }
            }
        }
        if (rows.isEmpty()) {
            val n = walk.nearest
            if (n != null) Text(buildAnnotatedString {
                withStyle(SpanStyle(color = T.ink)) { append("Nothing in range here. ") }
                append("The nearest recording is ")
                withStyle(SpanStyle(color = T.ink)) { append(n.name) }
                append(", ")
                withStyle(SpanStyle(fontFamily = T.mono)) { append(if (n.dist < 1000) String.format("%.0f m", n.dist) else String.format("%.1f km", n.dist / 1000)) }
                append(" ${n.direction}.")
            }, color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            else Note("No recordings are published yet.")
        } else {
            Column {
                rows.forEachIndexed { i, r ->
                    if (i > 0) Box(Modifier.fillMaxWidth().height(1.dp_).background(T.hairline))
                    Box(Modifier.clickable { open(Sheet.Point(r.id)) }) { RowView(r) }
                }
            }
            walk.nearest?.let { n -> if (walk.solo == null) Text(buildAnnotatedString {
                withStyle(SpanStyle(color = T.ink)) { append("Nothing in range here. ") }; append("Walk toward ${n.name}, ${n.direction}.") },
                color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm)) }
            if (rows.any { it.phase is Walk.Phase.Downloading }) Note("First time here: each recording downloads once, then plays offline.")
        }
        if (walk.mode == Walk.Mode.ByHand) {
            Note("Listening from where you put yourself. Drag your dot, or press and hold anywhere, to walk.")
            Box(Modifier.fillMaxWidth().heightIn(min = T.target).clip(RoundedCornerShape(10.dp_)).background(T.raised)
                .border(1.dp_, T.hairline, RoundedCornerShape(10.dp_)).clickable { walk.useLocation() },
                contentAlignment = Alignment.Center) {
                Text("Use my location", color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.sm))
            }
        }
        if (walk.mode is Walk.Mode.Holding) Note("GPS is too rough here, so the sound holds where you last were until the fix is better than 40 m.")
    }

    @Composable
    private fun RowView(r: Walk.Row) {
        val (detail, fill, color) = when (val p = r.phase) {
            Walk.Phase.Playing -> Triple(if (r.dist < 1000) String.format("%.0f m", r.dist) else String.format("%.1f km", r.dist / 1000), r.level.coerceIn(0.0, 1.0), T.accent)
            Walk.Phase.Decoding -> Triple("Preparing", 1.0, T.faint.copy(alpha = 0.6f))
            is Walk.Phase.Downloading -> Triple(if (p.bytes > 0) String.format("Downloading · %.1f MB", p.bytes / 1048576.0) else "Downloading",
                                                p.fraction, T.faint.copy(alpha = 0.6f))
        }
        Column(Modifier.fillMaxWidth().heightIn(min = T.target).padding(vertical = T.s2), verticalArrangement = Arrangement.spacedBy(T.s1)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(9.dp_).clip(CircleShape).background(T.lamp))
                Spacer(Modifier.width(T.s2))
                Text(r.name, Modifier.weight(1f), color = T.ink, style = TextStyle(fontFamily = T.body, fontSize = T.base), maxLines = 1)
                Text(detail, color = T.dim, style = TextStyle(fontFamily = T.mono, fontSize = T.sm))
                Text("  ›", color = T.faint, style = TextStyle(fontFamily = T.body, fontSize = T.base))
            }
            Box(Modifier.fillMaxWidth().height(5.dp_).clip(CircleShape).background(T.raised)) {
                Box(Modifier.fillMaxWidth(fill.toFloat()).fillMaxHeight().clip(CircleShape).background(color))
            }
        }
    }

    @Composable private fun Chip(s: String) = Text(s, Modifier.border(1.dp_, T.hairline, RoundedCornerShape(5.dp_)).padding(horizontal = 6.dp_, vertical = 4.dp_),
        color = T.faint, style = TextStyle(fontFamily = T.mono, fontSize = T.xs))
    @Composable private fun Note(s: String) = Text(s, color = T.dim, style = TextStyle(fontFamily = T.body, fontSize = T.sm))

    /** MapLibre with the web's style (index.html "Map"), the published features inline, and the walker. */
    @Composable
    private fun Map(fc: JSONObject) {
        val here = walk.here
        val m = mapUi; val base = m.base; val boundary = m.boundary; val zones = m.zones; val sections = m.sections; val frame = m.frame
        val chord = walk.chord                                        /* re-read the sections as the walker crosses them */
        val playing = walk.playingRoute; val step = walk.chordStep; val routes = walk.routeList
        val state = remember { object { var style: Style? = null; var map: org.maplibre.android.maps.MapLibreMap? = null; var centredAt: Pair<Double, Double>? = null
                                        var framed = 0L; var zonesKey = ""; var sectionsKey = ""; var progKey = ""; var dragging = false } }
        AndroidView(factory = { ctx ->
            MapView(ctx).apply {
                onCreate(null); onStart(); onResume()
                getMapAsync { map ->
                    state.map = map
                    map.uiSettings.isLogoEnabled = false
                    map.uiSettings.attributionGravity = android.view.Gravity.TOP or android.view.Gravity.START
                    map.uiSettings.setAttributionTintColor(android.graphics.Color.rgb(0x9c, 0xa4, 0x9d))   // T.faint
                    /* A tap opens what is under it: a point's card, a route's card; on nothing it closes a card.
                       The walker moves by being dragged (Kerem, 2026-09-26: "move the listener by clicking and
                       dragging it, this will allow us to click points and routes"). */
                    map.addOnMapClickListener { ll ->
                        val p = map.projection.toScreenLocation(ll); val r = 24 * resources.displayMetrics.density
                        val box = android.graphics.RectF(p.x - r, p.y - r, p.x + r, p.y + r)
                        val pt = map.queryRenderedFeatures(box, "point-dot").firstOrNull()?.getStringProperty("id")
                        val rt = map.queryRenderedFeatures(box, "prog-seg", "route-line").firstNotNullOfOrNull { f ->
                            if (f.hasProperty("route")) f.getNumberProperty("route").toInt()
                            else f.getStringProperty("name")?.let { n -> walk.routeList.firstOrNull { it.name == n }?.index } }
                        when {
                            pt != null -> open(Sheet.Point(pt))
                            rt != null -> open(Sheet.Route(rt))
                            sheet is Sheet.Point || sheet is Sheet.Route -> sheet = Sheet.Walk
                        }
                        true
                    }
                    /* a drag that starts on the walker's dot moves it at once; a press and hold anywhere puts it
                       there and keeps dragging; the map holds still meanwhile */
                    val hold = android.view.GestureDetector(context, object : android.view.GestureDetector.SimpleOnGestureListener() {
                        override fun onLongPress(e: android.view.MotionEvent) {
                            state.dragging = true
                            val ll = map.projection.fromScreenLocation(android.graphics.PointF(e.x, e.y))
                            walk.walkBy(ll.longitude, ll.latitude)
                            val cancel = android.view.MotionEvent.obtain(e).apply { action = android.view.MotionEvent.ACTION_CANCEL }
                            this@apply.onTouchEvent(cancel); cancel.recycle()
                        }
                    })
                    var last: Pair<Double, Double>? = null
                    setOnTouchListener { _, e ->
                        if (e.actionMasked == android.view.MotionEvent.ACTION_DOWN) {
                            state.dragging = walk.here?.let { h ->
                                val p = map.projection.toScreenLocation(LatLng(h.second, h.first))
                                Math.hypot((p.x - e.x).toDouble(), (p.y - e.y).toDouble()) < 36 * resources.displayMetrics.density
                            } ?: false
                            last = null
                        }
                        if (!state.dragging) hold.onTouchEvent(e)
                        if (state.dragging && e.actionMasked == android.view.MotionEvent.ACTION_MOVE) {
                            val ll = map.projection.fromScreenLocation(android.graphics.PointF(e.x, e.y))
                            /* a fix every couple of metres, as GPS would give */
                            if (last?.let { Core.geoDistance(it.first, it.second, ll.longitude, ll.latitude) < 2 } != true) {
                                last = ll.longitude to ll.latitude; walk.walkBy(ll.longitude, ll.latitude)
                            }
                        }
                        val was = state.dragging
                        if (e.actionMasked == android.view.MotionEvent.ACTION_UP || e.actionMasked == android.view.MotionEvent.ACTION_CANCEL) state.dragging = false
                        was
                    }
                    map.setStyle(Style.Builder().fromJson(styleJson(fc))) { style ->
                        state.style = style
                        bounds(fc)?.let { map.moveCamera(CameraUpdateFactory.newLatLngBounds(it, 80)) }
                    }
                }
            }
        }, Modifier.fillMaxSize(), update = {
            val style = state.style ?: return@AndroidView
            fun show(id: String, on: Boolean) { style.getLayer(id)?.setProperties(PropertyFactory.visibility(if (on) Property.VISIBLE else Property.NONE)) }
            show("base-osm", base == MapUi.Base.Map); show("base-topo", base == MapUi.Base.Topo); show("base-sat", base == MapUi.Base.Satellite)
            show("park-line", boundary)
            for (id in listOf("zones-fill", "zones-line")) show(id, zones)
            for (id in listOf("sections-fill", "sections-line")) show(id, sections)
            /* zones: the one you stand in lit (drawZones) */
            if (zones) {
                val zs = walk.zoneCircles
                val inside = zs.map { z -> here?.let { Core.geoDistance(it.first, it.second, z[0], z[1]) <= z[2] } ?: false }
                val key = "${zs.size} " + inside.joinToString("") { if (it) "1" else "0" }
                if (key != state.zonesKey) {
                    state.zonesKey = key
                    (style.getSource("zones") as? GeoJsonSource)?.setGeoJson(shape(zs.indices.map { i -> circle(zs[i][0], zs[i][1], zs[i][2]) to JSONObject().put("inside", if (inside[i]) 1 else 0) }))
                }
            }
            /* sections: each in its mode's root colour from the playing route (drawSectors), the one underfoot lit */
            if (sections) {
                val (cells, active) = walk.sectionCells()
                val roots = routes.getOrNull(playing)?.sectors ?: routes.firstOrNull()?.sectors ?: emptyList()
                val key = "${cells.size} $active $playing ${cells.firstOrNull()?.firstOrNull()?.toList()} $chord"
                if (key != state.sectionsKey) {
                    state.sectionsKey = key
                    (style.getSource("sections") as? GeoJsonSource)?.setGeoJson(shape(cells.indices.map { i -> cells[i] to JSONObject()
                        .put("active", if (i == active) 1 else 0).put("colour", if (roots.isEmpty()) "#bae6b1" else T.rootHex(roots[i % roots.size])) }))
                }
            }
            /* the progression along every route, the chord playing lit (drawProgSegments) */
            val pk = "${routes.size} $playing $step"
            if (pk != state.progKey) {
                state.progKey = pk
                val fs = org.json.JSONArray()
                for (r in routes) for ((k, c) in r.prog.withIndex()) {
                    val n = r.prog.size.toDouble()
                    val line = org.json.JSONArray(); segment(r.coords, k / n, (k + 1) / n).forEach { line.put(org.json.JSONArray().put(it[0]).put(it[1])) }
                    fs.put(JSONObject().put("type", "Feature").put("geometry", JSONObject().put("type", "LineString").put("coordinates", line))
                        .put("properties", JSONObject().put("route", r.index).put("step", k).put("colour", T.rootHex(c.first))
                            .put("active", if (r.index == playing && k == step) 1 else 0)))
                }
                (style.getSource("progseg") as? GeoJsonSource)?.setGeoJson(JSONObject().put("type", "FeatureCollection").put("features", fs).toString())
            }
            if (frame != null && frame.second != state.framed) {    // Places / Fit all: frame it, and keep the walker's re-centring quiet
                state.framed = frame.second
                val (w, so, e, n) = frame.first.toList()
                state.map?.animateCamera(CameraUpdateFactory.newLatLngBounds(LatLngBounds.Builder().include(LatLng(so, w)).include(LatLng(n, e)).build(), 120))
            }
            here?.let { (lon, lat) ->
                (style.getSource("me") as? GeoJsonSource)?.setGeoJson("""{"type":"Point","coordinates":[$lon,$lat]}""")
                /* Street level on the walker; again if a fix jumps far (a stale first fix, or the emulator's
                   default position, would otherwise keep the map in the wrong city). Never for a walk by hand:
                   the tap is already on screen, and a framed place must stay framed while the fixes arrive. */
                val c = state.centredAt
                if (walk.mode != Walk.Mode.ByHand && (c == null || Core.geoDistance(c.first, c.second, lon, lat) > 1000))
                    state.map?.animateCamera(CameraUpdateFactory.newLatLngZoom(LatLng(lat, lon), 15.5))
                if (walk.mode != Walk.Mode.ByHand) state.centredAt = lon to lat
            }
        })
    }

    private fun styleJson(fc: JSONObject) = """
    {"version":8,"sources":{
      "base-sat":{"type":"raster","tileSize":256,"maxzoom":19,"attribution":"Imagery © Esri",
                  "tiles":["https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"]},
      "base-osm":{"type":"raster","tileSize":256,"maxzoom":19,"attribution":"© OpenStreetMap contributors",
                  "tiles":["https://tile.openstreetmap.org/{z}/{x}/{y}.png"]},
      "base-topo":{"type":"raster","tileSize":256,"maxzoom":17,"attribution":"© OpenStreetMap contributors, SRTM · © OpenTopoMap (CC-BY-SA)",
                  "tiles":["https://a.tile.opentopomap.org/{z}/{x}/{y}.png"]},
      "features":{"type":"geojson","data":$fc},
      "parks":{"type":"geojson","data":${assets.open("places.geojson").bufferedReader().readText()}},
      "zones":{"type":"geojson","data":{"type":"FeatureCollection","features":[]}},
      "sections":{"type":"geojson","data":{"type":"FeatureCollection","features":[]}},
      "progseg":{"type":"geojson","data":{"type":"FeatureCollection","features":[]}},
      "me":{"type":"geojson","data":{"type":"FeatureCollection","features":[]}}},
     "layers":[
      {"id":"ground","type":"background","paint":{"background-color":"#0d1310"}},
      {"id":"base-osm","type":"raster","source":"base-osm","layout":{"visibility":"none"}},
      {"id":"base-topo","type":"raster","source":"base-topo","layout":{"visibility":"none"}},
      {"id":"base-sat","type":"raster","source":"base-sat"},
      {"id":"park-line","type":"line","source":"parks","paint":{"line-color":"#e3e7e4","line-opacity":0.75,"line-width":1.5}},
      {"id":"sections-fill","type":"fill","source":"sections",
       "paint":{"fill-color":["get","colour"],"fill-opacity":["case",["==",["get","active"],1],0.22,0.10]}},
      {"id":"sections-line","type":"line","source":"sections",
       "paint":{"line-color":["get","colour"],"line-opacity":0.7,"line-width":["case",["==",["get","active"],1],2,1]}},
      {"id":"zones-fill","type":"fill","source":"zones",
       "paint":{"fill-color":["case",["==",["get","inside"],1],"#bae6b1","#bbceb5"],"fill-opacity":["case",["==",["get","inside"],1],0.34,0.16]}},
      {"id":"zones-line","type":"line","source":"zones",
       "paint":{"line-color":["case",["==",["get","inside"],1],"#bae6b1","#bbceb5"],"line-width":["case",["==",["get","inside"],1],2.4,1.4],"line-opacity":0.9}},
      {"id":"route-casing","type":"line","source":"features","filter":["==",["geometry-type"],"LineString"],
       "paint":{"line-color":"#0d1310","line-opacity":0.8,"line-width":8},"layout":{"line-cap":"round","line-join":"round"}},
      {"id":"route-line","type":"line","source":"features","filter":["==",["geometry-type"],"LineString"],
       "paint":{"line-color":"#ffffff","line-width":3},"layout":{"line-cap":"round","line-join":"round"}},
      {"id":"prog-seg","type":"line","source":"progseg",
       "paint":{"line-color":["get","colour"],"line-width":5,"line-opacity":0.85,"line-offset":8},"layout":{"line-cap":"butt","line-join":"round"}},
      {"id":"prog-active","type":"line","source":"progseg","filter":["==",["get","active"],1],
       "paint":{"line-color":["get","colour"],"line-width":11,"line-opacity":1,"line-offset":8},"layout":{"line-cap":"butt","line-join":"round"}},
      {"id":"point-halo","type":"circle","source":"features","filter":["==",["geometry-type"],"Point"],
       "paint":{"circle-radius":["interpolate",["linear"],["zoom"],10,["case",["has","icon"],8,6],13,["case",["has","icon"],17,12]],
                "circle-color":"#0d1310","circle-opacity":0.22,"circle-blur":0.7}},
      {"id":"point-dot","type":"circle","source":"features","filter":["==",["geometry-type"],"Point"],
       "paint":{"circle-radius":["interpolate",["linear"],["zoom"],10,["case",["has","icon"],5.5,3.5],13,["case",["has","icon"],11.5,6.5]],
                "circle-color":["case",["==",["get","has_audio"],true],"#bae6b1","#0d1310"],
                "circle-stroke-width":["interpolate",["linear"],["zoom"],10,1.2,13,2.2],
                "circle-stroke-color":["case",["==",["get","has_audio"],true],"#0d1310","#e3e7e4"]}},
      {"id":"me","type":"circle","source":"me","paint":{"circle-radius":10,"circle-color":"#e3e7e4","circle-stroke-width":4,"circle-stroke-color":"#0d1310"}}
     ]}"""

    /** a circle of r metres as a ring of 48 corners */
    private fun circle(lon: Double, lat: Double, r: Double): List<DoubleArray> {
        val dLat = r / 111_320; val dLon = r / (111_320 * Math.cos(Math.toRadians(lat)))
        return (0..48).map { i -> val a = i / 48.0 * 2 * Math.PI; doubleArrayOf(lon + dLon * Math.cos(a), lat + dLat * Math.sin(a)) }
    }
    /** rings with their properties -> polygons */
    private fun shape(rings: List<Pair<List<DoubleArray>, JSONObject>>): String {
        val fs = org.json.JSONArray()
        for ((r, props) in rings) {
            val ring = org.json.JSONArray(); (r + listOf(r.first())).forEach { ring.put(org.json.JSONArray().put(it[0]).put(it[1])) }
            fs.put(JSONObject().put("type", "Feature").put("properties", props)
                .put("geometry", JSONObject().put("type", "Polygon").put("coordinates", org.json.JSONArray().put(ring))))
        }
        return JSONObject().put("type", "FeatureCollection").put("features", fs).toString()
    }

    /** the stretch of a line from t0 to t1 of its length (index.html segmentCoords) */
    private fun segment(c: List<DoubleArray>, t0: Double, t1: Double): List<DoubleArray> {
        val cum = DoubleArray(c.size); for (i in 1 until c.size) cum[i] = cum[i - 1] + Core.geoDistance(c[i - 1][0], c[i - 1][1], c[i][0], c[i][1])
        val total = cum.lastOrNull() ?: 0.0
        fun at(d: Double): DoubleArray {
            val i = (1 until c.size).firstOrNull { cum[it] >= d } ?: return c.last()
            val f = (d - cum[i - 1]) / maxOf(cum[i] - cum[i - 1], 1e-9)
            return doubleArrayOf(c[i - 1][0] + (c[i][0] - c[i - 1][0]) * f, c[i - 1][1] + (c[i][1] - c[i - 1][1]) * f)
        }
        val d0 = t0 * total; val d1 = t1 * total
        return listOf(at(d0)) + c.indices.filter { cum[it] > d0 && cum[it] < d1 }.map { c[it] } + listOf(at(d1))
    }

    private fun bounds(fc: JSONObject): LatLngBounds? {
        val b = LatLngBounds.Builder(); var n = 0
        fun add(c: Any?) {
            if (c is org.json.JSONArray) {
                if (c.length() >= 2 && c.opt(0) is Number) { b.include(LatLng(c.getDouble(1), c.getDouble(0))); n++ }
                else for (i in 0 until c.length()) add(c.get(i))
            }
        }
        val fs = fc.getJSONArray("features")
        for (i in 0 until fs.length()) add(fs.getJSONObject(i).optJSONObject("geometry")?.opt("coordinates"))
        return if (n >= 2) b.build() else null
    }
}

/* dp for literal sizes that are drawings rather than tokens (the grip, a dot, a hairline). */
private val Int.dp_ get() = androidx.compose.ui.unit.Dp(this.toFloat())
private val Double.dp_ get() = androidx.compose.ui.unit.Dp(this.toFloat())
