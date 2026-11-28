package dash.app.ui

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dash.app.DashModel
import dash.core.Atlas
import kotlinx.coroutines.delay

/** Like DASH's own Home: one card per mod, a title and one line. */
@Composable
fun HomeScreen(model: DashModel, padding: PaddingValues, open: (Tab) -> Unit, allowBluetooth: () -> Unit) {
    val reports by model.reports.state.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val titan by model.titan.collectAsStateWithLifecycle()
    val faith by model.faith.collectAsStateWithLifecycle()
    val link by model.link.collectAsStateWithLifecycle()
    val gw by model.gatewayWords.collectAsStateWithLifecycle()
    val now = rememberNow()

    Screen("DASH", padding) {
        item {
            val (line, look) = Atlas.summary(reports, now, settings.units)
            InfoCard("Atlas", flag = if (look) "Needs a look" else "", onClick = { open(Tab.ATLAS) }) {
                Text(line)
                Text(Atlas.linkWords(reports, now))
            }
        }
        item {
            val g = titan.value
            val sum = reports.frame("titan", "sum")?.summary
            val line = g?.summary ?: sum ?: titan.error.ifEmpty { "Not heard yet" }
            val look = (g?.down ?: 0) > 0
            InfoCard("T.I.T.A.N", flag = if (look) "Needs a look" else "", onClick = { open(Tab.TITAN) }) {
                Text(line)
            }
        }
        item {
            InfoCard("FAITH", flag = if (faith.up == false) "Needs a look" else "", onClick = { open(Tab.FAITH) }) {
                Text(faith.words)
            }
        }
        item {
            InfoCard("DASH") {
                Text(link.words)
                reports.frame("dash", "bat")?.let { b ->
                    val parts = listOfNotNull(b.num("pct")?.let { "${it.toInt()}%" }, b.num("v")?.let { "%.2f V".format(it) })
                    if (parts.isNotEmpty()) Text("Battery " + parts.joinToString(", "))
                }
                if (gw.isNotEmpty()) StatusText(gw)
                if (!link.connected) {
                    OutlinedButton(onClick = allowBluetooth, modifier = Modifier.heightIn(min = 48.dp)) {
                        Text("Allow Bluetooth and find DASH")
                    }
                }
            }
        }
    }
}

/** The clock, ticking every 5 s, for "since 2 min ago". */
@Composable
fun rememberNow(): Long {
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(5_000)
            now = System.currentTimeMillis()
        }
    }
    return now
}
