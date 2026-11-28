package dash.app.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.util.UUID

/** The UUIDs DASH and the Atlas bridge use. */
object BleIds {
    /**
     * DASH's reports service: one dash-mod v1 JSON frame per notification.
     * Found by this UUID (in the advertisement); DASH's name ("DASH...") is
     * only in the scan response. Bonded, MITM, 6-digit passkey shown on DASH.
     */
    val REPORTS_SERVICE: UUID = UUID.fromString("6a7c0101-4a1e-4d4b-9a54-44415348aa01")
    val REPORTS_FRAMES: UUID = UUID.fromString("6a7c0102-4a1e-4d4b-9a54-44415348aa01")

    /** The Atlas bridge's remote start service (bridge/src/main.cpp). */
    val ATLAS_SERVICE: UUID = UUID.fromString("6a7c0001-4a1e-4d4b-9a54-41544c415331")
    val ATLAS_CMD: UUID = UUID.fromString("6a7c0002-4a1e-4d4b-9a54-41544c415331")
    val ATLAS_STATUS: UUID = UUID.fromString("6a7c0003-4a1e-4d4b-9a54-41544c415331")
    const val ATLAS_NAME = "Atlas"

    /** Client Characteristic Configuration: turns notifications on. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** 240-byte frames plus 3 bytes of ATT header fit in one packet. */
    const val MTU = 247
}

object BlePermissions {
    val needed: Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    fun granted(ctx: Context): Boolean = needed.all { ctx.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
}

/** Why Bluetooth can't be used right now, in words; null if it can. */
fun bleProblem(ctx: Context): String? {
    val adapter = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
        ?: return "This phone has no Bluetooth"
    if (!BlePermissions.granted(ctx)) return "Bluetooth permission not given. Tap Allow Bluetooth"
    if (!adapter.isEnabled) return "Bluetooth is off. Turn it on in the phone's settings"
    return null
}

// Android 13 changed these calls; one place for both shapes.
@SuppressLint("MissingPermission")
internal fun BluetoothGatt.enableNotify(ch: BluetoothGattCharacteristic): Boolean {
    if (!setCharacteristicNotification(ch, true)) return false
    val d = ch.getDescriptor(BleIds.CCCD) ?: return false
    val v = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        writeDescriptor(d, v) == android.bluetooth.BluetoothStatusCodes.SUCCESS
    } else {
        @Suppress("DEPRECATION")
        d.value = v
        @Suppress("DEPRECATION")
        writeDescriptor(d)
    }
}

@SuppressLint("MissingPermission")
internal fun BluetoothGatt.writeValue(ch: BluetoothGattCharacteristic, v: ByteArray): Boolean =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        writeCharacteristic(ch, v, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == android.bluetooth.BluetoothStatusCodes.SUCCESS
    } else {
        @Suppress("DEPRECATION")
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        @Suppress("DEPRECATION")
        ch.value = v
        @Suppress("DEPRECATION")
        writeCharacteristic(ch)
    }
