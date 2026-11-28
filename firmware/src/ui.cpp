#include "ui.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

#include "battery.h"
#include "hw.h"
#include "net.h"
#include "projects_link.h"
#include "settings.h"

#ifndef DASH_VERSION
#define DASH_VERSION "0.1.0-m1"
#endif

namespace ui {

namespace {
TFT_eSPI tft;
dash::Registry* links = nullptr;
ProjectsLink* projects = nullptr;

// Neutral dark with a gold accent, to sit next to FAITH's look.
uint16_t BG, CARD, TEXT, MUTED, GOLD, OK, BAD;

constexpr int W = 320, H = 480;
constexpr int kBarH = 32;              // status bar
constexpr int kTabH = 56;              // tab bar, comfortably over 44 px
constexpr int kTabY = H - kTabH;
constexpr int kTop = kBarH, kBottom = kTabY;
constexpr int kTabs = 4;               // Home, Projects, Links, Device
const char* kTabNames[kTabs] = {"Home", "Projects", "Links", "Device"};

constexpr int kRowH = 72, kRowY0 = kTop + 52;
constexpr int kToggleW = 84, kToggleH = 48;

Screen screen = Screen::Home, beforeTalk = Screen::Home;
bool needFull = true;
uint32_t lastTick = 0, lastTouch = 0, activity = 0;
bool touching = false;
uint32_t drawnLinksRev = 0, drawnHomeProjects = 0;
char drawnClock[8] = "";
char drawnBar[40] = "";

// ---------- small helpers ----------
void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum = TL_DATUM, int pad = 0) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, BG);
  tft.setTextPadding(pad);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

void clearContent() { tft.fillRect(0, kTop, W, kBottom - kTop, BG); }

void heading(const char* s) { text(s, 16, kTop + 14, 4, TEXT); }

// ---------- status bar ----------
void drawBar(bool force) {
  char left[24], right[16], sig[40];
  snprintf(left, sizeof left, "%s", net::up() ? (net::rssi() > -67 ? "Wi-Fi good" : "Wi-Fi weak") : "No Wi-Fi");
  uint32_t mv = hw::batteryMv();
  if (mv >= dash::kBatteryUsbMv) snprintf(right, sizeof right, "USB power");
  else snprintf(right, sizeof right, "%s%u%%", mv < dash::kBatteryLowMv ? "Low " : "", dash::batteryPercent(mv));
  snprintf(sig, sizeof sig, "%s|%s", left, right);
  if (!force && strcmp(sig, drawnBar) == 0) return;
  strcpy(drawnBar, sig);
  tft.fillRect(0, 0, W, kBarH, BG);
  text(left, 12, 8, 2, net::up() ? TEXT : BAD);
  text(right, W - 12, 8, 2, mv < dash::kBatteryLowMv ? BAD : TEXT, TR_DATUM);
  tft.drawFastHLine(0, kBarH - 1, W, CARD);
}

// ---------- tab bar ----------
void drawTabs() {
  tft.fillRect(0, kTabY, W, kTabH, BG);
  tft.drawFastHLine(0, kTabY, W, CARD);
  int tw = W / kTabs;
  for (int i = 0; i < kTabs; i++) {
    bool on = (int)screen == i;
    int x = i * tw;
    if (on) {
      tft.fillRect(x + 4, kTabY + 4, tw - 8, kTabH - 8, CARD);
      tft.fillRect(x + 16, kTabY + kTabH - 8, tw - 32, 3, GOLD);  // underline: not colour alone
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? TEXT : MUTED, on ? CARD : BG);
    uint8_t font = tft.textWidth(kTabNames[i], 4) <= tw - 12 ? 4 : 2;
    tft.drawString(kTabNames[i], x + tw / 2, kTabY + kTabH / 2 - 2, font);
  }
}

