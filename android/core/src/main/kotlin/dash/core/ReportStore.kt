package dash.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/** Where a frame came from. */
enum class Via(val words: String) {
    BLE("from DASH over Bluetooth"),
    GATEWAY("from the gateway"),
}

/** An Atlas alert (`t:"alert"`). `lvl` 1 info, 2 warning, 3 urgent. */
data class Alert(
    val id: String,
    val kind: String,
    val lvl: Int,
    val msg: String,
    val snap: Boolean,
    val ts: Long?,
    val heardAtMs: Long,
)

/** What the app knows from reports, at one moment. Immutable. */
data class Reports(
    /** The latest frame of each type, per mod: latest["atlas"]["obd"]. */
    val latest: Map<String, Map<String, Frame>> = emptyMap(),
    /** When each mod was last heard (phone clock, ms). */
    val heardAtMs: Map<String, Long> = emptyMap(),
    /** The type of the newest frame from each mod ("bye" means asleep). */
    val lastType: Map<String, String> = emptyMap(),
    /** Open Atlas alerts by id; `lvl` 0 removes one. */
    val alerts: Map<String, Alert> = emptyMap(),
    /** Plain words for check-engine codes, from `dtx` frames. */
    val codeWords: Map<String, String> = emptyMap(),
    val lastVia: Via? = null,
) {
    fun frame(m: String, t: String): Frame? = latest[m]?.get(t)

    /** The newest, most urgent alert first. */
    val sortedAlerts: List<Alert>
        get() = alerts.values.sortedWith(compareByDescending<Alert> { it.lvl }.thenByDescending { it.heardAtMs })
}

/**
 * A small in-memory store of dash-mod frames. Bluetooth and the gateway both
 * feed it; the screens read [state].
 */
class ReportStore(private val clock: () -> Long = System::currentTimeMillis) {
    private val _state = MutableStateFlow(Reports())
    val state: StateFlow<Reports> = _state.asStateFlow()

    /** Parses and adds one frame. False if it isn't a dash-mod frame. */
    fun accept(text: String, via: Via, maxBytes: Int = Frame.MAX_BYTES): Boolean {
        val f = Frame.parse(text, maxBytes) ?: return false
        add(f, via)
        return true
    }

    fun add(f: Frame, via: Via) {
        // A gateway frame may be old: when it carries `ts`, it was heard then.
        val now = if (via == Via.GATEWAY && f.ts != null) minOf(f.ts * 1000, clock()) else clock()
        _state.update { r ->
            var alerts = r.alerts
            var words = r.codeWords
            when {
                f.m == "atlas" && f.t == "alert" -> {
                    val id = f.str("id")
                    val lvl = f.long("lvl")?.toInt()
                    if (id != null && lvl != null) {
                        alerts = if (lvl <= 0) alerts - id else alerts + (id to Alert(
                            id = id,
                            kind = f.str("k") ?: "",
                            lvl = lvl.coerceAtMost(3),
                            msg = f.str("msg") ?: "",
                            snap = f.fields.bool("snap") ?: false,
                            ts = f.ts,
                            heardAtMs = now,
                        ))
                    }
                }
                f.m == "atlas" && f.t == "dtx" -> {
                    val c = f.str("c")
                    val d = f.str("d")
                    if (c != null && d != null) words = words + (c to d.take(100))
                }
            }
            val perMod = (r.latest[f.m] ?: emptyMap()) + (f.t to f)
            r.copy(
                latest = r.latest + (f.m to perMod),
                heardAtMs = r.heardAtMs + (f.m to maxOf(now, r.heardAtMs[f.m] ?: 0L)),
                lastType = r.lastType + (f.m to f.t),
                alerts = alerts,
                codeWords = words,
                lastVia = via,
            )
        }
    }

    fun clear() {
        _state.value = Reports()
    }
}
