#include "projects_link.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

namespace {
constexpr uint32_t kEveryMs = 60000;
constexpr uint32_t kSoonMs = 10000;   // opening the screen fetches at most this often
constexpr uint16_t kTimeoutMs = 3000;  // blocks the loop this long at most (links move to a task in M2)
dash::Glance scratch;                  // parsed here first, so a bad reply keeps the last good one
}  // namespace

void ProjectsLink::begin() {
  if (!url_ || !*url_ || !token_ || !*token_) {
    enabled = false;  // no FAITH URL or token in secrets.h: nothing to show
    return;
  }
#ifndef DASH_FAITH_CA_PEM
  if (strncmp(url_, "https://", 8) == 0) {
    configError_ = true;
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "https needs DASH_FAITH_CA_PEM");
  }
#endif
}

void ProjectsLink::loop(uint32_t now, bool netUp) {
  if (configError_) return;
  if (!netUp) {
    state_ = dash::LinkState::Waiting;
    detail_[0] = 0;
    next_ = now;
    return;
  }
  if (soon_) {
    soon_ = false;
    if (now - last_ >= kSoonMs) next_ = now;
  }
  if ((int32_t)(now - next_) < 0) return;
  fetch();
  last_ = millis();
  next_ = last_ + (state_ == dash::LinkState::Up ? kEveryMs : retry_.fail(esp_random() % 1000));
}

void ProjectsLink::fetch() {
  bool https = strncmp(url_, "https://", 8) == 0;
  if (state_ != dash::LinkState::Up) state_ = dash::LinkState::Connecting;

  WiFiClient plain;
  WiFiClientSecure secure;
#ifdef DASH_FAITH_CA_PEM
  secure.setCACert(DASH_FAITH_CA_PEM);
#endif
  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  String full = String(url_) + "/api/projects/glance";
  if (!http.begin(https ? (WiFiClient&)secure : plain, full)) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "bad URL in secrets.h");
    return;
  }
  http.addHeader("Authorization", String("Bearer ") + token_);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    if (dash::parseGlance(body.c_str(), body.length(), scratch)) {
      glance_ = scratch;
      have_ = true;
      rev_++;
      state_ = dash::LinkState::Up;
      retry_.reset();
      dash::glanceSummary(glance_, detail_, sizeof detail_);
    } else {
      state_ = dash::LinkState::Error;
      snprintf(detail_, sizeof detail_, "odd reply: is this FAITH?");
    }
  } else if (code == 401) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "token refused: make a new one");
  } else if (code == 404) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "FAITH has no projects: update it");
  } else if (code < 0) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "no reply: is FAITH running?");
  } else {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "answered %d", code);
  }
  http.end();
}
