#include "atlas_mod.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "ble_reports.h"
#include "config.h"
#include "espnow_bus.h"
#include "gfx.h"
#include "hw.h"
#include "net.h"
#include "share.h"

using namespace gfx;

namespace {
const char* const kUnits[] = {"US", "Metric"};
const char* const kLinks[] = {"Auto", "ESP-NOW", "Wi-Fi"};
const char* const kPages[] = {"Gauges", "Codes", "Trip", "Fob"};
constexpr int kNPages = 4;
constexpr char kTopicSub[] = "dash/v1/atlas/#";
constexpr char kTopicOut[] = "dash/v1/atlas/dash";
constexpr uint32_t kReqEveryMs = 60000;  // ask again while nothing is heard
constexpr int kBodyY = kTop + 104;
constexpr int kCodesPerPage = 3;
constexpr int kFobBtnW = (W - 24 - 8) / 2, kFobBtnH = 44, kFobBtnY = kBodyY + 132;
constexpr uint32_t kFobEveryMs = 30000;
// The QR button sits right of the "Atlas" heading.
constexpr int kQrBtnX = 96, kQrBtnY = kTop + 2, kQrBtnW = 56, kQrBtnH = 44;
constexpr int kQrSide = 220, kQrY = kTop + 52;
}  // namespace

AtlasMod* AtlasMod::self_ = nullptr;

AtlasMod::AtlasMod() : units_{"units", "Units", kUnits, 2, 0}, link_{"link", "Link", kLinks, 3, 0}, mqtt_(tcp_) { self_ = this; }

void AtlasMod::begin() {
  configError_ = false;
  if (useEspNow() && !espnowOk_) {
    espnowOk_ = espnow::begin(DASH_ESPNOW_KEY);
    if (espnowOk_ && *DASH_ATLAS_PEER_MAC) {
      peerSet_ = espnow::parseMac(DASH_ATLAS_PEER_MAC, peer_) && espnow::addPeer(DASH_ATLAS_PEER_MAC);
      if (!peerSet_) configError_ = true;
    }
  }
  if (useMqtt() && !*DASH_ATLAS_MQTT_HOST && !*DASH_ATLAS_WIFI_SSID && !useEspNow()) configError_ = true;
  mqtt_.setCallback(onMqtt);
  mqtt_.setBufferSize(512);
  mqtt_.setSocketTimeout(2);
  mqtt_.setKeepAlive(15);
  updateDetail(millis());
}

void AtlasMod::settingChanged(int i) {
  if (i == 1) {
    if (!useMqtt() && mqtt_.connected()) mqtt_.disconnect();
    begin();
  }
  drawnRev_ = UINT32_MAX;  // redraw with the new units
}

void AtlasMod::loop(uint32_t now, bool netUp) {
  if (useMqtt()) mqttLoop(now, netUp);
  // Ask for everything when nothing has been heard for a while (ESP-NOW
  // only: MQTT has the retained frames).
  if (useEspNow() && espnowOk_ && !dash::atlasLive(st_, now) && now - lastReq_ >= kReqEveryMs) request(now);
  updateDetail(now);
  if (now - lastSum_ >= 1000) {  // the phone gets it when it changes (at most every 10 s)
    lastSum_ = now;
    char line[96];
    dash::atlasSummary(st_, now, unixNow(now), units(), line, sizeof line, nullptr);
    ble_reports::summary(id(), line);
  }
  if (open_ && page_ == 3 && fobReachable() && (int32_t)(now - fobNext_) >= 0) fobFetch(now);
}

