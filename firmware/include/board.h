// Pins for the Freenove ESP32 Display 4.0" that are not the display itself
// (the display and touch pins are in platformio.ini). Source: docs/hardware.md.
#pragma once

namespace board {
constexpr int kBatteryAdc = 34;     // battery mV = reading x 2
constexpr int kBootButton = 0;      // BOOT key, active low, has a pull-up
constexpr int kPttButton = 36;      // planned side button A, input only, needs external 10k pull-up
constexpr int kLedR = 22, kLedG = 16, kLedB = 17;  // active low
constexpr int kBacklight = 27;
constexpr int kSpeakerEnable = 4;   // amp enable; Freenove drives it LOW to play (unverified)
}  // namespace board
