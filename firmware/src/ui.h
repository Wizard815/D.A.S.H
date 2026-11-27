// Screens. Portrait 320x480: status bar on top, tab bar at the bottom.
#pragma once
#include <stdint.h>

#include "connector.h"

class FilesLink;
namespace dash {
class ModList;
}

namespace ui {

// Tabs: Home, Files, mods (FAITH, Atlas; or a Mods list), Links. Device and
// a mod's settings open from Links.
enum class Screen : uint8_t { Home, Files, Mod, Mods, Links, Device, ModSettings, Talk, kCount };

void begin(dash::Registry* links, FilesLink* files, dash::ModList* mods);
// Runs the touch calibration screen and saves the result.
void calibrateTouch();
void loop(uint32_t nowMs);

void show(Screen s);
void nextTab();
Screen current();

// Push-to-talk placeholder until the Hermes link lands (M2).
void talkStart();
void talkEnd();

// Any touch or button press; used to wake the backlight.
uint32_t lastActivity();
void noteActivity(uint32_t nowMs);

}  // namespace ui
