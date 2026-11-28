#include "atlas.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "glance.h"  // asciiCopy

namespace dash {

AtlasObd::AtlasObd()
    : ect(NAN), iat(NAN), oil(NAN), atf(NAN), volts(NAN), rpm(NAN), kph(NAN), fuel(NAN), l100(NAN), l100a(NAN), rangeKm(NAN) {}
AtlasWeather::AtlasWeather() : c(NAN), rh(NAN), hpa(NAN), hi(NAN), lo(NAN) {}
AtlasPower::AtlasPower() : volts(NAN), soc(NAN) {}
AtlasGps::AtlasGps() : kph(NAN), hdg(NAN) {}

namespace {
float num(JsonVariantConst v) { return v.is<float>() || v.is<int>() ? v.as<float>() : NAN; }

void copy(char* dst, size_t n, JsonVariantConst v) { asciiCopy(dst, n, v.is<const char*>() ? v.as<const char*>() : ""); }

// A code looks like P0420: a letter PCBU and four hex digits.
bool validCode(const char* c) {
  if (!c || strlen(c) != 5 || !strchr("PCBU", c[0])) return false;
  for (int i = 1; i < 5; i++)
    if (!strchr("0123456789ABCDEF", c[i])) return false;
  return true;
}
}  // namespace

AtlasFrame AtlasState::apply(const char* json, size_t len, uint32_t now) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json, len) || !doc.is<JsonObjectConst>()) return AtlasFrame::Bad;
  const char* t = doc["t"] | "";
  if (!*t) return AtlasFrame::Bad;
  AtlasFrame f = AtlasFrame::Other;

  if (!strcmp(t, "hi")) {
    f = AtlasFrame::Hi;
    copy(from, sizeof from, doc["from"]);
    copy(mode, sizeof mode, doc["mode"]);
    ign = (doc["ign"] | 0) != 0;
    upS = doc["up"] | 0;
  } else if (!strcmp(t, "bye")) {
    f = AtlasFrame::Bye;
    copy(byeWhy, sizeof byeWhy, doc["why"]);
  } else if (!strcmp(t, "obd")) {
    f = AtlasFrame::Obd;
    obd.ect = num(doc["ect"]);
    obd.iat = num(doc["iat"]);
    obd.oil = num(doc["oil"]);
    obd.atf = num(doc["atf"]);
    obd.volts = num(doc["v"]);
    obd.rpm = num(doc["rpm"]);
    obd.kph = num(doc["kph"]);
    obd.fuel = num(doc["fuel"]);
    obd.l100 = num(doc["l100"]);
    obd.l100a = num(doc["l100a"]);
    obd.rangeKm = num(doc["rng"]);
    obd.atMs = now;
    obd.have = true;
  } else if (!strcmp(t, "dtc")) {
    f = AtlasFrame::Dtc;
    AtlasCodes next;
    next.mil = (doc["mil"] | 0) != 0;
    for (JsonVariantConst c : doc["codes"].as<JsonArrayConst>()) {
      const char* s = c | "";
      if (!validCode(s) || next.n >= AtlasCodes::kMax) continue;
      strcpy(next.code[next.n], s);
      // Keep words the truck already sent for a code still listed.
      for (int i = 0; i < dtc.n; i++)
        if (!strcmp(dtc.code[i], s)) strcpy(next.words[next.n], dtc.words[i]);
      next.n++;
    }
    next.atMs = now;
    next.have = true;
    dtc = next;
  } else if (!strcmp(t, "dtx")) {
    f = AtlasFrame::Dtx;
    const char* c = doc["c"] | "";
    for (int i = 0; i < dtc.n; i++)
      if (!strcmp(dtc.code[i], c)) copy(dtc.words[i], sizeof dtc.words[i], doc["d"]);
  } else if (!strcmp(t, "sync")) {
    f = AtlasFrame::Sync;
    copy(sync.st, sizeof sync.st, doc["st"]);
    sync.queued = doc["q"] | 0;
    sync.lastUnix = doc["last"] | 0;
    copy(sync.via, sizeof sync.via, doc["via"]);
    copy(sync.err, sizeof sync.err, doc["err"]);
    sync.atMs = now;
    sync.have = true;
  } else if (!strcmp(t, "wx")) {
    f = AtlasFrame::Wx;
    wx.c = num(doc["c"]);
    wx.rh = num(doc["rh"]);
    wx.hpa = num(doc["hpa"]);
    wx.hi = num(doc["hi"]);
    wx.lo = num(doc["lo"]);
    copy(wx.txt, sizeof wx.txt, doc["txt"]);
    wx.atMs = now;
    wx.have = true;
  } else if (!strcmp(t, "pwr")) {
    f = AtlasFrame::Pwr;
    pwr.volts = num(doc["v"]);
    pwr.soc = num(doc["soc"]);
    copy(pwr.src, sizeof pwr.src, doc["src"]);
    pwr.atMs = now;
    pwr.have = true;
  } else if (!strcmp(t, "cmd")) {
    f = AtlasFrame::Cmd;
    copy(cmdWhat, sizeof cmdWhat, doc["what"]);
    copy(cmdMsg, sizeof cmdMsg, doc["msg"]);
    cmdOk = (doc["ok"] | 0) != 0;
    cmdAtMs = now;
  } else if (!strcmp(t, "gps")) {
    f = AtlasFrame::Gps;
    JsonVariantConst la = doc["lat"], lo = doc["lon"];
    if (la.is<double>() && lo.is<double>()) {
      gps.lat = la.as<double>();
      gps.lon = lo.as<double>();
      gps.kph = num(doc["kph"]);
      gps.hdg = num(doc["hdg"]);
      gps.atMs = now;
      gps.have = true;
    }
  }
  uint32_t ts = doc["ts"] | 0u;
  if (ts > 1700000000u) {
    clockUnix = ts;
    clockAtMs = now;
  }
  // Any frame but bye means it's awake again.
  asleep = f == AtlasFrame::Bye;
  if (!asleep) byeWhy[0] = 0;
  heard = true;
  lastMs = now;
  rev++;
  return f;
}

