package dash.core

import kotlin.math.roundToInt

enum class Units { US, METRIC }

/** A gauge or fact for the Atlas screen, in words. */
data class Line(val label: String, val value: String, val warn: String = "")

/**
 * Atlas's live status from reports, in words. Mirrors the device's
 * atlasSummary (firmware/lib/dash_core/src/atlas.cpp), simplified.
 */
object Atlas {
    /** truckd says `hi` every 10 s; this long with nothing is "no word". */
    const val LIVE_MS = 30_000L

    fun live(r: Reports, nowMs: Long): Boolean {
        val at = r.heardAtMs["atlas"] ?: return false
        return r.lastType["atlas"] != "bye" && nowMs - at < LIVE_MS
    }

    /** "Live", "Asleep, since 3 h ago", "No word since 2 min ago", "Not heard yet". */
    fun linkWords(r: Reports, nowMs: Long): String {
        val at = r.heardAtMs["atlas"] ?: return "Not heard yet"
        val ago = ageWords((nowMs - at) / 1000)
        if (r.lastType["atlas"] == "bye") return "Asleep, since $ago"
        return if (nowMs - at < LIVE_MS) "Live" else "No word since $ago"
    }

    /** One line for the Home card, and whether it needs a look. */
    fun summary(r: Reports, nowMs: Long, units: Units): Pair<String, Boolean> {
        if (r.heardAtMs["atlas"] == null) return "Not heard yet" to false
        r.sortedAlerts.firstOrNull { it.lvl >= 2 }?.let {
            return "${alertKindWords(it.kind)}: ${it.msg.ifEmpty { "check Atlas" }}" to true
        }
        val raw = r.latest["atlas"]?.keys ?: emptySet()
        if ("sum" in raw && raw.none { it in setOf("obd", "dtc", "sync") }) {
            r.frame("atlas", "sum")?.summary?.let { return it to false }
        }
        val codes = codes(r)
        if (codes.isNotEmpty()) {
            val urgent = codes.any { dtcUrgent(it) }
            val head = if (urgent) "Stop soon:" else "Check engine:"
            return (if (codes.size == 1) "$head ${codes[0]}" else "$head ${codes.size} codes") to true
        }
        val obd = r.frame("atlas", "obd")
        if (live(r, nowMs) && obd != null) {
            val parts = listOfNotNull(
                obd.num("ect")?.let { temp(it, units) },
                obd.num("v")?.let { volts(it) },
                obd.num("rng")?.let { "${distance(it, units)} range" },
            )
            if (parts.isNotEmpty()) return parts.joinToString(", ") to coolantWarning(obd.num("ect")).isNotEmpty()
        }
        val link = linkWords(r, nowMs)
        val sync = r.frame("atlas", "sync")
        val st = sync?.str("st")
        return when {
            sync == null -> link to false
            else -> "$link. ${syncWords(st)}" to (st == "failed")
        }
    }

    fun codes(r: Reports): List<String> {
        val dtc = r.frame("atlas", "dtc") ?: return emptyList()
        return dtc.fields.arr("codes")?.mapNotNull { (it as? kotlinx.serialization.json.JsonPrimitive)?.content }?.take(8)
            ?: emptyList()
    }

    fun gauges(r: Reports, units: Units): List<Line> {
        val o = r.frame("atlas", "obd") ?: return emptyList()
        val out = mutableListOf<Line>()
        o.num("ect")?.let { out += Line("Coolant", temp(it, units), coolantWarning(it)) }
        o.num("oil")?.let { out += Line("Oil", temp(it, units)) }
        o.num("atf")?.let { out += Line("Transmission", temp(it, units)) }
        o.num("iat")?.let { out += Line("Intake", temp(it, units)) }
        o.num("v")?.let { out += Line("Battery", volts(it), if (it < 11.8) "Low" else "") }
        o.num("fuel")?.let { out += Line("Fuel", "${it.roundToInt()}%") }
        o.num("l100")?.let { out += Line("Economy now", economy(it, units)) }
        o.num("l100a")?.let { out += Line("Economy trip", economy(it, units)) }
        o.num("rng")?.let { out += Line("Range", distance(it, units)) }
        o.num("kph")?.let { out += Line("Speed", speed(it, units)) }
        return out
    }

