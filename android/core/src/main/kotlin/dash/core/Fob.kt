package dash.core

import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import java.security.SecureRandom

/** The fob module's commands, as truckd's POST /api/cmd/fob takes them. */
enum class FobCommand(val c: String, val words: String) {
    LOCK("lock", "Lock"),
    UNLOCK_DRIVER("unlock_driver", "Unlock driver"),
    UNLOCK_ALL("unlock_all", "Unlock all"),
    START("start", "Start");

    companion object {
        fun words(c: String): String = entries.firstOrNull { it.c == c }?.words ?: c
    }
}

data class FobEntry(val days: String, val at: String, val c: String) {
    /** "Mon-Fri 07:40 Start" */
    val words: String
        get() {
            val d = buildString {
                days.forEachIndexed { i, ch ->
                    append(if (i == 0 || days[i - 1] == '-' || days[i - 1] == ',') ch.uppercaseChar() else ch)
                }
            }
            return "$d $at ${FobCommand.words(c)}"
        }
}

data class FobLast(val c: String, val at: Long, val ok: Boolean, val err: String)

/** truckd's GET /api/fob, like firmware/lib/dash_core/src/fob.cpp parses it. */
data class FobStatus(
    /** % or null when not known. */
    val battery: Int?,
    val last: FobLast?,
    val schedule: List<FobEntry>,
    val skipNext: Boolean,
    /** Paused until this date ("2026-12-28"), or "". */
    val until: String,
    val skips: List<String>,
    val clockAgeS: Long?,
) {
    /** "Paused until 2026-12-28", "Skips the next one", "Skips 2026-12-04", or "". */
    val pauseWords: String
        get() = when {
            until.isNotEmpty() -> "Paused until $until"
            skipNext -> "Skips the next one"
            skips.size == 1 -> "Skips ${skips[0]}"
            skips.size > 1 -> "Skips ${skips.size} days, first ${skips[0]}"
            else -> ""
        }

    /** "Last: Lock, worked, 2 min ago" or "Last: Start, failed: no answer". */
    fun lastWords(nowS: Long): String {
        val l = last ?: return ""
        if (l.c.isEmpty()) return ""
        val ago = if (l.at in 1 until nowS) ", ${ageWords(nowS - l.at)}" else ""
        val how = if (l.ok) "worked" else "failed${if (l.err.isNotEmpty()) ": ${l.err}" else ""}"
        return "Last: ${FobCommand.words(l.c)}, $how$ago"
    }

    companion object {
        /** Null if it isn't a fob status (it must have a `schedule` list). */
        fun parse(text: String): FobStatus? = parseObject(text)?.let(::fromJson)

        fun fromJson(o: JsonObject): FobStatus? {
            val sched = o.arr("schedule") ?: return null
            val last = o.obj("last")?.let {
                FobLast(it.str("c") ?: "", it.long("at") ?: 0, it.bool("ok") ?: false, it.str("err") ?: "")
            }
            val p = o.obj("pauses")
            return FobStatus(
                battery = o.int("battery")?.takeIf { it >= 0 },
                last = last,
                schedule = sched.mapNotNull { e ->
                    (e as? JsonObject)?.let { FobEntry(it.str("days") ?: "", it.str("at") ?: "", it.str("c") ?: "") }
                },
                skipNext = p?.bool("skip_next") ?: false,
                until = p?.str("until") ?: "",
                skips = p?.arr("skip")?.mapNotNull { (it as? JsonPrimitive)?.takeIf { d -> d.isString }?.content } ?: emptyList(),
                clockAgeS = o.long("clock_age_s")?.takeIf { it >= 0 },
            )
        }
    }
}

object Nonce {
    private val rng = SecureRandom()
    private const val ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyz"

    /** 20 letters and digits: inside the 16-40 truckd and the bridge accept. */
    fun make(len: Int = 20): String = buildString { repeat(len) { append(ALPHABET[rng.nextInt(ALPHABET.length)]) } }

    fun valid(n: String): Boolean = n.length in 16..40 && n.all { it.isLetterOrDigit() && it.code < 128 || it == '-' || it == '_' }
}

/** The body of POST /api/cmd/fob: {"c":..,"nonce":..,"exp":..}. */
fun fobCommandBody(cmd: FobCommand, nonce: String, exp: Long): String =
    buildJsonObject {
        put("c", cmd.c)
        put("nonce", nonce)
        put("exp", exp)
    }.toString()

/** truckd's commands expire this soon (the device uses 25 s; truckd allows 30). */
const val FOB_EXP_AHEAD_S = 25L
