package dash.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dash.app.DashModel
import dash.core.RemoteStart
import dash.core.Units

@Composable
fun SettingsScreen(model: DashModel, padding: PaddingValues, allowBluetooth: () -> Unit) {
    val saved by model.settings.collectAsStateWithLifecycle()
    var s by remember(saved) { mutableStateOf(saved) }
    var note by remember { mutableStateOf("") }

    Screen("Settings", padding) {
        item {
            InfoCard("Atlas (truckd)") {
                Field("truckd host", s.truckdHost, "atlas.local or 10.42.0.1:8080") { s = s.copy(truckdHost = it) }
                Secret("Owner token", s.ownerToken) { s = s.copy(ownerToken = it) }
                Secret("Start key (64 hex, from pairing)", s.startKey) { s = s.copy(startKey = it) }
                if (s.startKey.isNotBlank() && !RemoteStart.keyValid(s.startKey.trim())) {
                    StatusText("The start key must be 64 characters, 0-9 and a-f", error = true)
                }
                Text("Units", style = MaterialTheme.typography.labelLarge)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(selected = s.units == Units.US, onClick = { s = s.copy(units = Units.US) }, label = { Text("US") })
                    FilterChip(selected = s.units == Units.METRIC, onClick = { s = s.copy(units = Units.METRIC) }, label = { Text("Metric") })
                }
            }
        }
        item {
            InfoCard("T.I.T.A.N") {
                Field("Address", s.titanUrl, "http://titan.lan:8130") { s = s.copy(titanUrl = it) }
                Secret("Glance token", s.titanToken) { s = s.copy(titanToken = it) }
            }
        }
        item {
            InfoCard("FAITH") {
                Field("Address", s.faithUrl, "https://faith.lan") { s = s.copy(faithUrl = it) }
            }
        }
        item {
            InfoCard("Gateway (away from home)") {
                Text("Used when truckd or T.I.T.A.N don't answer on this network.", style = MaterialTheme.typography.bodySmall)
                Field("Gateway address", s.gatewayUrl, "https://dash.example.com") { s = s.copy(gatewayUrl = it) }
                if (s.gatewayUrl.isNotBlank() && !s.gatewayUrl.trim().startsWith("https://")) {
                    StatusText("The gateway address must start with https://", error = true)
                }
                Secret("Gateway token", s.gatewayToken) { s = s.copy(gatewayToken = it) }
            }
        }
        item {
            Button(
                onClick = {
                    model.save(s)
                    note = "Saved. Tokens and keys are encrypted on this phone"
                },
                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
            ) { Text("Save") }
            StatusText(note)
        }
        item {
            InfoCard("Bluetooth") {
                Text("DASH sends its reports over Bluetooth when it's close.")
                OutlinedButton(onClick = allowBluetooth, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Text("Allow Bluetooth and find DASH")
                }
                OutlinedButton(onClick = { model.stopBluetooth() }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)) {
                    Text("Stop Bluetooth")
                }
            }
        }
    }
}

@Composable
private fun Field(label: String, value: String, hint: String, onChange: (String) -> Unit) {
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(label) },
        placeholder = { Text(hint) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun Secret(label: String, value: String, onChange: (String) -> Unit) {
    var show by remember { mutableStateOf(false) }
    OutlinedTextField(
        value = value,
        onValueChange = onChange,
        label = { Text(label) },
        singleLine = true,
        visualTransformation = if (show) VisualTransformation.None else PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, autoCorrectEnabled = false),
        trailingIcon = {
            TextButton(onClick = { show = !show }) { Text(if (show) "Hide" else "Show") }
        },
        modifier = Modifier.fillMaxWidth(),
    )
}
