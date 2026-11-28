#include "ha_mod.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "atlas.h"  // ageText, alertSignal
#include "config.h"
#include "gfx.h"
#include "hw.h"
#include "net.h"

using namespace gfx;

namespace {
constexpr uint32_t kEveryMs = 30000;
constexpr uint32_t kSoonMs = 10000;    // opening the screen asks again at most this often
constexpr uint32_t kArmMs = 5000;      // the second tap of a toggle, within this
constexpr uint32_t kMsgMs = 6000;
constexpr uint16_t kTimeoutMs = 3000;  // one request blocks the loop this long at most
constexpr int kCardY = kTop + 52, kCardH = 40;
constexpr int kRowsY = kCardY + kCardH + 8, kRowH = 46, kPerPage = 5;
dash::HaReading scratch;  // read here first, so a bad reply keeps the last good one

bool https() { return strncmp(DASH_HA_URL, "https://", 8) == 0; }

// One HTTP request to HA. The clients live on the stack only for its length
// (a TLS session holds a lot of heap).
struct Request {
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  bool open(const char* path) {
#ifdef DASH_HA_CA_PEM
    secure.setCACert(DASH_HA_CA_PEM);
#endif
    http.useHTTP10(true);  // no chunked replies, so the JSON reads straight off the stream
    http.setTimeout(kTimeoutMs);
    http.setConnectTimeout(kTimeoutMs);
    String full = String(DASH_HA_URL) + path;
    if (!http.begin(https() ? (WiFiClient&)secure : plain, full)) return false;
    http.addHeader("Authorization", String("Bearer ") + DASH_HA_TOKEN);
    return true;
  }
  ~Request() { http.end(); }
};
}  // namespace

void HaMod::begin() {
  if (!*DASH_HA_URL || !*DASH_HA_TOKEN) {
    enabled = false;  // not set up in secrets.h: nothing to show
    return;
  }
  if (!dash::haConfigure(b_, DASH_HA_ENTITIES, DASH_HA_ALERTS)) {
    configError_ = true;
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "set DASH_HA_ENTITIES in secrets.h");
    return;
  }
#ifndef DASH_HA_CA_PEM
  if (https()) {
    configError_ = true;
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "https needs DASH_HA_CA_PEM");
  }
#endif
}

void HaMod::loop(uint32_t now, bool netUp) {
  if (configError_) return;
  if (!netUp) {
    if (state_ != dash::LinkState::Waiting) rev_++;
    state_ = dash::LinkState::Waiting;
    idx_ = 0;
    next_ = now;
    return;
  }
  if (toggle_ >= 0) {
    int i = toggle_;
    toggle_ = -1;
    toggle(i);
    return;
  }
  if (soon_) {
    soon_ = false;
    if (now - last_ >= kSoonMs) {
      idx_ = 0;
      next_ = now;
    }
  }
  if ((int32_t)(now - next_) < 0) return;
  if (idx_ == 0) roundAt_ = now;
  dash::LinkState was = state_;
  if (state_ != dash::LinkState::Up) state_ = dash::LinkState::Connecting;
  int code = fetch(idx_);
  last_ = millis();
  if (code == 200 || code == 404) {
    // 404: that entity doesn't exist; the rest still can.
    if (++idx_ < b_.n) {
      next_ = last_;  // the next entity on the next pass
    } else {
      idx_ = 0;
      have_ = true;
      state_ = dash::LinkState::Up;
      retry_.reset();
      dash::haSummary(b_, detail_, sizeof detail_);
      next_ = roundAt_ + kEveryMs;
    }
    rev_++;
    return;
  }
  state_ = dash::LinkState::Error;
  if (code == 401) snprintf(detail_, sizeof detail_, "token refused: check DASH_HA_TOKEN");
  else if (code == 1) snprintf(detail_, sizeof detail_, "odd reply: is this Home Assistant?");
  else if (code == 0) snprintf(detail_, sizeof detail_, "bad DASH_HA_URL in secrets.h");
  else if (code < 0) snprintf(detail_, sizeof detail_, "no reply: is Home Assistant running?");
  else snprintf(detail_, sizeof detail_, "Home Assistant answered %d", code);
  idx_ = 0;  // start the round again after the wait
  next_ = last_ + retry_.fail(esp_random() % 1000);
  if (state_ != was) rev_++;
}

