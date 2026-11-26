// D.A.S.H firmware, milestone M1: screen, touch, Wi-Fi, battery, buttons,
// and the connector registry with a first link (FAITH health), plus FAITH's
// family projects (read-only).
#include <Arduino.h>

#include "config.h"
#include "connector.h"
#include "faith_link.h"
#include "hw.h"
#include "net.h"
#include "projects_link.h"
#include "settings.h"
#include "ui.h"

namespace {
dash::Registry links;
FaithLink faith(DASH_FAITH_URL);
ProjectsLink projects(DASH_FAITH_URL, DASH_FAITH_TOKEN);

constexpr uint32_t kDimAfterMs = 60000;
constexpr uint8_t kBright = 255, kDim = 24;
uint32_t lastBattery = 0;
bool dimmed = false;

void wake(uint32_t now) {
  ui::noteActivity(now);
  if (dimmed) {
    dimmed = false;
    hw::setBacklight(kBright);
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("\nD.A.S.H starting");
  hw::begin();
  settings::begin();
  hw::sampleBattery();

  faith.enabled = settings::linkEnabled(faith.name(), true);
  links.add(&faith);
  projects.enabled = settings::linkEnabled(projects.name(), true);
  links.add(&projects);

  ui::begin(&links, &projects);  // may run touch calibration first
  net::begin();
  links.begin();
  ui::noteActivity(millis());
}

void loop() {
  uint32_t now = millis();

  hw::Inputs in = hw::readButtons(now);
  if (in.a != dash::ButtonEvent::None || in.b != dash::ButtonEvent::None) wake(now);
  if (in.a == dash::ButtonEvent::LongStart) ui::talkStart();
  if (in.a == dash::ButtonEvent::LongEnd || in.a == dash::ButtonEvent::Click) ui::talkEnd();
  if (in.b == dash::ButtonEvent::Click) ui::nextTab();
  if (in.b == dash::ButtonEvent::LongStart) ui::show(ui::Screen::Home);

  if (now - lastBattery >= 1000) {
    lastBattery = now;
    hw::sampleBattery();
  }

  net::loop(now);
  links.loop(now, net::up());

  uint32_t before = ui::lastActivity();
  ui::loop(now);
  if (ui::lastActivity() != before) wake(now);

  if (!dimmed && now - ui::lastActivity() > kDimAfterMs) {
    dimmed = true;
    hw::setBacklight(kDim);
  }

  int problems = links.problems();
  hw::setLed(!net::up() ? hw::Led::Problem : problems ? hw::Led::Busy : hw::Led::Ok);

  delay(5);
}