bool atlasLive(const AtlasState& s, uint32_t now) { return s.heard && !s.asleep && now - s.lastMs <= kAtlasStaleMs; }

uint32_t atlasUnixNow(const AtlasState& s, uint32_t now) { return s.clockUnix ? s.clockUnix + (now - s.clockAtMs) / 1000 : 0; }

void ageText(uint32_t s, char* buf, size_t n) {
  if (s < 10) snprintf(buf, n, "just now");
  else if (s < 60) snprintf(buf, n, "%u s ago", (unsigned)s);
  else if (s < 3600) snprintf(buf, n, "%u min ago", (unsigned)(s / 60));
  else if (s < 2 * 86400) snprintf(buf, n, "%u h ago", (unsigned)(s / 3600));
  else snprintf(buf, n, "%u days ago", (unsigned)(s / 86400));
}

void atlasLinkWords(const AtlasState& s, uint32_t now, char* buf, size_t n) {
  if (!s.heard) { snprintf(buf, n, "Not heard yet"); return; }
  char age[20];
  ageText((now - s.lastMs) / 1000, age, sizeof age);
  if (s.asleep) { snprintf(buf, n, "Asleep, since %s", age); return; }
  if (now - s.lastMs > kAtlasStaleMs) { snprintf(buf, n, "No word since %s", age); return; }
  snprintf(buf, n, "Live");
}

void fmtTemp(char* buf, size_t n, float c, Units u) {
  if (isnan(c)) { snprintf(buf, n, "--"); return; }
  if (u == Units::US) snprintf(buf, n, "%d F", (int)lroundf(c * 9.0f / 5.0f + 32.0f));
  else snprintf(buf, n, "%d C", (int)lroundf(c));
}

void fmtEconomy(char* buf, size_t n, float l100, Units u) {
  if (isnan(l100) || l100 <= 0.0f) { snprintf(buf, n, "--"); return; }
  if (u == Units::US) snprintf(buf, n, "%.1f mpg", 235.215f / l100);
  else snprintf(buf, n, "%.1f L/100km", l100);
}

void fmtDistance(char* buf, size_t n, float km, Units u) {
  if (isnan(km) || km < 0) { snprintf(buf, n, "--"); return; }
  if (u == Units::US) snprintf(buf, n, "%d mi", (int)lroundf(km * 0.621371f));
  else snprintf(buf, n, "%d km", (int)lroundf(km));
}

void fmtSpeed(char* buf, size_t n, float kph, Units u) {
  if (isnan(kph) || kph < 0) { snprintf(buf, n, "--"); return; }
  if (u == Units::US) snprintf(buf, n, "%d mph", (int)lroundf(kph * 0.621371f));
  else snprintf(buf, n, "%d km/h", (int)lroundf(kph));
}

