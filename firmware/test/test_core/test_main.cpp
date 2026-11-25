// Host tests for lib/dash_core. Run: pio test -e native
#include <unity.h>

#include "backoff.h"
#include "battery.h"
#include "button.h"
#include "connector.h"

using namespace dash;

void setUp() {}
void tearDown() {}

// ---------- battery ----------
void test_battery_ends() {
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercent(4200));
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercent(4500));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercent(3600));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercent(0));
}
void test_battery_interpolates() {
  TEST_ASSERT_EQUAL_UINT8(50, batteryPercent(3910));
  TEST_ASSERT_EQUAL_UINT8(55, batteryPercent(3930));  // halfway 3910..3950
  TEST_ASSERT_EQUAL_UINT8(80, batteryPercent(4020));
}
void test_battery_never_rises_as_voltage_falls() {
  uint8_t last = 100;
  for (uint32_t mv = 4250; mv >= 3500; mv -= 5) {
    uint8_t p = batteryPercent(mv);
    TEST_ASSERT_TRUE(p <= last);
    last = p;
  }
}
void test_battery_filter_smooths() {
  BatteryFilter f(0.5f);
  TEST_ASSERT_FALSE(f.ready());
  TEST_ASSERT_EQUAL_UINT32(4000, f.add(4000));
  TEST_ASSERT_EQUAL_UINT32(3900, f.add(3800));  // halfway
  TEST_ASSERT_TRUE(f.ready());
}

// ---------- button ----------
void test_button_ignores_bounce() {
  Button b(25, 450);
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 0));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, 5));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 10));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 30));  // only 20 ms stable
  TEST_ASSERT_EQUAL(ButtonEvent::Down, b.update(true, 36));
}
void test_button_click() {
  Button b(25, 450);
  b.update(true, 0);
  TEST_ASSERT_EQUAL(ButtonEvent::Down, b.update(true, 30));
  b.update(false, 100);
  TEST_ASSERT_EQUAL(ButtonEvent::Click, b.update(false, 130));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, 200));
}
void test_button_long_press() {
  Button b(25, 450);
  b.update(true, 0);
  TEST_ASSERT_EQUAL(ButtonEvent::Down, b.update(true, 30));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 400));
  TEST_ASSERT_EQUAL(ButtonEvent::LongStart, b.update(true, 480));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 2000));  // fires once
  b.update(false, 2100);
  TEST_ASSERT_EQUAL(ButtonEvent::LongEnd, b.update(false, 2130));
}
void test_button_survives_millis_wrap() {
  Button b(25, 450);
  uint32_t t = 0xFFFFFFF0u;
  b.update(true, t);
  TEST_ASSERT_EQUAL(ButtonEvent::Down, b.update(true, t + 30));  // wraps past 0
  TEST_ASSERT_EQUAL(ButtonEvent::LongStart, b.update(true, t + 500));
}

// ---------- backoff ----------
void test_backoff_doubles_and_caps() {
  Backoff b(1000, 8000);
  TEST_ASSERT_EQUAL_UINT32(1000, b.fail(500));  // r=500 -> exactly 100%
  TEST_ASSERT_EQUAL_UINT32(2000, b.fail(500));
  TEST_ASSERT_EQUAL_UINT32(4000, b.fail(500));
  TEST_ASSERT_EQUAL_UINT32(8000, b.fail(500));
  TEST_ASSERT_EQUAL_UINT32(8000, b.fail(500));
  for (int i = 0; i < 100; i++) b.fail(500);  // no overflow
  TEST_ASSERT_EQUAL_UINT32(8000, b.fail(500));
  b.reset();
  TEST_ASSERT_EQUAL_UINT32(1000, b.fail(500));
}
void test_backoff_jitter_range() {
  Backoff lo(1000, 1000), hi(1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(750, lo.fail(0));
  TEST_ASSERT_EQUAL_UINT32(1249, hi.fail(999));
}

// ---------- registry ----------
struct FakeLink : Connector {
  LinkState s = LinkState::Waiting;
  int begun = 0, loops = 0;
  const char* name() const override { return "fake"; }
  void begin() override { begun++; }
  void loop(uint32_t, bool netUp) override {
    loops++;
    if (netUp && s == LinkState::Waiting) s = LinkState::Up;
  }
  LinkState state() const override { return s; }
};

void test_registry_skips_disabled() {
  Registry r;
  FakeLink a, b;
  b.enabled = false;
  r.add(&a);
  r.add(&b);
  r.begin();
  r.loop(0, true);
  TEST_ASSERT_EQUAL(1, a.begun);
  TEST_ASSERT_EQUAL(0, b.begun);
  TEST_ASSERT_EQUAL(0, b.loops);
  TEST_ASSERT_EQUAL(0, r.problems());  // disabled links are not problems
}
void test_registry_revision_tracks_changes() {
  Registry r;
  FakeLink a;
  r.add(&a);
  uint32_t rev = r.revision();
  r.loop(0, false);
  TEST_ASSERT_EQUAL_UINT32(rev, r.revision());
  TEST_ASSERT_EQUAL(1, r.problems());
  r.loop(1, true);
  TEST_ASSERT_TRUE(r.revision() > rev);
  TEST_ASSERT_EQUAL(0, r.problems());
}
void test_registry_full() {
  Registry r;
  FakeLink l[Registry::kMax + 1];
  for (int i = 0; i < Registry::kMax; i++) TEST_ASSERT_TRUE(r.add(&l[i]));
  TEST_ASSERT_FALSE(r.add(&l[Registry::kMax]));
  TEST_ASSERT_FALSE(r.add(nullptr));
}
void test_state_text_never_empty() {
  for (int s = 0; s <= (int)LinkState::Error; s++)
    TEST_ASSERT_TRUE(linkStateText((LinkState)s)[0] != '\0');
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_ends);
  RUN_TEST(test_battery_interpolates);
  RUN_TEST(test_battery_never_rises_as_voltage_falls);
  RUN_TEST(test_battery_filter_smooths);
  RUN_TEST(test_button_ignores_bounce);
  RUN_TEST(test_button_click);
  RUN_TEST(test_button_long_press);
  RUN_TEST(test_button_survives_millis_wrap);
  RUN_TEST(test_backoff_doubles_and_caps);
  RUN_TEST(test_backoff_jitter_range);
  RUN_TEST(test_registry_skips_disabled);
  RUN_TEST(test_registry_revision_tracks_changes);
  RUN_TEST(test_registry_full);
  RUN_TEST(test_state_text_never_empty);
  return UNITY_END();
}
