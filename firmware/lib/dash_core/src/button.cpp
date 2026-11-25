#include "button.h"

namespace dash {

ButtonEvent Button::update(bool pressed, uint32_t now) {
  if (pressed != raw_) {
    raw_ = pressed;
    rawSince_ = now;
  }
  if (raw_ != stable_ && now - rawSince_ >= debounceMs_) {
    stable_ = raw_;
    if (stable_) {
      downAt_ = now;
      long_ = false;
      return ButtonEvent::Down;
    }
    return long_ ? ButtonEvent::LongEnd : ButtonEvent::Click;
  }
  if (stable_ && !long_ && now - downAt_ >= longMs_) {
    long_ = true;
    return ButtonEvent::LongStart;
  }
  return ButtonEvent::None;
}

}  // namespace dash