void fmtVolts(char* buf, size_t n, float v) {
  if (isnan(v)) snprintf(buf, n, "--");
  else snprintf(buf, n, "%.1f V", v);
}

const char* coolantWarning(float c) {
  if (isnan(c)) return "";
  return c >= 112.0f ? "HOT" : "";
}

const char* voltsWarning(float v, bool running) {
  if (isnan(v)) return "";
  if (v > 15.2f) return "HIGH";
  // A running engine charges at about 13.5-14.7 V; off, a healthy battery rests near 12.6 V.
  if (running ? v < 13.0f : v < 12.0f) return "LOW";
  return "";
}

namespace {
struct Known {
  const char* code;
  const char* words;
};
// Generic (SAE) codes that mean the same on every make. Plain words.
const Known kCodes[] = {
    {"P0010", "Camshaft timing actuator circuit (bank 1)"},
    {"P0011", "Camshaft timing too far advanced (bank 1)"},
    {"P0087", "Fuel pressure too low"},
    {"P0101", "Air flow sensor reading out of range"},
    {"P0102", "Air flow sensor signal low"},
    {"P0113", "Intake air temperature sensor signal high"},
    {"P0117", "Coolant temperature sensor signal low"},
    {"P0118", "Coolant temperature sensor signal high"},
    {"P0121", "Throttle position sensor reading out of range"},
    {"P0128", "Engine not warming up: check the thermostat"},
    {"P0133", "Oxygen sensor slow to respond (bank 1, sensor 1)"},
    {"P0135", "Oxygen sensor heater circuit (bank 1, sensor 1)"},
    {"P0141", "Oxygen sensor heater circuit (bank 1, sensor 2)"},
    {"P0171", "Engine running lean (bank 1): often a vacuum leak"},
    {"P0172", "Engine running rich (bank 1)"},
    {"P0174", "Engine running lean (bank 2): often a vacuum leak"},
    {"P0175", "Engine running rich (bank 2)"},
    {"P0217", "Engine overheating"},
    {"P0300", "Misfires in several cylinders"},
    {"P0325", "Knock sensor circuit (bank 1)"},
    {"P0335", "Crankshaft position sensor circuit"},
    {"P0340", "Camshaft position sensor circuit (bank 1)"},
    {"P0401", "Exhaust gas recirculation (EGR) flow too low"},
    {"P0402", "Exhaust gas recirculation (EGR) flow too high"},
    {"P0420", "Catalytic converter not cleaning well enough (bank 1)"},
    {"P0430", "Catalytic converter not cleaning well enough (bank 2)"},
    {"P0440", "Fuel vapour (EVAP) system fault"},
    {"P0442", "Small fuel vapour leak: check the gas cap"},
    {"P0446", "Fuel vapour (EVAP) vent control circuit"},
    {"P0455", "Large fuel vapour leak: check the gas cap"},
    {"P0456", "Very small fuel vapour leak: check the gas cap"},
    {"P0480", "Cooling fan control circuit"},
    {"P0500", "Vehicle speed sensor"},
    {"P0505", "Idle control system"},
    {"P0506", "Idle speed lower than expected"},
    {"P0507", "Idle speed higher than expected"},
    {"P0520", "Oil pressure sensor circuit"},
    {"P0524", "Engine oil pressure too low"},
    {"P0562", "System voltage low: check battery and alternator"},
    {"P0563", "System voltage high"},
    {"P0700", "Transmission computer has a fault stored"},
    {"P0715", "Transmission input speed sensor"},
    {"P0720", "Transmission output speed sensor"},
    {"P0740", "Torque converter clutch circuit"},
    {"P0750", "Transmission shift solenoid A"},
    {"P2135", "Throttle position sensors disagree"},
    {"U0100", "Lost contact with the engine computer"},
    {"U0101", "Lost contact with the transmission computer"},
    {"U0121", "Lost contact with the ABS computer"},
    {"U0155", "Lost contact with the instrument cluster"},
};
char cylBuf[32];
}  // namespace

const char* dtcWords(const char* code) {
  if (!code) return nullptr;
  for (const Known& k : kCodes)
    if (!strcmp(k.code, code)) return k.words;
  // P0301-P0312: misfire in one cylinder.
  if (!strncmp(code, "P03", 3) && strlen(code) == 5 && code[3] >= '0' && code[3] <= '1' && code[4] >= '0' && code[4] <= '9') {
    int cyl = (code[3] - '0') * 10 + (code[4] - '0');
    if (cyl >= 1 && cyl <= 12) {
      snprintf(cylBuf, sizeof cylBuf, "Misfire in cylinder %d", cyl);
      return cylBuf;
    }
  }
  return nullptr;
}

