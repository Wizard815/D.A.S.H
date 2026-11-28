#include "ha.h"

#include <stdio.h>
#include <string.h>

#include "glance.h"  // asciiCopy

namespace dash {

namespace {

bool idChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

// A valid "domain.object_id". Also keeps URLs and JSON bodies safe: no
// characters that would need escaping.
bool validId(const char* s, size_t len) {
  if (len < 3 || len > 64) return false;
  size_t dot = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '.') {
      if (dot || i == 0) return false;
      dot = i;
    } else if (!idChar(s[i])) {
      return false;
    }
  }
  return dot && dot + 1 < len;
}

int find(const HaBoard& b, const char* s, size_t len) {
  for (int i = 0; i < b.n; i++)
    if (b.e[i].idLen == len && !strncmp(b.e[i].id, s, len)) return i;
  return -1;
}

void addList(HaBoard& b, const char* list, bool alert) {
  if (!list) return;
  const char* p = list;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    const char* s = p;
    while (*p && *p != ',') p++;
    const char* end = p;
    while (end > s && end[-1] == ' ') end--;
    size_t len = end - s;
    if (!len || !validId(s, len)) continue;
    int at = find(b, s, len);
    if (at >= 0) {
      if (alert) b.e[at].alertable = true;
      continue;
    }
    if (b.n >= HaBoard::kMax) return;
    HaEntity& e = b.e[b.n++];
    e = HaEntity();
    e.id = s;
    e.idLen = (uint8_t)len;
    e.alertable = alert;
  }
}

bool isDomain(const HaEntity& e, const char* d) {
  size_t n = strlen(d);
  return e.idLen > n && !strncmp(e.id, d, n) && e.id[n] == '.';
}

int digits(const char*& p, int n) {
  int v = 0;
  for (int i = 0; i < n; i++, p++) {
    if (*p < '0' || *p > '9') return -1;
    v = v * 10 + (*p - '0');
  }
  return v;
}

// Days since 1970-01-01 (proleptic Gregorian).
int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

struct Words {
  const char* dc;
  const char* on;
  const char* off;
};
// How Home Assistant itself says a binary sensor's on and off.
const Words kBinary[] = {
    {"door", "open", "closed"},         {"garage_door", "open", "closed"},
    {"window", "open", "closed"},       {"opening", "open", "closed"},
    {"lock", "unlocked", "locked"},     {"moisture", "wet", "dry"},
    {"smoke", "smoke", "clear"},        {"gas", "gas", "clear"},
    {"carbon_monoxide", "CO", "clear"}, {"motion", "motion", "clear"},
    {"occupancy", "occupied", "clear"}, {"presence", "home", "away"},
    {"problem", "problem", "OK"},       {"safety", "unsafe", "safe"},
    {"battery", "low", "normal"},       {"tamper", "tampered", "clear"},
    {"connectivity", "connected", "disconnected"},
};

}  // namespace

int haConfigure(HaBoard& b, const char* entities, const char* alerts) {
  b = HaBoard();
  addList(b, entities, false);
  addList(b, alerts, true);
  return b.n;
}

void haEntityId(const HaEntity& e, char* buf, size_t n) {
  if (!n) return;
  size_t len = e.idLen < n - 1 ? e.idLen : n - 1;
  if (e.id) memcpy(buf, e.id, len);
  else len = 0;
  buf[len] = 0;
}

void haDomain(const HaEntity& e, char* buf, size_t n) {
  if (!n) return;
  size_t len = 0;
  while (e.id && len < e.idLen && e.id[len] != '.') len++;
  if (len > n - 1) len = n - 1;
  if (e.id) memcpy(buf, e.id, len);
  buf[len] = 0;
}

void haStateFilter(JsonDocument& f) {
  f.clear();
  f["state"] = true;
  f["last_changed"] = true;
  JsonObject a = f["attributes"].to<JsonObject>();
  a["friendly_name"] = true;
  a["unit_of_measurement"] = true;
  a["device_class"] = true;
}

bool haReadState(const JsonDocument& doc, HaReading& out) {
  const char* st = doc["state"];
  if (!st) return false;
  out = HaReading();
  asciiCopy(out.state, sizeof out.state, st);
  out.status = !strcmp(st, "unavailable") ? HaStatus::Unavailable : HaStatus::Ok;
  JsonObjectConst a = doc["attributes"];
  asciiCopy(out.name, sizeof out.name, a["friendly_name"] | "");
  const char* u = a["unit_of_measurement"] | "";
  if (!strncmp(u, "\xC2\xB0", 2)) u += 2;  // "°C" -> "C": the fonts have no degree sign
  asciiCopy(out.unit, sizeof out.unit, u);
  asciiCopy(out.dc, sizeof out.dc, a["device_class"] | "");
  out.changed = haParseTime(doc["last_changed"] | "");
  return true;
}

bool parseHaState(const char* json, size_t len, HaReading& out) {
  JsonDocument filter;
  haStateFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  return haReadState(doc, out);
}

