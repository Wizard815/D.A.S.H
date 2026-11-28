package dash.core

import kotlinx.serialization.json.JsonObject

/** T.I.T.A.N's GET /api/glance, like firmware/lib/dash_core/src/titan.cpp. */
data class Glance(
    val up: Int,
    val down: Int,
    val pending: Int,
    val notes: Int,
    /** T.I.T.A.N checks this often, in seconds; 0 if not said. */
    val every: Int,
    val downList: List<Down>,
    val services: List<Service>,
) {
    data class Down(val name: String, val detail: String, val cause: String) {
        /** "down because Outside access is down", the detail, or "not answering". */
        val words: String
            get() = when {
                cause.isNotEmpty() -> "down because $cause is down"
                detail.isNotEmpty() -> detail
                else -> "not answering"
            }
    }

    enum class Status(val words: String) { UP("up"), DOWN("down"), PENDING("checking") }

    data class Service(val name: String, val status: Status)

    /** "All 12 up", "Plex is down", "2 down: Plex, Minecraft", "Checking 3...". */
    val summary: String
        get() = when {
            down == 0 && pending > 0 && up == 0 -> "Checking $pending..."
            down == 0 -> "All $up up"
            down == 1 && downList.isNotEmpty() -> "${downList[0].name} is down"
            else -> "$down down" + if (downList.isEmpty()) "" else ": " + downList.joinToString(", ") { it.name }
        }

    companion object {
        /** Null unless `up` and `down` are numbers and `services` is a list. */
        fun parse(text: String): Glance? = parseObject(text)?.let(::fromJson)

        fun fromJson(o: JsonObject): Glance? {
            val up = o.int("up") ?: return null
            val down = o.int("down") ?: return null
            val services = o.arr("services") ?: return null
            return Glance(
                up = up,
                down = down,
                pending = o.int("pending") ?: 0,
                notes = o.int("notes") ?: 0,
                every = o.int("every") ?: 0,
                downList = o.arr("down_list")?.mapNotNull { e ->
                    (e as? JsonObject)?.let { Down(it.str("name") ?: "?", it.str("detail") ?: "", it.str("cause") ?: "") }
                } ?: emptyList(),
                services = services.mapNotNull { e ->
                    (e as? JsonObject)?.let {
                        val st = when (it.str("status")) {
                            "up" -> Status.UP
                            "down" -> Status.DOWN
                            else -> Status.PENDING
                        }
                        Service(it.str("name") ?: "?", st)
                    }
                },
            )
        }
    }
}
