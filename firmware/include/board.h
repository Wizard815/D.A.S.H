// Pins for the Freenove ESP32 Display 4.0" that are not the display itself
// (the display and touch pins are in platformio.ini). Source: docs/hardware.md.
#pragma once
#include <stdint.h>

namespace board {
constexpr int kBatteryAdc = 34;     // battery mV = reading x 2
constexpr int kBootButton = 0;      // BOOT key, active low, has a pull-up
constexpr int kPttButton = 36;      // planned side button A, input only, needs external 10k pull-up
constexpr int kLedR = 22, kLedG = 16, kLedB = 17;  // active low
constexpr int kBacklight = 27;
// microSD slot (VSPI). Kept free: the knob, side buttons and motor go on
// an MCP23017 I/O expander instead (docs/hardware.md).
constexpr int kSdSck = 18, kSdMiso = 19, kSdMosi = 23, kSdCs = 5;
// MCP23017 on the free I2C port (SDA 32, SCL 25), address 0x20. Its pins:
constexpr int kI2cSda = 32, kI2cScl = 25;
constexpr uint8_t kExpanderAddr = 0x20;
constexpr int kExpEncA = 0, kExpEncB = 1, kExpEncClick = 2;  // GPA0-2
constexpr int kExpButtonA = 3, kExpButtonB = 4;             // GPA3-4, internal pull-ups
constexpr int kExpMotor = 8;                                // GPB0, via an N-MOSFET
constexpr int kExpanderInt = 35;  // INTA, input only, 10k pull-up
constexpr int kSpeakerEnable = 4;   // amp enable; Freenove drives it LOW to play (unverified)
}  // namespace board
