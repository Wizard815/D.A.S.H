# DASH phone app (Android)

DASH is one app: the handheld and this phone app. DASH's mods live here too:
Atlas (the truck), T.I.T.A.N (homelab status) and a FAITH card. FAITH keeps
its own app.

Code: [`android/`](../../android). Version 0.1, not yet run on a phone.

## What it does

- **Home**: a card per mod, like DASH's Home: a title, one line, and
  "Needs a look" in words when something is wrong.
- **Atlas**: live status from DASH's reports (alerts, check-engine codes,
  gauges, sync, battery, weather, position) and the fob: Lock, Unlock driver,
  Unlock all, Start. Start always asks first.
  - On the truck Wi-Fi it calls truckd: `GET /api/fob`,
    `POST /api/cmd/fob {c, nonce, exp}` with the owner token.
  - **Start over Bluetooth** works with no Wi-Fi: it signs a request with the
    start key and writes it to the Atlas bridge (see
    [../mods/atlas.md](../mods/atlas.md)).
- **T.I.T.A.N**: `GET <url>/api/glance` with the glance token: counts, what's
  down and why, every service. Read-only.
- **FAITH**: whether FAITH answers (`/api/health`) and a link to open it.
- **Settings**: truckd host, owner token, start key, T.I.T.A.N address and
  token, FAITH address, gateway address and token, units. Every value is
  encrypted with a key in the Android Keystore.

## Bluetooth reports from DASH

The app connects to DASH's reports service and gets one dash-mod v1 frame per
notification.

| | |
| --- | --- |
| Service | `6a7c0101-4a1e-4d4b-9a54-44415348aa01` (in the advertisement; the app scans for it) |
| Characteristic | `6a7c0102-4a1e-4d4b-9a54-44415348aa01`, notify, one JSON frame (at most 240 bytes) each |
| Name | starts with `DASH`, in the scan response only |
| Security | bonded, MITM: DASH shows a 6-digit code and Android asks for it |
| MTU | the app asks for 247; without it frames are cut to 20 bytes |

Frames it reads: Atlas's (`hi`, `bye`, `obd`, `dtc`, `dtx`, `sync`, `wx`,
`pwr`, `gps`, `alert`, `cmd`), `sum` frames (`{"m":"titan","t":"sum","s":"All 12 up"}`,
where `s` is the text) and DASH's battery (`{"m":"dash","t":"bat","v":3.92,"pct":71}`).
In other frames `s` is the sender's sequence number. `ts` is left out when
DASH's clock isn't set.

## Away from home

Set a gateway (https only). When truckd or T.I.T.A.N don't answer on the
phone's network, the app asks the gateway instead, and while DASH isn't in
Bluetooth range it reads recent reports from it every 30 s. The API is in
[gateway-api.md](gateway-api.md).

## Build

Needs JDK 17 or newer and the Android SDK (platform 36).

```sh
cd android
./gradlew testDebugUnitTest assembleDebug
# the APK: app/build/outputs/apk/debug/app-debug.apk
```

The pure parts (frame, glance and fob parsing, signing, HTTP clients, gateway
fallback) are in `core/`, a plain Kotlin module. Test them without the SDK:

```sh
cd android
./gradlew -PcoreOnly :core:test
```

Pinned versions are in `gradle/libs.versions.toml`.

## What goes wrong

| You see | Why | Fix |
| --- | --- | --- |
| "DASH not found" | DASH is off, too far, or its firmware has no reports service | Bring it close; update DASH's firmware |
| "Pairing with DASH failed" | the 6-digit code was wrong or timed out | Try again and type the code DASH shows |
| "refused the subscription" | the phone isn't paired with DASH | Forget DASH in the phone's Bluetooth settings, then pair again |
| "Bluetooth permission not given" | the app wasn't allowed Bluetooth | Tap Allow Bluetooth, or allow Nearby devices in the app's settings |
| "truckd isn't answering" | not on the truck Wi-Fi, or the Pi is off | Join the truck Wi-Fi, set a gateway, or use Start over Bluetooth |
| "truckd refused the token" | wrong owner token | Pair again with truckd and paste the new token |
| "Bridge: refused: bad signature" | the start key is old (truckd rotated it) | Copy the new `start_key` into Settings |
| "Bridge: refused: expired" | the phone's clock is off | Turn on automatic date and time |
| "The Atlas bridge wasn't found" | too far, or no `BRIDGE_START_KEY` on the bridge | Stand near the truck; the bridge listens 1.5 s every 6 s, so give it 20 s |
| "Token refused: check TITAN_GLANCE_TOKEN" | the glance token doesn't match | Copy `TITAN_GLANCE_TOKEN` from T.I.T.A.N's `.env` |
| "The gateway address must start with https://" | an http gateway | Use https |
| Settings empty after restoring the phone | the Keystore key doesn't move to a new phone | Enter them again |
