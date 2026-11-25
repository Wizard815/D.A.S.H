#include "settings.h"

#include <Preferences.h>

namespace settings {

namespace {
Preferences prefs;
// NVS keys max out at 15 characters.
void linkKey(const char* name, char* out, size_t n) { snprintf(out, n, "on_%.10s", name); }
}  // namespace

void begin() { prefs.begin("dash", false); }

bool touchCalibration(uint16_t out[5]) {
  return prefs.getBytes("touch_cal", out, 5 * sizeof(uint16_t)) == 5 * sizeof(uint16_t);
}

void saveTouchCalibration(const uint16_t cal[5]) { prefs.putBytes("touch_cal", cal, 5 * sizeof(uint16_t)); }

bool linkEnabled(const char* name, bool fallback) {
  char k[16];
  linkKey(name, k, sizeof k);
  return prefs.getBool(k, fallback);
}

void setLinkEnabled(const char* name, bool on) {
  char k[16];
  linkKey(name, k, sizeof k);
  prefs.putBool(k, on);
}

}  // namespace settings
