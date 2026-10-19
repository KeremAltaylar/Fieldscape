package net.keremaltaylar.fieldscape

import org.junit.Assert.assertEquals
import org.junit.Test

class CompSummaryTest {
    private fun v(style: String, role: String = "accomp") = mapOf("style" to style, "role" to role)
    @Test fun noComp() = assertEquals("", compSummary(null))
    @Test fun template() = assertEquals("Arpeggio \u00B7 Strum \u00B7 Pedal",
        compSummary(mapOf("voice" to v("arpeggio"), "sect" to v("strum"), "v3" to v("pedal"))))
    @Test fun roleOff() = assertEquals("Off \u00B7 Euclidean \u00B7 Swells",
        compSummary(mapOf("voice" to v("free", "off"), "sect" to v("euclid"), "v3" to v("swells"))))
    @Test fun missingStyle() = assertEquals("Free \u00B7 Free \u00B7 Free", compSummary(mapOf("voice" to mapOf("role" to "accomp"))))
}
