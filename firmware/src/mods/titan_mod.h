// The T.I.T.A.N mod: the homelab's status board (what's up, what's down and
// why), read-only. DASH polls T.I.T.A.N's GET /api/glance with
// TITAN_GLANCE_TOKEN: the slice a family member sees, no one's personal
// services, no fixes. Fixing things stays in T.I.T.A.N's own page.
#pragma once
#include "backoff.h"
#include "mod.h"
#include "titan.h"
#include "view.h"

class TitanMod : public dash::Mod, public dash::ModView {
 public:
  const char* id() const override { return "titan"; }
  const char* name() const override { return "TITAN"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override { return state_; }
  const char* detail() const override { return detail_; }
  bool problem() const override { return configError_ || state_ == dash::LinkState::Error; }

  bool hasScreen() const override { return true; }
  dash::ModView* view() override { return this; }
  bool homeCard(dash::HomeCard& c, uint32_t nowMs) const override;

  void opened() override;
  bool dirty(uint32_t nowMs) override;
  void draw(uint32_t nowMs, bool full) override;
  void tap(int x, int y, uint32_t nowMs) override;

 private:
  void fetch();
  dash::LinkState state_ = dash::LinkState::Waiting;
  dash::Backoff retry_{2000, 120000};
  dash::TitanGlance g_;
  bool have_ = false, configError_ = false, soon_ = false;
  uint32_t next_ = 0, last_ = 0, gotAt_ = 0, rev_ = 0, drawnRev_ = UINT32_MAX;
  uint8_t page_ = 0, pages_ = 1;
  char detail_[56] = "";
};