uint32_t haParseTime(const char* s) {
  if (!s || strlen(s) < 19) return 0;
  const char* p = s;
  int y = digits(p, 4);
  if (*p++ != '-') return 0;
  int mo = digits(p, 2);
  if (*p++ != '-') return 0;
  int d = digits(p, 2);
  if (*p != 'T' && *p != ' ') return 0;
  p++;
  int h = digits(p, 2);
  if (*p++ != ':') return 0;
  int mi = digits(p, 2);
  if (*p++ != ':') return 0;
  int sec = digits(p, 2);
  if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60)
    return 0;
  if (*p == '.')
    for (p++; *p >= '0' && *p <= '9'; p++) {
    }
  int64_t t = (int64_t)daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
  if (*p == '+' || *p == '-') {
    int sign = *p++ == '+' ? 1 : -1;
    int oh = digits(p, 2);
    if (*p == ':') p++;
    int om = digits(p, 2);
    if (oh < 0 || om < 0) return 0;
    t -= sign * (oh * 3600 + om * 60);
  }
  return t > 0 && t < 0xFFFFFFFFLL ? (uint32_t)t : 0;
}

bool haAlertState(const char* s) {
  return !strcmp(s, "on") || !strcmp(s, "open") || !strcmp(s, "unlocked") || !strcmp(s, "problem");
}

bool haAlerting(const HaEntity& e) { return e.alertable && e.r.status == HaStatus::Ok && haAlertState(e.r.state); }

const char* haStateWords(const HaEntity& e) {
  switch (e.r.status) {
    case HaStatus::Unknown: return "...";
    case HaStatus::Missing: return "not found";
    case HaStatus::Unavailable: return "unavailable";
    case HaStatus::Ok: break;
  }
  bool on = !strcmp(e.r.state, "on"), off = !strcmp(e.r.state, "off");
  if (isDomain(e, "binary_sensor") && (on || off) && *e.r.dc)
    for (const Words& w : kBinary)
      if (!strcmp(w.dc, e.r.dc)) return on ? w.on : w.off;
  return e.r.state;
}

void haValueText(const HaEntity& e, char* buf, size_t n) {
  const char* w = haStateWords(e);
  if (e.r.status == HaStatus::Ok && *e.r.unit) snprintf(buf, n, "%s %s", w, e.r.unit);
  else snprintf(buf, n, "%s", w);
}

void haName(const HaEntity& e, char* buf, size_t n) {
  if (*e.r.name) {
    snprintf(buf, n, "%s", e.r.name);
    return;
  }
  char id[72];
  haEntityId(e, id, sizeof id);
  const char* dot = strchr(id, '.');
  snprintf(buf, n, "%s", dot ? dot + 1 : id);
}

int haAlertCount(const HaBoard& b) {
  int a = 0;
  for (int i = 0; i < b.n; i++) a += haAlerting(b.e[i]);
  return a;
}

void haSummary(const HaBoard& b, char* buf, size_t n) {
  if (!b.n) {
    snprintf(buf, n, "No entities set");
    return;
  }
  int a = 0, read = 0, bad = 0;
  for (int i = 0; i < b.n; i++) {
    const HaEntity& e = b.e[i];
    a += haAlerting(e);
    read += e.r.status != HaStatus::Unknown;
    bad += e.r.status == HaStatus::Missing || e.r.status == HaStatus::Unavailable;
  }
  char nm[32];
  if (a == 1) {
    for (int i = 0; i < b.n; i++)
      if (haAlerting(b.e[i])) {
        haName(b.e[i], nm, sizeof nm);
        snprintf(buf, n, "%s %s", nm, haStateWords(b.e[i]));
      }
    return;
  }
  if (a > 1) {
    int o = snprintf(buf, n, "%d alerts:", a);
    bool first = true;
    for (int i = 0; i < b.n && o > 0 && (size_t)o < n; i++) {
      if (!haAlerting(b.e[i])) continue;
      haName(b.e[i], nm, sizeof nm);
      o += snprintf(buf + o, n - o, "%s %s", first ? "" : ",", nm);
      first = false;
    }
    return;
  }
  if (!read) snprintf(buf, n, "Asking Home Assistant...");
  else if (bad) snprintf(buf, n, "All quiet, %d unavailable", bad);
  else snprintf(buf, n, "All quiet");
}

int haOrder(const HaBoard& b, uint8_t* out) {
  int k = 0;
  for (int i = 0; i < b.n; i++)
    if (haAlerting(b.e[i])) out[k++] = (uint8_t)i;
  for (int i = 0; i < b.n; i++)
    if (!haAlerting(b.e[i])) out[k++] = (uint8_t)i;
  return k;
}

HaAlertChange haCheckAlerts(HaBoard& b) {
  uint8_t mask = 0;
  for (int i = 0; i < b.n; i++)
    if (haAlerting(b.e[i])) mask |= (uint8_t)(1u << i);
  uint8_t rising = mask & (uint8_t)~b.alertMask;
  b.alertMask = mask;
  if (rising) {
    b.alertSeen = false;
    return HaAlertChange::Start;
  }
  if (!mask && !b.alertSeen) {
    b.alertSeen = true;
    return HaAlertChange::Stop;
  }
  return HaAlertChange::None;
}

bool haMarkSeen(HaBoard& b) {
  if (b.alertSeen) return false;
  b.alertSeen = true;
  return true;
}

bool haCanToggle(const HaEntity& e) {
  if (e.r.status != HaStatus::Ok) return false;
  if (strcmp(e.r.state, "on") && strcmp(e.r.state, "off")) return false;
  return isDomain(e, "switch") || isDomain(e, "light") || isDomain(e, "input_boolean") || isDomain(e, "fan");
}

void haToggleBody(const HaEntity& e, char* buf, size_t n) {
  char id[72];
  haEntityId(e, id, sizeof id);
  snprintf(buf, n, "{\"entity_id\":\"%s\"}", id);
}

}  // namespace dash
