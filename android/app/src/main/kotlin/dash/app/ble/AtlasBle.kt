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
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import dash.core.RemoteStart
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout

/**
 * Remote start over Bluetooth, with no Wi-Fi: finds the Atlas bridge, writes
 * one signed request to its command characteristic and returns the status it
 * answers ("ok: waking the Pi", "refused: ...", "busy: ...").
 *
 * The bridge listens 1.5 s every 6 s while the Pi is off, so the scan runs
 * for up to [SCAN_MS]; once connected the bridge stays up to 30 s.
 */
@SuppressLint("MissingPermission") // start() checks bleProblem() first
class AtlasBle(private val ctx: Context) {
    private val manager = ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager

    /** [request] is the JSON from [RemoteStart.request]. */
    suspend fun start(request: String, progress: (String) -> Unit): String {
        bleProblem(ctx)?.let { return it }
        progress("Looking for the Atlas bridge...")
        val device = find() ?: return "The Atlas bridge wasn't found. Is the phone near the truck? Is BRIDGE_START_KEY set?"
        progress("Found the bridge. Sending the start...")
        val answer = CompletableDeferred<String>()
        val cb = Cb(request.toByteArray(Charsets.UTF_8), answer)
        val gatt = device.connectGatt(ctx, false, cb, BluetoothDevice.TRANSPORT_LE)
        return try {
            withTimeout(ANSWER_MS) { answer.await() }
        } catch (e: TimeoutCancellationException) {
            "The bridge didn't answer. Try again closer to the truck"
        } finally {
            gatt.disconnect()
            gatt.close()
        }
    }

    private suspend fun find(): BluetoothDevice? {
        val scanner = manager?.adapter?.bluetoothLeScanner ?: return null
        val found = CompletableDeferred<BluetoothDevice>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                found.complete(result.device)
            }

            override fun onScanFailed(errorCode: Int) {
                found.completeExceptionally(IllegalStateException("scan failed $errorCode"))
            }
        }
        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(BleIds.ATLAS_SERVICE)).build(),
            ScanFilter.Builder().setDeviceName(BleIds.ATLAS_NAME).build(),
        )
        scanner.startScan(filters, ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), cb)
        return try {
            withTimeout(SCAN_MS) { found.await() }
        } catch (e: Exception) {
            null
        } finally {
            runCatching { scanner.stopScan(cb) }
        }
    }

    private class Cb(private val request: ByteArray, private val answer: CompletableDeferred<String>) : BluetoothGattCallback() {
        private var written = false

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> if (!g.requestMtu(BleIds.MTU)) g.discoverServices()
                BluetoothProfile.STATE_DISCONNECTED ->
                    answer.complete(if (written) "The bridge hung up before it answered" else "Lost the bridge. Try again")
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            // Below ~170 the request needs a long write, which Android does
            // by itself for WRITE_TYPE_DEFAULT; NimBLE on the bridge takes it.
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(BleIds.ATLAS_SERVICE)
            val st = svc?.getCharacteristic(BleIds.ATLAS_STATUS)
            if (svc?.getCharacteristic(BleIds.ATLAS_CMD) == null || st == null) {
                answer.complete("That device isn't the Atlas bridge")
                return
            }
            if (!g.enableNotify(st)) sendCommand(g) // no notify: write anyway and read status after
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            if (d.characteristic.uuid == BleIds.ATLAS_STATUS) sendCommand(g)
        }

        private fun sendCommand(g: BluetoothGatt) {
            val cmd = g.getService(BleIds.ATLAS_SERVICE)?.getCharacteristic(BleIds.ATLAS_CMD) ?: return
            if (!g.writeValue(cmd, request)) answer.complete("Couldn't write to the bridge. Try again")
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int) {
            if (ch.uuid != BleIds.ATLAS_CMD) return
            if (status != BluetoothGatt.GATT_SUCCESS) {
                answer.complete("The bridge didn't take the request (error $status)")
                return
            }
            written = true
            // The status notify normally follows at once; read it as well in
            // case notifications didn't turn on.
            g.getService(BleIds.ATLAS_SERVICE)?.getCharacteristic(BleIds.ATLAS_STATUS)?.let { g.readCharacteristic(it) }
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray) = status(ch, value)

        override fun onCharacteristicRead(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray, status: Int) =
            status(ch, value)

        @Deprecated("Android 12 and older")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) status(ch, ch.value ?: return)
        }

        @Deprecated("Android 12 and older")
        override fun onCharacteristicRead(g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int) {
            @Suppress("DEPRECATION")
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) status(ch, ch.value ?: return)
        }

        private fun status(ch: BluetoothGattCharacteristic, value: ByteArray) {
            if (ch.uuid != BleIds.ATLAS_STATUS || !written) return
            val text = value.toString(Charsets.UTF_8)
            // "ready" (or an old "busy" from before the write) isn't the answer.
            if (RemoteStart.outcome(text) == RemoteStart.Outcome.READY) return
            answer.complete(text)
        }
    }

    private companion object {
        const val SCAN_MS = 20_000L
        const val ANSWER_MS = 25_000L
    }
}
