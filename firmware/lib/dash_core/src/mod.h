// Mods: optional integrations for other apps (a truck, Home Assistant,
// Overseer, Argos...). Each mod is a link (it shows in Links with an On/Off
// switch), may have its own screen, its own settings and a Home card, and
// may receive dash-mod frames from ESP-NOW or MQTT. See docs/mods/README.md.
//
// Mods are compiled in (an ESP32 can't load code at run time); each one has
// a build flag (DASH_MOD_<NAME>) and a switch on the device.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "connector.h"

namespace dash {

// The drawing side of a mod. The firmware defines it (src/mods/view.h), so
// this library stays free of screen code and testable on a computer.
class ModView;

// One choice on a mod's settings screen, saved on the device by key.
struct ModSetting {
  const char* key;              // 7 characters at most (flash keys are short)
  const char* label;            // "Units"
  const char* const* options;   // {"US", "Metric"}
  uint8_t count;
  uint8_t value;                // index into options; the default until loaded

  const char* current() const { return value < count ? options[value] : ""; }
  void next() { value = count ? (uint8_t)((value + 1) % count) : 0; }
  void set(uint8_t v) { value = v < count ? v : 0; }
};

// A Home screen card: a title and one plain line.
struct HomeCard {
  char title[24] = "";
  char line[56] = "";
  bool attention = false;  // shown in words too ("Check engine"), never colour alone
};

// How a frame arrived.
enum class Via : uint8_t { EspNow, Mqtt, Other };
const char* viaText(Via v);  // "ESP-NOW", "Wi-Fi", "?"

class Mod : public Connector {
 public:
  // Short lowercase id: "truck". Also the "m" key of its dash-mod frames
  // and the prefix of its saved settings.
  virtual const char* id() const = 0;
  // Its own screen, or none.
  virtual bool hasScreen() const { return false; }
  virtual ModView* view() { return nullptr; }
  // Settings shown on its settings screen (tap its row in Links).
  virtual int settingCount() const { return 0; }
  virtual ModSetting* setting(int) { return nullptr; }
  virtual void settingChanged(int) {}
  // Fills a Home card; false shows none.
  virtual bool homeCard(HomeCard&, uint32_t /*nowMs*/) const { return false; }
  // A dash-mod frame whose "m" is this mod's id, from any transport.
  virtual void onFrame(const char* /*json*/, size_t /*len*/, Via /*via*/, uint32_t /*nowMs*/) {}
};

// The compiled-in mods, in the order they were added.
class ModList {
 public:
  static constexpr int kMax = 8;
  bool add(Mod* m);
  int count() const { return n_; }
  Mod* at(int i) const { return (i >= 0 && i < n_) ? list_[i] : nullptr; }
  Mod* find(const char* id) const;
  // Enabled mods with a screen, in order. Returns how many.
  int screens(Mod** out, int max) const;
  // Hands a frame to the enabled mod named by its "m" key. False when no
  // enabled mod takes it, or it isn't a frame.
  bool route(const char* json, size_t len, Via via, uint32_t nowMs);

 private:
  Mod* list_[kMax] = {};
  int n_ = 0;
};

// Reads "m" and "t" from a dash-mod frame (one JSON object, at most
// kFrameMax bytes). False if it isn't one.
constexpr size_t kFrameMax = 240;
bool peekFrame(const char* json, size_t len, char* mod, size_t nMod, char* type, size_t nType);

}  // namespace dash
