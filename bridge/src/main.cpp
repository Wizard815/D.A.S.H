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
// Remote start over Bluetooth, also in scan mode: every few seconds it wakes,
// listens on BLE, and sleeps again. A phone writes a signed start; the bridge
// checks the signature, wakes the Pi, and hands the request to truckd on the
// UART once the Pi is up. truckd checks it again and does the fob press and
// the safety checks. See docs/mods/atlas.md, "Remote start over Bluetooth".
//
// Frames for the bridge itself use "m":"bridge": ping -> pong, and
// {"m":"bridge","t":"need","sync":1} to say there is (or isn't) data waiting.
// truckd's "hi" frames carry "ts", which sets the bridge's clock.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <mbedtls/md.h>
#include <Preferences.h>
#include <sys/time.h>

#include <NimBLEDevice.h>

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
RTC_DATA_ATTR int64_t nextScanAt = 0;  // seconds on the RTC clock; 0: look now
RTC_DATA_ATTR bool clockSet = false;   // set from truckd's "ts", kept across sleep
RTC_DATA_ATTR char pending[232] = "";  // a checked start waiting for the Pi
RTC_DATA_ATTR uint32_t usedNonces[8];  // fingerprints of nonces already taken
RTC_DATA_ATTR uint8_t usedAt = 0;
RTC_DATA_ATTR int64_t lastBleWake = -1000;

int64_t nowS() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

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

// ---------- remote start over BLE ----------
// The phone writes {"c":"remote_start","nonce":"<16-40 chars>","exp":<unix s>,
// "sig":"<hex HMAC-SHA256 of remote_start|nonce|exp with the start key>"}.
// The bridge can't trust the phone, so it checks the signature itself: a
// stranger's writes can't wake the Pi and run the battery down.
// The start key: one truckd sent (it rotates it), kept in flash; else the
// one in bridge_config.h.
char startKeyHex[65] = "";

bool isHexKey(const char* k) {
  if (!k || strlen(k) != 64) return false;
  for (const char* p = k; *p; p++)
    if (!isxdigit((unsigned char)*p)) return false;
  return true;
}

const char* startKey() {
  if (!*startKeyHex) {
    Preferences p;
    p.begin("bridge", true);
    String k = p.getString("startkey", "");
    p.end();
    snprintf(startKeyHex, sizeof startKeyHex, "%s", isHexKey(k.c_str()) ? k.c_str() : BRIDGE_START_KEY);
  }
  return startKeyHex;
}

bool setStartKey(const char* k) {
  if (!isHexKey(k)) return false;
  Preferences p;
  p.begin("bridge", false);
  bool ok = p.putString("startkey", k) == 64;
  p.end();
  if (ok) snprintf(startKeyHex, sizeof startKeyHex, "%s", k);
  return ok;
}
constexpr int kExpAheadS = 120;  // the Pi takes ~30-60 s to boot
const char* kSvcUuid = "6a7c0001-4a1e-4d4b-9a54-41544c415331";
const char* kCmdUuid = "6a7c0002-4a1e-4d4b-9a54-41544c415331";
const char* kStatusUuid = "6a7c0003-4a1e-4d4b-9a54-41544c415331";

bool bleOn() { return isHexKey(startKey()); }

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h ? h : 1;
}

bool hexKey(uint8_t out[32]) {
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (sscanf(startKey() + 2 * i, "%2x", &v) != 1) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

bool sigOk(const char* nonce, long long exp, const char* sigHex) {
  uint8_t key[32], mac[32];
  if (!hexKey(key) || strlen(sigHex) != 64) return false;
  char msg[96];
  int n = snprintf(msg, sizeof msg, "remote_start|%s|%lld", nonce, exp);
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (mbedtls_md_hmac(md, key, sizeof key, (const uint8_t*)msg, n, mac)) return false;
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (sscanf(sigHex + 2 * i, "%2x", &v) != 1) return false;
    diff |= mac[i] ^ (uint8_t)v;
  }
  return diff == 0;
}

// Checks a start request. On success it's stored for truckd; the answer is
// what the phone sees.
const char* takeStart(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return "refused: not JSON";
  if (strcmp(doc["c"] | "", "remote_start")) return "refused: only remote_start";
  const char* nonce = doc["nonce"] | "";
  const char* sig = doc["sig"] | "";
  long long exp = doc["exp"] | 0LL;
  size_t nl = strlen(nonce);
  if (nl < 16 || nl > 40) return "refused: nonce must be 16-40 characters";
  for (const char* p = nonce; *p; p++)
    if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return "refused: nonce has odd characters";
  if (!sigOk(nonce, exp, sig)) return "refused: bad signature";
  if (clockSet) {
    int64_t now = nowS();
    if (exp < now) return "refused: expired; check the phone's clock";
    if (exp > now + kExpAheadS) return "refused: exp too far ahead";
  }
  uint32_t f = fnv(nonce);
  for (uint32_t u : usedNonces)
    if (u == f) return "refused: already used";
  if (*pending) return "busy: a start is already waiting for the Pi";
  if (nowS() - lastBleWake < 60) return "busy: wait a minute between starts";
  usedNonces[usedAt++ % 8] = f;
  snprintf(pending, sizeof pending, "{\"m\":\"bridge\",\"t\":\"cmd\",\"c\":\"remote_start\",\"nonce\":\"%s\",\"exp\":%lld,\"sig\":\"%s\",\"via\":\"ble\"}",
           nonce, exp, sig);
  lastBleWake = nowS();
  return "ok: waking the Pi";
}