void AtlasMod::mqttLoop(uint32_t now, bool netUp) {
  mqttUp_ = mqtt_.connected();
  if (mqttUp_) {
    mqtt_.loop();
    return;
  }
  // Only on the truck network, unless a host is set (e.g. a home-side broker bridge).
  bool reachable = netUp && (*DASH_ATLAS_MQTT_HOST || net::onAtlasWifi());
  if (!reachable || (int32_t)(now - nextMqtt_) < 0) return;
  if (*DASH_ATLAS_MQTT_HOST) mqtt_.setServer(DASH_ATLAS_MQTT_HOST, DASH_ATLAS_MQTT_PORT);
  else mqtt_.setServer(IPAddress(net::gateway()), DASH_ATLAS_MQTT_PORT);
  char cid[24];
  snprintf(cid, sizeof cid, "dash-%06x", (unsigned)(ESP.getEfuseMac() & 0xFFFFFF));
  bool ok = *DASH_ATLAS_MQTT_USER ? mqtt_.connect(cid, DASH_ATLAS_MQTT_USER, DASH_ATLAS_MQTT_PASS) : mqtt_.connect(cid);
  if (ok && mqtt_.subscribe(kTopicSub)) {
    retry_.reset();
    request(now);
  } else {
    nextMqtt_ = millis() + retry_.fail(esp_random() % 1000);
  }
}

void AtlasMod::request(uint32_t now) {
  lastReq_ = now;
  char f[48];
  int n = snprintf(f, sizeof f, "{\"m\":\"atlas\",\"t\":\"req\",\"s\":%u}", (unsigned)++seq_);
  if (useMqtt() && mqtt_.connected()) mqtt_.publish(kTopicOut, f);
  if (useEspNow() && espnowOk_) espnow::send(peerSet_ ? peer_ : nullptr, f, n);
}

void AtlasMod::onMqtt(char* topic, uint8_t* payload, unsigned int len) {
  if (!self_ || !strncmp(topic, kTopicOut, sizeof kTopicOut)) return;  // our own requests
  self_->onFrame((const char*)payload, len, dash::Via::Mqtt, millis());
}

void AtlasMod::onFrame(const char* json, size_t len, dash::Via via, uint32_t now) {
  if (via == dash::Via::EspNow && !useEspNow()) return;
  if (via == dash::Via::Mqtt && !useMqtt()) return;
  uint8_t wasLvl = st_.alertLvl;
  bool wasSeen = st_.alertSeen;
  char wasId[sizeof st_.alertId];
  memcpy(wasId, st_.alertId, sizeof wasId);
  dash::AtlasFrame f = st_.apply(json, len, now);
  if (f == dash::AtlasFrame::Alert &&
      (st_.alertLvl != wasLvl || st_.alertSeen != wasSeen || strcmp(st_.alertId, wasId) != 0)) {
    // A new alert buzzes and lights; a cleared one stops.
    hw::alert(dash::alertSignal(st_.alertLvl && !st_.alertSeen ? st_.alertLvl : 0));
  }
  if (f == dash::AtlasFrame::Alert) ble_reports::atlasAlert(json, len, st_);  // to the phone at once
  if (f != dash::AtlasFrame::Bad) lastVia_ = via;
}

dash::LinkState AtlasMod::state() const {
  if (configError_) return dash::LinkState::Error;
  if (dash::atlasLive(st_, millis())) return dash::LinkState::Up;
  return mqttUp_ ? dash::LinkState::Connecting : dash::LinkState::Waiting;
}

void AtlasMod::updateDetail(uint32_t now) {
  if (configError_) {
    snprintf(detail_, sizeof detail_, peerSet_ || !*DASH_ATLAS_PEER_MAC ? "set the truck Wi-Fi in secrets.h" : "bad DASH_ATLAS_PEER_MAC");
    return;
  }
  if (dash::atlasLive(st_, now)) {
    snprintf(detail_, sizeof detail_, "live over %s", dash::viaText(lastVia_));
    return;
  }
  if (st_.heard) {
    dash::atlasLinkWords(st_, now, detail_, sizeof detail_);
    detail_[0] = tolower(detail_[0]);
    return;
  }
  const char* how = link_.value == EspNowOnly ? "ESP-NOW" : link_.value == WifiOnly ? "truck Wi-Fi" : "ESP-NOW and truck Wi-Fi";
  snprintf(detail_, sizeof detail_, "listening on %s", how);
}

uint32_t AtlasMod::unixNow(uint32_t now) const {
  if (net::timeValid()) return (uint32_t)time(nullptr);
  return dash::atlasUnixNow(st_, now);
}

bool AtlasMod::homeCard(dash::HomeCard& c, uint32_t now) const {
  snprintf(c.title, sizeof c.title, "Atlas");
  dash::atlasSummary(st_, now, unixNow(now), units(), c.line, sizeof c.line, &c.attention);
  return true;
}

