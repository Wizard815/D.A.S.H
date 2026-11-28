package dash.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import dash.app.ui.DashApp
import dash.app.ui.DashTheme

class MainActivity : ComponentActivity() {
    private val model: DashModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { DashTheme { DashApp(model) } }
    }

    override fun onResume() {
        super.onResume()
        model.resume()
    }

    override fun onPause() {
        model.pause()
        super.onPause()
    }
}