// ---------- Home ----------
void drawHome(bool full) {
  char clock[8] = "--:--", date[32] = "Clock not set yet";
  if (net::timeValid()) {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(clock, sizeof clock, "%H:%M", &tmv);
    strftime(date, sizeof date, "%A %e %B", &tmv);
  }
  bool linksChanged = links && links->revision() != drawnLinksRev;
  uint32_t pr = projects ? projects->revision() : 0;
  if (!full && strcmp(clock, drawnClock) == 0 && !linksChanged && pr == drawnHomeProjects) return;
  strcpy(drawnClock, clock);
  if (links) drawnLinksRev = links->revision();
  drawnHomeProjects = pr;

  if (full) clearContent();
  text(clock, W / 2, kTop + 40, 7, TEXT, TC_DATUM, W - 40);
  text(date, W / 2, kTop + 110, 4, MUTED, TC_DATUM, W - 20);

  // Links summary card.
  int y = kTop + 170;
  tft.fillRoundRect(12, y, W - 24, 84, 10, CARD);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TEXT, CARD);
  int problems = links ? links->problems() : 0;
  char line[40];
  if (problems == 0) snprintf(line, sizeof line, "All links connected");
  else snprintf(line, sizeof line, "%d link%s need%s a look", problems, problems == 1 ? "" : "s", problems == 1 ? "s" : "");
  tft.drawString(line, 28, y + 16, 4);
  tft.setTextColor(MUTED, CARD);
  tft.drawString(problems ? "Open Links for details" : "Hermes, Orb and Studio come next", 28, y + 52, 2);

  y += 100;
  tft.fillRoundRect(12, y, W - 24, 84, 10, CARD);
  tft.setTextColor(TEXT, CARD);
  if (projects && projects->enabled) {
    // Tap it to open Projects.
    tft.drawString("Projects", 28, y + 16, 4);
    char sum[40];
    if (projects->hasGlance()) dash::glanceSummary(projects->glance(), sum, sizeof sum);
    else snprintf(sum, sizeof sum, "Asking FAITH...");
    bool needs = projects->hasGlance() && projects->glance().needsYou;
    tft.setTextColor(needs ? GOLD : MUTED, CARD);
    tft.drawString(sum, 28, y + 52, 2);
    tft.setTextColor(MUTED, CARD);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("Open >", W - 28, y + 16, 2);
  } else {
    tft.drawString("Next task", 28, y + 16, 4);
    tft.setTextColor(MUTED, CARD);
    tft.drawString("Shows here once Orb is linked (M4)", 28, y + 52, 2);
  }
}
constexpr int kHomeProjectsY = kTop + 270;

// ---------- Links ----------
void drawLinks(bool full) {
  if (!links) return;
  if (!full && links->revision() == drawnLinksRev) return;
  drawnLinksRev = links->revision();
  clearContent();
  heading("Links");
  for (int i = 0; i < links->count(); i++) {
    dash::Connector* c = links->at(i);
    int y = kRowY0 + i * kRowH;
    dash::LinkState s = c->enabled ? c->state() : dash::LinkState::Off;
    uint16_t dot = s == dash::LinkState::Up ? OK : s == dash::LinkState::Off ? MUTED : s == dash::LinkState::Error ? BAD : GOLD;
    tft.fillRoundRect(12, y, W - 24, kRowH - 8, 10, CARD);
    tft.fillCircle(30, y + 22, 6, dot);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TEXT, CARD);
    tft.drawString(c->name(), 46, y + 10, 4);
    char line[64];
    const char* d = c->enabled ? c->detail() : "";
    snprintf(line, sizeof line, "%s%s%s", dash::linkStateText(s), *d ? ": " : "", d);
    tft.setTextColor(MUTED, CARD);
    tft.drawString(line, 46, y + 40, 2);
    // On/off switch, labelled with words.
    int bx = W - 24 - kToggleW, by = y + (kRowH - 8 - kToggleH) / 2;
    tft.fillRoundRect(bx, by, kToggleW, kToggleH, 8, c->enabled ? GOLD : BG);
    tft.drawRoundRect(bx, by, kToggleW, kToggleH, 8, GOLD);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(c->enabled ? BG : TEXT, c->enabled ? GOLD : BG);
    tft.drawString(c->enabled ? "On" : "Off", bx + kToggleW / 2, by + kToggleH / 2, 4);
  }
  if (links->count() == 0) text("No links set up.", 16, kRowY0, 4, MUTED);
}