int HaMod::fetch(int i) {
  dash::HaEntity& e = b_.e[i];
  char path[96] = "/api/states/";
  size_t o = strlen(path);
  dash::haEntityId(e, path + o, sizeof path - o);
  Request rq;
  if (!rq.open(path)) return 0;
  int code = rq.http.GET();
  if (code == 404) {
    e.r = dash::HaReading();
    e.r.status = dash::HaStatus::Missing;
    checkAlerts();
    return code;
  }
  if (code != 200) return code;
  JsonDocument filter;
  dash::haStateFilter(filter);
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, rq.http.getStream(), DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  if (err || !dash::haReadState(doc, scratch)) return 1;
  e.r = scratch;
  checkAlerts();
  return 200;
}

void HaMod::checkAlerts() {
  switch (dash::haCheckAlerts(b_)) {
    case dash::HaAlertChange::Start:
      hw::alert(dash::alertSignal(2));  // amber, two buzzes, again every 5 min until seen
      signalling_ = true;
      break;
    case dash::HaAlertChange::Stop:
      if (signalling_) hw::alert(dash::alertSignal(0));
      signalling_ = false;
      break;
    case dash::HaAlertChange::None: break;
  }
}

void HaMod::say(const char* msg) {
  snprintf(msg_, sizeof msg_, "%s", msg);
  msgUntil_ = millis() + kMsgMs;
  rev_++;
}

void HaMod::toggle(int i) {
#if DASH_HA_ALLOW_TOGGLE
  dash::HaEntity& e = b_.e[i];
  char nm[32], path[64] = "/api/services/", body[96], m[56];
  size_t o = strlen(path);
  dash::haDomain(e, path + o, sizeof path - o);
  strncat(path, "/toggle", sizeof path - strlen(path) - 1);
  dash::haToggleBody(e, body, sizeof body);
  dash::haName(e, nm, sizeof nm);
  int code;
  {
    Request rq;
    if (!rq.open(path)) {
      say("bad DASH_HA_URL in secrets.h");
      return;
    }
    rq.http.addHeader("Content-Type", "application/json");
    code = rq.http.POST(body);
  }
  if (code == 200) {
    snprintf(m, sizeof m, "%s: asked to turn %s", nm, strcmp(e.r.state, "on") ? "on" : "off");
    fetch(i);  // read it back, so the screen shows what HA did
  } else if (code == 401 || code == 403) {
    snprintf(m, sizeof m, "Not allowed: the token's user can't do that");
  } else if (code < 0) {
    snprintf(m, sizeof m, "No reply: %s wasn't changed", nm);
  } else {
    snprintf(m, sizeof m, "Home Assistant answered %d", code);
  }
  say(m);
#else
  (void)i;
#endif
}

bool HaMod::homeCard(dash::HomeCard& c, uint32_t) const {
  snprintf(c.title, sizeof c.title, "Home Assistant");
  if (!have_) {
    snprintf(c.line, sizeof c.line, "%s", state_ == dash::LinkState::Error ? detail_ : "Asking Home Assistant...");
    c.attention = state_ == dash::LinkState::Error;
    return true;
  }
  dash::haSummary(b_, c.line, sizeof c.line);
  c.attention = dash::haAlertCount(b_) > 0;
  return true;
}

void HaMod::opened() {
  dash::haMarkSeen(b_);  // looking at the screen counts as seen
  if (signalling_) hw::alert(dash::alertSignal(0));
  signalling_ = false;
  page_ = 0;
  armed_ = -1;
  drawnRev_ = UINT32_MAX;
  soon_ = true;
}

bool HaMod::dirty(uint32_t now) {
  if (armed_ >= 0 && (int32_t)(now - armUntil_) >= 0) {
    armed_ = -1;
    rev_++;
  }
  if (*msg_ && (int32_t)(now - msgUntil_) >= 0) {
    *msg_ = 0;
    rev_++;
  }
  return rev_ != drawnRev_;
}

