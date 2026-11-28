package dash.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FobTest {
    private val body = """
        {"battery":76,"last":{"c":"lock","at":1764300000,"ok":1,"err":""},
         "schedule":[{"days":"mon-fri","at":"07:40","c":"start"},{"days":"sat,sun","at":"09:00","c":"unlock_driver"}],
         "pauses":{"skip_next":false,"until":"","skip":["2026-12-04"]},"clock_age_s":12}
    """.trimIndent()

    @Test
    fun parse_readsFobStatus() {
        val f = FobStatus.parse(body)!!
        assertEquals(76, f.battery)
        assertEquals("lock", f.last!!.c)
        assertTrue(f.last!!.ok)
        assertEquals(listOf("Mon-Fri 07:40 Start", "Sat,Sun 09:00 Unlock driver"), f.schedule.map { it.words })
        assertEquals("Skips 2026-12-04", f.pauseWords)
        assertEquals(12L, f.clockAgeS)
        assertEquals("Last: Lock, worked, 2 min ago", f.lastWords(1764300120))
    }

    @Test
    fun parse_unknownsStayUnknown() {
        val f = FobStatus.parse("""{"schedule":[],"battery":-1,"pauses":{"until":"2026-12-28"}}""")!!
        assertNull(f.battery)
        assertNull(f.last)
        assertNull(f.clockAgeS)
        assertEquals("Paused until 2026-12-28", f.pauseWords)
        assertEquals("", f.lastWords(0))
    }

    @Test
    fun parse_refusesOtherShapes() {
        assertNull(FobStatus.parse("""{"battery":50}"""))
        assertNull(FobStatus.parse("nope"))
    }

    @Test
    fun commandBody_isTruckdShape() {
        assertEquals(
            """{"c":"unlock_all","nonce":"abcdefghij0123456789","exp":1764300025}""",
            fobCommandBody(FobCommand.UNLOCK_ALL, "abcdefghij0123456789", 1764300025),
        )
    }

    @Test
    fun truckd_postsWithOwnerToken() {
        val calls = mutableListOf<String>()
        val t = Truckd("atlas.local", "owner", Http { m, u, b, body -> calls += "$m $u $b $body"; HttpReply(202, "{}") })
        val r = t.fobCommand(FobCommand.LOCK, "abcdefghij0123456789", 1764300025)
        assertEquals(
            """POST http://atlas.local:8080/api/cmd/fob owner {"c":"lock","nonce":"abcdefghij0123456789","exp":1764300025}""",
            calls.single(),
        )
        assertEquals("Lock sent. Waiting for the fob module...", (r as ApiResult.Ok).value)
    }

    @Test
    fun truckd_errorWords() {
        fun with(code: Int, body: String) = Truckd("10.0.0.1:9000", "o", Http { _, _, _, _ -> HttpReply(code, body) }).fobStatus()
        assertEquals("fob module isn't paired", (with(409, """{"error":"fob module isn't paired"}""") as ApiResult.Err).words)
        assertEquals("truckd refused the token", (with(401, "") as ApiResult.Err).words)
        assertEquals("truckd isn't answering", (with(-1, "") as ApiResult.Err).words)
    }

    @Test
    fun truckdBase_addsSchemeAndPort() {
        assertEquals("http://atlas.local:8080", truckdBase("atlas.local"))
        assertEquals("http://10.0.0.1:9000", truckdBase("10.0.0.1:9000/"))
        assertEquals("https://truck.example", truckdBase("https://truck.example"))
    }

    @Test
    fun fallback_usesGatewayOnlyWhenLocalDoesNotAnswer() {
        val seen = mutableListOf<String>()
        val local = Truckd("atlas.local", "o", Http { _, u, _, _ -> seen += u; HttpReply(-1, "") })
        val gw = Gateway("https://dash.example/", "g", Http { _, u, b, _ -> seen += "$u $b"; HttpReply(200, body) })
        val r = FallbackAtlas(local, gw).fobStatus() as ApiResult.Ok
        assertEquals(Path.GATEWAY, r.path)
        assertEquals(listOf("http://atlas.local:8080/api/fob", "https://dash.example/api/v1/atlas/fob g"), seen)

        // A refusal from truckd is an answer: don't go around it.
        val refusing = Truckd("atlas.local", "o", Http { _, _, _, _ -> HttpReply(401, "") })
        assertTrue(FallbackAtlas(refusing, gw).fobStatus() is ApiResult.Err)

        // The same nonce goes both ways, so truckd can't run it twice.
        val bodies = mutableListOf<String?>()
        val l2 = Truckd("atlas.local", "o", Http { _, _, _, b -> bodies += b; HttpReply(-1, "") })
        val g2 = Gateway("https://dash.example", "g", Http { _, _, _, b -> bodies += b; HttpReply(202, "") })
        FallbackAtlas(l2, g2).fobCommand(FobCommand.START, "abcdefghij0123456789", 5)
        assertEquals(bodies[0], bodies[1])

        assertTrue((FallbackAtlas(null, null).fobStatus() as ApiResult.Err).words.contains("Settings"))
    }

    @Test
    fun gateway_needsHttps() {
        try {
            Gateway("http://dash.example", "g", Http { _, _, _, _ -> HttpReply(200, "") })
            throw AssertionError("expected a refusal")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message!!.contains("https"))
        }
    }
}
