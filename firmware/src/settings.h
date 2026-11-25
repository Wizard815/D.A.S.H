// Settings kept in flash (NVS) across restarts. No secrets here yet; when
// tokens arrive (M2/M6) they go in their own namespace and are never logged.
#pragma once
#include <stdint.h>

namespace settings {

void begin();

bool touchCalibration(uint16_t out[5]);  // false if never calibrated
void saveTouchCalibration(const uint16_t cal[5]);

// Per-connector on/off switch, by connector name.
bool linkEnabled(const char* name, bool fallback = true);
void setLinkEnabled(const char* name, bool on);

}  // namespace settings
