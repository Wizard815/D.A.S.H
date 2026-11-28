#include "settings.h"

#include <Preferences.h>

namespace settings {

namespace {
Preferences prefs;
// NVS keys max out at 15 characters.
void linkKey(const char* name, char* out, size_t n) { snprintf(out, n, "on_%.10s", name); }
// "m" + up to 6 of the mod id + "_" + up to 7 of the key: 15 at most.
void modKey(const char* mod, const char* key, char* out, size_t n) { snprintf(out, n, "m%.6s_%.7s", mod, key); }
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

uint8_t modValue(const char* mod, const char* key, uint8_t fallback) {
  char k[16];
  modKey(mod, key, k, sizeof k);
  return prefs.getUChar(k, fallback);
}

void setModValue(const char* mod, const char* key, uint8_t value) {
  char k[16];
  modKey(mod, key, k, sizeof k);
  prefs.putUChar(k, value);
}

}  // namespace settings