bool tapLinks(int x, int y) {
  if (!links) return false;
  for (int i = 0; i < links->count(); i++) {
    int ry = kRowY0 + i * kRowH;
    int bx = W - 24 - kToggleW;
    // Generous hit box: the whole right part of the row.
    if (y >= ry && y < ry + kRowH - 8 && x >= bx - 12) {
      dash::Connector* c = links->at(i);
      c->enabled = !c->enabled;
      settings::setLinkEnabled(c->name(), c->enabled);
      if (c->enabled) c->begin();
      drawLinks(true);
      return true;
    }
  }
  return false;
}

// ---------- Projects ----------
// FAITH's family projects, read-only: a list by project, and one thread's
// latest line. Tap a thread to open it. Pages, not scrolling, until the knob (M3).
constexpr int kListY = kTop + 52;
constexpr int kNavH = 48, kNavY = kBottom - kNavH - 6;
constexpr int kHeadRowH = 28, kThreadRowH = 56;
constexpr int kMaxRows = dash::Glance::kProjects + dash::Glance::kThreads;
constexpr int kMaxPages = 16;
dash::GlanceRow rows[kMaxRows];
int nRows = 0, nPages = 1, page = 0;
int pageStart[kMaxPages + 1] = {0};
char openThread[40] = "";  // the thread being read, by id; empty: the list
uint32_t drawnProjectsRev = UINT32_MAX;
uint32_t drawnProjectsLinks = UINT32_MAX;

// Cuts `s` to fit `maxW` pixels in `font`, ending with "..." when cut.
void fit(char* out, size_t n, const char* s, uint8_t font, int maxW) {
  snprintf(out, n, "%s", s);
  if (tft.textWidth(out, font) <= maxW) return;
  size_t len = strlen(out);
  while (len > 0) {
    out[--len] = 0;
    char tmp[80];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (tft.textWidth(tmp, font) <= maxW) {
      snprintf(out, n, "%s", tmp);
      return;
    }
  }
}

// Draws `s` wrapped at spaces in `font`, from y, at most `lines` lines. Returns the next y.
int wrap(const char* s, int x, int y, int maxW, uint8_t font, uint16_t fg, uint16_t bg, int lines) {
  char line[80];
  const char* p = s;
  int lh = tft.fontHeight(font) + 4;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, bg);
  while (*p && lines-- > 0) {
    while (*p == ' ') p++;
    size_t take = 0, lastSpace = 0;
    while (p[take]) {
      snprintf(line, sizeof line, "%.*s", (int)(take + 1), p);
      if (tft.textWidth(line, font) > maxW) break;
      if (p[take] == ' ') lastSpace = take;
      take++;
    }
    if (p[take] && lastSpace && lines > 0) take = lastSpace;  // break at a word
    if (p[take] && lines == 0) {                               // last line: cut with "..."
      char cut[80];
      fit(cut, sizeof cut, p, font, maxW);
      tft.drawString(cut, x, y, font);
      return y + lh;
    }
    snprintf(line, sizeof line, "%.*s", (int)take, p);
    tft.drawString(line, x, y, font);
    p += take;
    y += lh;
  }
  return y;
}

uint16_t statusColour(const char* st) {
  if (dash::threadNeedsYou(st)) return BAD;
  if (!strcmp(st, "working")) return GOLD;
  return MUTED;
}

