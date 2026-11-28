// Home Assistant: a few entities and alerts, read with the REST API
// (GET /api/states/<entity_id>, a long-lived token). The pure parts: the
// entity list from secrets.h, parsing one state reply with small fixed
// buffers, and saying it in words. See docs/mods/ha.md.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

namespace dash {

enum class HaStatus : uint8_t { Unknown, Ok, Unavailable, Missing };

// One state reply, cut to what DASH shows. Text is plain ASCII.
struct HaReading {
  HaStatus status = HaStatus::Unknown;
  char name[28] = "";   // attributes.friendly_name
  char state[20] = "";  // "on", "21.5", "unlocked"
  char unit[8] = "";    // attributes.unit_of_measurement ("°C" becomes "C")
  char dc[16] = "";     // attributes.device_class ("door", "moisture")
  uint32_t changed = 0; // last_changed in unix seconds; 0: unknown
};

struct HaEntity {
  const char* id = nullptr;  // points into the config string, which must outlive it
  uint8_t idLen = 0;
  bool alertable = false;  // listed in DASH_HA_ALERTS
  HaReading r;
};

struct HaBoard {
  static constexpr int kMax = 8;
  HaEntity e[kMax];
  uint8_t n = 0;
  uint8_t alertMask = 0;  // which entities were alerting at the last check
  bool alertSeen = true;  // set once the person has looked
};

// Fills the board from two comma-separated lists of entity ids
// ("binary_sensor.front_door, lock.back"). Alert entities are shown too; an
// id in both lists is shown once. Ids that aren't "domain.object_id" in
// lowercase letters, digits and "_" are skipped. At most kMax. Returns n.
int haConfigure(HaBoard& b, const char* entities, const char* alerts);

void haEntityId(const HaEntity& e, char* buf, size_t n);  // "light.kitchen"
void haDomain(const HaEntity& e, char* buf, size_t n);    // "light"

// The ArduinoJson filter that keeps only the fields read here, so a reply
// with big attributes (a weather or media entity) stays small in RAM.
void haStateFilter(JsonDocument& filter);
// Reads a parsed (filtered) state reply. False if it isn't one; out is then
// left as it was.
bool haReadState(const JsonDocument& doc, HaReading& out);
bool parseHaState(const char* json, size_t len, HaReading& out);

// "2026-11-28T10:15:03.123456+00:00" to unix seconds; 0 if it isn't one.
uint32_t haParseTime(const char* iso);

// "on", "open", "unlocked", "problem": an alert entity in one of these alerts.
bool haAlertState(const char* state);
bool haAlerting(const HaEntity& e);

// The state in words: a door's "on" is "open", a leak sensor's is "wet";
// "unavailable", "not found", "..." (not read yet).
const char* haStateWords(const HaEntity& e);
// State and unit: "21.5 C", "open".
void haValueText(const HaEntity& e, char* buf, size_t n);
// A name for the screen: friendly_name, else the id after the dot.
void haName(const HaEntity& e, char* buf, size_t n);

// The Home card line: "All quiet", "Front door open", "2 alerts: Front
// door, Garage", "All quiet, 1 unavailable", "Asking Home Assistant...".
void haSummary(const HaBoard& b, char* buf, size_t n);
int haAlertCount(const HaBoard& b);

// Shown order: alerting entities first, then the rest as configured.
int haOrder(const HaBoard& b, uint8_t* out);

// After new readings. Start: an alert entity just turned on (light and
// buzz). Stop: every alert cleared before anyone looked.
enum class HaAlertChange : uint8_t { None, Start, Stop };
HaAlertChange haCheckAlerts(HaBoard& b);
// The person looked (opened the screen). True if an alert was waiting.
bool haMarkSeen(HaBoard& b);

// A switch, light, input_boolean or fan that reads on or off.
bool haCanToggle(const HaEntity& e);
// {"entity_id":"light.kitchen"} for POST /api/services/<domain>/toggle.
void haToggleBody(const HaEntity& e, char* buf, size_t n);

}  // namespace dash
