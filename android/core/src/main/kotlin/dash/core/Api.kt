package dash.core

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URI

/** An HTTP answer. `code` is -1 when nothing answered (no route, timeout). */
data class HttpReply(val code: Int, val body: String)

/** The one way the app does HTTP, so tests can use a fake. */
fun interface Http {
    fun send(method: String, url: String, bearer: String?, body: String?): HttpReply
}

/** Plain HttpURLConnection: works on Android and on the JVM, no extra library. */
class UrlConnectionHttp(private val connectMs: Int = 3000, private val readMs: Int = 8000) : Http {
    override fun send(method: String, url: String, bearer: String?, body: String?): HttpReply {
        val c = try {
            URI(url).toURL().openConnection() as HttpURLConnection
        } catch (e: Exception) {
            return HttpReply(-1, "")
        }
        return try {
            c.requestMethod = method
            c.connectTimeout = connectMs
            c.readTimeout = readMs
            c.instanceFollowRedirects = false // never send a token on to somewhere else
            c.setRequestProperty("Accept", "application/json")
            if (bearer != null) c.setRequestProperty("Authorization", "Bearer $bearer")
            if (body != null) {
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/json")
                c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            }
            val code = c.responseCode
            val stream = if (code >= 400) c.errorStream else c.inputStream
            val text = stream?.use { s -> s.readBytes().take(256 * 1024).toByteArray().toString(Charsets.UTF_8) } ?: ""
            HttpReply(code, text)
        } catch (e: IOException) {
            HttpReply(-1, "")
        } finally {
            c.disconnect()
        }
    }
}

/** Which way a request went. */
enum class Path(val words: String) {
    LOCAL("on the local network"),
    GATEWAY("through the gateway"),
}

sealed interface ApiResult<out T> {
    data class Ok<T>(val value: T, val path: Path) : ApiResult<T>

    /** `words` says what happened and what to do. `noReply`: nothing answered. */
    data class Err(val words: String, val code: Int, val noReply: Boolean = code < 0) : ApiResult<Nothing>
}

/** Atlas's owner tools: the fob module's status and commands. */
interface AtlasOwner {
    fun fobStatus(): ApiResult<FobStatus>

    /** Send one command. The same nonce is reused if it is retried another way. */
    fun fobCommand(cmd: FobCommand, nonce: String, exp: Long): ApiResult<String>
}

/** T.I.T.A.N's read-only board. */
interface TitanBoard {
    fun glance(): ApiResult<Glance>
}

/** Recent dash-mod frames, for when DASH isn't in Bluetooth range. */
interface ReportFeed {
    fun recent(sinceS: Long?): ApiResult<List<Frame>>
}

/** truckd's {"error":"..."} in words, or a fallback by status code. */
internal fun errorWords(r: HttpReply, who: String): String {
    parseObject(r.body)?.str("error")?.takeIf { it.isNotBlank() }?.let { return it.take(120) }
    return when {
        r.code < 0 -> "$who isn't answering"
        r.code == 401 || r.code == 403 -> "$who refused the token"
        else -> "$who answered ${r.code}"
    }
}

/** "atlas.local" → "http://atlas.local:8080"; keeps a scheme or port given. */
fun truckdBase(host: String, defaultPort: Int = 8080): String {
    val h = host.trim().trimEnd('/')
    if (h.startsWith("http://") || h.startsWith("https://")) return h
    val hasPort = h.substringAfterLast(']').contains(':')
    return if (hasPort) "http://$h" else "http://$h:$defaultPort"
}

/** truckd on the truck network: GET /api/fob and POST /api/cmd/fob. */
class Truckd(host: String, private val ownerToken: String, private val http: Http) : AtlasOwner {
    private val base = truckdBase(host)

    override fun fobStatus(): ApiResult<FobStatus> {
        val r = http.send("GET", "$base/api/fob", ownerToken, null)
        if (r.code != 200) return ApiResult.Err(errorWords(r, "truckd"), r.code)
        val f = FobStatus.parse(r.body) ?: return ApiResult.Err("truckd sent an odd fob status", r.code)
        return ApiResult.Ok(f, Path.LOCAL)
    }

    override fun fobCommand(cmd: FobCommand, nonce: String, exp: Long): ApiResult<String> {
        val r = http.send("POST", "$base/api/cmd/fob", ownerToken, fobCommandBody(cmd, nonce, exp))
        if (r.code != 200 && r.code != 202) return ApiResult.Err(errorWords(r, "truckd"), r.code)
        return ApiResult.Ok("${cmd.words} sent. Waiting for the fob module...", Path.LOCAL)
    }
}

/** T.I.T.A.N's GET <url>/api/glance with the glance token. */
class TitanHttp(url: String, private val token: String, private val http: Http) : TitanBoard {
    private val base = url.trim().trimEnd('/')

    override fun glance(): ApiResult<Glance> = glanceFrom(http.send("GET", "$base/api/glance", token, null), Path.LOCAL)
}

internal fun glanceFrom(r: HttpReply, path: Path): ApiResult<Glance> = when {
    r.code == 200 -> Glance.parse(r.body)?.let { ApiResult.Ok(it, path) }
        ?: ApiResult.Err("Odd reply: is this T.I.T.A.N?", r.code)
    r.code == 401 -> ApiResult.Err("Token refused: check TITAN_GLANCE_TOKEN", r.code)
    r.code == 503 -> ApiResult.Err("Off: set TITAN_GLANCE_TOKEN on T.I.T.A.N", r.code)
    r.code == 404 -> ApiResult.Err("T.I.T.A.N has no /api/glance: update it", r.code)
    r.code < 0 -> ApiResult.Err("No reply: is T.I.T.A.N running, and is the phone on the home network?", r.code)
    else -> ApiResult.Err("T.I.T.A.N answered ${r.code}", r.code)
}

/**
 * Tries the local service first. When nothing answers there (the phone is
 * away from the truck or home network) and a gateway is set, asks the gateway.
 */
class FallbackAtlas(private val local: AtlasOwner?, private val gateway: AtlasOwner?) : AtlasOwner {
    override fun fobStatus() = first({ it.fobStatus() })

    override fun fobCommand(cmd: FobCommand, nonce: String, exp: Long) = first({ it.fobCommand(cmd, nonce, exp) })

    private fun <T> first(call: (AtlasOwner) -> ApiResult<T>): ApiResult<T> = fallback(local, gateway, call, "Set the truckd host and owner token, or a gateway, in Settings")
}

class FallbackTitan(private val local: TitanBoard?, private val gateway: TitanBoard?) : TitanBoard {
    override fun glance() = fallback(local, gateway, { it.glance() }, "Set the T.I.T.A.N address and token, or a gateway, in Settings")
}

private fun <S, T> fallback(local: S?, gateway: S?, call: (S) -> ApiResult<T>, unset: String): ApiResult<T> {
    if (local == null && gateway == null) return ApiResult.Err(unset, 0, noReply = false)
    val a = local?.let(call)
    if (a == null || (a is ApiResult.Err && a.noReply && gateway != null)) {
        return gateway?.let(call) ?: a!!
    }
    return a
}