void navButton(const char* label, int x, int w, bool primary) {
  tft.fillRoundRect(x, kNavY, w, kNavH, 8, primary ? GOLD : CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(primary ? BG : TEXT, primary ? GOLD : CARD);
  tft.drawString(label, x + w / 2, kNavY + kNavH / 2, 4);
}

void layoutPages() {
  nRows = dash::glanceRows(projects->glance(), rows, kMaxRows);
  nPages = 0;
  int y = kListY, bottom = kNavY - 6;
  for (int i = 0; i < nRows; i++) {
    int h = rows[i].heading ? kHeadRowH : kThreadRowH;
    // Keep a heading with its first thread.
    int need = h + (rows[i].heading && i + 1 < nRows && !rows[i + 1].heading ? kThreadRowH : 0);
    if (i == 0 || y + need > bottom) {
      if (nPages >= kMaxPages) { nRows = i; break; }
      pageStart[nPages++] = i;
      y = kListY;
    }
    y += h;
  }
  if (nPages == 0) pageStart[nPages++] = 0;
  pageStart[nPages] = nRows;
  if (page >= nPages) page = nPages - 1;
}

// Where a row of the current page is drawn, or -1 past the page.
int rowY(int i) {
  int y = kListY;
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    if (k == i) return y;
    y += rows[k].heading ? kHeadRowH : kThreadRowH;
  }
  return -1;
}

int findThread(const char* id) {
  const dash::Glance& g = projects->glance();
  for (int i = 0; i < g.nThreads; i++)
    if (!strcmp(g.threads[i].id, id)) return i;
  return -1;
}

void drawProjectsMessage(const char* a, const char* b) {
  text(a, 16, kListY + 8, 4, TEXT);
  wrap(b, 16, kListY + 48, W - 32, 2, MUTED, BG, 4);
}

void drawThread(int ti) {
  const dash::Glance& g = projects->glance();
  const dash::GlanceThread& t = g.threads[ti];
  char buf[64];
  text(g.projects[t.project].name, 16, kTop + 16, 2, MUTED);
  int y = wrap(t.title, 16, kTop + 38, W - 32, 4, TEXT, BG, 2) + 6;
  // Status as a badge with words.
  const char* words = dash::threadStatusText(t.status);
  int bw = tft.textWidth(words, 2) + 20;
  tft.fillRoundRect(16, y, bw, 26, 13, CARD);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(statusColour(t.status), CARD);
  tft.drawString(words, 26, y + 13, 2);
  y += 42;
  if (*t.last) {
    text("Latest", 16, y, 2, MUTED);
    y = wrap(t.last, 16, y + 22, W - 32, 4, TEXT, BG, 4) + 12;
  }
  snprintf(buf, sizeof buf, "%s", dash::threadNeedsYou(t.status) ? "Answer it in the FAITH app or a chat." : "Read it all or send it more in the FAITH app.");
  wrap(buf, 16, y, W - 32, 2, MUTED, BG, 2);
  navButton("Back", 12, W - 24, false);
}

void drawProjects(bool full) {
  uint32_t rev = projects ? projects->revision() : 0;
  uint32_t lrev = links ? links->revision() : 0;
  if (!full && rev == drawnProjectsRev && lrev == drawnProjectsLinks) return;
  drawnProjectsRev = rev;
  drawnProjectsLinks = lrev;
  clearContent();

  if (!projects || !projects->enabled) {
    heading("Projects");
    drawProjectsMessage("Projects are off", "Add DASH_FAITH_TOKEN to secrets.h (see the firmware README), or turn Projects on in Links.");
    return;
  }
  if (!projects->hasGlance()) {
    heading("Projects");
    const char* d = projects->detail();
    drawProjectsMessage("Asking FAITH...", projects->state() == dash::LinkState::Error && *d ? d : "Your projects show here in a moment.");
    return;
  }
  layoutPages();
  if (*openThread) {
    int ti = findThread(openThread);
    if (ti >= 0) {
      drawThread(ti);
      return;
    }
    openThread[0] = 0;  // put away or gone: back to the list
  }

  heading("Projects");
  const dash::Glance& g = projects->glance();
  char sum[40];
  dash::glanceSummary(g, sum, sizeof sum);
  text(sum, W - 16, kTop + 20, 2, g.needsYou ? GOLD : MUTED, TR_DATUM);
  if (nRows == 0) {
    drawProjectsMessage("No projects", "FAITH shows no projects for this token.");
    return;
  }
  char line[80];
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    int y = rowY(k);
    if (rows[k].heading) {
      const dash::GlanceProject& p = g.projects[rows[k].index];
      text(p.name, 16, y + 8, 2, MUTED);
      if (p.count == 0) text("No threads yet", W - 16, y + 8, 2, MUTED, TR_DATUM);
      continue;
    }
    const dash::GlanceThread& t = g.threads[rows[k].index];
    tft.fillRoundRect(12, y, W - 24, kThreadRowH - 6, 8, CARD);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TEXT, CARD);
    fit(line, sizeof line, t.title, 2, W - 56);
    tft.drawString(line, 24, y + 7, 2);
    tft.setTextColor(TEXT, CARD);
    tft.drawString(">", W - 32, y + 7, 2);
    const char* words = dash::threadStatusText(t.status);
    tft.setTextColor(statusColour(t.status), CARD);
    tft.drawString(words, 24, y + 27, 2);
    int x = 24 + tft.textWidth(words, 2) + 10;
    if (*t.last && strcmp(t.last, words)) {
      fit(line, sizeof line, t.last, 2, W - 24 - x - 12);
      tft.setTextColor(MUTED, CARD);
      tft.drawString(line, x, y + 27, 2);
    }
  }
  if (nPages > 1) {
    char more[32];
    snprintf(more, sizeof more, "Page %d of %d", page + 1, nPages);
    text(more, 16, kNavY + 16, 2, MUTED);
    navButton(page + 1 < nPages ? "Next" : "First", W - 12 - 128, 128, true);
  }
}

