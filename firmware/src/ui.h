// Screens. Portrait 320x480: status bar on top, tab bar at the bottom.
#pragma once
#include <stdint.h>

#include "connector.h"

class ProjectsLink;
class FilesLink;
namespace dash {
class ModList;
}

namespace ui {

// Tabs: Home, Projects, Files, a mod (or Mods), Links. Device and a mod's
// settings open from Links.
enum class Screen : uint8_t { Home, Projects, Files, Mod, Mods, Links, Device, ModSettings, Talk, kCount };

// `projects` may be null (no FAITH token).
void begin(dash::Registry* links, ProjectsLink* projects, FilesLink* files, dash::ModList* mods);
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
