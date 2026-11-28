package dash.app.ui

import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dash.app.DashModel

/** FAITH has its own app; here is only whether it answers, and a link. */
@Composable
fun FaithScreen(model: DashModel, padding: PaddingValues) {
    val faith by model.faith.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val ctx = LocalContext.current
    Screen("FAITH", padding) {
        item {
            InfoCard("Status", flag = if (faith.up == false) "Needs a look" else "") {
                StatusText(faith.words, error = faith.up == false)
                Text("FAITH is the family's agent, with its own app. Chat, approvals and projects live there.")
                if (settings.faithUrl.isNotBlank()) {
                    Button(
                        onClick = {
                            try {
                                ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(settings.faithUrl.trim())))
                            } catch (e: ActivityNotFoundException) {
                                // No browser or FAITH app takes the link; the status above still shows.
                            }
                        },
                        modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                    ) { Text("Open FAITH") }
                }
            }
        }
        item { Text("Opens FAITH's app if it handles the link, otherwise the browser.", style = MaterialTheme.typography.bodySmall) }
    }
}
