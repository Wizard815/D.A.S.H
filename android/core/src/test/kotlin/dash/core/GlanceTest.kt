package dash.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GlanceTest {
    private val body = """
        {"up":10,"down":2,"pending":1,"notes":3,"every":60,
         "down_list":[{"name":"Plex","detail":"no answer","cause":"Outside access"},
                      {"name":"Minecraft","detail":"","cause":""}],
         "services":[{"name":"Plex","status":"down"},{"name":"Wiki","status":"up"},
                     {"name":"Mail","status":"pending"},{"name":"Odd"}]}
    """.trimIndent()

    @Test
    fun parse_readsTheGlanceShape() {
        val g = Glance.parse(body)!!
        assertEquals(10, g.up)
        assertEquals(2, g.down)
        assertEquals(1, g.pending)
        assertEquals(3, g.notes)
        assertEquals(60, g.every)
        assertEquals("down because Outside access is down", g.downList[0].words)
        assertEquals("not answering", g.downList[1].words)
        assertEquals(
            listOf(Glance.Status.DOWN, Glance.Status.UP, Glance.Status.PENDING, Glance.Status.PENDING),
            g.services.map { it.status },
        )
        assertEquals("2 down: Plex, Minecraft", g.summary)
    }

    @Test
    fun summary_words() {
        fun g(up: Int, down: Int, pending: Int, names: List<String>) =
            Glance(up, down, pending, 0, 0, names.map { Glance.Down(it, "", "") }, emptyList())
        assertEquals("All 12 up", g(12, 0, 0, emptyList()).summary)
        assertEquals("Plex is down", g(11, 1, 0, listOf("Plex")).summary)
        assertEquals("Checking 3...", g(0, 0, 3, emptyList()).summary)
        assertEquals("1 down", g(11, 1, 0, emptyList()).summary)
    }

    @Test
    fun parse_refusesOtherShapes() {
        assertNull(Glance.parse("""{"up":1,"down":0}"""))
        assertNull(Glance.parse("""{"up":"1","down":0,"services":[]}"""))
        assertNull(Glance.parse("<html>"))
    }

    @Test
    fun http_errorsInWords() {
        fun with(code: Int) = TitanHttp("http://titan.lan:8130/", "t", Http { _, _, _, _ -> HttpReply(code, "") }).glance()
        assertEquals("Token refused: check TITAN_GLANCE_TOKEN", (with(401) as ApiResult.Err).words)
        assertEquals("Off: set TITAN_GLANCE_TOKEN on T.I.T.A.N", (with(503) as ApiResult.Err).words)
        assertEquals("T.I.T.A.N has no /api/glance: update it", (with(404) as ApiResult.Err).words)
    }

    @Test
    fun http_sendsBearerToGlance() {
        var seen = ""
        val t = TitanHttp("http://titan.lan:8130/", "tok", Http { m, u, b, _ -> seen = "$m $u $b"; HttpReply(200, body) })
        val r = t.glance() as ApiResult.Ok
        assertEquals("GET http://titan.lan:8130/api/glance tok", seen)
        assertEquals(Path.LOCAL, r.path)
    }
}