// ---------- screen ----------
void AtlasMod::opened() {
  open_ = true;
  if (st_.alertLvl && !st_.alertSeen) {  // looking at Atlas counts as seen
    st_.alertSeen = true;
    st_.rev++;
    hw::alert(dash::alertSignal(0));
  }
  page_ = 0;
  codePage_ = 0;
  qr_ = 0;
  drawnRev_ = UINT32_MAX;
  request(millis());
}

bool AtlasMod::dirty(uint32_t now) {
  if (qr_) return !qrDrawn_;
  bool live = dash::atlasLive(st_, now);
  if (st_.rev != drawnRev_ || live != drawnLive_) return true;
  if (page_ == 3 && fobRev_ != drawnFobRev_) return true;
  if (armUntil_ && (int32_t)(now - armUntil_) >= 0) {  // the start confirm ran out
    armUntil_ = 0;
    fobRev_++;
    return true;
  }
  // Ages ("2 min ago") move: redraw the header every 5 s.
  return now - drawnTick_ >= 5000;
}

void AtlasMod::drawHeader(uint32_t now) {
  tft.fillRect(0, kTop, W, 48, BG);
  heading("Atlas");
  button("QR", kQrBtnX, kQrBtnY, kQrBtnW, kQrBtnH, false);
  char w[72];
  constexpr int kStatusW = W - 16 - (kQrBtnX + kQrBtnW + 8);
  auto status = [&](uint16_t colour) {
    char cut[72];
    fit(cut, sizeof cut, w, 2, kStatusW);
    text(cut, W - 16, kTop + 20, 2, colour, TR_DATUM);
  };
  bool live = dash::atlasLive(st_, now);
  // An alert says so here until it's cleared.
  if (st_.alertLvl) {
    snprintf(w, sizeof w, "%s%s", dash::alertKindWords(st_.alertK), st_.alertSnap ? ": photo in app" : "");
    status(st_.alertLvl >= 3 ? BAD : GOLD);
    return;
  }
  // A remote start (or other owner command) in the last 2 minutes says so here.
  if (st_.cmdAtMs && now - st_.cmdAtMs < 120000) {
    snprintf(w, sizeof w, "%s", st_.cmdMsg[0] ? st_.cmdMsg : st_.cmdOk ? "Command done" : "Command failed");
    status(st_.cmdOk ? GOOD : BAD);
    return;
  }
  if (live) snprintf(w, sizeof w, "Live, %s%s", dash::viaText(lastVia_), st_.ign ? ", running" : "");
  else dash::atlasLinkWords(st_, now, w, sizeof w);
  status(live ? GOOD : st_.asleep ? MUTED : GOLD);
}

namespace {
void tile(int i, const char* label, const char* value, const char* warn, bool stale) {
  int col = i % 2, row = i / 2;
  int tw = (W - 24 - 8) / 2, th = 64;
  int x = 12 + col * (tw + 8), y = kBodyY + row * (th + 6);
  tft.fillRoundRect(x, y, tw, th, 8, CARD);
  textOn(label, x + 10, y + 6, 2, MUTED, CARD);
  char v[32];
  snprintf(v, sizeof v, "%s%s%s", value, *warn ? " " : "", warn);
  textOn(v, x + 10, y + 28, 4, *warn ? BAD : stale ? MUTED : TEXT, CARD);
}
}  // namespace

