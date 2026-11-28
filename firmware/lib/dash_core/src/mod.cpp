#include "mod.h"

#include <ArduinoJson.h>
#include <string.h>

namespace dash {

bool ModList::add(Mod* m) {
  if (!m || n_ >= kMax || find(m->id())) return false;
  list_[n_++] = m;
  return true;
}

Mod* ModList::find(const char* id) const {
  if (!id) return nullptr;
  for (int i = 0; i < n_; i++)
    if (!strcmp(list_[i]->id(), id)) return list_[i];
  return nullptr;
}

int ModList::screens(Mod** out, int max) const {
  int k = 0;
  for (int i = 0; i < n_ && k < max; i++)
    if (list_[i]->enabled && list_[i]->hasScreen()) out[k++] = list_[i];
  return k;
}

const char* viaText(Via v) {
  switch (v) {
    case Via::EspNow: return "ESP-NOW";
    case Via::Mqtt: return "Wi-Fi";
    default: return "?";
  }
}

bool ModList::route(const char* json, size_t len, Via via, uint32_t now) {
  char mod[16], type[12];
  if (!peekFrame(json, len, mod, sizeof mod, type, sizeof type)) return false;
  Mod* m = find(mod);
  if (!m || !m->enabled) return false;
  m->onFrame(json, len, via, now);
  return true;
}

bool peekFrame(const char* json, size_t len, char* mod, size_t nMod, char* type, size_t nType) {
  if (nMod) mod[0] = 0;
  if (nType) type[0] = 0;
  if (!json || len == 0 || len > kFrameMax) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObjectConst>()) return false;
  const char* m = doc["m"] | "";
  const char* t = doc["t"] | "";
  if (!*m || !*t || strlen(m) >= nMod || strlen(t) >= nType) return false;
  strcpy(mod, m);
  strcpy(type, t);
  return true;
}

}  // namespace dash
