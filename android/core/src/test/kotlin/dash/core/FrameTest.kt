package dash.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FrameTest {
    @Test
    fun parse_readsKeys() {
        val f = Frame.parse("""{"m":"atlas","t":"hi","s":41,"from":"truckd","up":120,"ign":1,"mode":"driving","ts":1764214000}""")!!
        assertEquals("atlas", f.m)
        assertEquals("hi", f.t)
        assertEquals(41L, f.seq)
        assertEquals(1764214000L, f.ts)
        assertEquals("driving", f.str("mode"))
        assertEquals(120.0, f.num("up")!!, 0.0)
    }

    @Test
    fun parse_sumCarriesTextInS() {
        val f = Frame.parse("""{"m":"titan","t":"sum","s":"All 12 up"}""")!!
        assertNull(f.seq)
        assertEquals("All 12 up", f.summary)
    }

    @Test
    fun parse_dashBatteryAndAlertSeq() {
        val b = Frame.parse("""{"m":"dash","t":"bat","v":3.92,"pct":71,"ts":1764300000}""")!!
        assertEquals(3.92, b.num("v")!!, 0.0)
        assertEquals(71.0, b.num("pct")!!, 0.0)
        // ts is left out when DASH's clock isn't set.
        assertNull(Frame.parse("""{"m":"dash","t":"bat","v":3.92,"pct":71}""")!!.ts)
        // In alert frames "s" is Atlas's sequence number, not text.
        val a = Frame.parse("""{"m":"atlas","t":"alert","s":88,"id":"a_17","k":"cooler","lvl":2,"msg":"Cooler at 6 °C for 20 min","ts":1764300000}""")!!
        assertEquals(88L, a.seq)
    }

    @Test
    fun parse_missingFieldIsUnknown() {
        val f = Frame.parse("""{"m":"dash","t":"bat"}""")!!
        assertNull(f.num("v"))
        // A string where a number belongs reads as missing, never zero.
        assertNull(Frame.parse("""{"m":"atlas","t":"obd","ect":"hot"}""")!!.num("ect"))
    }

    @Test
    fun parse_refusesBadFrames() {
        assertNull(Frame.parse("not json"))
        assertNull(Frame.parse("[1,2]"))
        assertNull(Frame.parse("""{"t":"hi"}"""))
        assertNull(Frame.parse("""{"m":"atlas"}"""))
        assertNull(Frame.parse("""{"m":"","t":"hi"}"""))
        val long = """{"m":"atlas","t":"x","pad":"${"a".repeat(240)}"}"""
        assertNull(Frame.parse(long))
        assertTrue(Frame.parse(long, maxBytes = 1024) != null)
    }

    @Test
    fun store_keepsLatestPerTypeAndAlerts() {
        var now = 1_000_000L
        val st = ReportStore { now }
        assertTrue(st.accept("""{"m":"atlas","t":"obd","s":1,"ect":80}""", Via.BLE))
        assertTrue(st.accept("""{"m":"atlas","t":"obd","s":2,"ect":92}""", Via.BLE))
        assertFalse(st.accept("garbage", Via.BLE))
        st.accept("""{"m":"atlas","t":"alert","s":3,"id":"a_17","k":"cooler","lvl":2,"msg":"Cooler at 6 °C for 20 min","ts":1764300000}""", Via.BLE)
        st.accept("""{"m":"atlas","t":"alert","s":4,"id":"a_18","k":"theft","lvl":3,"msg":"Ignition on, no owner nearby"}""", Via.BLE)
        st.accept("""{"m":"atlas","t":"dtx","s":5,"c":"P0420","d":"Catalyst below threshold"}""", Via.BLE)
        st.accept("""{"m":"dash","t":"bat","v":3.92}""", Via.BLE)
        val r = st.state.value
        assertEquals(92.0, r.frame("atlas", "obd")!!.num("ect")!!, 0.0)
        assertEquals(listOf("a_18", "a_17"), r.sortedAlerts.map { it.id })
        assertEquals("Catalyst below threshold", r.codeWords["P0420"])
        assertEquals(3.92, r.frame("dash", "bat")!!.num("v")!!, 0.0)
        assertEquals(Via.BLE, r.lastVia)
        // lvl 0 clears that id.
        st.accept("""{"m":"atlas","t":"alert","s":6,"id":"a_18","lvl":0}""", Via.BLE)
        assertEquals(listOf("a_17"), st.state.value.sortedAlerts.map { it.id })
    }

    @Test
    fun atlas_summaryAndLink() {
        var now = 1_000_000L
        val st = ReportStore { now }
        assertEquals("Not heard yet" to false, Atlas.summary(st.state.value, now, Units.METRIC))
        st.accept("""{"m":"atlas","t":"hi","s":1,"mode":"driving"}""", Via.BLE)
        st.accept("""{"m":"atlas","t":"obd","s":2,"ect":92,"v":14.1,"rng":402}""", Via.BLE)
        assertEquals("92 °C, 14.1 V, 402 km range" to false, Atlas.summary(st.state.value, now, Units.METRIC))
        assertEquals("Live", Atlas.linkWords(st.state.value, now))
        st.accept("""{"m":"atlas","t":"dtc","s":3,"mil":1,"codes":["P0420","P0171"]}""", Via.BLE)
        assertEquals("Check engine: 2 codes" to true, Atlas.summary(st.state.value, now, Units.US))
        st.accept("""{"m":"atlas","t":"dtc","s":4,"mil":1,"codes":["P0301"]}""", Via.BLE)
        assertEquals("Stop soon: P0301" to true, Atlas.summary(st.state.value, now, Units.US))
        st.accept("""{"m":"atlas","t":"alert","s":5,"id":"a_1","k":"cooler","lvl":2,"msg":"Cooler at 6 °C"}""", Via.BLE)
        assertEquals("Cooler: Cooler at 6 °C" to true, Atlas.summary(st.state.value, now, Units.US))
        st.accept("""{"m":"atlas","t":"bye","s":6,"why":"ign_off"}""", Via.BLE)
        now += 3 * 3600 * 1000L
        assertEquals("Asleep, since 3 h ago", Atlas.linkWords(st.state.value, now))
    }

    @Test
    fun atlas_usesDashSummaryWhenThatIsAllItHas() {
        val st = ReportStore { 0L }
        st.accept("""{"m":"atlas","t":"sum","s":"92 °C, 14.1 V"}""", Via.BLE)
        assertEquals("92 °C, 14.1 V" to false, Atlas.summary(st.state.value, 0L, Units.METRIC))
    }

    @Test
    fun gateway_parsesReports() {
        val frames = Gateway.parseReports(
            """{"now":1764300100,"frames":[{"m":"atlas","t":"hi","s":1,"ts":1764300000},{"bad":1},{"m":"titan","t":"sum","s":"Plex is down"}]}""",
        )!!
        assertEquals(listOf("hi", "sum"), frames.map { it.t })
        assertNull(Gateway.parseReports("""{"nope":[]}"""))
    }
}
