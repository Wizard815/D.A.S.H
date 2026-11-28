#include "hw.h"

#include <Arduino.h>
#include <Wire.h>

#include "battery.h"
#include "board.h"

namespace hw {

namespace {
dash::Button btnA, btnB;
dash::BatteryFilter battery(0.15f);
constexpr int kBacklightChannel = 7;

// The vibration motor sits on the MCP23017 (GPB0). Only when the expander is
// fitted (DASH_HAS_EXPANDER=1); without it, alerts use the light alone.
bool expander = false;
void motor(bool on) {
  if (!expander) return;
  Wire.beginTransmission(board::kExpanderAddr);
  Wire.write(0x13);  // GPIOB
  Wire.write(on ? (1 << (board::kExpMotor - 8)) : 0);
  Wire.endTransmission();
}

Led base = Led::Off;
dash::AlertSignal sig{};
bool sigOn = false;
uint32_t sigStart = 0, roundStart = 0;

void writeLed(bool r, bool g, bool b) {
#if DASH_RGB_LED
  digitalWrite(board::kLedR, r ? LOW : HIGH);
  digitalWrite(board::kLedG, g ? LOW : HIGH);
  digitalWrite(board::kLedB, b ? LOW : HIGH);
#else
  (void)r; (void)g; (void)b;
#endif
}

void showBase() { writeLed(base == Led::Problem, base == Led::Ok, base == Led::Busy); }
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
#if DASH_HAS_EXPANDER
  Wire.begin(board::kI2cSda, board::kI2cScl);
  Wire.beginTransmission(board::kExpanderAddr);
  Wire.write(0x01);  // IODIRB
  Wire.write((uint8_t)~(1 << (board::kExpMotor - 8)));  // the motor pin is an output
  expander = Wire.endTransmission() == 0;
  motor(false);
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
  base = l;
  if (!sigOn) showBase();
}

void alert(const dash::AlertSignal& s) {
  sig = s;
  sigOn = s.r || s.g || s.b || s.buzzes;
  sigStart = roundStart = millis();
  if (!sigOn) {
    motor(false);
    showBase();
  }
}

void loop(uint32_t now) {
  if (!sigOn) return;
  // Light: steady, or blinking.
  bool lit = !sig.blinkMs || ((now - sigStart) / sig.blinkMs) % 2 == 0;
  writeLed(lit && sig.r, lit && sig.g, lit && sig.b);
  // Motor: `buzzes` pulses each round, rounds every repeatMs (or once).
  uint32_t t = now - roundStart;
  uint32_t period = sig.buzzMs + sig.gapMs;
  bool buzz = sig.buzzes && period && t < period * sig.buzzes && (t % period) < sig.buzzMs;
  motor(buzz);
  if (sig.repeatMs && t >= sig.repeatMs) roundStart = now;
}

}  // namespace hw
