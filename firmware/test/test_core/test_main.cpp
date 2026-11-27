// Host tests for lib/dash_core. Run: pio test -e native
#include <unity.h>

#include "backoff.h"
#include "battery.h"
#include "button.h"
#include "connector.h"
#include "glance.h"
#include "atlas.h"
#include "share.h"
#include "mod.h"
#include <math.h>
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

// ---------- mods ----------
struct FakeMod : Mod {
  const char* id_;
  int frames = 0;
  char lastType[12] = "";
  explicit FakeMod(const char* id) : id_(id) {}
  const char* id() const override { return id_; }
  const char* name() const override { return id_; }
  void loop(uint32_t, bool) override {}
  LinkState state() const override { return LinkState::Up; }
  bool hasScreen() const override { return true; }
  void onFrame(const char* json, size_t len, Via, uint32_t) override {
    char m[16];
    peekFrame(json, len, m, sizeof m, lastType, sizeof lastType);
    frames++;
  }
};

void test_mods_route_by_id() {
  ModList l;
  FakeMod a("atlas"), b("ha");
  TEST_ASSERT_TRUE(l.add(&a));
  TEST_ASSERT_TRUE(l.add(&b));
  TEST_ASSERT_FALSE(l.add(&a));  // ids are unique
  const char* f = "{\"m\":\"atlas\",\"t\":\"obd\",\"s\":1}";
  TEST_ASSERT_TRUE(l.route(f, strlen(f), Via::EspNow, 0));
  TEST_ASSERT_EQUAL(1, a.frames);
  TEST_ASSERT_EQUAL(0, b.frames);
  TEST_ASSERT_EQUAL_STRING("obd", a.lastType);
  const char* other = "{\"m\":\"argos\",\"t\":\"hi\"}";
  TEST_ASSERT_FALSE(l.route(other, strlen(other), Via::Mqtt, 0));  // nobody by that id
  a.enabled = false;
  TEST_ASSERT_FALSE(l.route(f, strlen(f), Via::EspNow, 0));  // switched off: ignored
  TEST_ASSERT_EQUAL(1, a.frames);
}
void test_mods_screens_skip_disabled() {
  ModList l;
  FakeMod a("atlas"), b("ha");
  l.add(&a);
  l.add(&b);
  Mod* out[4];
  TEST_ASSERT_EQUAL(2, l.screens(out, 4));
  a.enabled = false;
  TEST_ASSERT_EQUAL(1, l.screens(out, 4));
  TEST_ASSERT_EQUAL_PTR(&b, out[0]);
}
void test_peek_frame_rejects() {
  char m[16], t[12];
  TEST_ASSERT_FALSE(peekFrame("nope", 4, m, sizeof m, t, sizeof t));
  TEST_ASSERT_FALSE(peekFrame("[1,2]", 5, m, sizeof m, t, sizeof t));
  TEST_ASSERT_FALSE(peekFrame("{\"t\":\"hi\"}", 10, m, sizeof m, t, sizeof t));  // no mod id
  char big[300];
  memset(big, ' ', sizeof big);
  memcpy(big, "{\"m\":\"atlas\",\"t\":\"hi\"}", 22);
  TEST_ASSERT_FALSE(peekFrame(big, sizeof big, m, sizeof m, t, sizeof t));  // over 240 bytes
  TEST_ASSERT_TRUE(peekFrame(big, 200, m, sizeof m, t, sizeof t));
}
void test_mod_setting_cycles() {
  static const char* const opts[] = {"US", "Metric"};
  ModSetting s{"units", "Units", opts, 2, 0};
  TEST_ASSERT_EQUAL_STRING("US", s.current());
  s.next();
  TEST_ASSERT_EQUAL_STRING("Metric", s.current());
  s.next();
  TEST_ASSERT_EQUAL_STRING("US", s.current());
  s.set(9);  // a saved value from an older build
  TEST_ASSERT_EQUAL_STRING("US", s.current());
}
void test_registry_problem_override() {
  struct Sleepy : FakeLink {
    bool problem() const override { return false; }
  };
  Registry r;
  Sleepy a;
  r.add(&a);
  r.loop(0, false);
  TEST_ASSERT_EQUAL(0, r.problems());
}

