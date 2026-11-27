// The FAITH mod: FAITH's health and the family's projects, read-only. It was
// built in (a Projects tab); now it's a mod like Atlas, so DASH and its phone
// app carry the same list. FAITH itself stays its own app and server.
#pragma once
#include "faith_link.h"
#include "mod.h"
#include "projects_link.h"
#include "view.h"

class FaithMod : public dash::Mod, public dash::ModView {
 public:
  FaithMod();
  const char* id() const override { return "faith"; }
  const char* name() const override { return "FAITH"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override { return health_.state(); }
  const char* detail() const override;
  bool problem() const override { return health_.problem(); }

  bool hasScreen() const override { return true; }
  dash::ModView* view() override { return this; }
  bool homeCard(dash::HomeCard& c, uint32_t nowMs) const override;

  void opened() override;
  bool dirty(uint32_t nowMs) override;
  void draw(uint32_t nowMs, bool full) override;
  void tap(int x, int y, uint32_t nowMs) override;

 private:
  FaithLink health_;
  ProjectsLink projects_;
  mutable char detail_[64] = "";
};
