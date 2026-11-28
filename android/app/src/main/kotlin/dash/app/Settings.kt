package dash.app

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import dash.core.Units
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Everything the app is set up with. Tokens and keys are secrets. */
data class Settings(
    /** truckd on the truck network, e.g. "atlas.local" or "10.42.0.1:8080". */
    val truckdHost: String = "",
    val ownerToken: String = "",
    /** The 64-hex start key from truckd's POST /api/pair (start_key). */
    val startKey: String = "",
    val titanUrl: String = "",
    val titanToken: String = "",
    /** https only, e.g. https://dash.thefellashub.com. Empty: no remote mode. */
    val gatewayUrl: String = "",
    val gatewayToken: String = "",
    /** FAITH's address, for its status card. */
    val faithUrl: String = "",
    val units: Units = Units.US,
)

/**
 * Keeps settings in SharedPreferences, every value encrypted with AES-GCM
 * under a key that lives in the Android Keystore and never leaves it.
 * (androidx.security's EncryptedSharedPreferences is deprecated, so this does
 * the same small job directly.)
 */
class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("dash_settings", Context.MODE_PRIVATE)

    fun load(): Settings {
        fun g(k: String) = get(k) ?: ""
        return Settings(
            truckdHost = g("truckd_host"),
            ownerToken = g("owner_token"),
            startKey = g("start_key"),
            titanUrl = g("titan_url"),
            titanToken = g("titan_token"),
            gatewayUrl = g("gateway_url"),
            gatewayToken = g("gateway_token"),
            faithUrl = g("faith_url"),
            units = if (g("units") == "metric") Units.METRIC else Units.US,
        )
    }

    fun save(s: Settings) {
        val e = prefs.edit()
        fun p(k: String, v: String) {
            if (v.isEmpty()) e.remove(k) else e.putString(k, encrypt(v))
        }
        p("truckd_host", s.truckdHost.trim())
        p("owner_token", s.ownerToken.trim())
        p("start_key", s.startKey.trim().lowercase())
        p("titan_url", s.titanUrl.trim())
        p("titan_token", s.titanToken.trim())
        p("gateway_url", s.gatewayUrl.trim())
        p("gateway_token", s.gatewayToken.trim())
        p("faith_url", s.faithUrl.trim())
        p("units", if (s.units == Units.METRIC) "metric" else "us")
        e.apply()
    }

    private fun get(k: String): String? {
        val v = prefs.getString(k, null) ?: return null
        // A value that won't decrypt (the key was wiped, e.g. after a
        // restore to a new phone) reads as unset; Settings asks again.
        return runCatching { decrypt(v) }.getOrNull()
    }

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return gen.generateKey()
    }

    private fun encrypt(plain: String): String {
        val c = Cipher.getInstance(TRANSFORM)
        c.init(Cipher.ENCRYPT_MODE, key())
        val out = c.doFinal(plain.toByteArray(Charsets.UTF_8))
        return Base64.encodeToString(c.iv, Base64.NO_WRAP) + ":" + Base64.encodeToString(out, Base64.NO_WRAP)
    }

    private fun decrypt(stored: String): String {
        val (iv, body) = stored.split(":", limit = 2)
        val c = Cipher.getInstance(TRANSFORM)
        c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, Base64.decode(iv, Base64.NO_WRAP)))
        return c.doFinal(Base64.decode(body, Base64.NO_WRAP)).toString(Charsets.UTF_8)
    }

    private companion object {
        const val ALIAS = "dash-settings"
        const val TRANSFORM = "AES/GCM/NoPadding"
    }
}