// ---------- Atlas ----------
AtlasFrame feed(AtlasState& s, const char* json, uint32_t now) { return s.apply(json, strlen(json), now); }

void test_atlas_obd_and_units() {
  AtlasState s;
  TEST_ASSERT_EQUAL((int)AtlasFrame::Obd, (int)feed(s, "{\"m\":\"atlas\",\"t\":\"obd\",\"ect\":92,\"v\":14.1,\"l100\":13.2,\"rng\":402,\"kph\":96}", 1000));
  TEST_ASSERT_TRUE(s.obd.have);
  TEST_ASSERT_TRUE(isnan(s.obd.oil));  // not sent: unknown
  char b[24];
  fmtTemp(b, sizeof b, s.obd.ect, Units::US);
  TEST_ASSERT_EQUAL_STRING("198 F", b);
  fmtTemp(b, sizeof b, s.obd.ect, Units::Metric);
  TEST_ASSERT_EQUAL_STRING("92 C", b);
  fmtEconomy(b, sizeof b, s.obd.l100, Units::US);
  TEST_ASSERT_EQUAL_STRING("17.8 mpg", b);
  fmtDistance(b, sizeof b, s.obd.rangeKm, Units::US);
  TEST_ASSERT_EQUAL_STRING("250 mi", b);
  fmtSpeed(b, sizeof b, s.obd.kph, Units::US);
  TEST_ASSERT_EQUAL_STRING("60 mph", b);
  fmtTemp(b, sizeof b, s.obd.oil, Units::US);
  TEST_ASSERT_EQUAL_STRING("--", b);
  fmtEconomy(b, sizeof b, 0, Units::US);  // stopped: no divide by zero
  TEST_ASSERT_EQUAL_STRING("--", b);
}
void test_atlas_live_stale_asleep() {
  AtlasState s;
  char b[40];
  atlasLinkWords(s, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Not heard yet", b);
  feed(s, "{\"t\":\"hi\",\"from\":\"truckd\",\"mode\":\"driving\",\"ign\":1}", 1000);
  TEST_ASSERT_TRUE(atlasLive(s, 5000));
  TEST_ASSERT_TRUE(s.ign);
  TEST_ASSERT_EQUAL_STRING("driving", s.mode);
  atlasLinkWords(s, 5000, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Live", b);
  atlasLinkWords(s, 1000 + 125000, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("No word since 2 min ago", b);
  feed(s, "{\"t\":\"bye\",\"why\":\"ign_off\"}", 200000);
  TEST_ASSERT_TRUE(s.asleep);
  TEST_ASSERT_FALSE(atlasLive(s, 200000));
  atlasLinkWords(s, 200000 + 3 * 3600000u, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Asleep, since 3 h ago", b);
  feed(s, "{\"t\":\"hi\",\"ts\":1764200000}", 300000);  // woke up, with the time
  TEST_ASSERT_FALSE(s.asleep);
  TEST_ASSERT_EQUAL_UINT32(1764200010, atlasUnixNow(s, 310000));
  TEST_ASSERT_EQUAL_UINT32(0, atlasUnixNow(AtlasState(), 310000));
}
void test_atlas_codes_words() {
  AtlasState s;
  feed(s, "{\"t\":\"dtc\",\"mil\":1,\"codes\":[\"P0420\",\"P0304\",\"bad\",\"P1ABC\",\"U0100\"]}", 0);
  TEST_ASSERT_TRUE(s.dtc.mil);
  TEST_ASSERT_EQUAL(4, s.dtc.n);  // "bad" dropped
  char b[104];
  dtcDescribe(s.dtc, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Catalytic converter not cleaning well enough (bank 1)", b);
  dtcDescribe(s.dtc, 1, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Misfire in cylinder 4", b);
  TEST_ASSERT_TRUE(dtcUrgent("P0304"));
  TEST_ASSERT_FALSE(dtcUrgent("P0420"));
  dtcDescribe(s.dtc, 2, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Engine/transmission, maker-specific", b);
  // The truck's own words win, and survive a repeat of the same list.
  feed(s, "{\"t\":\"dtx\",\"c\":\"P1ABC\",\"d\":\"Fuel pump driver module\"}", 0);
  feed(s, "{\"t\":\"dtc\",\"mil\":1,\"codes\":[\"P1ABC\"]}", 0);
  TEST_ASSERT_EQUAL(1, s.dtc.n);
  dtcDescribe(s.dtc, 0, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Fuel pump driver module", b);
  dtcArea("P0301", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("Engine/transmission: ignition or misfire", b);
  feed(s, "{\"t\":\"dtc\",\"mil\":0,\"codes\":[]}", 0);  // cleared
  TEST_ASSERT_EQUAL(0, s.dtc.n);
  TEST_ASSERT_FALSE(s.dtc.mil);
}
void test_atlas_codes_cap() {
  AtlasState s;
  feed(s, "{\"t\":\"dtc\",\"codes\":[\"P0101\",\"P0102\",\"P0113\",\"P0117\",\"P0118\",\"P0121\",\"P0128\",\"P0133\",\"P0135\",\"P0141\"]}", 0);
  TEST_ASSERT_EQUAL(AtlasCodes::kMax, s.dtc.n);
}
void test_atlas_sync_weather_power_gps() {
  AtlasState s;
  feed(s, "{\"t\":\"sync\",\"st\":\"waiting\",\"q\":37,\"last\":1764200000,\"via\":\"home wifi\"}", 0);
  TEST_ASSERT_EQUAL_UINT32(37, s.sync.queued);
  TEST_ASSERT_EQUAL_STRING("Waiting to sync", syncWords(s.sync.st));
  TEST_ASSERT_EQUAL_STRING("Sync not known", syncWords("weird"));
  feed(s, "{\"t\":\"wx\",\"c\":21.5,\"rh\":40,\"txt\":\"Clear\"}", 0);
  TEST_ASSERT_EQUAL_FLOAT(21.5f, s.wx.c);
  TEST_ASSERT_TRUE(isnan(s.wx.hpa));
  feed(s, "{\"t\":\"pwr\",\"v\":12.9,\"soc\":80}", 0);
  TEST_ASSERT_EQUAL_FLOAT(80.0f, s.pwr.soc);
  feed(s, "{\"t\":\"gps\",\"lat\":44.97,\"lon\":-93.26,\"kph\":10,\"hdg\":270}", 0);
  TEST_ASSERT_TRUE(s.gps.have);
  TEST_ASSERT_TRUE(fabs(s.gps.lon + 93.26) < 1e-4);
  AtlasState t;
  feed(t, "{\"t\":\"gps\"}", 0);  // no position: nothing kept
  TEST_ASSERT_FALSE(t.gps.have);
  TEST_ASSERT_EQUAL((int)AtlasFrame::Bad, (int)feed(t, "{oops", 0));
  TEST_ASSERT_EQUAL((int)AtlasFrame::Bad, (int)feed(t, "{\"x\":1}", 0));
}
void test_atlas_warnings() {
  TEST_ASSERT_EQUAL_STRING("HOT", coolantWarning(115));
  TEST_ASSERT_EQUAL_STRING("", coolantWarning(95));
  TEST_ASSERT_EQUAL_STRING("LOW", voltsWarning(12.4f, true));   // running and not charging
  TEST_ASSERT_EQUAL_STRING("", voltsWarning(12.4f, false));     // parked: fine
  TEST_ASSERT_EQUAL_STRING("LOW", voltsWarning(11.8f, false));
  TEST_ASSERT_EQUAL_STRING("HIGH", voltsWarning(15.8f, true));
  TEST_ASSERT_EQUAL_STRING("", voltsWarning(NAN, true));
}
void test_atlas_summary() {
  AtlasState s;
  char b[56];
  bool att = true;
  atlasSummary(s, 0, 0, Units::US, b, sizeof b, &att);
  TEST_ASSERT_EQUAL_STRING("Not heard yet", b);
  TEST_ASSERT_FALSE(att);
  feed(s, "{\"t\":\"obd\",\"ect\":92,\"v\":14.1,\"rng\":402}", 1000);
  atlasSummary(s, 2000, 0, Units::US, b, sizeof b, &att);
  TEST_ASSERT_EQUAL_STRING("198 F, 14.1 V, 250 mi range", b);
  feed(s, "{\"t\":\"dtc\",\"mil\":1,\"codes\":[\"P0217\"]}", 1000);
  atlasSummary(s, 2000, 0, Units::US, b, sizeof b, &att);
  TEST_ASSERT_EQUAL_STRING("Stop soon: P0217", b);
  TEST_ASSERT_TRUE(att);
  AtlasState z;
  feed(z, "{\"t\":\"sync\",\"st\":\"synced\",\"last\":1000}", 0);
  feed(z, "{\"t\":\"bye\",\"why\":\"ign_off\"}", 0);
  atlasSummary(z, 0, 1000 + 7200, Units::US, b, sizeof b, &att);
  TEST_ASSERT_EQUAL_STRING("Asleep, since just now. Synced 2 h ago", b);
}
void test_age_text() {
  char b[20];
  ageText(3, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("just now", b);
  ageText(45, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("45 s ago", b);
  ageText(720, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("12 min ago", b);
  ageText(3 * 86400, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("3 days ago", b);
}

// ---------- files ----------
void test_safe_file_name() {
  char b[32];
  TEST_ASSERT_TRUE(safeFileName("C:\\Users\\me\\photo 1.jpg", b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("photo 1.jpg", b);
  TEST_ASSERT_TRUE(safeFileName("../../etc/passwd", b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("passwd", b);
  TEST_ASSERT_FALSE(safeFileName("..", b, sizeof b));
  TEST_ASSERT_FALSE(safeFileName("/", b, sizeof b));
  TEST_ASSERT_FALSE(safeFileName("", b, sizeof b));
  TEST_ASSERT_TRUE(safeFileName(".hidden", b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("hidden", b);
  TEST_ASSERT_TRUE(safeFileName("caf\xC3\xA9<b>.txt", b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("caf___b_.txt", b);
  char s[12];
  TEST_ASSERT_TRUE(safeFileName("a-very-long-file-name.pdf", s, sizeof s));
  TEST_ASSERT_EQUAL_STRING("a-very-.pdf", s);  // extension kept
}
void test_numbered_name() {
  char b[32];
  numberedName("photo.jpg", 1, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("photo.jpg", b);
  numberedName("photo.jpg", 2, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("photo (2).jpg", b);
  numberedName("notes", 3, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("notes (3)", b);
  char s[14];
  numberedName("longername.jpg", 12, s, sizeof s);
  TEST_ASSERT_TRUE(strlen(s) < sizeof s);
  TEST_ASSERT_NOT_NULL(strstr(s, " (12).jpg"));
}
void test_fmt_bytes() {
  char b[16];
  fmtBytes(512, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("512 B", b);
  fmtBytes(3482, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("3.4 KB", b);
  fmtBytes(1610612736ULL, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("1.5 GB", b);
}
void test_wifi_join_code() {
  char b[80];
  wifiJoinCode("DASH-Share", "abc;def", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:DASH-Share;P:abc\\;def;;", b);
  wifiJoinCode("Open", "", b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:Open;;", b);
  char tiny[12];
  wifiJoinCode("DASH-Share", "abcdefghij", tiny, sizeof tiny);
  TEST_ASSERT_TRUE(strlen(tiny) < sizeof tiny);
}
void test_share_password() {
  uint8_t r[10] = {0, 1, 2, 3, 4, 250, 251, 252, 253, 254};
  char p[11];
  sharePassword(r, p, sizeof p);
  TEST_ASSERT_EQUAL(10, (int)strlen(p));
  for (int i = 0; i < 10; i++) TEST_ASSERT_NULL(strchr("0O1lIio", p[i]));
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
  RUN_TEST(test_mods_route_by_id);
  RUN_TEST(test_mods_screens_skip_disabled);
  RUN_TEST(test_peek_frame_rejects);
  RUN_TEST(test_mod_setting_cycles);
  RUN_TEST(test_registry_problem_override);
  RUN_TEST(test_atlas_obd_and_units);
  RUN_TEST(test_atlas_live_stale_asleep);
  RUN_TEST(test_atlas_codes_words);
  RUN_TEST(test_atlas_codes_cap);
  RUN_TEST(test_atlas_sync_weather_power_gps);
  RUN_TEST(test_atlas_warnings);
  RUN_TEST(test_atlas_summary);
  RUN_TEST(test_age_text);
  RUN_TEST(test_safe_file_name);
  RUN_TEST(test_numbered_name);
  RUN_TEST(test_fmt_bytes);
  RUN_TEST(test_wifi_join_code);
  RUN_TEST(test_share_password);
  return UNITY_END();
}
