// FAITH's family projects: polls GET /api/projects/glance with this device's
// FAITH token (Authorization: Bearer). FAITH decides what the token sees:
// a person's own token sees their project, Family and Parents (grown-ups);
// a household token sees Family only. Read-only for now: starting and
// answering threads waits for pairing and talk (roadmap M2 and M6).
#pragma once
#include "backoff.h"
#include "connector.h"
#include "glance.h"

class ProjectsLink : public dash::Connector {
 public:
  ProjectsLink(const char* baseUrl, const char* token) : url_(baseUrl), token_(token) {}
  const char* name() const override { return "Projects"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override { return state_; }
  const char* detail() const override { return detail_; }

  // The last good glance; kept while FAITH is briefly away.
  const dash::Glance& glance() const { return glance_; }
  bool hasGlance() const { return have_; }
  // Bumps on every new glance, so the screen knows to redraw.
  uint32_t revision() const { return rev_; }
  // Fetch again on the next loop (opening the Projects screen).
  void refreshSoon() { soon_ = true; }

 private:
  void fetch();
  const char* url_;
  const char* token_;
  dash::LinkState state_ = dash::LinkState::Waiting;
  dash::Backoff retry_{2000, 120000};
  uint32_t next_ = 0, last_ = 0, rev_ = 0;
  bool configError_ = false, have_ = false, soon_ = false;
  char detail_[48] = "";
  dash::Glance glance_;
};
