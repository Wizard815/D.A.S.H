#include "titan_mod.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ble_reports.h"
#include "config.h"
#include "gfx.h"

using namespace gfx;

namespace {
constexpr uint32_t kEveryMs = 60000;
constexpr uint32_t kSoonMs = 10000;    // opening the screen fetches at most this often
constexpr uint16_t kTimeoutMs = 3000;  // blocks the loop this long at most
constexpr int kBodyY = kTop + 52;
constexpr int kDownH = 56, kRowH = 28;
dash::TitanGlance scratch;  // parsed here first, so a bad reply keeps the last good one
}  // namespace

void TitanMod::begin() {
  if (!*DASH_TITAN_URL || !*DASH_TITAN_TOKEN) {
    enabled = false;  // not set up in secrets.h: nothing to show
    return;
  }
#ifndef DASH_TITAN_CA_PEM
  if (strncmp(DASH_TITAN_URL, "https://", 8) == 0) {
    configError_ = true;
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "https needs DASH_TITAN_CA_PEM");
  }
#endif
}

void TitanMod::loop(uint32_t now, bool netUp) {
  if (configError_) return;
  if (!netUp) {
    state_ = dash::LinkState::Waiting;
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

void TitanMod::fetch() {
  bool https = strncmp(DASH_TITAN_URL, "https://", 8) == 0;
  if (state_ != dash::LinkState::Up) state_ = dash::LinkState::Connecting;
  WiFiClient plain;
  WiFiClientSecure secure;
#ifdef DASH_TITAN_CA_PEM
  secure.setCACert(DASH_TITAN_CA_PEM);
#endif
  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  String full = String(DASH_TITAN_URL) + "/api/glance";
  if (!http.begin(https ? (WiFiClient&)secure : plain, full)) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "bad DASH_TITAN_URL in secrets.h");
    rev_++;
    return;
  }
  http.addHeader("Authorization", String("Bearer ") + DASH_TITAN_TOKEN);
  int code = http.GET();
  dash::LinkState was = state_;
  if (code == 200) {
    String body = http.getString();
    if (dash::parseTitan(body.c_str(), body.length(), scratch)) {
      g_ = scratch;
      have_ = true;
      gotAt_ = millis();
      state_ = dash::LinkState::Up;
      retry_.reset();
      dash::titanSummary(g_, detail_, sizeof detail_);
    } else {
      state_ = dash::LinkState::Error;
      snprintf(detail_, sizeof detail_, "odd reply: is this T.I.T.A.N?");
    }
  } else if (code == 401) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "token refused: check TITAN_GLANCE_TOKEN");
  } else if (code == 503) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "off: set TITAN_GLANCE_TOKEN on T.I.T.A.N");
  } else if (code == 404) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "T.I.T.A.N has no /api/glance: update it");
  } else if (code < 0) {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "no reply: is T.I.T.A.N running?");
  } else {
    state_ = dash::LinkState::Error;
    snprintf(detail_, sizeof detail_, "answered %d", code);
  }
  http.end();
  if (code == 200 || state_ != was) rev_++;
  // The phone gets the Home card's words when they change.
  dash::HomeCard c;
  homeCard(c, millis());
  ble_reports::summary(id(), c.line);
}

bool TitanMod::homeCard(dash::HomeCard& c, uint32_t) const {
  snprintf(c.title, sizeof c.title, "T.I.T.A.N");
  if (!have_) {
    snprintf(c.line, sizeof c.line, "%s", state_ == dash::LinkState::Error ? detail_ : "Asking T.I.T.A.N...");
    c.attention = state_ == dash::LinkState::Error;
    return true;
  }
  dash::titanSummary(g_, c.line, sizeof c.line);
  c.attention = g_.down > 0;
  return true;
}

void TitanMod::opened() {
  page_ = 0;
  drawnRev_ = UINT32_MAX;
  soon_ = true;
}

bool TitanMod::dirty(uint32_t) { return rev_ != drawnRev_; }

