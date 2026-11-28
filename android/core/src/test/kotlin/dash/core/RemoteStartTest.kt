package dash.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RemoteStartTest {
    private val key = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

    // printf 'remote_start|a1b2c3d4e5f60718293a4b5c|1764300090' |
    //   openssl dgst -sha256 -mac HMAC -macopt hexkey:<key> -r
    @Test
    fun sign_matchesOpenssl() {
        assertEquals(
            "2a1cabccfaec083affb0cfcbb1d3eabbf429071a667f634bd5f232853f69a2b2",
            RemoteStart.sign(key, "a1b2c3d4e5f60718293a4b5c", 1764300090),
        )
    }

    // Printed by `tools/atlas-sign.sh <key> 90`.
    @Test
    fun sign_matchesAtlasSignScript() {
        assertEquals(
            "b9f92e27f8cf9bba80cfcc98e8e21f819b081332c53c1bc3b4aa10c3bd5a772d",
            RemoteStart.sign(key, "b1ece1a2f2b6be2d828fe87b", 1795839756),
        )
        // Upper-case hex keys sign the same.
        assertEquals(
            RemoteStart.sign(key, "b1ece1a2f2b6be2d828fe87b", 1795839756),
            RemoteStart.sign(key.uppercase(), "b1ece1a2f2b6be2d828fe87b", 1795839756),
        )
    }

    @Test
    fun request_isTheBridgeShape() {
        val text = RemoteStart.request(key, nowS = 1764300000, aheadS = 90, nonce = "a1b2c3d4e5f60718293a4b5c")
        assertEquals(
            """{"c":"remote_start","nonce":"a1b2c3d4e5f60718293a4b5c","exp":1764300090,""" +
                """"sig":"2a1cabccfaec083affb0cfcbb1d3eabbf429071a667f634bd5f232853f69a2b2"}""",
            text,
        )
        // Fits one write with the 247-byte MTU (244 bytes of value).
        assertTrue(text.length <= 244)
    }

    @Test
    fun request_refusesBadInput() {
        assertFails { RemoteStart.request(key, 1764300000, aheadS = 121) }
        assertFails { RemoteStart.request(key, 1764300000, aheadS = 0) }
        assertFails { RemoteStart.request(key, 1764300000, nonce = "short") }
        assertFails { RemoteStart.request(key, 1764300000, nonce = "has spaces in it, sadly") }
        assertFails { RemoteStart.sign("abcd", "a1b2c3d4e5f60718293a4b5c", 1) }
        assertFalse(RemoteStart.keyValid(key.dropLast(1) + "g"))
    }

    @Test
    fun randomNonces_areValidAndDiffer() {
        val a = Nonce.make(24)
        val b = Nonce.make(24)
        assertTrue(Nonce.valid(a))
        assertTrue(a != b)
    }

    @Test
    fun outcome_readsBridgeStatus() {
        assertEquals(RemoteStart.Outcome.OK, RemoteStart.outcome("ok: waking the Pi"))
        assertEquals(RemoteStart.Outcome.REFUSED, RemoteStart.outcome("refused: bad signature"))
        assertEquals(RemoteStart.Outcome.BUSY, RemoteStart.outcome("busy: wait a minute between starts"))
        assertEquals(RemoteStart.Outcome.READY, RemoteStart.outcome("ready"))
    }

    private fun assertFails(block: () -> Unit) {
        try {
            block()
        } catch (e: IllegalArgumentException) {
            return
        }
        throw AssertionError("expected IllegalArgumentException")
    }
}
