#include "faith_link.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

namespace {
constexpr uint32_t kEveryMs = 30000;
constexpr uint16_t kTimeoutMs = 3000;  // the check blocks the loop this long at most
}  // namespace

void FaithLink::begin() {
  if (!url_ || !*url_) {
    enabled = false;
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

void FaithLink::loop(uint32_t now, bool netUp) {
  if (configError_) return;
  if (!netUp) {
    state_ = dash::LinkState::Waiting;
    detail_[0] = 0;
    next_ = now;  // check as soon as Wi-Fi is back
    return;
  }
  if ((int32_t)(now - next_) < 0) return;
  check();
  next_ = millis() + (state_ == dash::LinkState::Up ? kEveryMs : retry_.fail(esp_random() % 1000));
}

void FaithLink::check() {
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
  String full = String(url_) + "/api/health";
  if (!http.begin(https ? (WiFiClient&)secure : plain, full)) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "bad URL in secrets.h");
    return;
  }
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    // {"ok":true,"name":"..."}; no JSON library needed for this.
    if (body.indexOf("\"ok\":true") >= 0) {
      state_ = dash::LinkState::Up;
      retry_.reset();
      snprintf(detail_, sizeof detail_, "answering");
    } else {
      state_ = dash::LinkState::Error;
      snprintf(detail_, sizeof detail_, "odd reply: is this FAITH?");
    }
  } else if (code < 0) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "no reply: is FAITH running?");
  } else {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "answered %d", code);
  }
  http.end();
}
