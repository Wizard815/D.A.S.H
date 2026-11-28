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
