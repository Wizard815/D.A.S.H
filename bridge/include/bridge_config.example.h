// Copy to include/bridge_config.h and fill in. bridge_config.h is git-ignored.
#pragma once

// DASH's MAC (Links > Device info on DASH). Empty: send to everyone in
// range (broadcast, not encrypted).
#define BRIDGE_DASH_MAC ""
// The fob module's MAC (an Atlas mod with its own battery, A.T.L.A.S
// modules/fob). Empty: no fob module. It uses the same ESP-NOW key.
#define BRIDGE_FOB_MAC ""
// 16 characters, the same as DASH_ESPNOW_KEY on DASH. Empty: not encrypted.
#define BRIDGE_ESPNOW_KEY ""
// The truck network's channel (hostapd on the Pi is pinned to 6).
#define BRIDGE_CHANNEL 6

// Home Wi-Fi names to look for while the Pi is off. Up to 4.
#define BRIDGE_HOME_SSIDS {"your-home-wifi"}

// How often to look while parked with data waiting, in seconds.
#define BRIDGE_SCAN_EVERY_S 600
// With the LTE modem on the Pi: while parked with data waiting, wake the Pi
// this often to sync the small things over LTE (the big ones still wait for
// home Wi-Fi). 0: off, sync only at home.
#define BRIDGE_LTE_SYNC_EVERY_H 6
// Wakes without a good sync before slowing to hourly looks.
#define BRIDGE_MAX_WAKES 10

// Pins (ESP32-C3 SuperMini).
#define BRIDGE_UART_RX 20    // to Pi TXD (GPIO14)
#define BRIDGE_UART_TX 21    // to Pi RXD (GPIO15)
#define BRIDGE_AWAKE_PIN 3   // from a Pi GPIO that is high while the Pi runs (0-5 only: it wakes the C3)
#define BRIDGE_WAKE_PIN 4    // to the Witty Pi switch line (Pi header GPIO4): pulled low to wake

// Remote start over Bluetooth while the Pi is off (A.T.L.A.S docs/remote-start.md).
// The 32-byte start key as 64 hex characters, the same one truckd and the
// DASH phone app use to sign starts. Empty: Bluetooth stays off.
#define BRIDGE_START_KEY ""
// The name phones see.
#define BRIDGE_BLE_NAME "Atlas"
// While the Pi is off the bridge wakes this often and listens this long, so a
// phone finds it within a few seconds (about 3-5 mA on average; measure yours).
#define BRIDGE_BLE_EVERY_S 6
#define BRIDGE_BLE_LISTEN_MS 1500
