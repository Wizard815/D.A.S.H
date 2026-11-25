// Debounced button with click and long-press. Feed it the raw pin state and
// the time; it returns at most one event per update.
#pragma once
#include <stdint.h>

namespace dash {

enum class ButtonEvent : uint8_t {
  None,
  Down,       // debounced press started
  Click,      // released before the long-press time
  LongStart,  // still held at the long-press time (push-to-talk starts here)
  LongEnd,    // released after a long press (push-to-talk stops here)
};

class Button {
 public:
  explicit Button(uint32_t debounceMs = 25, uint32_t longMs = 450)
      : debounceMs_(debounceMs), longMs_(longMs) {}

  ButtonEvent update(bool pressed, uint32_t nowMs);
  bool held() const { return stable_; }

 private:
  uint32_t debounceMs_, longMs_;
  bool raw_ = false, stable_ = false, long_ = false;
  uint32_t rawSince_ = 0, downAt_ = 0;
};

}  // namespace dash
