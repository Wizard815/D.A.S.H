// Battery voltage to percent for a single Li-ion cell. No Arduino code here,
// so it runs in the native tests.
#pragma once
#include <stdint.h>

namespace dash {

// Percent (0-100) from cell millivolts, on a typical Li-ion discharge curve.
// Under load the cell reads lower than at rest, so treat this as a guide.
uint8_t batteryPercent(uint32_t cellMv);

// Smooths noisy ADC readings. The first reading is taken as is.
class BatteryFilter {
 public:
  explicit BatteryFilter(float alpha = 0.1f) : alpha_(alpha) {}
  uint32_t add(uint32_t mv);
  uint32_t mv() const { return (uint32_t)(value_ + 0.5f); }
  bool ready() const { return seeded_; }

 private:
  float alpha_;
  float value_ = 0;
  bool seeded_ = false;
};

// Readings above this usually mean USB power with no cell, or charging.
constexpr uint32_t kBatteryUsbMv = 4300;
// Below this the cell is too low to trust; show "low" and dim the screen.
constexpr uint32_t kBatteryLowMv = 3500;

}  // namespace dash
