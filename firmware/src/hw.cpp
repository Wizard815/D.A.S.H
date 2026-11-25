#include "hw.h"

#include <Arduino.h>

#include "battery.h"
#include "board.h"

namespace hw {

namespace {
dash::Button btnA, btnB;
dash::BatteryFilter battery(0.15f);
constexpr int kBacklightChannel = 7;
}  // namespace

void begin() {
  pinMode(board::kBootButton, INPUT_PULLUP);
#if DASH_HAS_PTT_BUTTON
  pinMode(board::kPttButton, INPUT);  // external pull-up; pins 34-39 have none
#endif
#if DASH_RGB_LED
  for (int p : {board::kLedR, board::kLedG, board::kLedB}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);  // active low: HIGH is off
  }
#endif
  analogReadResolution(12);
  analogSetPinAttenuation(board::kBatteryAdc, ADC_11db);
  ledcSetup(kBacklightChannel, 5000, 8);
  ledcAttachPin(board::kBacklight, kBacklightChannel);
  setBacklight(255);
}

bool bootHeld() { return digitalRead(board::kBootButton) == LOW; }

Inputs readButtons(uint32_t now) {
  Inputs in;
#if DASH_HAS_PTT_BUTTON
  in.a = btnA.update(digitalRead(board::kPttButton) == LOW, now);
#endif
  in.b = btnB.update(bootHeld(), now);
  return in;
}

uint32_t sampleBattery() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(board::kBatteryAdc);
  return battery.add(sum / 8 * 2);  // board divider halves the cell voltage
}

uint32_t batteryMv() { return battery.mv(); }

void setBacklight(uint8_t level) { ledcWrite(kBacklightChannel, level); }

void setLed(Led l) {
#if DASH_RGB_LED
  // Dim colours: the LED is bright and the device sits near your face.
  bool r = l == Led::Problem, g = l == Led::Ok, b = l == Led::Busy;
  digitalWrite(board::kLedR, r ? LOW : HIGH);
  digitalWrite(board::kLedG, g ? LOW : HIGH);
  digitalWrite(board::kLedB, b ? LOW : HIGH);
#else
  (void)l;
#endif
}

}  // namespace hw
