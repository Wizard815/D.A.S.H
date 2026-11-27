// Wi-Fi and the clock.
#pragma once
#include <stdint.h>

namespace net {

void begin();
void loop(uint32_t nowMs);
bool up();
// Joined Atlas's truck network (DASH_ATLAS_WIFI_SSID), not home.
bool onAtlasWifi();
// The router's address (on the truck network: the Atlas brain). 0 when down.
uint32_t gateway();
int rssi();
const char* ip();
// Plain words for the status bar and Device screen.
const char* statusText();
// True once NTP has set the clock.
bool timeValid();

}  // namespace net
