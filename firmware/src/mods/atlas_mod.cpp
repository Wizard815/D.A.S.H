#include "atlas_mod.h"

#include <WiFi.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "espnow_bus.h"
#include "gfx.h"
#include "net.h"

using namespace gfx;

namespace {
const char* const kUnits[] = {"US", "Metric"};
const char* const kLinks[] = {"Auto", "ESP-NOW", "Wi-Fi"};
const char* const kPages[] = {"Gauges", "Codes", "Trip"};
constexpr char kTopicSub[] = "dash/v1/atlas/#";
constexpr char kTopicOut[] = "dash/v1/atlas/dash";
constexpr uint32_t kReqEveryMs = 60000;  // ask again while nothing is heard
constexpr int kBodyY = kTop + 104;
constexpr int kCodesPerPage = 3;
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
  dash::AtlasFrame f = st_.apply(json, len, now);
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
  page_ = 0;
  codePage_ = 0;
  drawnRev_ = UINT32_MAX;
  request(millis());
}

bool AtlasMod::dirty(uint32_t now) {
  bool live = dash::atlasLive(st_, now);
  if (st_.rev != drawnRev_ || live != drawnLive_) return true;
  // Ages ("2 min ago") move: redraw the header every 5 s.
  return now - drawnTick_ >= 5000;
}

void AtlasMod::drawHeader(uint32_t now) {
  tft.fillRect(0, kTop, W, 48, BG);
  heading("Atlas");
  char w[72];
  bool live = dash::atlasLive(st_, now);
  // A remote start (or other owner command) in the last 2 minutes says so here.
  if (st_.cmdAtMs && now - st_.cmdAtMs < 120000) {
    snprintf(w, sizeof w, "%s", st_.cmdMsg[0] ? st_.cmdMsg : st_.cmdOk ? "Command done" : "Command failed");
    text(w, W - 16, kTop + 20, 2, st_.cmdOk ? GOOD : BAD, TR_DATUM);
    return;
  }
  if (live) snprintf(w, sizeof w, "Live, %s%s", dash::viaText(lastVia_), st_.ign ? ", running" : "");
  else dash::atlasLinkWords(st_, now, w, sizeof w);
  text(w, W - 16, kTop + 20, 2, live ? GOOD : st_.asleep ? MUTED : GOLD, TR_DATUM);
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
  bool live = dash::atlasLive(st_, now);
  bool dataChanged = full || st_.rev != drawnRev_ || live != drawnLive_;
  drawnTick_ = now;
  drawHeader(now);
  if (full) segments(kPages, 3, page_, kTop + 50);
  if (!dataChanged) return;
  drawnRev_ = st_.rev;
  drawnLive_ = live;
  if (page_ == 0) drawGauges(now, full);
  else if (page_ == 1) drawCodes(now);
  else drawTrip(now);
}

void AtlasMod::tap(int x, int y, uint32_t now) {
  if (y >= kTop + 50 && y < kTop + 94) {
    page_ = segmentAt(x, 3);
    codePage_ = 0;
    segments(kPages, 3, page_, kTop + 50);
    drawnRev_ = UINT32_MAX;
    tft.fillRect(0, kBodyY, W, kBottom - kBodyY, BG);
    draw(now, true);
    return;
  }
  if (page_ == 1 && y >= kNavY && x >= W - 12 - 128 - 12 && st_.dtc.n > kCodesPerPage) {
    codePage_++;
    drawCodes(now);
  }
}