void tapProjects(int x, int y) {
  if (!projects || !projects->hasGlance()) return;
  if (*openThread) {
    if (y >= kNavY) {
      openThread[0] = 0;
      drawProjects(true);
    }
    return;
  }
  if (nPages > 1 && y >= kNavY && x >= W - 12 - 128 - 12) {
    page = (page + 1) % nPages;
    drawProjects(true);
    return;
  }
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    int ry = rowY(k);
    if (rows[k].heading || y < ry || y >= ry + kThreadRowH - 6) continue;
    snprintf(openThread, sizeof openThread, "%s", projects->glance().threads[rows[k].index].id);
    drawProjects(true);
    return;
  }
}

// ---------- Device ----------
void row(int i, const char* label, const char* value) {
  int y = kTop + 60 + i * 34;
  text(label, 16, y, 2, MUTED);
  text(value, 120, y, 2, TEXT, TL_DATUM, W - 130);
}

void drawDevice(bool full) {
  if (full) {
    clearContent();
    heading("Device");
    text("Hold BOOT while powering on to", 16, kBottom - 52, 2, MUTED);
    text("calibrate the touch screen again.", 16, kBottom - 32, 2, MUTED);
  }
  char v[48];
  uint32_t mv = hw::batteryMv();
  snprintf(v, sizeof v, "%u mV (%u%%)%s", mv, dash::batteryPercent(mv), mv >= dash::kBatteryUsbMv ? ", USB" : "");
  row(0, "Battery", v);
  row(1, "Wi-Fi", net::statusText());
  row(2, "Address", *net::ip() ? net::ip() : "none");
  if (net::up()) snprintf(v, sizeof v, "%d dBm", net::rssi()); else snprintf(v, sizeof v, "-");
  row(3, "Signal", v);
  snprintf(v, sizeof v, "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024));
  row(4, "Memory", v);
  uint32_t s = millis() / 1000;
  snprintf(v, sizeof v, "%uh %02um %02us", s / 3600, (s / 60) % 60, s % 60);
  row(5, "Up for", v);
  row(6, "Firmware", DASH_VERSION);
}

// ---------- Talk (placeholder) ----------
void drawTalk() {
  clearContent();
  text("Listening...", W / 2, kTop + 120, 4, GOLD, TC_DATUM);
  text("Hold-to-talk needs the", W / 2, kTop + 180, 2, MUTED, TC_DATUM);
  text("Hermes link (roadmap M2).", W / 2, kTop + 200, 2, MUTED, TC_DATUM);
  text("Let go to go back.", W / 2, kTop + 240, 2, MUTED, TC_DATUM);
}

void draw(bool full) {
  switch (screen) {
    case Screen::Home: drawHome(full); break;
    case Screen::Projects: drawProjects(full); break;
    case Screen::Links: drawLinks(full); break;
    case Screen::Device: drawDevice(full); break;
    case Screen::Talk: if (full) drawTalk(); break;
    default: break;
  }
}

void onTap(int x, int y) {
  if (y >= kTabY) {
    show((Screen)min(x / (W / kTabs), kTabs - 1));
    return;
  }
  if (screen == Screen::Links) tapLinks(x, y);
  if (screen == Screen::Projects) tapProjects(x, y);
  if (screen == Screen::Home && projects && projects->enabled && y >= kHomeProjectsY && y < kHomeProjectsY + 84) show(Screen::Projects);
}
}  // namespace

