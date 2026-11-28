package dash.core

import kotlinx.serialization.json.JsonObject

/**
 * The user's own gateway (for example https://dash.thefellashub.com), for
 * when the phone is away from the truck and home networks. The API is in
 * docs/app/gateway-api.md:
 *
 * - `GET  /api/v1/reports?since=<unix s>` → `{"now":..,"frames":[<dash-mod v1>...]}`
 * - `GET  /api/v1/atlas/fob`  → truckd's `GET /api/fob`, as is
 * - `POST /api/v1/atlas/fob`  `{c,nonce,exp}` → truckd's `POST /api/cmd/fob`
 * - `GET  /api/v1/titan/glance` → T.I.T.A.N's `GET /api/glance`, as is
 *
 * Every call carries `Authorization: Bearer <gateway token>`. The gateway
 * holds its own truckd and T.I.T.A.N tokens; the phone's never leave it.
 */
class Gateway(url: String, private val token: String, private val http: Http) : AtlasOwner, TitanBoard, ReportFeed {
    val base: String = url.trim().trimEnd('/')

    init {
        require(base.startsWith("https://")) { "The gateway address must start with https://" }
    }

    override fun recent(sinceS: Long?): ApiResult<List<Frame>> {
        val q = if (sinceS != null) "?since=$sinceS" else ""
        val r = http.send("GET", "$base/api/v1/reports$q", token, null)
        if (r.code != 200) return ApiResult.Err(errorWords(r, "The gateway"), r.code)
        val frames = parseReports(r.body) ?: return ApiResult.Err("The gateway sent odd reports", r.code)
        return ApiResult.Ok(frames, Path.GATEWAY)
    }

    override fun fobStatus(): ApiResult<FobStatus> {
        val r = http.send("GET", "$base/api/v1/atlas/fob", token, null)
        if (r.code != 200) return ApiResult.Err(errorWords(r, "The gateway"), r.code)
        val f = FobStatus.parse(r.body) ?: return ApiResult.Err("The gateway sent an odd fob status", r.code)
        return ApiResult.Ok(f, Path.GATEWAY)
    }

    override fun fobCommand(cmd: FobCommand, nonce: String, exp: Long): ApiResult<String> {
        val r = http.send("POST", "$base/api/v1/atlas/fob", token, fobCommandBody(cmd, nonce, exp))
        if (r.code != 200 && r.code != 202) return ApiResult.Err(errorWords(r, "The gateway"), r.code)
        return ApiResult.Ok("${cmd.words} sent through the gateway. Waiting for the fob module...", Path.GATEWAY)
    }

    override fun glance(): ApiResult<Glance> {
        val r = http.send("GET", "$base/api/v1/titan/glance", token, null)
        if (r.code == 502 || r.code == 504) return ApiResult.Err(errorWords(r, "T.I.T.A.N (through the gateway)"), r.code)
        if (r.code == 401 && parseObject(r.body)?.str("error") == null) return ApiResult.Err("The gateway refused its token", r.code)
        return glanceFrom(r, Path.GATEWAY)
    }

    companion object {
        /** Gateway frames come from MQTT too, where a frame may pass 240 bytes. */
        const val MAX_FRAME_BYTES = 1024

        /** `{"frames":[...]}` → frames; bad ones are skipped. Null if it isn't that shape. */
        fun parseReports(text: String): List<Frame>? {
            val o = parseObject(text) ?: return null
            val list = o.arr("frames") ?: return null
            return list.mapNotNull { el ->
                (el as? JsonObject)?.takeIf { it.toString().toByteArray().size <= MAX_FRAME_BYTES }?.let(Frame::fromJson)
            }
        }
    }
}
