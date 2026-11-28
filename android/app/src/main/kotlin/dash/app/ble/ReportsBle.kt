package dash.app.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import dash.core.Frame
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** The reports link to DASH, in words for the screens. */
data class LinkState(val connected: Boolean, val words: String)

/**
 * Finds DASH by its reports service UUID (its name is only in the scan
 * response), bonds with it (DASH shows a 6-digit passkey; Android asks for
 * it), asks for a 247-byte MTU so frames aren't cut to 20 bytes, subscribes
 * to its frames characteristic and hands every notification's text to
 * [onFrame]. Reconnects by itself while started.
 */
@SuppressLint("MissingPermission") // every entry point checks bleProblem() first
class ReportsBle(private val ctx: Context, private val onFrame: (String) -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val manager = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val _state = MutableStateFlow(LinkState(false, "Bluetooth off in the app"))
    val state: StateFlow<LinkState> = _state.asStateFlow()

    private var wanted = false
    private var scanning = false
    private var gatt: BluetoothGatt? = null

    fun start() {
        wanted = true
        if (gatt == null && !scanning) scan()
    }

    fun stop() {
        wanted = false
        main.removeCallbacksAndMessages(null)
        unregisterBond()
        stopScan()
        gatt?.close()
        gatt = null
        _state.value = LinkState(false, "Bluetooth off in the app")
    }

    private fun scan() {
        bleProblem(ctx)?.let {
            _state.value = LinkState(false, it)
            retryLater()
            return
        }
        val scanner = manager?.adapter?.bluetoothLeScanner ?: return retryLater()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanning = true
        _state.value = LinkState(false, "Looking for DASH...")
        // By service UUID: it is in DASH's advertisement; the name is only in
        // the scan response.
        val filters = listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(BleIds.REPORTS_SERVICE)).build())
        scanner.startScan(filters, settings, scanCb)
        main.postDelayed({
            if (scanning) {
                stopScan()
                _state.value = LinkState(false, "DASH not found. Is it on and within a few metres?")
                retryLater()
            }
        }, SCAN_MS)
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        runCatching { manager?.adapter?.bluetoothLeScanner?.stopScan(scanCb) }
    }

    private fun retryLater() {
        if (wanted) main.postDelayed({ if (wanted && gatt == null && !scanning) scan() }, RETRY_MS)
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = (result.scanRecord?.deviceName ?: result.device.name ?: "").ifEmpty { "DASH" }
            stopScan()
            if (result.device.bondState == BluetoothDevice.BOND_BONDED) connect(result.device, name) else bond(result.device, name)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            _state.value = LinkState(false, "Bluetooth scan failed ($errorCode). Trying again soon")
            retryLater()
        }
    }

    private var bondReceiver: BroadcastReceiver? = null

    /** DASH pairs with bonding and MITM: it shows a 6-digit passkey and Android asks for it. */
    private fun bond(d: BluetoothDevice, name: String) {
        _state.value = LinkState(false, "Pairing with $name: enter the 6-digit code DASH shows")
        unregisterBond()
        val r = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                @Suppress("DEPRECATION")
                val dev: BluetoothDevice? = i.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                if (dev?.address != d.address) return
                when (i.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR)) {
                    BluetoothDevice.BOND_BONDED -> {
                        unregisterBond()
                        connect(d, name)
                    }
                    BluetoothDevice.BOND_NONE -> {
                        unregisterBond()
                        _state.value = LinkState(false, "Pairing with $name failed. Check the code on DASH and try again")
                        retryLater()
                    }
                }
            }
        }
        bondReceiver = r
        val filter = IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) ctx.registerReceiver(r, filter, Context.RECEIVER_EXPORTED)
        else ctx.registerReceiver(r, filter)
        if (!d.createBond()) {
            unregisterBond()
            connect(d, name) // already bonding or bonded; subscribing asks again if needed
        }
    }

    private fun unregisterBond() {
        bondReceiver?.let { runCatching { ctx.unregisterReceiver(it) } }
        bondReceiver = null
    }

    private fun connect(d: BluetoothDevice, name: String) {
        _state.value = LinkState(false, "Connecting to $name...")
        gatt = d.connectGatt(ctx, false, GattCb(name), BluetoothDevice.TRANSPORT_LE)
    }

    private inner class GattCb(private val name: String) : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (!g.requestMtu(BleIds.MTU)) g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                g.close()
                main.post {
                    if (gatt === g) gatt = null
                    _state.value = LinkState(false, "Lost DASH. Looking again...")
                    retryLater()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            // Below 243 a 240-byte frame would be cut short; DASH asks for 247 too.
            if (mtu < Frame.MAX_BYTES + 3) _state.value = LinkState(false, "Small Bluetooth packets ($mtu): long frames may be cut")
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val ch = g.getService(BleIds.REPORTS_SERVICE)?.getCharacteristic(BleIds.REPORTS_FRAMES)
            if (ch == null) {
                _state.value = LinkState(false, "$name has no reports service. Update DASH's firmware")
                g.disconnect()
                return
            }
            if (!g.enableNotify(ch)) {
                _state.value = LinkState(false, "Couldn't subscribe to $name's reports")
                g.disconnect()
                return
            }
            _state.value = LinkState(false, "Subscribing to $name...")
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (d.characteristic.uuid != BleIds.REPORTS_FRAMES) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                _state.value = LinkState(true, "Connected to $name")
            } else {
                // Usually not paired (insufficient authentication): forget
                // this link; the next try bonds first.
                _state.value = LinkState(false, "$name refused the subscription ($status). Pair with DASH and try again")
                g.disconnect()
            }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray) {
            if (ch.uuid == BleIds.REPORTS_FRAMES) deliver(value)
        }

        @Deprecated("Android 12 and older")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU && ch.uuid == BleIds.REPORTS_FRAMES) deliver(ch.value ?: return)
        }

        private fun deliver(v: ByteArray) {
            val text = v.toString(Charsets.UTF_8)
            main.post { onFrame(text) }
        }
    }

    private companion object {
        const val SCAN_MS = 15_000L
        const val RETRY_MS = 20_000L
    }
}