void begin(dash::Registry* l, ProjectsLink* p) {
  links = l;
  projects = p;
  tft.init();
  tft.setRotation(0);  // portrait, USB at the bottom (verify on hardware)
  BG = tft.color565(27, 26, 24);
  CARD = tft.color565(42, 40, 35);
  TEXT = tft.color565(239, 233, 220);
  MUTED = tft.color565(160, 152, 138);
  GOLD = tft.color565(201, 162, 39);
  OK = tft.color565(95, 163, 106);
  BAD = tft.color565(220, 110, 90);
  tft.fillScreen(BG);

  uint16_t cal[5];
  if (settings::touchCalibration(cal) && !hw::bootHeld()) tft.setTouch(cal);
  else calibrateTouch();
  tft.fillScreen(BG);
  needFull = true;
}

void calibrateTouch() {
  tft.fillScreen(BG);
  text("Touch calibration", W / 2, 120, 4, TEXT, TC_DATUM);
  text("Tap each corner arrow as it appears.", W / 2, 170, 2, MUTED, TC_DATUM);
  text("Use a stylus or fingernail.", W / 2, 192, 2, MUTED, TC_DATUM);
  uint16_t cal[5];
  tft.calibrateTouch(cal, GOLD, BG, 20);
  settings::saveTouchCalibration(cal);
  Serial.printf("touch calibration: %u %u %u %u %u\n", cal[0], cal[1], cal[2], cal[3], cal[4]);
}

void show(Screen s) {
  if (s == screen && !needFull) {
    // Tapping Projects again goes back to its list.
    if (s == Screen::Projects && *openThread) {
      openThread[0] = 0;
      needFull = true;
    }
    return;
  }
  if (s == Screen::Projects) {
    page = 0;
    openThread[0] = 0;
    if (projects) projects->refreshSoon();
  }
  screen = s;
  needFull = true;
}

void nextTab() {
  int i = screen == Screen::Talk ? 0 : ((int)screen + 1) % kTabs;
  show((Screen)i);
}

Screen current() { return screen; }

void talkStart() {
  if (screen != Screen::Talk) beforeTalk = screen;
  show(Screen::Talk);
}

void talkEnd() {
  if (screen == Screen::Talk) show(beforeTalk);
}

uint32_t lastActivity() { return activity; }
void noteActivity(uint32_t now) { activity = now; }

void loop(uint32_t now) {
  // Touch: act on the press, not while held.
  if (now - lastTouch >= 20) {
    lastTouch = now;
    uint16_t x, y;
    bool down = tft.getTouch(&x, &y);
    if (down && !touching) {
      noteActivity(now);
      onTap(x, y);
    }
    touching = down;
  }

  if (needFull) {
    needFull = false;
    drawBar(true);
    if (screen != Screen::Talk) drawTabs();
    else tft.fillRect(0, kTabY, W, kTabH, BG);
    draw(true);
    lastTick = now;
    return;
  }
  // Data-driven screens: Links redraws on change, the rest once a second.
  if (screen == Screen::Links) drawLinks(false);
  if (now - lastTick >= 1000) {
    lastTick = now;
    drawBar(false);
    if (screen != Screen::Links) draw(false);
  }
}

}  // namespace ui