void AtlasMod::drawGauges(uint32_t now, bool full) {
  const dash::AtlasObd& o = st_.obd;
  if (full) tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
  if (!o.have) {
    wrap("No gauges yet. They show when the truck is on and Atlas reads the OBD2 port.", 16, kBodyY + 8, W - 32, 2, MUTED, BG, 4);
    return;
  }
  bool stale = !dash::atlasLive(st_, now) || now - o.atMs > dash::kAtlasStaleMs;
  dash::Units u = units();
  char v[24], lbl[32];
  dash::fmtTemp(v, sizeof v, o.ect, u);
  tile(0, "Coolant", v, dash::coolantWarning(o.ect), stale);
  dash::fmtTemp(v, sizeof v, o.oil, u);
  tile(1, "Oil", v, "", stale);
  dash::fmtTemp(v, sizeof v, o.atf, u);
  tile(2, "Transmission", v, "", stale);
  dash::fmtTemp(v, sizeof v, o.iat, u);
  tile(3, "Intake air", v, "", stale);
  dash::fmtVolts(v, sizeof v, o.volts);
  tile(4, "Battery", v, dash::voltsWarning(o.volts, st_.ign), stale);
  if (isnan(o.fuel)) snprintf(v, sizeof v, "--");
  else snprintf(v, sizeof v, "%d%%", (int)lroundf(o.fuel));
  tile(5, "Fuel", v, "", stale);
  char avg[16];
  dash::fmtEconomy(avg, sizeof avg, o.l100a, u);
  snprintf(lbl, sizeof lbl, "%s now", u == dash::Units::US ? "MPG" : "Economy");
  if (strcmp(avg, "--")) snprintf(lbl, sizeof lbl, "Now (trip %s)", avg);
  dash::fmtEconomy(v, sizeof v, o.l100, u);
  // The tile is narrow: drop the unit from the big number in US units.
  if (u == dash::Units::US) { char* sp = strstr(v, " mpg"); if (sp) *sp = 0; }
  tile(6, lbl, v, "", stale);
  dash::fmtDistance(v, sizeof v, o.rangeKm, u);
  tile(7, "Range", v, "", stale);
}

void AtlasMod::drawCodes(uint32_t now) {
  tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
  const dash::AtlasCodes& c = st_.dtc;
  if (!c.have) {
    wrap("No codes read yet. Atlas reads them when the truck is on.", 16, kBodyY + 8, W - 32, 2, MUTED, BG, 3);
    return;
  }
  char line[64];
  snprintf(line, sizeof line, "Check engine light: %s", c.mil ? "ON" : "off");
  text(line, 16, kBodyY + 4, 2, c.mil ? BAD : TEXT);
  if (c.n == 0) {
    text("No trouble codes.", 16, kBodyY + 36, 4, GOOD);
    return;
  }
  int pages = (c.n + kCodesPerPage - 1) / kCodesPerPage;
  if (codePage_ >= pages) codePage_ = 0;
  int y = kBodyY + 28;
  char words[104];
  for (int i = codePage_ * kCodesPerPage; i < c.n && i < (codePage_ + 1) * kCodesPerPage; i++) {
    tft.fillRoundRect(12, y, W - 24, 80, 8, CARD);
    textOn(c.code[i], 24, y + 8, 4, TEXT, CARD);
    if (dash::dtcUrgent(c.code[i])) {
      int bw = tft.textWidth("Stop soon", 2) + 20;
      tft.fillRoundRect(W - 24 - bw, y + 8, bw, 24, 12, BAD);
      textOn("Stop soon", W - 24 - bw / 2, y + 20, 2, BG, BAD, MC_DATUM);
    }
    dash::dtcDescribe(c, i, words, sizeof words);
    wrap(*words ? words : "No description for this code", 24, y + 38, W - 48, 2, MUTED, CARD, 2);
    y += 86;
  }
  if (pages > 1) {
    snprintf(line, sizeof line, "Codes %d-%d of %d", codePage_ * kCodesPerPage + 1, min((int)c.n, (codePage_ + 1) * kCodesPerPage), c.n);
    text(line, 16, kNavY + 16, 2, MUTED);
    navButton(codePage_ + 1 < pages ? "Next" : "First", W - 12 - 128, 128, true);
  }
}

