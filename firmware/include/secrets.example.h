// Copy to include/secrets.h and fill in. secrets.h is git-ignored.
// Phone setup over BLE (roadmap M7) will replace this file.
#pragma once

#define DASH_WIFI_SSID "your-network"
#define DASH_WIFI_PASS "your-password"

// Time server for the clock. Use your router or a homelab NTP server to keep
// the device off the internet.
#define DASH_NTP_SERVER "pool.ntp.org"
// POSIX TZ string, e.g. US Eastern: "EST5EDT,M3.2.0,M11.1.0"
#define DASH_TZ "UTC0"

// FAITH base URL, no trailing slash. Leave empty to turn the FAITH link off.
#define DASH_FAITH_URL "http://faith.lan:8080"
// For an https:// URL, paste FAITH's CA certificate (PEM) here. Without it the
// device refuses https rather than trusting any certificate.
// #define DASH_FAITH_CA_PEM "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// A FAITH token for the Projects screen. On the FAITH server:
//   docker exec faith faith token add client <your-id> dash
// It's your own screen: your project, Family and (grown-ups) Parents.
// Leave empty to turn the Projects screen off. Pairing (roadmap M6) replaces this.
#define DASH_FAITH_TOKEN ""

// ---- Atlas (the truck homelab mod). All optional: leave empty to skip. ----
// Atlas's truck Wi-Fi. DASH tries home first, then this, so in the truck it
// joins Atlas by itself. The MQTT broker is then the truck network's router.
#define DASH_ATLAS_WIFI_SSID ""
#define DASH_ATLAS_WIFI_PASS ""
// Set only if the broker isn't the truck router (for example 192.168.50.2).
#define DASH_ATLAS_MQTT_HOST ""
#define DASH_ATLAS_MQTT_USER ""
#define DASH_ATLAS_MQTT_PASS ""
// Sync to the Truck Share: on the truck Wi-Fi, DASH copies its SD card files
// to copyparty on the Pi (folder /dash), where phones get the fuller viewer.
// The password of copyparty's "dash" account (write only); empty: no sync.
#define DASH_ATLAS_SHARE_PW ""
// Fob page (remote start, lock, unlock, schedule): a truckd owner token.
// Pair DASH like a phone (press the box's button, then POST /api/pair) and
// paste the "atl_..." token here. Empty: no Fob page controls.
#define DASH_ATLAS_OWNER_TOKEN ""
// ESP-NOW: the bridge's MAC (printed by the bridge at start) and a shared
// 16-character key, the same as BRIDGE_ESPNOW_KEY. Without a MAC DASH hears
// any sender; without a key frames aren't encrypted.
#define DASH_ATLAS_PEER_MAC ""
#define DASH_ESPNOW_KEY ""

// ---- T.I.T.A.N (the homelab status board mod). Optional. ----
// T.I.T.A.N's address and its TITAN_GLANCE_TOKEN (read-only). For https,
// also set DASH_TITAN_CA_PEM like DASH_FAITH_CA_PEM above.
#define DASH_TITAN_URL ""
#define DASH_TITAN_TOKEN ""
