package dash.app.ui

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dash.app.DashModel
import dash.core.Glance

/** T.I.T.A.N's board, read-only. Fixing things stays in T.I.T.A.N's own page. */
@Composable
fun TitanScreen(model: DashModel, padding: PaddingValues) {
    val titan by model.titan.collectAsStateWithLifecycle()
    Screen("T.I.T.A.N", padding) {
        val g = titan.value
        item {
            InfoCard("Status", flag = if ((g?.down ?: 0) > 0) "Needs a look" else "") {
                if (g != null) {
                    Text(g.summary, style = MaterialTheme.typography.titleLarge)
                    Fact("Up", "${g.up}")
                    Fact("Down", "${g.down}")
                    Fact("Checking", "${g.pending}")
                    Fact("Notes for you", "${g.notes}")
                    if (g.every > 0) Text("T.I.T.A.N checks every ${g.every} s", style = MaterialTheme.typography.bodySmall)
                    if (titan.via.isNotEmpty()) Text("Read ${titan.via}", style = MaterialTheme.typography.bodySmall)
                } else if (titan.error.isEmpty()) {
                    Text("Not heard yet")
                }
                StatusText(titan.error, error = true)
                OutlinedButton(onClick = { model.refreshTitanNow() }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Text(if (titan.busy) "Checking..." else "Refresh")
                }
            }
        }
        if (g != null && g.downList.isNotEmpty()) {
            item {
                InfoCard("Down") {
                    g.downList.forEach { d ->
                        Fact(d.name, "", "Down")
                        Text(d.words, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
        if (g != null && g.services.isNotEmpty()) {
            item {
                InfoCard("Every service") {
                    g.services.forEach { s ->
                        Fact(s.name, s.status.words, if (s.status == Glance.Status.DOWN) "Down" else "")
                    }
                }
            }
        }
        item { Text("Fixing things stays in T.I.T.A.N's own page. This app can't restart anything.", style = MaterialTheme.typography.bodySmall) }
    }
}
