package dash.app

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dash.app.ble.AtlasBle
import dash.app.ble.LinkState
import dash.app.ble.ReportsBle
import dash.core.ApiResult
import dash.core.AtlasOwner
import dash.core.FOB_EXP_AHEAD_S
import dash.core.FallbackAtlas
import dash.core.FallbackTitan
import dash.core.FobCommand
import dash.core.FobStatus
import dash.core.Gateway
import dash.core.Glance
import dash.core.Nonce
import dash.core.RemoteStart
import dash.core.ReportStore
import dash.core.TitanBoard
import dash.core.TitanHttp
import dash.core.Truckd
import dash.core.UrlConnectionHttp
import dash.core.Via
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** A fetched thing, its words when it failed, and whether a fetch is running. */
data class Fetched<T>(val value: T? = null, val error: String = "", val busy: Boolean = false, val via: String = "")

/** FAITH's /api/health, in words. */
data class FaithState(val up: Boolean? = null, val words: String = "Not checked yet")

class DashModel(app: Application) : AndroidViewModel(app) {
    private val store = SettingsStore(app)
    private val http = UrlConnectionHttp()

    private val _settings = MutableStateFlow(store.load())
    val settings: StateFlow<Settings> = _settings.asStateFlow()

    val reports = ReportStore()
    private val reportsBle = ReportsBle(app) { reports.accept(it, Via.BLE) }
    val link: StateFlow<LinkState> = reportsBle.state

    private val _titan = MutableStateFlow(Fetched<Glance>())
    val titan: StateFlow<Fetched<Glance>> = _titan.asStateFlow()

    private val _fob = MutableStateFlow(Fetched<FobStatus>())
    val fob: StateFlow<Fetched<FobStatus>> = _fob.asStateFlow()

    /** The last fob or remote start result, in words. */
    private val _fobMsg = MutableStateFlow("")
    val fobMsg: StateFlow<String> = _fobMsg.asStateFlow()

    private val _faith = MutableStateFlow(FaithState())
    val faith: StateFlow<FaithState> = _faith.asStateFlow()

    private val _gatewayWords = MutableStateFlow("")
    val gatewayWords: StateFlow<String> = _gatewayWords.asStateFlow()

    private var poller: Job? = null

    fun save(s: Settings) {
        store.save(s)
        _settings.value = store.load()
        refreshAll()
    }

    /** Bluetooth reports from DASH. Call after the permission is given. */
    fun startBluetooth() = reportsBle.start()

    fun stopBluetooth() = reportsBle.stop()

    /** While the app is in front: T.I.T.A.N every minute, gateway reports every 30 s when DASH isn't connected. */
    fun resume() {
        if (poller?.isActive == true) return
        poller = viewModelScope.launch {
            var tick = 0
            while (isActive) {
                if (tick % 2 == 0) {
                    refreshTitan()
                    checkFaith()
                }
                if (!link.value.connected) pullGateway()
                tick++
                delay(30_000)
            }
        }
    }

    fun pause() {
        poller?.cancel()
        poller = null
    }

    fun refreshAll() {
        viewModelScope.launch {
            refreshTitan()
            refreshFob()
            checkFaith()
            if (!link.value.connected) pullGateway()
        }
    }

    private fun gateway(): Gateway? {
        val s = _settings.value
        if (s.gatewayUrl.isBlank() || s.gatewayToken.isBlank()) return null
        return runCatching { Gateway(s.gatewayUrl, s.gatewayToken, http) }.getOrNull()
    }

    private fun atlas(): AtlasOwner {
        val s = _settings.value
        val local = if (s.truckdHost.isNotBlank() && s.ownerToken.isNotBlank()) Truckd(s.truckdHost, s.ownerToken, http) else null
        return FallbackAtlas(local, gateway())
    }

