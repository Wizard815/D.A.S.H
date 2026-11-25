#include "backoff.h"

namespace dash {

uint32_t Backoff::fail(uint32_t r) {
  uint64_t base = first_;
  for (uint32_t i = 0; i < step_ && base < max_; i++) base *= 2;
  if (base > max_) base = max_;
  if (step_ < 32) step_++;
  uint64_t wait = base * (750 + (r % 1000) / 2) / 1000;  // 75%..125%
  return (uint32_t)wait;
}

}  // namespace dash
