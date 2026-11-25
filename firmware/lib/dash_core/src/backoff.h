// Retry timing for connectors: doubles after each failure up to a cap, with
// jitter so several connectors don't all retry at once after Wi-Fi returns.
#pragma once
#include <stdint.h>

namespace dash {

class Backoff {
 public:
  Backoff(uint32_t firstMs = 1000, uint32_t maxMs = 60000) : first_(firstMs), max_(maxMs) {}

  // Call after a failure. `rand01k` is 0-999 from any random source; the wait
  // is 75%-125% of the current step. Returns the wait in ms.
  uint32_t fail(uint32_t rand01k);
  void reset() { step_ = 0; }
  uint32_t failures() const { return step_; }

 private:
  uint32_t first_, max_, step_ = 0;
};

}  // namespace dash
