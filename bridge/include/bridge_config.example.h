// Copy to include/bridge_config.h and fill in. bridge_config.h is git-ignored.
#pragma once

// DASH's MAC (Links > Device info on DASH). Empty: send to everyone in
// range (broadcast, not encrypted).
#define BRIDGE_DASH_MAC ""
// 16 characters, the same as DASH_ESPNOW_KEY on DASH. Empty: not encrypted.
#define BRIDGE_ESPNOW_KEY ""
// The truck network's channel (hostapd on the Pi is pinned to 6).
#define BRIDGE_CHANNEL 6

// Home Wi-Fi names to look for while the Pi is off. Up to 4.
#define BRIDGE_HOME_SSIDS {"your-home-wifi"}

// How often to look while parked with data waiting, in seconds.
#define BRIDGE_SCAN_EVERY_S 600
// Wakes without a good sync before slowing to hourly looks.
#define BRIDGE_MAX_WAKES 10

// Pins (ESP32-C3 SuperMini).
#define BRIDGE_UART_RX 20    // to Pi TXD (GPIO14)
#define BRIDGE_UART_TX 21    // to Pi RXD (GPIO15)
#define BRIDGE_AWAKE_PIN 3   // from a Pi GPIO that is high while the Pi runs (0-5 only: it wakes the C3)
#define BRIDGE_WAKE_PIN 4    // to the Witty Pi switch line (Pi header GPIO4): pulled low to wake
