package dash.core

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.longOrNull

/**
 * One dash-mod v1 frame: a JSON object with `m` (mod id), `t` (type),
 * `s` (sender's sequence number), optional `ts` (unix seconds) and data
 * fields. A missing field means "unknown", never zero. See
 * docs/mods/README.md.
 */
data class Frame(
    val m: String,
    val t: String,
    /** The sender's sequence number, when `s` is a number. */
    val seq: Long?,
    val ts: Long?,
    val fields: JsonObject,
) {
    fun str(k: String): String? = fields.str(k)
    fun num(k: String): Double? = fields.num(k)
    fun long(k: String): Long? = fields.long(k)

    /**
     * The one-line summary a `sum` frame carries. The DASH reports service
     * sends it in `s` as a string (`{"m":"titan","t":"sum","s":"All 12 up"}`);
     * `line` is accepted too, for senders that keep `s` for the sequence.
     */
    val summary: String?
        get() = fields.str("s") ?: fields.str("line")

    companion object {
        /** ESP-NOW carries 250 bytes; dash-mod frames stay at or under 240. */
        const val MAX_BYTES = 240

        private val json = Json { isLenient = false }

        /**
         * Parses one frame. Null if it isn't JSON, isn't an object, has no
         * `m` or `t`, or is longer than [maxBytes] (UTF-8).
         */
        fun parse(text: String, maxBytes: Int = MAX_BYTES): Frame? {
            if (text.toByteArray(Charsets.UTF_8).size > maxBytes) return null
            val el = runCatching { json.parseToJsonElement(text) }.getOrNull() ?: return null
            return fromJson(el)
        }

        fun fromJson(el: JsonElement): Frame? {
            val o = el as? JsonObject ?: return null
            val m = o.str("m")?.takeIf { it.isNotEmpty() } ?: return null
            val t = o.str("t")?.takeIf { it.isNotEmpty() } ?: return null
            val seq = (o["s"] as? JsonPrimitive)?.takeIf { !it.isString }?.longOrNull
            return Frame(m, t, seq, o.long("ts"), o)
        }
    }
}

// Small, forgiving readers for JSON objects: wrong type reads as missing.
internal fun JsonObject.str(k: String): String? =
    (this[k] as? JsonPrimitive)?.takeIf { it.isString }?.content

internal fun JsonObject.num(k: String): Double? =
    (this[k] as? JsonPrimitive)?.takeIf { !it.isString }?.doubleOrNull

internal fun JsonObject.long(k: String): Long? =
    (this[k] as? JsonPrimitive)?.takeIf { !it.isString }?.let { it.longOrNull ?: it.doubleOrNull?.toLong() }

internal fun JsonObject.int(k: String): Int? = long(k)?.toInt()

internal fun JsonObject.bool(k: String): Boolean? {
    val p = this[k] as? JsonPrimitive ?: return null
    if (p.isString) return null
    return p.booleanOrNull ?: p.longOrNull?.let { it != 0L }
}

internal fun JsonObject.obj(k: String): JsonObject? = this[k] as? JsonObject

internal fun JsonObject.arr(k: String): JsonArray? = this[k] as? JsonArray

internal fun parseObject(text: String): JsonObject? =
    runCatching { Json.parseToJsonElement(text) as? JsonObject }.getOrNull()
