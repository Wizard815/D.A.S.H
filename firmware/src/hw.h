// Board hardware that isn't the screen: battery, buttons, LED, backlight.
#pragma once
#include <stdint.h>

#include "atlas.h"  // AlertSignal
#include "button.h"

namespace hw {

struct Inputs {
  dash::ButtonEvent a = dash::ButtonEvent::None;  // push-to-talk (side button A)
  dash::ButtonEvent b = dash::ButtonEvent::None;  // back / next (BOOT key for now)
};

void begin();
Inputs readButtons(uint32_t nowMs);
bool bootHeld();  // raw read, for "hold BOOT at power-on" checks

// Call about once a second. Returns smoothed cell millivolts.
uint32_t sampleBattery();
uint32_t batteryMv();

// 0-255. Uses PWM on the backlight pin.
void setBacklight(uint8_t level);

enum class Led : uint8_t { Off, Ok, Busy, Problem };
void setLed(Led l);  // the everyday status; an alert takes over the light while it runs

// An alert on the light and the vibration motor (docs/mods/atlas-protocol.md).
// A signal with no colour and no buzz stops it. Runs from hw::loop.
void alert(const dash::AlertSignal& s);
void loop(uint32_t nowMs);

}  // namespace hw