void dtcArea(const char* code, char* buf, size_t n) {
  if (n) buf[0] = 0;
  if (!validCode(code)) return;
  const char* sys = code[0] == 'P' ? "Engine/transmission" : code[0] == 'C' ? "Chassis" : code[0] == 'B' ? "Body" : "Network";
  const char* who = (code[1] == '1' || code[1] == '3') ? ", maker-specific" : "";
  const char* what = "";
  if (code[0] == 'P' && (code[1] == '0' || code[1] == '2')) {
    switch (code[2]) {
      case '0': case '1': case '2': what = ": fuel and air"; break;
      case '3': what = ": ignition or misfire"; break;
      case '4': what = ": emissions"; break;
      case '5': what = ": speed and idle"; break;
      case '6': what = ": engine computer"; break;
      case '7': case '8': case '9': what = ": transmission"; break;
      default: break;
    }
  }
  snprintf(buf, n, "%s%s%s", sys, what, who);
}

void dtcDescribe(const AtlasCodes& c, int i, char* buf, size_t n) {
  if (n) buf[0] = 0;
  if (i < 0 || i >= c.n) return;
  if (*c.words[i]) { snprintf(buf, n, "%s", c.words[i]); return; }
  const char* w = dtcWords(c.code[i]);
  if (w) { snprintf(buf, n, "%s", w); return; }
  dtcArea(c.code[i], buf, n);
}

bool dtcUrgent(const char* code) {
  if (!code) return false;
  if (!strcmp(code, "P0217") || !strcmp(code, "P0524") || !strcmp(code, "P0300")) return true;
  const char* w = dtcWords(code);
  return w && !strncmp(w, "Misfire", 7);
}

const char* syncWords(const char* st) {
  if (!st || !*st) return "Sync not known";
  if (!strcmp(st, "waiting")) return "Waiting to sync";
  if (!strcmp(st, "syncing")) return "Syncing";
  if (!strcmp(st, "synced")) return "Synced";
  if (!strcmp(st, "failed")) return "Sync didn't work";
  if (!strcmp(st, "off")) return "Sync off";
  return "Sync not known";
}

void atlasSummary(const AtlasState& s, uint32_t now, uint32_t nowUnix, Units u, char* buf, size_t n, bool* attention) {
  bool att = false;
  if (!s.heard) {
    snprintf(buf, n, "Not heard yet");
  } else if (s.dtc.have && s.dtc.n > 0) {
    bool urgent = false;
    for (int i = 0; i < s.dtc.n; i++) urgent = urgent || dtcUrgent(s.dtc.code[i]);
    att = true;
    if (s.dtc.n == 1) snprintf(buf, n, "%s %s", urgent ? "Stop soon:" : "Check engine:", s.dtc.code[0]);
    else snprintf(buf, n, "%s %d codes", urgent ? "Stop soon:" : "Check engine:", s.dtc.n);
  } else if (atlasLive(s, now) && s.obd.have) {
    char t[12], v[12], m[16];
    fmtTemp(t, sizeof t, s.obd.ect, u);
    fmtVolts(v, sizeof v, s.obd.volts);
    fmtDistance(m, sizeof m, s.obd.rangeKm, u);
    att = *coolantWarning(s.obd.ect) || *voltsWarning(s.obd.volts, s.ign);
    snprintf(buf, n, "%s, %s, %s range", t, v, m);
  } else {
    char link[32];
    atlasLinkWords(s, now, link, sizeof link);
    if (s.sync.have && s.sync.lastUnix && nowUnix > s.sync.lastUnix) {
      char age[20];
      ageText(nowUnix - s.sync.lastUnix, age, sizeof age);
      snprintf(buf, n, "%s. Synced %s", link, age);
    } else if (s.sync.have) {
      snprintf(buf, n, "%s. %s", link, syncWords(s.sync.st));
    } else {
      snprintf(buf, n, "%s", link);
    }
    att = s.sync.have && !strcmp(s.sync.st, "failed");
  }
  if (attention) *attention = att;
}

const char* atlasPowerSource(const char* src) {
  if (!strcmp(src, "truck")) return "on truck power";
  if (!strcmp(src, "overland")) return "on the overland battery";
  if (!strcmp(src, "reserve")) return "on its reserve battery";
  return "";
}

}  // namespace dash
