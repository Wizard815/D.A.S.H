// Build-time settings. Real values live in include/secrets.h (git-ignored);
// without it the build uses the example so CI still compiles, and warns.
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found: using secrets.example.h. Copy it and fill in your Wi-Fi."
#include "secrets.example.h"
#endif

// Settings added after a secrets.h was made: off unless set.
#ifndef DASH_FAITH_TOKEN
#define DASH_FAITH_TOKEN ""
#endif

#ifndef DASH_ATLAS_WIFI_SSID
#define DASH_ATLAS_WIFI_SSID ""  // Atlas's truck network; empty: home Wi-Fi only
#define DASH_ATLAS_WIFI_PASS ""
#endif
#ifndef DASH_ATLAS_MQTT_HOST
#define DASH_ATLAS_MQTT_HOST ""  // empty: the truck network's router, which is the Atlas brain
#endif
#ifndef DASH_ATLAS_MQTT_PORT
#define DASH_ATLAS_MQTT_PORT 1883
#endif
#ifndef DASH_ATLAS_MQTT_USER
#define DASH_ATLAS_MQTT_USER ""
#define DASH_ATLAS_MQTT_PASS ""
#endif
#ifndef DASH_ATLAS_OWNER_TOKEN
#define DASH_ATLAS_OWNER_TOKEN ""  // truckd owner token (atl_...) for the Fob page; empty: no fob controls
#endif
#ifndef DASH_ATLAS_API_HOST
#define DASH_ATLAS_API_HOST ""  // empty: the truck network's router (the Atlas brain)
#endif
#ifndef DASH_ATLAS_API_PORT
#define DASH_ATLAS_API_PORT 8080
#endif
#ifndef DASH_ATLAS_PEER_MAC
#define DASH_ATLAS_PEER_MAC ""  // the bridge's MAC; empty: hear any ESP-NOW sender
#endif
#ifndef DASH_ATLAS_SHARE_PW
#define DASH_ATLAS_SHARE_PW ""  // copyparty "dash" account password; empty: no sync to Atlas
#endif
#ifndef DASH_ATLAS_SHARE_HOST
#define DASH_ATLAS_SHARE_HOST ""  // empty: the truck network's router (the Atlas brain)
#endif
#ifndef DASH_ATLAS_SHARE_PORT
#define DASH_ATLAS_SHARE_PORT 3923
#endif
#ifndef DASH_ATLAS_SHARE_DIR
#define DASH_ATLAS_SHARE_DIR "/dash/"  // the copyparty folder DASH's files go to
#endif
#ifndef DASH_TITAN_URL
#define DASH_TITAN_URL ""  // T.I.T.A.N, e.g. http://titan.lan:8130; empty: no T.I.T.A.N mod
#endif
#ifndef DASH_TITAN_TOKEN
#define DASH_TITAN_TOKEN ""  // T.I.T.A.N's TITAN_GLANCE_TOKEN
#endif
#ifndef DASH_ESPNOW_KEY
#define DASH_ESPNOW_KEY ""  // 16 characters, the same on the bridge; empty: not encrypted
#endif
