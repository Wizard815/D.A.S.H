// Atlas bridge (ESP32-C3). Two jobs on one small chip:
//
// Bridge mode, while the Pi's awake pin is high: every line of JSON on the
// UART (a dash-mod frame from truckd) goes out over ESP-NOW to DASH, and
// every ESP-NOW frame from DASH comes out on the UART as one line.
//
// Scan mode, while the Pi is off: if the last sync frame it relayed wasn't
// "synced", it looks for a home Wi-Fi name every few minutes and, when one
// shows up, wakes the Pi through the Witty Pi's switch line. Between looks it
// sleeps (about 10 uA), and it wakes at once when the Pi turns on.
//
// Frames for the bridge itself use "m":"bridge": ping -> pong, and
// {"m":"bridge","t":"need","sync":1} to say there is (or isn't) data waiting.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

#if __has_include("bridge_config.h")
#include "bridge_config.h"
#else
#warning "include/bridge_config.h not found: using bridge_config.example.h"
#include "bridge_config.example.h"
#endif

namespace {
constexpr size_t kFrameMax = 240;
constexpr uint32_t kAsleepAfterMs = 5000;  // awake pin low this long: the Pi is off

// Kept across deep sleep.
RTC_DATA_ATTR bool needSync = true;  // first power-on: look, to be safe
RTC_DATA_ATTR uint8_t wakesTried = 0;

HardwareSerial uart(1);
uint8_t dashMac[6];
bool haveDash = false;
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW receive queue (the callback runs in the Wi-Fi task).
struct Slot {
  uint8_t len;
  char data[kFrameMax + 1];
};
Slot ring[8];
volatile int head = 0, tail = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len <= 0 || len > (int)kFrameMax) return;
  if (haveDash && memcmp(mac, dashMac, 6) != 0) return;  // only DASH
  portENTER_CRITICAL(&mux);
  int next = (head + 1) % 8;
  if (next != tail) {
    memcpy(ring[head].data, data, len);
    ring[head].data[len] = 0;
    ring[head].len = len;
    head = next;
  }
  portEXIT_CRITICAL(&mux);
}

