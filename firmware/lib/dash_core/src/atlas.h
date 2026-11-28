// Atlas (the truck homelab) as DASH sees it: the latest of each dash-mod
// frame the truck sends, and plain words for the screen. Pure logic, no
// radio code; the firmware feeds frames in from ESP-NOW or MQTT.
// Wire format: docs/mods/atlas-protocol.md. Metric on the wire.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

constexpr uint32_t kAtlasStaleMs = 30000;  // older than this: say how old

enum class Units : uint8_t { US, Metric };

struct AtlasObd {
  // NAN when the truck didn't send it.
  float ect, iat, oil, atf;  // coolant, intake air, oil, transmission: deg C
  float volts, rpm, kph, fuel;  // fuel: percent
  float l100, l100a;            // L/100 km now, trip average
  float rangeKm;
  uint32_t atMs = 0;
  bool have = false;
  AtlasObd();
};

struct AtlasCodes {
  static constexpr int kMax = 8;
  char code[kMax][8] = {};
  char words[kMax][104] = {};  // from a dtx frame; empty: use DASH's own table
  uint8_t n = 0;
  bool mil = false;  // check-engine light on
  uint32_t atMs = 0;
  bool have = false;
};

struct AtlasSync {
  char st[10] = "";  // waiting, syncing, synced, failed, off
  uint32_t queued = 0;
  uint32_t lastUnix = 0;  // last good sync, unix seconds; 0 never
  char via[24] = "";
  char err[64] = "";
  uint32_t atMs = 0;
  bool have = false;
};

struct AtlasWeather {
  float c, rh, hpa, hi, lo;  // NAN when unknown
  char txt[24] = "";
  uint32_t atMs = 0;
  bool have = false;
  AtlasWeather();
};

struct AtlasPower {
  float volts, soc;  // the brain's own battery
  char src[10] = "";  // live power source: truck, overland or reserve; "" if not said
  uint32_t atMs = 0;
  bool have = false;
  AtlasPower();
};

struct AtlasGps {
  double lat = 0, lon = 0;
  float kph, hdg;
  uint32_t atMs = 0;
  bool have = false;
  AtlasGps();
};

enum class AtlasFrame : uint8_t { Bad, Hi, Bye, Obd, Dtc, Dtx, Sync, Wx, Pwr, Gps, Cmd, Alert, Other };

struct AtlasState {
  AtlasObd obd;
  AtlasCodes dtc;
  AtlasSync sync;
  AtlasWeather wx;
  AtlasPower pwr;
  AtlasGps gps;
  char from[16] = "";   // "truckd" or "bridge"
  char mode[10] = "";   // driving, share, sync
  bool ign = false;
  uint32_t upS = 0;
  bool heard = false;   // any frame since boot
  uint32_t lastMs = 0;  // last frame of any kind
  bool asleep = false;  // said bye, nothing since
  char byeWhy[16] = "";
  // The last owner command's result (remote start), from a cmd frame.
  char cmdWhat[12] = "", cmdMsg[64] = "";
  bool cmdOk = false;
  uint32_t cmdAtMs = 0;
  // The latest alert (cooler, cam, theft, lowbatt). lvl 0: cleared.
  char alertK[10] = "", alertId[16] = "", alertMsg[72] = "";
  uint8_t alertLvl = 0;
  bool alertSnap = false;  // a cam snapshot is waiting in the phone app
  bool alertSeen = true;   // set once the person has looked (opened Atlas)
  uint32_t alertAtMs = 0;
  uint32_t rev = 0;     // bumps on every applied frame
  uint32_t clockUnix = 0, clockAtMs = 0;  // from any frame's "ts": the truck's clock

  // Applies one frame (its "m" already checked). Bad: not JSON or no "t".
  AtlasFrame apply(const char* json, size_t len, uint32_t nowMs);
};

// How Atlas is doing, in words: "Live", "Asleep", "No word for 2 min", "Not heard yet".
void atlasLinkWords(const AtlasState& s, uint32_t nowMs, char* buf, size_t n);
bool atlasLive(const AtlasState& s, uint32_t nowMs);
// Unix time from the truck's own clock ("ts"), for when DASH has no NTP
// (the truck network has no internet). 0 if the truck never sent one.
uint32_t atlasUnixNow(const AtlasState& s, uint32_t nowMs);

// "just now", "45 s ago", "12 min ago", "3 h ago", "2 days ago".
void ageText(uint32_t ageS, char* buf, size_t n);

// Values with units; "--" when unknown.
void fmtTemp(char* buf, size_t n, float c, Units u);
void fmtEconomy(char* buf, size_t n, float l100, Units u);  // "17.8 mpg" or "13.2 L/100km"
void fmtDistance(char* buf, size_t n, float km, Units u);   // "250 mi"
void fmtSpeed(char* buf, size_t n, float kph, Units u);     // "60 mph"
void fmtVolts(char* buf, size_t n, float v);                // "14.1 V"

// Warnings, as a word: "HOT", "LOW", "HIGH", or "" when fine.
const char* coolantWarning(float c);
const char* voltsWarning(float v, bool running);

// Check-engine codes in plain words. Table of common generic codes, else
// the code's area ("Powertrain: ignition or misfire"), else "".
const char* dtcWords(const char* code);
void dtcArea(const char* code, char* buf, size_t n);
// The best words for code i: the truck's own, DASH's table, then the area.
void dtcDescribe(const AtlasCodes& c, int i, char* buf, size_t n);
// Codes that mean pull over soon (overheating, misfire, oil pressure).
bool dtcUrgent(const char* code);

// "Waiting to sync", "Syncing", "Synced", "Sync didn't work", "Sync off".
const char* syncWords(const char* st);

// One line for the Home card and the Links row.
void atlasSummary(const AtlasState& s, uint32_t nowMs, uint32_t nowUnix, Units u, char* buf, size_t n, bool* attention);

// The power source in words: "on truck power", "on the overland battery",
// "on its reserve battery", or "" when not said.
const char* atlasPowerSource(const char* src);

// How DASH signals an alert level on its light and vibration motor.
// lvl 1 (info): blue, one short buzz, once.
// lvl 2 (warning): amber blinking slowly, two buzzes, again every 5 min
//   until it's seen.
// lvl 3 (urgent: theft, food warm for long): red blinking fast, three long
//   buzzes, again every 30 s until it's seen.
struct AlertSignal {
  bool r, g, b;
  uint16_t blinkMs;       // 0: steady
  uint8_t buzzes;         // pulses per round
  uint16_t buzzMs, gapMs;
  uint32_t repeatMs;      // 0: once
};
AlertSignal alertSignal(uint8_t lvl);

// "Cooler", "Camera", "Theft", "Low battery"; the raw kind otherwise.
const char* alertKindWords(const char* k);

}  // namespace dash
