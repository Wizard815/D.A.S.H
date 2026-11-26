// Host tests for lib/dash_core. Run: pio test -e native
#include <unity.h>

#include "backoff.h"
#include "battery.h"
#include "button.h"
#include "connector.h"
#include "glance.h"
#include <stdio.h>
#include <string.h>

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

// ---------- projects glance ----------
// The shape FAITH's GET /api/projects/glance sends (server/src/projects/projects.ts).
static const char* kGlance = R"({"projects":[
 {"id":"person-sam","name":"Sam","needs_you":0,"working":1,"threads":[
  {"id":"t1","title":"Gift for Robin","status":"working","last":""},
  {"id":"t2","title":"Dinner ideas","status":"done","last":"Tonight is fish tacos — Robin’s pick…"}]},
 {"id":"family","name":"Family","needs_you":1,"working":0,"threads":[
  {"id":"t3","title":"Saturday 🎉","status":"waiting","last":"Waiting for approval"}]},
 {"id":"parents","name":"Parents","needs_you":0,"working":0,"threads":[]}]})";

static Glance g;  // big: keep it off the stack
void test_glance_parses() {
  TEST_ASSERT_TRUE(parseGlance(kGlance, strlen(kGlance), g));
  TEST_ASSERT_EQUAL(3, g.nProjects);
  TEST_ASSERT_EQUAL(3, g.nThreads);
  TEST_ASSERT_EQUAL_STRING("Family", g.projects[1].name);
  TEST_ASSERT_EQUAL(2, g.projects[1].first);
  TEST_ASSERT_EQUAL(1, g.projects[1].count);
  TEST_ASSERT_EQUAL(0, g.projects[2].count);
  TEST_ASSERT_EQUAL(1, g.threads[2].project);
  TEST_ASSERT_EQUAL(1, g.needsYou);
  TEST_ASSERT_EQUAL(1, g.working);
  // Folded to ASCII for the screen's fonts; the emoji is dropped.
  TEST_ASSERT_EQUAL_STRING("Tonight is fish tacos - Robin's pick...", g.threads[1].last);
  TEST_ASSERT_EQUAL_STRING("Saturday ", g.threads[2].title);
}
void test_glance_rejects_junk() {
  const char* bad[] = {"", "nope", "{\"error\":\"Sign in again\"}", "[1,2]"};
  for (const char* b : bad) {
    g.nProjects = 9;
    TEST_ASSERT_FALSE(parseGlance(b, strlen(b), g));
    TEST_ASSERT_EQUAL(0, g.nProjects);
  }
}
void test_glance_caps_and_clips() {
  // 10 projects of 5 long threads: keeps 8 projects and 40 threads, clips text.
  static char big[16000];
  int o = snprintf(big, sizeof big, "{\"projects\":[");
  for (int p = 0; p < 10; p++) {
    o += snprintf(big + o, sizeof big - o, "%s{\"id\":\"p%d\",\"name\":\"P\",\"needs_you\":0,\"working\":5,\"threads\":[", p ? "," : "", p);
    for (int t = 0; t < 5; t++)
      o += snprintf(big + o, sizeof big - o, "%s{\"id\":\"t\",\"title\":\"%s\",\"status\":\"working\",\"last\":\"x\"}", t ? "," : "",
                    "A very long thread title that goes on and on and on");
    o += snprintf(big + o, sizeof big - o, "]}");
  }
  snprintf(big + o, sizeof big - o, "]}");
  TEST_ASSERT_TRUE(parseGlance(big, strlen(big), g));
  TEST_ASSERT_EQUAL(Glance::kProjects, g.nProjects);
  TEST_ASSERT_EQUAL(Glance::kThreads, g.nThreads);
  TEST_ASSERT_EQUAL(sizeof g.threads[0].title - 1, strlen(g.threads[0].title));
}
void test_glance_words() {
  TEST_ASSERT_EQUAL_STRING("Waiting for approval", threadStatusText("waiting"));
  TEST_ASSERT_EQUAL_STRING("Didn't work", threadStatusText("failed"));
  TEST_ASSERT_EQUAL_STRING("Unknown", threadStatusText("odd"));
  TEST_ASSERT_TRUE(threadNeedsYou("failed"));
  TEST_ASSERT_FALSE(threadNeedsYou("done"));
  char line[40];
  TEST_ASSERT_TRUE(parseGlance(kGlance, strlen(kGlance), g));
  glanceSummary(g, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("1 needs you, 1 working", line);
  g.needsYou = 0; g.working = 0;
  glanceSummary(g, line, sizeof line);
  TEST_ASSERT_EQUAL_STRING("Nothing going on", line);
}
void test_glance_rows() {
  TEST_ASSERT_TRUE(parseGlance(kGlance, strlen(kGlance), g));
  GlanceRow rows[16];
  int n = glanceRows(g, rows, 16);
  TEST_ASSERT_EQUAL(6, n);  // Sam, t1, t2, Family, t3, Parents
  TEST_ASSERT_TRUE(rows[0].heading);
  TEST_ASSERT_FALSE(rows[2].heading);
  TEST_ASSERT_EQUAL(1, rows[2].index);
  TEST_ASSERT_TRUE(rows[5].heading);
  TEST_ASSERT_EQUAL(2, rows[5].index);
  TEST_ASSERT_EQUAL(3, glanceRows(g, rows, 3));  // never past the buffer
}
void test_ascii_copy_bounds() {
  char s[5];
  asciiCopy(s, sizeof s, "\xE2\x80\xA6\xE2\x80\xA6");  // two ellipses, room for 4
  TEST_ASSERT_EQUAL_STRING("....", s);
  asciiCopy(s, sizeof s, "\xC3");  // a cut-off character
  TEST_ASSERT_EQUAL_STRING("", s);
  asciiCopy(s, sizeof s, nullptr);
  TEST_ASSERT_EQUAL_STRING("", s);
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
  RUN_TEST(test_glance_parses);
  RUN_TEST(test_glance_rejects_junk);
  RUN_TEST(test_glance_caps_and_clips);
  RUN_TEST(test_glance_words);
  RUN_TEST(test_glance_rows);
  RUN_TEST(test_ascii_copy_bounds);
  return UNITY_END();
}
