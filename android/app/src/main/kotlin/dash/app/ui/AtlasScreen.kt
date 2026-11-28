package dash.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dash.app.DashModel
import dash.core.Atlas
import dash.core.FobCommand

private enum class StartWay { WIFI, BLUETOOTH }

@Composable
fun AtlasScreen(model: DashModel, padding: PaddingValues) {
    val reports by model.reports.state.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val fob by model.fob.collectAsStateWithLifecycle()
    val msg by model.fobMsg.collectAsStateWithLifecycle()
    val now = rememberNow()
    var confirm by remember { mutableStateOf<StartWay?>(null) }
    LaunchedEffect(Unit) { model.refreshFobNow() }

    Screen("Atlas", padding) {
        item {
            InfoCard("Truck") {
                Text(Atlas.linkWords(reports, now))
                reports.lastVia?.let { Text("Reports ${it.words}", style = MaterialTheme.typography.bodySmall) }
            }
        }
        val alerts = reports.sortedAlerts
        if (alerts.isNotEmpty()) {
            item {
                InfoCard("Alerts", flag = if (alerts.any { it.lvl >= 2 }) "Needs a look" else "") {
                    alerts.forEach { a ->
                        Fact("${Atlas.alertLevelWords(a.lvl)}: ${Atlas.alertKindWords(a.kind)}", "")
                        Text(a.msg)
                        if (a.snap) Text("A camera snapshot is waiting", style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
        item {
            InfoCard("Fob") {
                val f = fob.value
                if (f != null) {
                    Fact("Fob module battery", f.battery?.let { "$it%" } ?: "not known", if ((f.battery ?: 100) <= 10) "Low" else "")
                    f.pauseWords.takeIf { it.isNotEmpty() }?.let { Text(it) }
                    f.lastWords(now / 1000).takeIf { it.isNotEmpty() }?.let { Text(it) }
                    f.schedule.take(3).forEach { Text(it.words, style = MaterialTheme.typography.bodySmall) }
                    if (fob.via.isNotEmpty()) Text("Status ${fob.via}", style = MaterialTheme.typography.bodySmall)
                }
                StatusText(fob.error, error = true)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FobButton("Lock", Modifier.weight(1f)) { model.fobCommand(FobCommand.LOCK) }
                    FobButton("Unlock driver", Modifier.weight(1f)) { model.fobCommand(FobCommand.UNLOCK_DRIVER) }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FobButton("Unlock all", Modifier.weight(1f)) { model.fobCommand(FobCommand.UNLOCK_ALL) }
                    Button(
                        onClick = { confirm = StartWay.WIFI },
                        modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                    ) { Text("Start...") }
                }
                OutlinedButton(onClick = { confirm = StartWay.BLUETOOTH }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Text("Start over Bluetooth (no Wi-Fi)...")
                }
                OutlinedButton(onClick = { model.refreshFobNow() }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Text(if (fob.busy) "Checking..." else "Refresh")
                }
                StatusText(msg)
                cmdResult(reports)?.let { Text(it) }
            }
        }
        val codes = Atlas.codes(reports)
        item {
            InfoCard("Codes", flag = if (codes.isNotEmpty()) "Check engine" else "") {
                if (reports.frame("atlas", "dtc") == null) Text("Not heard yet")
                else if (codes.isEmpty()) Text("No codes. Check-engine light off")
                codes.forEach { c ->
                    val urgent = if (Atlas.dtcUrgent(c)) "Stop soon" else ""
                    Fact(c, "", urgent)
                    reports.codeWords[c]?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
                }
            }
        }
        item {
            InfoCard("Gauges") {
                val g = Atlas.gauges(reports, settings.units)
                if (g.isEmpty()) Text("No gauges yet: the truck sends them while driving")
                g.forEach { Fact(it.label, it.value, it.warn) }
            }
        }
        item {
            InfoCard("Trip") {
                val t = Atlas.trip(reports, now / 1000, settings.units)
                if (t.isEmpty()) Text("Nothing heard yet")
                t.forEach { Fact(it.label, it.value, it.warn) }
            }
        }
    }

    confirm?.let { way ->
        AlertDialog(
            onDismissRequest = { confirm = null },
            title = { Text("Start the truck?") },
            text = {
                Text(
                    if (way == StartWay.WIFI) "This starts the engine through truckd. Make sure it's safe to run."
                    else "This wakes the Atlas Pi over Bluetooth, which then starts the engine. Make sure it's safe to run.",
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirm = null
                    if (way == StartWay.WIFI) model.fobCommand(FobCommand.START) else model.bluetoothStart()
                }) { Text("Start") }
            },
            dismissButton = { TextButton(onClick = { confirm = null }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun FobButton(label: String, modifier: Modifier, onClick: () -> Unit) {
    OutlinedButton(onClick = onClick, modifier = modifier.heightIn(min = 48.dp)) { Text(label) }
}

/** The newest `cmd` frame: "Start worked" or "Start failed: ...". */
private fun cmdResult(r: dash.core.Reports): String? {
    val f = r.frame("atlas", "cmd") ?: return null
    val what = f.str("what")?.let { FobCommand.words(it) } ?: "Command"
    val ok = f.fields["ok"]?.toString() == "1" || f.fields["ok"]?.toString() == "true"
    val msg = f.str("msg")?.takeIf { it.isNotEmpty() }
    return if (ok) "$what worked${msg?.let { ": $it" } ?: ""}" else "$what failed${msg?.let { ": $it" } ?: ""}"
}