void AtlasMod::drawTrip(uint32_t now) {
  tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
  dash::Units u = units();
  uint32_t unix = unixNow(now);
  int y = kBodyY;
  char a[64], b[96], t1[16], t2[16];
  auto card = [&](const char* title, uint16_t titleColour, const char* line) {
    tft.fillRoundRect(12, y, W - 24, 66, 8, CARD);
    textOn(title, 24, y + 8, 2, titleColour, CARD);
    wrap(line, 24, y + 28, W - 48, 2, MUTED, CARD, 2);
    y += 70;
  };

  // Sync
  if (st_.sync.have) {
    snprintf(a, sizeof a, "Sync: %s", dash::syncWords(st_.sync.st));
    char age[20] = "";
    if (st_.sync.lastUnix && unix > st_.sync.lastUnix) dash::ageText(unix - st_.sync.lastUnix, age, sizeof age);
    snprintf(b, sizeof b, "%u waiting. %s%s%s%s%s", (unsigned)st_.sync.queued, st_.sync.lastUnix ? "Last synced " : "Never synced",
             age, *st_.sync.via ? " via " : "", st_.sync.via, *st_.sync.err ? ". " : "");
    if (*st_.sync.err) strncat(b, st_.sync.err, sizeof b - strlen(b) - 1);
    card(a, !strcmp(st_.sync.st, "failed") ? BAD : TEXT, b);
  } else {
    card("Sync: not heard yet", MUTED, "Atlas says how syncing home is going.");
  }
  // Weather
  if (st_.wx.have) {
    dash::fmtTemp(t1, sizeof t1, st_.wx.c, u);
    snprintf(a, sizeof a, "Weather: %s%s%s", t1, *st_.wx.txt ? ", " : "", st_.wx.txt);
    b[0] = 0;
    if (!isnan(st_.wx.rh)) snprintf(b + strlen(b), sizeof b - strlen(b), "Humidity %d%%. ", (int)lroundf(st_.wx.rh));
    if (!isnan(st_.wx.hpa)) snprintf(b + strlen(b), sizeof b - strlen(b), "%d hPa. ", (int)lroundf(st_.wx.hpa));
    if (!isnan(st_.wx.hi) || !isnan(st_.wx.lo)) {
      dash::fmtTemp(t1, sizeof t1, st_.wx.hi, u);
      dash::fmtTemp(t2, sizeof t2, st_.wx.lo, u);
      snprintf(b + strlen(b), sizeof b - strlen(b), "High %s, low %s", t1, t2);
    }
    card(a, TEXT, b);
  } else {
    card("Weather: not heard yet", MUTED, "From the truck's sensor or forecast.");
  }
  // Brain battery
  if (st_.pwr.have) {
    char v[12];
    dash::fmtVolts(v, sizeof v, st_.pwr.volts);
    if (isnan(st_.pwr.soc)) snprintf(a, sizeof a, "Atlas battery: %s", v);
    else snprintf(a, sizeof a, "Atlas battery: %d%%", (int)lroundf(st_.pwr.soc));
    const char* srcw = dash::atlasPowerSource(st_.pwr.src);
    snprintf(b, sizeof b, "%s%s%s. Mode: %s", v, *srcw ? ", " : "", srcw, *st_.mode ? st_.mode : "not said");
    bool low = !isnan(st_.pwr.soc) && st_.pwr.soc < 20;
    card(a, low ? BAD : TEXT, b);
  } else {
    snprintf(b, sizeof b, "Mode: %s", *st_.mode ? st_.mode : "not said");
    card("Atlas battery: not heard yet", MUTED, b);
  }
  // GPS
  if (st_.gps.have) {
    snprintf(a, sizeof a, "Position %.4f, %.4f", st_.gps.lat, st_.gps.lon);
    dash::fmtSpeed(t1, sizeof t1, st_.gps.kph, u);
    char age[20];
    dash::ageText((now - st_.gps.atMs) / 1000, age, sizeof age);
    if (isnan(st_.gps.hdg)) snprintf(b, sizeof b, "%s, %s", t1, age);
    else snprintf(b, sizeof b, "%s heading %d, %s", t1, (int)lroundf(st_.gps.hdg), age);
    card(a, TEXT, b);
  } else {
    card("Position: not heard yet", MUTED, "From Atlas's GPS while it's on.");
  }
}

void AtlasMod::draw(uint32_t now, bool full) {
  if (qr_) {
    drawQr();
    return;
  }
  bool live = dash::atlasLive(st_, now);
  bool dataChanged = full || st_.rev != drawnRev_ || live != drawnLive_ || (page_ == 3 && fobRev_ != drawnFobRev_);
  drawnTick_ = now;
  drawHeader(now);
  if (full) segments(kPages, kNPages, page_, kTop + 50);
  if (!dataChanged) return;
  drawnRev_ = st_.rev;
  drawnLive_ = live;
  if (page_ == 0) drawGauges(now, full);
  else if (page_ == 1) drawCodes(now);
  else if (page_ == 2) drawTrip(now);
  else drawFob(now);
}

