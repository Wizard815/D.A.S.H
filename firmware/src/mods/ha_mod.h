// The Home Assistant mod: a few entities and alerts. DASH asks HA's REST API
// (GET /api/states/<entity_id>, a long-lived token) for each entity in
// DASH_HA_ENTITIES and DASH_HA_ALERTS, one per loop pass, every 30 s. An alert
// entity turning on lights and buzzes DASH (amber) until the screen is opened.
// Switches and lights toggle with two taps only when DASH_HA_ALLOW_TOGGLE is 1.
// See docs/mods/ha.md.
#pragma once
#include "backoff.h"
#include "ha.h"
#include "mod.h"
#include "view.h"

class HaMod : public dash::Mod, public dash::ModView {
 public:
  const char* id() const override { return "ha"; }
  const char* name() const override { return "HA"; }
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
  // HTTP status, or < 0 for no reply; 1 for a 200 that isn't a state.
  int fetch(int i);
  void toggle(int i);
  void checkAlerts();
  void say(const char* msg);
  dash::LinkState state_ = dash::LinkState::Waiting;
  dash::Backoff retry_{2000, 120000};
  dash::HaBoard b_;
  bool have_ = false, configError_ = false, soon_ = false, signalling_ = false;
  uint8_t idx_ = 0;  // next entity of this round
  int8_t armed_ = -1, toggle_ = -1;
  uint8_t page_ = 0, pages_ = 1;
  uint32_t next_ = 0, roundAt_ = 0, last_ = 0, armUntil_ = 0, msgUntil_ = 0;
  uint32_t rev_ = 0, drawnRev_ = UINT32_MAX;
  char detail_[56] = "";
  char msg_[56] = "";
};
