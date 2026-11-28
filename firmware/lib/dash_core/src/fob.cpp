#include "fob.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "glance.h"  // asciiCopy

namespace dash {

bool parseFob(const char* json, size_t len, FobStatus& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if (!doc["schedule"].is<JsonArrayConst>()) return false;
  FobStatus f;
  f.battery = doc["battery"] | -1;
  JsonObjectConst last = doc["last"];
  asciiCopy(f.lastC, sizeof f.lastC, last["c"] | "");
  f.lastAt = last["at"] | 0u;
  f.lastOk = (last["ok"] | 0) != 0;
  asciiCopy(f.lastErr, sizeof f.lastErr, last["err"] | "");
  for (JsonObjectConst e : doc["schedule"].as<JsonArrayConst>()) {
    if (f.n >= FobStatus::kEntries) break;
    FobEntry& t = f.entries[f.n++];
    asciiCopy(t.days, sizeof t.days, e["days"] | "");
    asciiCopy(t.at, sizeof t.at, e["at"] | "");
    asciiCopy(t.c, sizeof t.c, e["c"] | "");
  }
  JsonObjectConst p = doc["pauses"];
  f.skipNext = p["skip_next"] | false;
  asciiCopy(f.until, sizeof f.until, p["until"] | "");
  for (JsonVariantConst d : p["skip"].as<JsonArrayConst>()) {
    if (f.nSkips >= FobStatus::kSkips) break;
    asciiCopy(f.skips[f.nSkips++], sizeof f.skips[0], d | "");
  }
  f.clockAgeS = doc["clock_age_s"] | -1;
  out = f;
  return true;
}

const char* fobActionWords(const char* c) {
  if (!strcmp(c, "start")) return "Start";
  if (!strcmp(c, "lock")) return "Lock";
  if (!strcmp(c, "unlock_driver")) return "Unlock driver";
  if (!strcmp(c, "unlock_all")) return "Unlock all";
  return c;
}

void fobPauseWords(const FobStatus& f, char* buf, size_t n) {
  if (*f.until) snprintf(buf, n, "Paused until %s", f.until);
  else if (f.skipNext) snprintf(buf, n, "Skips the next one");
  else if (f.nSkips == 1) snprintf(buf, n, "Skips %s", f.skips[0]);
  else if (f.nSkips > 1) snprintf(buf, n, "Skips %u days, first %s", f.nSkips, f.skips[0]);
  else if (n) buf[0] = 0;
}

void fobEntryWords(const FobEntry& e, char* buf, size_t n) {
  char d[16];
  snprintf(d, sizeof d, "%s", e.days);
  // "mon-fri" -> "Mon-Fri"
  for (size_t i = 0; d[i]; i++)
    if (i == 0 || d[i - 1] == '-' || d[i - 1] == ',') d[i] = (char)toupper((unsigned char)d[i]);
  snprintf(buf, n, "%s %s %s", d, e.at, fobActionWords(e.c));
}

}  // namespace dash