void AtlasMod::tap(int x, int y, uint32_t now) {
  if (qr_) {
    if (y >= kNavY) {  // Close
      qr_ = 0;
      drawnRev_ = UINT32_MAX;
      clearContent();
      draw(now, true);
    } else if (y >= kQrY && y < kNavY) {  // the code (or the words under it): the other one
      qr_ = qr_ == 1 ? 2 : 1;
      qrDrawn_ = false;
    }
    return;
  }
  if (hit(x, y, kQrBtnX - 4, kTop, kQrBtnW + 8, 48)) {
    qr_ = 1;
    qrDrawn_ = false;
    return;
  }
  if (y >= kTop + 50 && y < kTop + 94) {
    page_ = segmentAt(x, kNPages);
    if (page_ == 3) fobNext_ = now;  // fetch on the next loop
    codePage_ = 0;
    segments(kPages, kNPages, page_, kTop + 50);
    drawnRev_ = UINT32_MAX;
    tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
    draw(now, true);
    return;
  }
  if (page_ == 3) {
    // Buttons: Lock | Unlock driver, Unlock all | Start, Skip next | Clear pause.
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 2; c++)
        if (hit(x, y, 12 + c * (kFobBtnW + 8), kFobBtnY + r * (kFobBtnH + 6), kFobBtnW, kFobBtnH)) {
          int b = r * 2 + c;
          if (b == 0) fobCommand("lock", now);
          else if (b == 1) fobCommand("unlock_driver", now);
          else if (b == 2) fobCommand("unlock_all", now);
          else if (b == 3) {
            // A start needs a second tap within 5 s.
            if (armUntil_ && (int32_t)(now - armUntil_) < 0) {
              armUntil_ = 0;
              fobCommand("start", now);
            } else {
              armUntil_ = now + 5000;
              snprintf(fobMsg_, sizeof fobMsg_, "Tap Start again to start the truck");
              fobRev_++;
            }
          } else if (b == 4) fobPause("{\"skip_next\":true}", now);
          else fobPause("{\"clear\":true}", now);
          return;
        }
    return;
  }
  if (page_ == 1 && y >= kNavY && x >= W - 12 - 128 - 12 && st_.dtc.n > kCodesPerPage) {
    codePage_++;
    drawCodes(now);
  }
}

// ---------- Fob page ----------
bool AtlasMod::fobReachable() const {
  return *DASH_ATLAS_OWNER_TOKEN && WiFi.status() == WL_CONNECTED && (*DASH_ATLAS_API_HOST || net::onAtlasWifi());
}

int AtlasMod::fobHttp(const char* method, const char* path, const String& body, String* reply) {
  WiFiClient c;
  HTTPClient http;
  http.setTimeout(3000);
  http.setConnectTimeout(3000);
  String host = *DASH_ATLAS_API_HOST ? String(DASH_ATLAS_API_HOST) : IPAddress(net::gateway()).toString();
  String url = "http://" + host + ":" + String(DASH_ATLAS_API_PORT) + path;
  if (!http.begin(c, url)) return -1;
  http.addHeader("Authorization", String("Bearer ") + DASH_ATLAS_OWNER_TOKEN);
  int code;
  if (!strcmp(method, "GET")) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    code = http.sendRequest(method, body);
  }
  if (reply && code > 0) *reply = http.getString();
  http.end();
  return code;
}

namespace {
// truckd's {"error": "..."} in words, or a fallback.
void errorWords(const String& body, int code, char* buf, size_t n) {
  JsonDocument d;
  if (code > 0 && !deserializeJson(d, body) && d["error"].is<const char*>()) snprintf(buf, n, "%.60s", d["error"].as<const char*>());
  else if (code == 401 || code == 403) snprintf(buf, n, "truckd refused the owner token");
  else if (code < 0) snprintf(buf, n, "truckd isn't answering");
  else snprintf(buf, n, "truckd answered %d", code);
}
}  // namespace

