// FAITH, first step: is it answering? Polls GET /api/health.
// Pairing, PIN sign-in and confirms come in roadmap M6.
#pragma once
#include "backoff.h"
#include "connector.h"

class FaithLink : public dash::Connector {
 public:
  explicit FaithLink(const char* baseUrl) : url_(baseUrl) {}
  const char* name() const override { return "FAITH"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override { return state_; }
  const char* detail() const override { return detail_; }

 private:
  void check();
  const char* url_;
  dash::LinkState state_ = dash::LinkState::Waiting;
  dash::Backoff retry_{2000, 120000};
  uint32_t next_ = 0;
  bool configError_ = false;
  char detail_[48] = "";
};
