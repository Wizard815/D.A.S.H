// Live reports to the phone over Bluetooth LE, without Wi-Fi. DASH
// advertises as "DASH-<last 4 hex of its MAC>" with one GATT service; the
// phone pairs once with a passkey shown on DASH's screen, subscribes, and
// gets dash-mod v1 frames: each mod's summary when it changes (at most
// every 10 s), Atlas alerts at once, and DASH's battery.
// Contract and pairing: docs/ble-reports.md. Build flag: DASH_BLE_REPORTS.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"

namespace dash {
struct AtlasState;
}

namespace ble_reports {

#if DASH_BLE_REPORTS
// Starts the radio, the service and advertising. Call once in setup.
void begin();
// Sends what's due. Call every loop pass.
void loop(uint32_t nowMs);
// A mod's latest summary line (its Home card words). Cheap: mods call it
// often; it goes to the phone only when it changed.
void summary(const char* mod, const char* text);
// A frame to send at once (alerts), at most dash::kFrameMax bytes.
void frame(const char* json, size_t len);
// An Atlas alert: passed through as it came, or rebuilt shorter from the
// state when it's over 240 bytes (MQTT can carry more).
void atlasAlert(const char* json, size_t len, const dash::AtlasState& s);
// A paired phone is subscribed: reports are going out.
bool listening();
// The pairing code to show on screen while a phone pairs; 0 otherwise.
uint32_t passkey();
// "Not mine" on the pairing screen: drop the phone that's pairing.
void rejectPairing();
// Forgets every paired phone; each has to pair again.
void forgetPhones();
// For the Device screen: "DASH-A1B2, waiting for a phone".
const char* status();
#else
inline void begin() {}
inline void loop(uint32_t) {}
inline void summary(const char*, const char*) {}
inline void frame(const char*, size_t) {}
inline void atlasAlert(const char*, size_t, const dash::AtlasState&) {}
inline bool listening() { return false; }
inline uint32_t passkey() { return 0; }
inline void rejectPairing() {}
inline void forgetPhones() {}
inline const char* status() { return "off in this build (DASH_BLE_REPORTS=0)"; }
#endif

}  // namespace ble_reports
