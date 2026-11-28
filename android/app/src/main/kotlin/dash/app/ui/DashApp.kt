package dash.app.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import dash.app.DashModel
import dash.app.R
import dash.app.ble.BlePermissions

enum class Tab(val label: String, val icon: Int) {
    HOME("Home", R.drawable.ic_home),
    ATLAS("Atlas", R.drawable.ic_truck),
    TITAN("T.I.T.A.N", R.drawable.ic_server),
    FAITH("FAITH", R.drawable.ic_family),
    SETTINGS("Settings", R.drawable.ic_settings),
}

@Composable
fun DashApp(model: DashModel) {
    var tab by rememberSaveable { mutableStateOf(Tab.HOME) }
    val ctx = LocalContext.current
    val askBle = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { got ->
        if (got.values.all { it }) model.startBluetooth()
    }
    val allowBluetooth = {
        if (BlePermissions.granted(ctx)) model.startBluetooth() else askBle.launch(BlePermissions.needed)
    }
    LaunchedEffect(Unit) {
        if (BlePermissions.granted(ctx)) model.startBluetooth()
        model.refreshAll()
    }
    BackHandler(enabled = tab != Tab.HOME) { tab = Tab.HOME }

    Scaffold(
        bottomBar = {
            NavigationBar {
                Tab.entries.forEach { t ->
                    NavigationBarItem(
                        selected = tab == t,
                        onClick = { tab = t },
                        icon = { Icon(painterResource(t.icon), contentDescription = null, modifier = Modifier.size(24.dp)) },
                        label = { Text(t.label) },
                    )
                }
            }
        },
    ) { padding ->
        when (tab) {
            Tab.HOME -> HomeScreen(model, padding, open = { tab = it }, allowBluetooth = allowBluetooth)
            Tab.ATLAS -> AtlasScreen(model, padding)
            Tab.TITAN -> TitanScreen(model, padding)
            Tab.FAITH -> FaithScreen(model, padding)
            Tab.SETTINGS -> SettingsScreen(model, padding, allowBluetooth = allowBluetooth)
        }
    }
}
