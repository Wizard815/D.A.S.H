package dash.app.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp

private val Dark = darkColorScheme(
    primary = Color(0xFF7FB2FF),
    secondary = Color(0xFF9CCB8A),
    error = Color(0xFFFF8A80),
    background = Color(0xFF101418),
    surface = Color(0xFF101418),
    surfaceVariant = Color(0xFF1C2329),
)

private val Light = lightColorScheme(
    primary = Color(0xFF1F5FBF),
    secondary = Color(0xFF3C7A2B),
    error = Color(0xFFB3261E),
)

@Composable
fun DashTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light) {
        Surface(color = MaterialTheme.colorScheme.background, content = content)
    }
}

/** A screen: a scrolling column with a title. */
@Composable
fun Screen(title: String, padding: PaddingValues, content: LazyListScope.() -> Unit) {
    LazyColumn(
        modifier = Modifier.padding(padding),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item { Text(title, style = MaterialTheme.typography.headlineMedium) }
        content()
    }
}

/** A card with a title. `flag` is said in words ("Needs a look"), never colour alone. */
@Composable
fun InfoCard(title: String, flag: String = "", onClick: (() -> Unit)? = null, content: @Composable () -> Unit) {
    val colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
    val body: @Composable () -> Unit = {
        Column(Modifier.fillMaxWidth().heightIn(min = 48.dp).padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                if (flag.isNotEmpty()) Badge(flag, bad = true)
            }
            content()
        }
    }
    if (onClick != null) Card(onClick = onClick, colors = colors) { body() } else Card(colors = colors) { body() }
}

@Composable
fun Badge(text: String, bad: Boolean) {
    Surface(
        color = if (bad) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.secondaryContainer,
        shape = MaterialTheme.shapes.small,
    ) {
        Text(
            text,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

/** A label and its value on one line. A warning shows as words after it. */
@Composable
fun Fact(label: String, value: String, warn: String = "") {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        Text(value, style = MaterialTheme.typography.bodyLarge, fontWeight = FontWeight.SemiBold)
        if (warn.isNotEmpty()) {
            Text("  ")
            Badge(warn, bad = true)
        }
    }
}

/** Text that changes as things happen; screen readers announce it. */
@Composable
fun StatusText(text: String, error: Boolean = false) {
    if (text.isEmpty()) return
    Text(
        text,
        color = if (error) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
        style = MaterialTheme.typography.bodyMedium,
        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
    )
}