void HaMod::draw(uint32_t now, bool) {
  drawnRev_ = rev_;
  clearContent();
  heading("Home Assistant");
  if (!have_) {
    text("Asking...", 16, kCardY + 8, 4, TEXT);
    wrap(state_ == dash::LinkState::Error || configError_ ? detail_ : "Your entities show here in a moment.", 16,
         kCardY + 48, W - 32, 2, MUTED, BG, 4);
    return;
  }
  bool stale = state_ != dash::LinkState::Up;
  text(stale ? "Last known" : "Live", W - 16, kTop + 20, 2, stale ? GOLD : GOOD, TR_DATUM);

  // The summary, or a toggle's result, or why the data is old.
  char a[80], cut[80];
  int alerts = dash::haAlertCount(b_);
  tft.fillRoundRect(12, kCardY, W - 24, kCardH, 8, CARD);
  uint16_t col = alerts ? BAD : GOOD;
  if (*msg_) {
    snprintf(a, sizeof a, "%s", msg_);
    col = TEXT;
  } else if (state_ == dash::LinkState::Error) {
    snprintf(a, sizeof a, "%s", detail_);
    col = GOLD;
  } else {
    dash::haSummary(b_, a, sizeof a);
  }
  uint8_t font = tft.textWidth(a, 4) <= W - 48 ? 4 : 2;
  fit(cut, sizeof cut, a, font, W - 48);
  textOn(cut, 24, kCardY + (font == 4 ? 8 : 12), font, col, CARD);

  // Rows: alerts first. The dot's colour is backed by the words on the row.
  uint8_t order[dash::HaBoard::kMax];
  int n = dash::haOrder(b_, order);
  pages_ = (n + kPerPage - 1) / kPerPage;
  if (pages_ < 1) pages_ = 1;
  if (page_ >= pages_) page_ = 0;
  uint32_t unix = net::timeValid() ? (uint32_t)time(nullptr) : 0;
  for (int k = 0; k < kPerPage && page_ * kPerPage + k < n; k++) {
    int i = order[page_ * kPerPage + k];
    const dash::HaEntity& e = b_.e[i];
    bool alert = dash::haAlerting(e);
    int y = kRowsY + k * kRowH;
    tft.fillRoundRect(12, y, W - 24, kRowH - 4, 8, CARD);
    if (alert) tft.fillRoundRect(12, y, 6, kRowH - 4, 3, BAD);
    bool off = e.r.status != dash::HaStatus::Ok;
    tft.fillCircle(30, y + 12, 5, alert ? BAD : off ? MUTED : GOOD);
    char val[40], nm[32];
    dash::haValueText(e, val, sizeof val);
    fit(cut, sizeof cut, val, 2, 120);
    int vw = tft.textWidth(cut, 2);
    textOn(cut, W - 24, y + 4, 2, alert ? BAD : off ? MUTED : TEXT, CARD, TR_DATUM);
    dash::haName(e, nm, sizeof nm);
    fit(cut, sizeof cut, nm, 2, W - 24 - 44 - vw - 12);
    textOn(cut, 44, y + 4, 2, TEXT, CARD);
    // Second line: the arm prompt, or how long ago it changed.
    if (armed_ == i) {
      snprintf(a, sizeof a, "Tap again to turn %s", strcmp(e.r.state, "on") ? "on" : "off");
      textOn(a, 44, y + 22, 2, GOLD, CARD);
      continue;
    }
    char age[20] = "";
    if (unix && e.r.changed && unix >= e.r.changed) dash::ageText(unix - e.r.changed, age, sizeof age);
    if (alert) snprintf(a, sizeof a, "Alert%s%s", *age ? ", changed " : "", age);
    else if (*age) snprintf(a, sizeof a, "changed %s", age);
    else *a = 0;
    if (*a) textOn(a, 44, y + 22, 2, alert ? BAD : MUTED, CARD);
  }
  if (pages_ > 1) {
    snprintf(a, sizeof a, "More (%u/%u)", page_ + 1, pages_);
    navButton(a, W - 12 - 160, 160, false);
  }
  navButton("Refresh", 12, 128, false);
}

void HaMod::tap(int x, int y, uint32_t now) {
  if (y >= kNavY) {
    if (x < 12 + 128 + 12) {
      idx_ = 0;
      next_ = now;  // ask again on the next loop
      return;
    }
    if (pages_ > 1 && x >= W - 12 - 160) {
      page_ = (page_ + 1) % pages_;
      armed_ = -1;
      rev_++;
    }
    return;
  }
#if DASH_HA_ALLOW_TOGGLE
  if (!have_ || y < kRowsY) return;
  int k = (y - kRowsY) / kRowH;
  if (k >= kPerPage) return;
  uint8_t order[dash::HaBoard::kMax];
  int n = dash::haOrder(b_, order);
  if (page_ * kPerPage + k >= n) return;
  int i = order[page_ * kPerPage + k];
  if (!dash::haCanToggle(b_.e[i])) return;
  if (armed_ == i && (int32_t)(now - armUntil_) < 0) {
    armed_ = -1;
    toggle_ = (int8_t)i;  // sent from the next loop pass
  } else {
    armed_ = (int8_t)i;
    armUntil_ = now + kArmMs;
  }
  rev_++;
#else
  (void)x;
  (void)now;
#endif
}