    fun trip(r: Reports, nowS: Long, units: Units): List<Line> {
        val out = mutableListOf<Line>()
        r.frame("atlas", "sync")?.let { s ->
            val last = s.long("last")?.let { if (nowS > it) "last good ${ageWords(nowS - it)}" else null }
            val q = s.long("q")?.let { "$it waiting" }
            out += Line("Sync", listOfNotNull(syncWords(s.str("st")), q, last).joinToString(", "),
                if (s.str("st") == "failed") (s.str("err")?.ifEmpty { null } ?: "Failed") else "")
        }
        r.frame("atlas", "pwr")?.let { p ->
            val soc = p.num("soc")?.let { "${it.roundToInt()}%" }
            val v = p.num("v")?.let { volts(it) }
            val src = p.str("src")?.let { powerSource(it) }
            out += Line("Atlas battery", listOfNotNull(soc ?: v, src).joinToString(", "))
        }
        r.frame("atlas", "wx")?.let { w ->
            val parts = listOfNotNull(w.num("c")?.let { temp(it, units) }, w.str("txt"), w.num("rh")?.let { "${it.roundToInt()}% humidity" })
            out += Line("Weather", parts.joinToString(", "))
        }
        r.frame("atlas", "gps")?.let { g ->
            val lat = g.num("lat")
            val lon = g.num("lon")
            if (lat != null && lon != null) out += Line("Position", "%.5f, %.5f".format(lat, lon))
        }
        r.frame("atlas", "hi")?.let { h ->
            h.str("mode")?.let { out += Line("Mode", it.replaceFirstChar(Char::uppercase)) }
        }
        return out
    }

    fun syncWords(st: String?): String = when (st) {
        "waiting" -> "Waiting to sync"
        "syncing" -> "Syncing"
        "synced" -> "Synced"
        "partial" -> "Light sync done; photos wait for home Wi-Fi"
        "failed" -> "Sync failed"
        "off" -> "Sync off"
        null, "" -> "Sync not known"
        else -> st
    }

    fun alertKindWords(k: String): String = when (k) {
        "cooler" -> "Cooler"
        "cam" -> "Camp cam"
        "theft" -> "Theft"
        "lowbatt" -> "Low battery"
        "" -> "Alert"
        else -> k.replaceFirstChar(Char::uppercase)
    }

    fun alertLevelWords(lvl: Int): String = when (lvl) {
        3 -> "Urgent"
        2 -> "Warning"
        else -> "Info"
    }

    fun powerSource(src: String): String = when (src) {
        "truck" -> "on truck power"
        "overland" -> "on the overland battery"
        "reserve" -> "on its reserve battery"
        else -> src
    }

    /** Overheating, misfire and oil pressure: stop soon. */
    fun dtcUrgent(code: String): Boolean {
        val c = code.uppercase()
        return c in setOf("P0217", "P0218", "P0520", "P0521", "P0522", "P0523", "P0524") ||
            c.startsWith("P030")
    }

    fun coolantWarning(ectC: Double?): String = when {
        ectC == null -> ""
        ectC >= 112 -> "Stop soon: overheating"
        ectC >= 105 -> "Hot"
        else -> ""
    }

    fun temp(c: Double, u: Units) =
        if (u == Units.US) "${(c * 9 / 5 + 32).roundToInt()} °F" else "${c.roundToInt()} °C"

    fun volts(v: Double) = "%.1f V".format(v)

    fun distance(km: Double, u: Units) =
        if (u == Units.US) "${(km * 0.621371).roundToInt()} mi" else "${km.roundToInt()} km"

    fun speed(kph: Double, u: Units) =
        if (u == Units.US) "${(kph * 0.621371).roundToInt()} mph" else "${kph.roundToInt()} km/h"

    fun economy(l100: Double, u: Units): String =
        if (u == Units.US) (if (l100 > 0) "%.1f mpg".format(235.215 / l100) else "-") else "%.1f L/100 km".format(l100)
}

/** "just now", "40 s ago", "2 min ago", "3 h ago", "2 days ago". */
fun ageWords(seconds: Long): String = when {
    seconds < 5 -> "just now"
    seconds < 60 -> "$seconds s ago"
    seconds < 3600 -> "${seconds / 60} min ago"
    seconds < 86400 -> "${seconds / 3600} h ago"
    else -> "${seconds / 86400} days ago"
}