void AtlasMod::fobFetch(uint32_t now) {
  fobNext_ = now + kFobEveryMs;
  String body;
  int code = fobHttp("GET", "/api/fob", "", &body);
  if (code == 200) {
    dash::FobStatus f;
    if (dash::parseFob(body.c_str(), body.length(), f)) {
      fob_ = f;
      fobHave_ = true;
    }
  } else if (!*fobMsg_) {
    errorWords(body, code, fobMsg_, sizeof fobMsg_);
  }
  fobRev_++;
}

void AtlasMod::fobCommand(const char* c, uint32_t now) {
  uint32_t unix = unixNow(now);
  if (unix < 1700000000u) {
    snprintf(fobMsg_, sizeof fobMsg_, "No clock yet: wait for Atlas or the internet");
    fobRev_++;
    return;
  }
  char nonce[21];
  for (int i = 0; i < 20; i++) nonce[i] = "0123456789abcdefghijklmnopqrstuvwxyz"[esp_random() % 36];
  nonce[20] = 0;
  char body[128];
  snprintf(body, sizeof body, "{\"c\":\"%s\",\"nonce\":\"%s\",\"exp\":%lu}", c, nonce, (unsigned long)(unix + 25));
  String reply;
  int code = fobHttp("POST", "/api/cmd/fob", body, &reply);
  if (code == 202 || code == 200) snprintf(fobMsg_, sizeof fobMsg_, "%s sent. Waiting for the fob module...", dash::fobActionWords(c));
  else errorWords(reply, code, fobMsg_, sizeof fobMsg_);
  fobNext_ = now + 12000;  // the fob module answers within ~10 s
  fobRev_++;
}

void AtlasMod::fobPause(const char* body, uint32_t now) {
  String reply;
  int code = fobHttp("POST", "/api/fob/pause", body, &reply);
  if (code == 200) snprintf(fobMsg_, sizeof fobMsg_, "Done");
  else errorWords(reply, code, fobMsg_, sizeof fobMsg_);
  fobNext_ = now;
  fobRev_++;
}

void AtlasMod::drawFob(uint32_t now) {
  drawnFobRev_ = fobRev_;
  tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
  if (!*DASH_ATLAS_OWNER_TOKEN) {
    wrap("Remote start, lock and unlock need a truckd owner token: pair DASH like a phone and set DASH_ATLAS_OWNER_TOKEN in secrets.h. "
         "The DASH phone app also does all of this.",
         16, kBodyY + 8, W - 32, 2, MUTED, BG, 6);
    return;
  }
  if (!fobReachable()) {
    wrap("Join the truck Wi-Fi to use the fob module. Its schedule keeps running on its own.", 16, kBodyY + 8, W - 32, 2, MUTED, BG, 4);
    return;
  }
  char a[72], b[72];
  int y = kBodyY;
  tft.fillRoundRect(12, y, W - 24, 56, 8, CARD);
  if (fobHave_) {
    if (fob_.battery >= 0) snprintf(a, sizeof a, "Fob module %d%%", fob_.battery);
    else snprintf(a, sizeof a, "Fob module");
    textOn(a, 24, y + 6, 2, fob_.battery >= 0 && fob_.battery <= 10 ? BAD : TEXT, CARD);
    fobPauseWords(fob_, b, sizeof b);
    if (!*b && *fob_.lastC) snprintf(b, sizeof b, "Last: %s, %s", dash::fobActionWords(fob_.lastC), fob_.lastOk ? "done" : "didn't work");
    char cut[72];
    fit(cut, sizeof cut, *b ? b : "Nothing paused", 2, W - 48);
    textOn(cut, 24, y + 30, 2, MUTED, CARD);
  } else {
    textOn("Asking truckd...", 24, y + 18, 2, MUTED, CARD);
  }
  y += 62;
  // Schedule: up to 3 lines here; the phone app edits it.
  if (fobHave_) {
    if (!fob_.n) text("No schedule. Set one in the phone app.", 16, y, 2, MUTED);
    for (int i = 0; i < fob_.n && i < 3; i++) {
      dash::fobEntryWords(fob_.entries[i], a, sizeof a);
      if (i == 2 && fob_.n > 3) snprintf(a + strlen(a), sizeof a - strlen(a), " (+%u more)", fob_.n - 3);
      text(a, 16, y + i * 22, 2, TEXT);
    }
  }
  const char* labels[6] = {"Lock", "Unlock driver", "Unlock all", armUntil_ ? "Tap to start" : "Start", "Skip next", "Clear pause"};
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 2; c++) {
      int i = r * 2 + c;
      button(labels[i], 12 + c * (kFobBtnW + 8), kFobBtnY + r * (kFobBtnH + 6), kFobBtnW, kFobBtnH, i == 3 && armUntil_);
    }
  if (*fobMsg_) {
    char cut[72];
    fit(cut, sizeof cut, fobMsg_, 2, W - 32);
    text(cut, 16, kFobBtnY + 3 * (kFobBtnH + 6), 2, GOLD);
  }
  (void)now;
}

