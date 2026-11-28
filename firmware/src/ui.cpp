#include "ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "battery.h"
#include "ble_reports.h"
#include "files_link.h"
#include "gfx.h"
#include "hw.h"
#include "mod.h"
#include "mods/mods.h"
#include "mods/view.h"
#include "net.h"
#include "settings.h"

#ifndef DASH_VERSION
#define DASH_VERSION "0.2.0-mods"
#endif

namespace ui {

namespace {
using namespace gfx;

dash::Registry* links = nullptr;
FilesLink* files = nullptr;
dash::ModList* modList = nullptr;

// Tabs: Home, Files, up to two mods by name (with three or more, the first
// and a "Mods" list), then Links. Device sits under Links. Five at most, so
// each stays over 60 px wide.
struct Tab {
  const char* name;
  Screen screen;
  dash::Mod* mod;  // for Screen::Mod
};
constexpr int kMaxTabs = 5;
Tab tabs[kMaxTabs];
int nTabs = 0;

constexpr int kRowH = 64, kRowY0 = kTop + 52;
constexpr int kToggleW = 84, kToggleH = 44;

Screen screen = Screen::Home, beforeTalk = Screen::Home;
dash::Mod* openMod = nullptr;      // the mod on Screen::Mod or Screen::ModSettings
dash::Mod* modsBack = nullptr;     // came from the Mods list
bool needFull = true;
uint32_t lastTick = 0, lastTouch = 0, activity = 0;
bool touching = false;
int touchX0 = 0, touchX = 0, touchY = 0;  // where the finger went down, and is now
uint32_t drawnLinksRev = 0;
char drawnHome[200] = "";
char drawnBar[40] = "";

void buildTabs() {
  nTabs = 0;
  tabs[nTabs++] = {"Home", Screen::Home, nullptr};
  tabs[nTabs++] = {"Files", Screen::Files, nullptr};
  dash::Mod* shown[dash::ModList::kMax];
  int n = modList ? modList->screens(shown, dash::ModList::kMax) : 0;
  if (n >= 1) tabs[nTabs++] = {shown[0]->name(), Screen::Mod, shown[0]};
  if (n == 2) tabs[nTabs++] = {shown[1]->name(), Screen::Mod, shown[1]};
  else if (n > 2) tabs[nTabs++] = {"Mods", Screen::Mods, nullptr};
  tabs[nTabs++] = {"Links", Screen::Links, nullptr};
}

// Which tab shows as selected for the current screen.
int currentTab() {
  for (int i = 0; i < nTabs; i++) {
    if (tabs[i].screen == Screen::Mod && screen == Screen::Mod && tabs[i].mod == openMod) return i;
    if (tabs[i].screen == Screen::Mods && screen == Screen::Mods) return i;
    if (tabs[i].screen == screen && screen != Screen::Mod) return i;
    if (tabs[i].screen == Screen::Links && (screen == Screen::Device || screen == Screen::ModSettings)) return i;
  }
  // A mod without its own tab, opened from the Mods list.
  for (int i = 0; i < nTabs; i++)
    if (tabs[i].screen == Screen::Mods && screen == Screen::Mod) return i;
  return -1;
}

dash::ModView* currentView() {
  if (screen == Screen::Files) return files;
  if (screen == Screen::Mod && openMod) return openMod->view();
  return nullptr;
}

// ---------- status bar ----------
void drawBar(bool force) {
  char left[24], right[16], sig[40];
  const char* share = files && files->sharing() ? ", sharing" : "";
  snprintf(left, sizeof left, "%s%s", net::up() ? (net::onAtlasWifi() ? "Truck Wi-Fi" : net::rssi() > -67 ? "Wi-Fi good" : "Wi-Fi weak") : "No Wi-Fi", share);
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
  int tw = W / nTabs, sel = currentTab();
  for (int i = 0; i < nTabs; i++) {
    bool on = i == sel;
    int x = i * tw;
    if (on) {
      tft.fillRect(x + 4, kTabY + 4, tw - 8, kTabH - 8, CARD);
      tft.fillRect(x + 12, kTabY + kTabH - 8, tw - 24, 3, GOLD);  // underline: not colour alone
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? TEXT : MUTED, on ? CARD : BG);
    uint8_t font = tft.textWidth(tabs[i].name, 4) <= tw - 12 ? 4 : 2;
    tft.drawString(tabs[i].name, x + tw / 2, kTabY + kTabH / 2 - 2, font);
  }
}

// ---------- Home ----------
// Clock and date, then cards: links, projects, files, then each mod's.
constexpr int kCardH = 64, kCardGap = 8, kCardY0 = kTop + 150, kQrChipW = 96;
constexpr int kMaxCards = (kBottom - 4 - kCardY0 + kCardGap) / (kCardH + kCardGap);
struct Card {
  dash::HomeCard c;
  Screen open;
  dash::Mod* mod;
};
Card cards[kMaxCards];
int nCards = 0;

void collectCards(uint32_t now) {
  nCards = 0;
  // Links only when something needs a look; it's quiet otherwise.
  int problems = links ? links->problems() : 0;
  if (problems) {
    Card& k = cards[nCards++];
    k = Card();
    snprintf(k.c.title, sizeof k.c.title, "Links");
    snprintf(k.c.line, sizeof k.c.line, "%d link%s need%s a look", problems, problems == 1 ? "" : "s", problems == 1 ? "s" : "");
    k.c.attention = true;
    k.open = Screen::Links;
  }
  // Mods with something to say come before the everyday cards.
  if (modList)
    for (int i = 0; i < modList->count() && nCards < kMaxCards; i++) {
      dash::Mod* m = modList->at(i);
      if (!m->enabled) continue;
      Card& k = cards[nCards];
      k = Card();
      if (!m->homeCard(k.c, now)) continue;
      k.open = m->hasScreen() ? Screen::Mod : Screen::Links;
      k.mod = m;
      nCards++;
    }
  if (files && files->enabled && nCards < kMaxCards) {
    Card& k = cards[nCards++];
    k = Card();
    files->homeCard(k.c);
    k.open = Screen::Files;
  }
}

void drawHome(bool full) {
  uint32_t now = millis();
  char clock[8] = "--:--", date[32] = "Clock not set yet";
  if (net::timeValid()) {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(clock, sizeof clock, "%H:%M", &tmv);
    strftime(date, sizeof date, "%A %e %B", &tmv);
  }
  collectCards(now);
  // Redraw only when something on it changed.
  char sig[sizeof drawnHome];
  int o = snprintf(sig, sizeof sig, "%s|", clock);
  for (int i = 0; i < nCards && o < (int)sizeof sig; i++) o += snprintf(sig + o, sizeof sig - o, "%s%s%d|", cards[i].c.title, cards[i].c.line, cards[i].c.attention);
  if (!full && strcmp(sig, drawnHome) == 0) return;
  strcpy(drawnHome, sig);

  clearContent();
  text(clock, W / 2, kTop + 30, 7, TEXT, TC_DATUM, W - 40);
  text(date, W / 2, kTop + 100, 4, MUTED, TC_DATUM, W - 20);
  char line[64];
  for (int i = 0; i < nCards; i++) {
    int y = kCardY0 + i * (kCardH + kCardGap);
    const dash::HomeCard& c = cards[i].c;
    tft.fillRoundRect(12, y, W - 24, kCardH, 10, CARD);
    if (c.attention) tft.fillRect(12, y + 8, 4, kCardH - 16, GOLD);  // with words below, never alone
    textOn(c.title, 28, y + 8, 4, TEXT, CARD);
    if (cards[i].open == Screen::Files) button("QR code", W - 12 - kQrChipW - 6, y + 8, kQrChipW, 44, true);  // the quick QR button
    else textOn("Open >", W - 28, y + 12, 2, MUTED, CARD, TR_DATUM);
    fit(line, sizeof line, c.line, 2, W - 56 - (cards[i].open == Screen::Files ? kQrChipW : 0));
    textOn(line, 28, y + 40, 2, c.attention ? GOLD : MUTED, CARD);
  }
  if (nCards == 0) text("Nothing to show yet.", W / 2, kCardY0 + 20, 2, MUTED, TC_DATUM);
}

// ---------- Links ----------
// Each link with its state in words and an On/Off switch. Mods with
// settings show "Settings >" under their name: tap the left of the row.
constexpr int kDeviceBtnY = kNavY;
int maxLinkRows() { return (kDeviceBtnY - 8 - kRowY0) / kRowH; }

dash::Mod* modFor(dash::Connector* c) {
  for (int k = 0; modList && k < modList->count(); k++)
    if (modList->at(k) == c) return modList->at(k);
  return nullptr;
}

void drawLinks(bool full) {
  if (!links) return;
  if (!full && links->revision() == drawnLinksRev) return;
  drawnLinksRev = links->revision();
  clearContent();
  heading("Links");
  int rows = min(links->count(), maxLinkRows());
  for (int i = 0; i < rows; i++) {
    dash::Connector* c = links->at(i);
    dash::Mod* m = modFor(c);
    int y = kRowY0 + i * kRowH;
    dash::LinkState s = c->enabled ? c->state() : dash::LinkState::Off;
    uint16_t dot = s == dash::LinkState::Up ? GOOD : s == dash::LinkState::Off ? MUTED : s == dash::LinkState::Error ? BAD : GOLD;
    tft.fillRoundRect(12, y, W - 24, kRowH - 6, 10, CARD);
    tft.fillCircle(28, y + 18, 6, dot);
    textOn(c->name(), 42, y + 8, 4, TEXT, CARD);
    if (m && m->settingCount()) textOn("Settings >", 42 + tft.textWidth(c->name(), 4) + 10, y + 12, 2, GOLD, CARD);
    char line[80], cut[80];
    const char* d = c->enabled ? c->detail() : "";
    snprintf(line, sizeof line, "%s%s%s", dash::linkStateText(s), *d ? ": " : "", d);
    fit(cut, sizeof cut, line, 2, W - 24 - kToggleW - 54);
    textOn(cut, 42, y + 36, 2, MUTED, CARD);
    // On/off switch, labelled with words.
    int bx = W - 20 - kToggleW, by = y + (kRowH - 6 - kToggleH) / 2;
    tft.fillRoundRect(bx, by, kToggleW, kToggleH, 8, c->enabled ? GOLD : BG);
    tft.drawRoundRect(bx, by, kToggleW, kToggleH, 8, GOLD);
    textOn(c->enabled ? "On" : "Off", bx + kToggleW / 2, by + kToggleH / 2, 4, c->enabled ? BG : TEXT, c->enabled ? GOLD : BG, MC_DATUM);
  }
  if (links->count() == 0) text("No links set up.", 16, kRowY0, 4, MUTED);
  navButton("Device info", 12, W - 24, false);
}

void tapLinks(int x, int y) {
  if (!links) return;
  if (y >= kDeviceBtnY) {
    show(Screen::Device);
    return;
  }
  int rows = min(links->count(), maxLinkRows());
  for (int i = 0; i < rows; i++) {
    int ry = kRowY0 + i * kRowH;
    if (y < ry || y >= ry + kRowH - 6) continue;
    dash::Connector* c = links->at(i);
    int bx = W - 20 - kToggleW;
    // Generous hit box: the whole right part of the row.
    if (x >= bx - 12) {
      c->enabled = !c->enabled;
      settings::setLinkEnabled(c->name(), c->enabled);
      if (c->enabled) c->begin();
      if (!c->enabled && c == files) files->stopShare();
      if (modFor(c)) {
        buildTabs();
        drawTabs();
      }
      drawLinks(true);
      return;
    }
    dash::Mod* m = modFor(c);
    if (m && m->settingCount()) {
      openMod = m;
      show(Screen::ModSettings);
    }
    return;
  }
}

// ---------- a mod's settings ----------
constexpr int kSetRowH = 64, kSetY0 = kTop + 56, kSetBtnW = 120;

void drawModSettings() {
  clearContent();
  if (!openMod) return;
  char h[40];
  snprintf(h, sizeof h, "%s settings", openMod->name());
  heading(h);
  for (int i = 0; i < openMod->settingCount(); i++) {
    dash::ModSetting* s = openMod->setting(i);
    int y = kSetY0 + i * kSetRowH;
    tft.fillRoundRect(12, y, W - 24, kSetRowH - 8, 10, CARD);
    textOn(s->label, 24, y + 18, 4, TEXT, CARD);
    button(s->current(), W - 20 - kSetBtnW, y + 6, kSetBtnW, kSetRowH - 20, true);
  }
  text("Tap a choice to change it. Saved right away.", 16, kNavY - 28, 2, MUTED);
  navButton("Back to Links", 12, W - 24, false);
}

void tapModSettings(int x, int y) {
  if (!openMod) return;
  if (y >= kNavY) {
    show(Screen::Links);
    return;
  }
  for (int i = 0; i < openMod->settingCount(); i++) {
    int ry = kSetY0 + i * kSetRowH;
    if (y >= ry && y < ry + kSetRowH - 8) {
      openMod->setting(i)->next();
      mods::settingChanged(openMod, i);
      drawModSettings();
      return;
    }
  }
}

// ---------- the Mods list (more than one mod with a screen) ----------
void drawMods() {
  clearContent();
  heading("Mods");
  dash::Mod* shown[dash::ModList::kMax];
  int n = modList ? modList->screens(shown, dash::ModList::kMax) : 0;
  uint32_t now = millis();
  for (int i = 0; i < n && i < kMaxCards + 1; i++) {
    int y = kTop + 52 + i * (kCardH + kCardGap);
    dash::HomeCard c;
    shown[i]->homeCard(c, now);
    tft.fillRoundRect(12, y, W - 24, kCardH, 10, CARD);
    textOn(shown[i]->name(), 28, y + 8, 4, TEXT, CARD);
    textOn("Open >", W - 28, y + 12, 2, MUTED, CARD, TR_DATUM);
    char line[64];
    fit(line, sizeof line, *c.line ? c.line : shown[i]->detail(), 2, W - 56);
    textOn(line, 28, y + 40, 2, c.attention ? GOLD : MUTED, CARD);
  }
  if (n == 0) wrap("No mods are on. Turn one on in Links.", 16, kTop + 60, W - 32, 2, MUTED, BG, 2);
}

// Tells the screen being left (Files or a mod) so it can stop a video.
void leaving() {
  dash::ModView* v = currentView();
  if (v) v->closed();
}

void openModScreen(dash::Mod* m) {
  if (!m || !m->hasScreen()) return;
  if (!(screen == Screen::Mod && openMod == m)) leaving();
  // Tapping its tab again starts it over (e.g. back to the project list).
  openMod = m;
  screen = Screen::Mod;
  if (m->view()) m->view()->opened();
  needFull = true;
}

void tapMods(int, int y) {
  dash::Mod* shown[dash::ModList::kMax];
  int n = modList ? modList->screens(shown, dash::ModList::kMax) : 0;
  for (int i = 0; i < n; i++) {
    int ry = kTop + 52 + i * (kCardH + kCardGap);
    if (y >= ry && y < ry + kCardH) {
      modsBack = shown[i];
      openModScreen(shown[i]);
      return;
    }
  }
}

void tapHome(int x, int y) {
  for (int i = 0; i < nCards; i++) {
    int ry = kCardY0 + i * (kCardH + kCardGap);
    if (y < ry || y >= ry + kCardH) continue;
    if (cards[i].open == Screen::Mod) openModScreen(cards[i].mod);
    else if (cards[i].open == Screen::Files && x >= W - 12 - kQrChipW - 12 && files) {
      show(Screen::Files);
      files->showQr();  // straight to the code, sharing turned on
    } else show(cards[i].open);
    return;
  }
}

// ---------- Device ----------
constexpr int kDevRowY0 = kTop + 52, kDevRowH = 28;
uint32_t forgetArmed = 0;  // "Forget phones" needs a second tap within 5 s

void row(int i, const char* label, const char* value) {
  int y = kDevRowY0 + i * kDevRowH;
  text(label, 16, y, 2, MUTED);
  text(value, 120, y, 2, TEXT, TL_DATUM, W - 130);
}

void drawDevice(bool full) {
  if (full) {
    clearContent();
    heading("Device");
    text("Hold BOOT at power-on to recalibrate touch.", 16, kNavY - 26, 2, MUTED);
    navButton("Back to Links", 12, W - 24, false);
  }
  char v[48];
  uint32_t mv = hw::batteryMv();
  snprintf(v, sizeof v, "%u mV (%u%%)%s", mv, dash::batteryPercent(mv), mv >= dash::kBatteryUsbMv ? ", USB" : "");
  row(0, "Battery", v);
  row(1, "Wi-Fi", net::statusText());
  row(2, "Address", *net::ip() ? net::ip() : "none");
  if (net::up()) snprintf(v, sizeof v, "%d dBm, channel %d", net::rssi(), WiFi.channel()); else snprintf(v, sizeof v, "-");
  row(3, "Signal", v);
  snprintf(v, sizeof v, "%s", WiFi.macAddress().c_str());
  row(4, "MAC", v);  // the bridge needs this for ESP-NOW
  snprintf(v, sizeof v, "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024));
  row(5, "Memory", v);
  uint32_t s = millis() / 1000;
  snprintf(v, sizeof v, "%uh %02um %02us", s / 3600, (s / 60) % 60, s % 60);
  row(6, "Up for", v);
  row(7, "Firmware", DASH_VERSION);
  // Bluetooth reports to the phone; tap twice to forget paired phones.
  if (forgetArmed && millis() - forgetArmed > 5000) forgetArmed = 0;
  row(8, "Phone", forgetArmed ? "tap again to forget paired phones" : ble_reports::status());
}

void tapDevice(int y) {
  if (y >= kNavY) {
    show(Screen::Links);
    return;
  }
  int ry = kDevRowY0 + 8 * kDevRowH;
  if (!DASH_BLE_REPORTS || y < ry - 6 || y >= ry + kDevRowH + 6) return;
  if (forgetArmed) {
    forgetArmed = 0;
    ble_reports::forgetPhones();
  } else {
    forgetArmed = millis();
  }
  drawDevice(false);
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
  uint32_t now = millis();
  switch (screen) {
    case Screen::Home: drawHome(full); break;
    case Screen::Links: drawLinks(full); break;
    case Screen::Device: drawDevice(full); break;
    case Screen::Mods: if (full) drawMods(); break;
    case Screen::ModSettings: if (full) drawModSettings(); break;
    case Screen::Talk: if (full) drawTalk(); break;
    case Screen::Files:
    case Screen::Mod: {
      dash::ModView* v = currentView();
      if (!v) break;
      if (full) clearContent();
      if (full || v->dirty(now)) v->draw(now, full);
      break;
    }
    default: break;
  }
}

void onTap(int x, int y) {
  if (y >= kTabY) {
    if (screen == Screen::Talk) return;
    int i = min(x / (W / nTabs), nTabs - 1);
    if (tabs[i].screen == Screen::Mod) openModScreen(tabs[i].mod);
    else show(tabs[i].screen);
    return;
  }
  if (y < kTop) return;
  uint32_t now = millis();
  switch (screen) {
    case Screen::Home: tapHome(x, y); break;
    case Screen::Links: tapLinks(x, y); break;
    case Screen::Device: tapDevice(y); break;
    case Screen::Mods: tapMods(x, y); break;
    case Screen::ModSettings: tapModSettings(x, y); break;
    case Screen::Files:
    case Screen::Mod: {
      dash::ModView* v = currentView();
      if (v) {
        v->tap(x, y, now);
        if (v->dirty(now)) v->draw(now, false);
      }
      break;
    }
    default: break;
  }
}
}  // namespace

void begin(dash::Registry* l, FilesLink* f, dash::ModList* m) {
  links = l;
  files = f;
  modList = m;
  buildTabs();
  tft.init();
  tft.setRotation(0);  // portrait, USB at the bottom (verify on hardware)
  initPalette();
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
  if (s == Screen::Mod) {
    openModScreen(openMod);
    return;
  }
  if (s != screen) leaving();
  if (s == screen && !needFull) {
    // Tapping Files again goes back to its main view.
    if (s == Screen::Files && files) {
      files->closed();
      files->opened();
      needFull = true;
    }
    return;
  }
  if (s == Screen::Files && files) files->opened();
  screen = s;
  needFull = true;
}

void nextTab() {
  int i = screen == Screen::Talk ? -1 : currentTab();
  const Tab& t = tabs[(i + 1) % nTabs];
  if (t.screen == Screen::Mod) openModScreen(t.mod);
  else show(t.screen);
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

// A phone pairing for Bluetooth reports: the code, over whatever is open.
uint32_t shownPasskey = 0;

void drawPasskey(uint32_t pk) {
  clearContent();
  heading("Pair a phone");
  char code[8];
  snprintf(code, sizeof code, "%06u", (unsigned)pk);
  text(code, W / 2, kTop + 90, 7, GOLD, TC_DATUM);
  wrap("A phone is pairing with DASH for live reports. Type this code on the phone. "
       "If it isn't your phone, tap Not mine.",
       16, kTop + 170, W - 32, 2, TEXT, BG, 5);
  navButton("Not mine", 12, W - 24, false);
}

void loop(uint32_t now) {
  uint32_t pk = ble_reports::passkey();
  if (pk != shownPasskey) {
    shownPasskey = pk;
    if (pk) {
      noteActivity(now);  // wakes the backlight
      drawPasskey(pk);
    } else {
      needFull = true;  // paired or gone: back to what was open
    }
  }
  if (shownPasskey) {
    if (now - lastTouch >= 20) {
      lastTouch = now;
      uint16_t x, y;
      bool down = tft.getTouch(&x, &y);
      if (down && !touching && y >= kNavY && y < kNavY + kNavH) ble_reports::rejectPairing();
      touching = down;
    }
    return;
  }
  // Touch: act on the press, not while held.
  if (now - lastTouch >= 20) {
    lastTouch = now;
    uint16_t x, y;
    bool down = tft.getTouch(&x, &y);
    if (down && !touching) {
      noteActivity(now);
      touchX0 = x;
      onTap(x, y);
    }
    if (down) {
      touchX = x;
      touchY = y;
    }
    if (!down && touching) {
      // Lifted: views get it too, for swipes.
      dash::ModView* v = currentView();
      if (v && touchY >= kTop && touchY < kBottom) {
        v->release(touchX, touchY, touchX - touchX0, now);
        if (v->dirty(now)) v->draw(now, false);
      }
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
  // Links and mod screens redraw on change; the rest once a second.
  if (screen == Screen::Links) drawLinks(false);
  if (screen == Screen::Files || screen == Screen::Mod) draw(false);
  if (now - lastTick >= 1000) {
    lastTick = now;
    drawBar(false);
    if (screen != Screen::Links && screen != Screen::Files && screen != Screen::Mod) draw(false);
  }
}

}  // namespace ui
