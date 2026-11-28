package dash.core

import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * Remote start over Bluetooth, to the Atlas bridge (bridge/src/main.cpp).
 * The request is
 * `{"c":"remote_start","nonce":"<16-40>","exp":<unix s>,"sig":"<hex>"}` where
 * `sig` is HMAC-SHA256 of `remote_start|<nonce>|<exp>` with the 32-byte start
 * key (64 hex). tools/atlas-sign.sh makes the same request.
 */
object RemoteStart {
    /** The bridge refuses `exp` more than 120 s ahead. */
    const val MAX_AHEAD_S = 120L

    /** Like tools/atlas-sign.sh: the Pi takes 30-60 s to boot. */
    const val DEFAULT_AHEAD_S = 90L

    fun keyValid(hex: String): Boolean = hex.length == 64 && hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }

    /** Lower-case hex HMAC-SHA256 of `remote_start|nonce|exp`. */
    fun sign(keyHex: String, nonce: String, exp: Long): String {
        require(keyValid(keyHex)) { "the start key must be 64 hex characters" }
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(hexToBytes(keyHex), "HmacSHA256"))
        val out = mac.doFinal("remote_start|$nonce|$exp".toByteArray(Charsets.UTF_8))
        return out.joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }

    /** The JSON to write to the bridge's command characteristic. */
    fun request(keyHex: String, nowS: Long, aheadS: Long = DEFAULT_AHEAD_S, nonce: String = Nonce.make(24)): String {
        require(aheadS in 1..MAX_AHEAD_S) { "exp must be 1-120 s ahead" }
        require(Nonce.valid(nonce)) { "nonce must be 16-40 letters, digits, - or _" }
        val exp = nowS + aheadS
        return buildJsonObject {
            put("c", "remote_start")
            put("nonce", nonce)
            put("exp", exp)
            put("sig", sign(keyHex, nonce, exp))
        }.toString()
    }

    /** The bridge's status text ("ok: waking the Pi", "refused: ...", "busy: ..."). */
    enum class Outcome { OK, REFUSED, BUSY, READY, OTHER }

    fun outcome(status: String): Outcome = when {
        status.startsWith("ok") -> Outcome.OK
        status.startsWith("refused") -> Outcome.REFUSED
        status.startsWith("busy") -> Outcome.BUSY
        status == "ready" -> Outcome.READY
        else -> Outcome.OTHER
    }

    internal fun hexToBytes(hex: String): ByteArray =
        ByteArray(hex.length / 2) { i -> hex.substring(2 * i, 2 * i + 2).toInt(16).toByte() }
}