// ---------- QR screen ----------
void AtlasMod::shareUrl(char* buf, size_t n) const {
  // The share's host: set, else the truck router (the Pi) when on the truck
  // Wi-Fi, else its mDNS name.
  if (*DASH_ATLAS_SHARE_HOST) snprintf(buf, n, "http://%s:%u/", DASH_ATLAS_SHARE_HOST, (unsigned)DASH_ATLAS_SHARE_PORT);
  else if (net::onAtlasWifi() && net::gateway())
    snprintf(buf, n, "http://%s:%u/", IPAddress(net::gateway()).toString().c_str(), (unsigned)DASH_ATLAS_SHARE_PORT);
  else snprintf(buf, n, "http://atlas.local:%u/", (unsigned)DASH_ATLAS_SHARE_PORT);
}

void AtlasMod::drawQr() {
  qrDrawn_ = true;
  clearContent();
  heading(qr_ == 1 ? "Truck Wi-Fi" : "Truck Share");
  text(qr_ == 1 ? "1 of 2" : "2 of 2", W - 16, kTop + 20, 2, MUTED, TR_DATUM);
  char code[256], a[96];  // a 32-byte name and 63-byte password, all escaped, fit
  int y = kQrY;
  if (qr_ == 1) {
    if (!*DASH_ATLAS_WIFI_SSID) {
      wrap("No truck Wi-Fi is set. Put its name and password in DASH_ATLAS_WIFI_SSID and DASH_ATLAS_WIFI_PASS in "
           "firmware/include/secrets.h, then build and flash DASH again.",
           16, y + 8, W - 32, 2, TEXT, BG, 6);
      wrap("Tap here for the Truck Share's address.", 16, y + 150, W - 32, 2, MUTED, BG, 2);
    } else {
      dash::wifiJoinCode(DASH_ATLAS_WIFI_SSID, DASH_ATLAS_WIFI_PASS, code, sizeof code);
      int side = qr(code, (W - kQrSide) / 2, y, kQrSide);
      if (!side) {
        wrap("The name and password are too long for a QR code. Join the truck Wi-Fi by hand.", 16, y + 8, W - 32, 2, TEXT, BG, 4);
        side = 100;
      }
      snprintf(a, sizeof a, "Network: %s", DASH_ATLAS_WIFI_SSID);
      char cut[96];
      fit(cut, sizeof cut, a, 2, W - 32);
      text(cut, W / 2, y + side + 6, 2, TEXT, TC_DATUM);
      text("Scan to join. Tap the code for the Share.", W / 2, y + side + 28, 2, MUTED, TC_DATUM);
    }
  } else {
    shareUrl(code, sizeof code);
    int side = qr(code, (W - kQrSide) / 2, y, kQrSide);
    char cut[96];
    fit(cut, sizeof cut, code, 2, W - 32);
    text(cut, W / 2, y + side + 6, 2, TEXT, TC_DATUM);
    text("Join the truck Wi-Fi first. Tap for Wi-Fi.", W / 2, y + side + 28, 2, MUTED, TC_DATUM);
  }
  navButton("Close", W - 12 - 128, 128, true);
}