volatile bool bleClient = false, bleWantWake = false;
NimBLECharacteristic* statusChr = nullptr;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override { bleClient = true; }
  void onDisconnect(NimBLEServer*) override { bleClient = false; }
};

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    const char* answer = takeStart(v.data(), v.size());
    Serial.printf("ble: %s\n", answer);
    if (!strncmp(answer, "ok", 2)) bleWantWake = true;
    statusChr->setValue(answer);
    statusChr->notify();
  }
};

// One listening window: advertise, and if a phone connects, stay until it
// leaves (at most 30 s).
void bleWindow() {
  NimBLEDevice::init(BRIDGE_BLE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  NimBLEService* svc = srv->createService(kSvcUuid);
  NimBLECharacteristic* cmd = svc->createCharacteristic(kCmdUuid, NIMBLE_PROPERTY::WRITE);
  cmd->setCallbacks(new CmdCb());
  statusChr = svc->createCharacteristic(kStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusChr->setValue(*pending ? "busy: a start is waiting for the Pi" : "ready");
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kSvcUuid);
  adv->setMinInterval(160);  // 100 ms: found quickly in a short window
  adv->setMaxInterval(240);
  adv->start();
  uint32_t t0 = millis();
  while (millis() - t0 < BRIDGE_BLE_LISTEN_MS || (bleClient && millis() - t0 < 30000)) {
    if (bleWantWake) {
      bleWantWake = false;
      delay(300);  // let the notify go out first
      wakePi();
    }
    delay(20);
  }
  NimBLEDevice::deinit(true);
}

// ---------- scan mode ----------
void scanMode() {
  // A start the Pi never came up for is dropped after 5 minutes.
  if (*pending && nowS() - lastBleWake > 300) {
    Serial.println("scan: the Pi didn't come up for the start; dropped it");
    pending[0] = 0;
  }
  if (bleOn()) bleWindow();

  int64_t now = nowS();
  uint32_t nextWifi = 0;  // seconds until the next Wi-Fi look; 0: none planned
  if (needSync) {
    if (now >= nextScanAt) {
      if (wakesTried >= BRIDGE_MAX_WAKES) {
        // It woke the Pi this many times and no sync came back: look hourly.
        Serial.println("scan: many wakes without a sync; next look in an hour");
        wakesTried = BRIDGE_MAX_WAKES - 1;
        nextScanAt = now + 3600;
      } else {
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
        nextScanAt = now + BRIDGE_SCAN_EVERY_S;
      }
    }
    nextWifi = (uint32_t)(nextScanAt - now);
  }
  if (bleOn()) sleepNow(true, BRIDGE_BLE_EVERY_S);
  if (needSync) sleepNow(true, nextWifi ? nextWifi : 1);
  Serial.println("scan: nothing waiting to sync; sleeping until the Pi turns on");
  sleepNow(false, 0);
}

// ---------- bridge mode ----------
void handleOwn(JsonDocument& doc) {
  const char* t = doc["t"] | "";
  if (!strcmp(t, "ping")) {
    char out[96];
    snprintf(out, sizeof out, "{\"m\":\"bridge\",\"t\":\"pong\",\"mac\":\"%s\",\"dash\":%s}", WiFi.macAddress().c_str(), haveDash ? "true" : "false");
    sendLine(out);
  } else if (!strcmp(t, "key")) {
    // A new start key from truckd (POST /api/start-key/rotate). Only over the
    // UART, the wire from the Pi: never over ESP-NOW or Bluetooth.
    bool ok = setStartKey(doc["k"] | "");
    sendLine(ok ? "{\"m\":\"bridge\",\"t\":\"key\",\"ok\":1}" : "{\"m\":\"bridge\",\"t\":\"key\",\"ok\":0,\"err\":\"need 64 hex characters\"}");
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
  // truckd's clock: lets the bridge check a start's expiry while the Pi is off.
  long long ts = doc["ts"] | 0LL;
  if (ts > 1700000000LL) {
    struct timeval tv = {(time_t)ts, 0};
    int64_t was = nowS();
    settimeofday(&tv, nullptr);
    nextScanAt += nowS() - was;  // keep the next look the same distance away
    clockSet = true;
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
  // A start that came over Bluetooth while the Pi was off.
  if (*pending) {
    sendLine(pending);
    Serial.println("bridge: handed the waiting start to truckd");
    pending[0] = 0;
  }

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
