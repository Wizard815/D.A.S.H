#include "battery.h"

namespace dash {

namespace {
struct Point { uint16_t mv; uint8_t pct; };
// Resting Li-ion cell, highest first.
const Point kCurve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 70},
    {3950, 60},  {3910, 50}, {3870, 40}, {3850, 30}, {3840, 20}, {3800, 10},
    {3700, 5},   {3600, 0},
};
constexpr int kPoints = sizeof(kCurve) / sizeof(kCurve[0]);
}  // namespace

uint8_t batteryPercent(uint32_t mv) {
  if (mv >= kCurve[0].mv) return 100;
  if (mv <= kCurve[kPoints - 1].mv) return 0;
  for (int i = 1; i < kPoints; i++) {
    if (mv >= kCurve[i].mv) {
      const Point& hi = kCurve[i - 1];
      const Point& lo = kCurve[i];
      uint32_t span = hi.mv - lo.mv;
      uint32_t into = mv - lo.mv;
      return (uint8_t)(lo.pct + ((hi.pct - lo.pct) * into + span / 2) / span);
    }
  }
  return 0;
}

uint32_t BatteryFilter::add(uint32_t mv) {
  if (!seeded_) {
    value_ = (float)mv;
    seeded_ = true;
  } else {
    value_ += alpha_ * ((float)mv - value_);
  }
  return this->mv();
}

}  // namespace dash
