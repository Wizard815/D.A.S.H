#include "titan.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "glance.h"  // asciiCopy

namespace dash {

bool parseTitan(const char* json, size_t len, TitanGlance& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if (!doc["up"].is<int>() || !doc["down"].is<int>() || !doc["services"].is<JsonArrayConst>()) return false;
  out = TitanGlance();
  out.up = doc["up"] | 0;
  out.down = doc["down"] | 0;
  out.pending = doc["pending"] | 0;
  out.notes = doc["notes"] | 0;
  out.every = doc["every"] | 0;
  for (JsonObjectConst d : doc["down_list"].as<JsonArrayConst>()) {
    if (out.nDown >= TitanGlance::kDown) break;
    TitanDown& t = out.downs[out.nDown++];
    asciiCopy(t.name, sizeof t.name, d["name"] | "?");
    asciiCopy(t.detail, sizeof t.detail, d["detail"] | "");
    asciiCopy(t.cause, sizeof t.cause, d["cause"] | "");
  }
  for (JsonObjectConst s : doc["services"].as<JsonArrayConst>()) {
    if (out.nServices >= TitanGlance::kServices) break;
    TitanService& t = out.services[out.nServices++];
    asciiCopy(t.name, sizeof t.name, s["name"] | "?");
    const char* st = s["status"] | "pending";
    t.status = !strcmp(st, "up") ? 'u' : !strcmp(st, "down") ? 'd' : 'p';
  }
  return true;
}

void titanSummary(const TitanGlance& g, char* buf, size_t n) {
  if (g.down == 0) {
    if (g.pending && !g.up) snprintf(buf, n, "Checking %u...", g.pending);
    else snprintf(buf, n, "All %u up", g.up);
    return;
  }
  if (g.down == 1 && g.nDown) {
    snprintf(buf, n, "%s is down", g.downs[0].name);
    return;
  }
  int o = snprintf(buf, n, "%u down:", g.down);
  for (int i = 0; i < g.nDown && o > 0 && (size_t)o < n; i++) o += snprintf(buf + o, n - o, "%s %s", i ? "," : "", g.downs[i].name);
}

void titanDownWords(const TitanDown& d, char* buf, size_t n) {
  if (*d.cause) snprintf(buf, n, "down because %s is down", d.cause);
  else snprintf(buf, n, "%s", *d.detail ? d.detail : "not answering");
}

}  // namespace dash
