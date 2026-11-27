// The drawing side of a mod (or a built-in screen that works the same way,
// like Files). Draw with the helpers in gfx.h, inside gfx::kTop..kBottom.
#pragma once
#include <stdint.h>

namespace dash {

class ModView {
 public:
  virtual ~ModView() = default;
  // The screen was opened (its tab tapped). Good moment to ask for fresh data.
  virtual void opened() {}
  // True when the screen should redraw now (new data, a clock tick).
  virtual bool dirty(uint32_t nowMs) = 0;
  // full: clear and draw everything; otherwise just what changed.
  virtual void draw(uint32_t nowMs, bool full) = 0;
  // A tap inside the content area (on the press).
  virtual void tap(int x, int y, uint32_t nowMs) = 0;
  // The finger lifted at (x, y), dx pixels from where it went down. A swipe
  // is |dx| of 60 or more.
  virtual void release(int /*x*/, int /*y*/, int /*dx*/, uint32_t /*nowMs*/) {}
  // The screen was left (another tab). Stop anything it was playing.
  virtual void closed() {}
};

}  // namespace dash