bool parseMac(const char* t, uint8_t out[6]) {
  unsigned v[6];
  if (!t || sscanf(t, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

void sendLine(const char* s) {
  uart.print(s);
  uart.print('\n');
}

// ---------- scan mode ----------
void sleepNow(bool withTimer, uint32_t seconds) {
  WiFi.mode(WIFI_OFF);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BRIDGE_AWAKE_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
  if (withTimer) esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

void wakePi() {
  // Like pressing the Witty Pi's button: pull the switch line low briefly.
  pinMode(BRIDGE_WAKE_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(BRIDGE_WAKE_PIN, LOW);
  delay(500);
  digitalWrite(BRIDGE_WAKE_PIN, HIGH);
  pinMode(BRIDGE_WAKE_PIN, INPUT);
}

void scanMode() {
  if (!needSync) {
    Serial.println("scan: nothing waiting to sync; sleeping until the Pi turns on");
    sleepNow(false, 0);
  }
  if (wakesTried >= BRIDGE_MAX_WAKES) {
    // It woke the Pi this many times and no sync came back: look hourly.
    Serial.println("scan: many wakes without a sync; next look in an hour");
    wakesTried = BRIDGE_MAX_WAKES - 1;
    sleepNow(true, 3600);
  }
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  static const char* const homes[] = BRIDGE_HOME_SSIDS;
  bool found = false;
  for (int i = 0; i < n && !found; i++)
    for (const char* h : homes)
      if (*h && WiFi.SSID(i) == h) found = true;
  WiFi.scanDelete();
  if (found) {
    Serial.println("scan: home in range; waking the Pi");
    wakesTried++;
    wakePi();
    // The Pi raises the awake pin when it's up, which starts bridge mode.
    // If it doesn't, try again on the next look.
  } else {
    Serial.println("scan: no home Wi-Fi yet");
  }
  sleepNow(true, BRIDGE_SCAN_EVERY_S);
}

// ---------- bridge mode ----------
void handleOwn(JsonDocument& doc) {
  const char* t = doc["t"] | "";
  if (!strcmp(t, "ping")) {
    char out[96];
    snprintf(out, sizeof out, "{\"m\":\"bridge\",\"t\":\"pong\",\"mac\":\"%s\",\"dash\":%s}", WiFi.macAddress().c_str(), haveDash ? "true" : "false");
    sendLine(out);
  } else if (!strcmp(t, "need")) {
    needSync = (doc["sync"] | 1) != 0;
    if (!needSync) wakesTried = 0;
  }
}

void fromPi(char* line, size_t len) {
  if (len == 0 || line[0] != '{') return;
  JsonDocument doc;
  if (deserializeJson(doc, line, len)) return;
  const char* m = doc["m"] | "";
  if (!strcmp(m, "bridge")) {
    handleOwn(doc);
    return;
  }
  // Remember sync state for scan mode.
  if (!strcmp(doc["t"] | "", "sync")) {
    needSync = strcmp(doc["st"] | "", "synced") != 0;
    if (!needSync) wakesTried = 0;
  }
  if (len > kFrameMax) return;  // too big for ESP-NOW; truckd keeps frames under 240
  esp_now_send(haveDash ? dashMac : kBroadcast, (const uint8_t*)line, len);
}

void bridgeMode() {
  uart.begin(115200, SERIAL_8N1, BRIDGE_UART_RX, BRIDGE_UART_TX);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(BRIDGE_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("bridge: ESP-NOW didn't start; restarting");
    delay(1000);
    ESP.restart();
  }
  esp_now_register_recv_cb(onRecv);
  // A key longer than 16 characters fails to build here, on purpose.
  static const char key[17] = BRIDGE_ESPNOW_KEY;
  bool encrypt = strlen(key) == 16;
  if (encrypt) esp_now_set_pmk((const uint8_t*)key);
  esp_now_peer_info_t p = {};
  p.channel = BRIDGE_CHANNEL;
  p.ifidx = WIFI_IF_STA;
  haveDash = parseMac(BRIDGE_DASH_MAC, dashMac);
  if (haveDash) {
    memcpy(p.peer_addr, dashMac, 6);
    p.encrypt = encrypt;
    if (encrypt) memcpy(p.lmk, key, 16);
  } else {
    memcpy(p.peer_addr, kBroadcast, 6);  // broadcast can't be encrypted
  }
  esp_now_add_peer(&p);
  Serial.printf("bridge: MAC %s, channel %d, DASH %s\n", WiFi.macAddress().c_str(), BRIDGE_CHANNEL, haveDash ? BRIDGE_DASH_MAC : "any (broadcast)");
  char hi[96];
  snprintf(hi, sizeof hi, "{\"m\":\"bridge\",\"t\":\"hi\",\"mac\":\"%s\"}", WiFi.macAddress().c_str());
  sendLine(hi);

  char line[512];
  size_t n = 0;
  uint32_t lowSince = 0;
  for (;;) {
    while (uart.available()) {
      char c = uart.read();
      if (c == '\r') continue;
      if (c == '\n') {
        line[n] = 0;
        fromPi(line, n);
        n = 0;
      } else if (n + 1 < sizeof line) {
        line[n++] = c;
      }
    }
    for (;;) {
      Slot s;
      bool got = false;
      portENTER_CRITICAL(&mux);
      if (tail != head) {
        s = ring[tail];
        tail = (tail + 1) % 8;
        got = true;
      }
      portEXIT_CRITICAL(&mux);
      if (!got) break;
      sendLine(s.data);
    }
    // The Pi turned off: switch to scan mode.
    if (digitalRead(BRIDGE_AWAKE_PIN) == LOW) {
      if (!lowSince) lowSince = millis() | 1;
      if (millis() - lowSince > kAsleepAfterMs) {
        esp_now_deinit();
        Serial.println("bridge: the Pi is off; scan mode");
        scanMode();
      }
    } else {
      lowSince = 0;
    }
    delay(2);
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(BRIDGE_AWAKE_PIN, INPUT_PULLDOWN);
  pinMode(BRIDGE_WAKE_PIN, INPUT);  // released until a wake is needed
  delay(20);
  if (digitalRead(BRIDGE_AWAKE_PIN) == HIGH) bridgeMode();
  else scanMode();
}

void loop() {}
