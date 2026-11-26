// Screens. Portrait 320x480: status bar on top, tab bar at the bottom.
#pragma once
#include <stdint.h>

#include "connector.h"

class ProjectsLink;

namespace ui {

// The first four are the tabs, in order.
enum class Screen : uint8_t { Home, Projects, Links, Device, Talk, kCount };

// `projects` may be null (no FAITH token).
void begin(dash::Registry* links, ProjectsLink* projects);
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