    private fun titanBoard(): TitanBoard {
        val s = _settings.value
        val local = if (s.titanUrl.isNotBlank() && s.titanToken.isNotBlank()) TitanHttp(s.titanUrl, s.titanToken, http) else null
        return FallbackTitan(local, gateway())
    }

    suspend fun refreshTitan() {
        _titan.value = _titan.value.copy(busy = true)
        val r = withContext(Dispatchers.IO) { titanBoard().glance() }
        _titan.value = when (r) {
            is ApiResult.Ok -> Fetched(r.value, via = r.path.words)
            is ApiResult.Err -> _titan.value.copy(error = r.words, busy = false)
        }
    }

    fun refreshTitanNow() {
        viewModelScope.launch { refreshTitan() }
    }

    suspend fun refreshFob() {
        _fob.value = _fob.value.copy(busy = true)
        val r = withContext(Dispatchers.IO) { atlas().fobStatus() }
        _fob.value = when (r) {
            is ApiResult.Ok -> Fetched(r.value, via = r.path.words)
            is ApiResult.Err -> _fob.value.copy(error = r.words, busy = false)
        }
    }

    fun refreshFobNow() {
        viewModelScope.launch { refreshFob() }
    }

    /** Lock, unlock or start over truckd's HTTP API (or the gateway). Start is confirmed in the UI first. */
    fun fobCommand(cmd: FobCommand) {
        viewModelScope.launch {
            val now = System.currentTimeMillis() / 1000
            if (now < 1_700_000_000L) {
                _fobMsg.value = "The phone's clock looks wrong. Set the date and time first"
                return@launch
            }
            _fobMsg.value = "Sending ${cmd.words}..."
            val r = withContext(Dispatchers.IO) { atlas().fobCommand(cmd, Nonce.make(), now + FOB_EXP_AHEAD_S) }
            _fobMsg.value = when (r) {
                is ApiResult.Ok -> r.value
                is ApiResult.Err -> r.words
            }
            delay(12_000) // the fob module answers within about 10 s
            refreshFob()
        }
    }

    /** Remote start over Bluetooth to the Atlas bridge, for when there's no Wi-Fi. */
    fun bluetoothStart() {
        val key = _settings.value.startKey
        if (!RemoteStart.keyValid(key)) {
            _fobMsg.value = "Set the 64-character start key in Settings first"
            return
        }
        viewModelScope.launch {
            val req = RemoteStart.request(key, System.currentTimeMillis() / 1000)
            val answer = AtlasBle(getApplication()).start(req) { _fobMsg.value = it }
            _fobMsg.value = "Bridge: $answer"
        }
    }

    private suspend fun pullGateway() {
        val g = gateway() ?: run {
            _gatewayWords.value = if (_settings.value.gatewayUrl.isNotBlank() && !_settings.value.gatewayUrl.startsWith("https://"))
                "The gateway address must start with https://" else ""
            return
        }
        val since = reports.state.value.heardAtMs.values.maxOrNull()?.let { it / 1000 }
        when (val r = withContext(Dispatchers.IO) { g.recent(since) }) {
            is ApiResult.Ok -> {
                r.value.forEach { reports.add(it, Via.GATEWAY) }
                _gatewayWords.value = "Gateway: ${r.value.size} new reports"
            }
            is ApiResult.Err -> _gatewayWords.value = "Gateway: ${r.words}"
        }
    }

    private suspend fun checkFaith() {
        val url = _settings.value.faithUrl.trim().trimEnd('/')
        if (url.isEmpty()) {
            _faith.value = FaithState(null, "Not set up. Add FAITH's address in Settings")
            return
        }
        val r = withContext(Dispatchers.IO) { http.send("GET", "$url/api/health", null, null) }
        _faith.value = when {
            r.code == 200 -> FaithState(true, "Answering")
            r.code < 0 -> FaithState(false, "FAITH isn't answering. Check that its container is running")
            else -> FaithState(false, "FAITH answered ${r.code}")
        }
    }

    override fun onCleared() {
        reportsBle.stop()
    }
}
