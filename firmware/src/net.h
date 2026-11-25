// Wi-Fi and the clock.
#pragma once
#include <stdint.h>

namespace net {

void begin();
void loop(uint32_t nowMs);
bool up();
int rssi();
const char* ip();
// Plain words for the status bar and Device screen.
const char* statusText();
// True once NTP has set the clock.
bool timeValid();

}  // namespace net
