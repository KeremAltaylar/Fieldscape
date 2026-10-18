package net.keremaltaylar.fieldscape

private val STYLE_LABELS = mapOf("free" to "Free", "arpeggio" to "Arpeggio", "pedal" to "Pedal", "strum" to "Strum", "euclid" to "Euclidean", "swells" to "Swells")

/** One line for a route's composition, as web/compose-model.js summary(): per voice (voice, sect, v3) "Off" or its style's label. No comp: "". */
fun compSummary(comp: Map<String, Map<String, String>>?): String =
    if (comp == null) "" else listOf("voice", "sect", "v3").joinToString(" · ") { v ->
        val c = comp[v]
        if (c?.get("role") == "off") "Off" else STYLE_LABELS[c?.get("style")] ?: "Free"
    }
