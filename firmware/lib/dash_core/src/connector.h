// The interface every link (Hermes, FAITH, Orb, Studio) implements, and a
// registry the UI reads. See docs/architecture.md, principle 1.
#pragma once
#include <stdint.h>

namespace dash {

enum class LinkState : uint8_t { Off, Waiting, Connecting, Up, Error };

// Words for the screen, so state never depends on colour alone.
const char* linkStateText(LinkState s);

class Connector {
 public:
  virtual ~Connector() = default;
  virtual const char* name() const = 0;
  // Called once at boot when enabled.
  virtual void begin() {}
  // Called every main-loop pass. `netUp` is false while Wi-Fi is down.
  virtual void loop(uint32_t nowMs, bool netUp) = 0;
  virtual LinkState state() const = 0;
  // A short plain line: "answering", "no reply: check FAITH is running".
  virtual const char* detail() const { return ""; }

  bool enabled = true;
};

class Registry {
 public:
  static constexpr int kMax = 6;
  bool add(Connector* c);
  int count() const { return n_; }
  Connector* at(int i) const { return (i >= 0 && i < n_) ? list_[i] : nullptr; }
  void begin();
  void loop(uint32_t nowMs, bool netUp);
  // Bumps whenever any enabled connector's state changes, so the UI knows to redraw.
  uint32_t revision() const { return rev_; }
  // Enabled connectors that are not Up.
  int problems() const;

 private:
  Connector* list_[kMax] = {};
  LinkState last_[kMax] = {};
  int n_ = 0;
  uint32_t rev_ = 0;
};

}  // namespace dash