void TitanMod::draw(uint32_t, bool) {
  drawnRev_ = rev_;
  clearContent();
  heading("T.I.T.A.N");
  if (!have_) {
    text("Asking T.I.T.A.N...", 16, kBodyY + 8, 4, TEXT);
    wrap(state_ == dash::LinkState::Error ? detail_ : "The homelab's status shows here in a moment.", 16, kBodyY + 48, W - 32, 2,
         MUTED, BG, 4);
    return;
  }
  char a[80];
  bool stale = state_ != dash::LinkState::Up;
  text(stale ? "Last known" : "Live", W - 16, kTop + 20, 2, stale ? GOLD : GOOD, TR_DATUM);

  // Counts card.
  tft.fillRoundRect(12, kBodyY, W - 24, 62, 8, CARD);
  dash::titanSummary(g_, a, sizeof a);
  char cut[80];
  fit(cut, sizeof cut, a, 4, W - 48);
  textOn(cut, 24, kBodyY + 8, 4, g_.down ? BAD : GOOD, CARD);
  int o = snprintf(a, sizeof a, "%u up, %u down", g_.up, g_.down);
  if (g_.pending) o += snprintf(a + o, sizeof a - o, ", %u checking", g_.pending);
  if (g_.notes) snprintf(a + o, sizeof a - o, ". %u note%s for you", g_.notes, g_.notes == 1 ? "" : "s");
  fit(cut, sizeof cut, a, 2, W - 48);
  textOn(cut, 24, kBodyY + 38, 2, MUTED, CARD);

  // Page 0: what's down, then everything. Later pages: the rest of everything.
  int y = kBodyY + 72;
  int bottom = kNavY - 6;
  int first = 0;
  if (page_ == 0) {
    for (int i = 0; i < g_.nDown && y + kDownH <= bottom; i++) {
      const dash::TitanDown& d = g_.downs[i];
      tft.fillRoundRect(12, y, W - 24, kDownH - 6, 8, CARD);
      tft.fillRoundRect(12, y, 6, kDownH - 6, 3, BAD);
      fit(cut, sizeof cut, d.name, 2, W - 56);
      textOn(cut, 28, y + 6, 2, TEXT, CARD);
      dash::titanDownWords(d, a, sizeof a);
      fit(cut, sizeof cut, a, 2, W - 56);
      textOn(cut, 28, y + 26, 2, MUTED, CARD);
      y += kDownH;
    }
  }
  // Everything, two columns, a dot and a name. The dot's colour is backed by
  // the words in the counts card above.
  int perCol = (bottom - y - 24) / kRowH;
  if (perCol < 1) perCol = 1;
  int perPage = perCol * 2;
  int firstPer = perPage;
  // Later pages have the full height.
  int fullPer = ((bottom - (kBodyY + 72) - 24) / kRowH) * 2;
  pages_ = 1;
  if (g_.nServices > firstPer) pages_ = 1 + (g_.nServices - firstPer + fullPer - 1) / fullPer;
  if (page_ >= pages_) page_ = 0;
  if (page_ > 0) {
    first = firstPer + (page_ - 1) * fullPer;
    perPage = fullPer;
    perCol = fullPer / 2;
  }
  text("Everything", 16, y, 2, MUTED);
  y += 22;
  int cw = (W - 32) / 2;
  for (int k = 0; k < perPage && first + k < g_.nServices; k++) {
    const dash::TitanService& s = g_.services[first + k];
    int cx = 16 + (k / perCol) * cw, cy = y + (k % perCol) * kRowH;
    uint16_t col = s.status == 'u' ? GOOD : s.status == 'd' ? BAD : GOLD;
    tft.fillCircle(cx + 6, cy + 9, 5, col);
    fit(cut, sizeof cut, s.name, 2, cw - 24);
    text(cut, cx + 18, cy + 1, 2, s.status == 'd' ? TEXT : MUTED);
  }
  if (pages_ > 1) {
    snprintf(a, sizeof a, "More (%u/%u)", page_ + 1, pages_);
    navButton(a, W - 12 - 160, 160, false);
  }
  navButton("Refresh", 12, 128, false);
}

void TitanMod::tap(int x, int y, uint32_t now) {
  if (y < kNavY) return;
  if (x < 12 + 128 + 12) {
    next_ = now;  // fetch on the next loop
    return;
  }
  if (pages_ > 1 && x >= W - 12 - 160) {
    page_ = (page_ + 1) % pages_;
    rev_++;
  }
}
