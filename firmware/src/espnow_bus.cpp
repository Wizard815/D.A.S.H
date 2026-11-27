#include "espnow_bus.h"

#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

#include "mod.h"

namespace espnow {

namespace {
struct Slot {
  uint8_t from[6];
  uint8_t len;
  char data[dash::kFrameMax + 1];
};
constexpr int kSlots = 8;
Slot ring[kSlots];
volatile int head = 0, tail = 0;  // written by the Wi-Fi task (head) and the loop (tail)
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool started = false, encrypted = false;
uint8_t lmk[16];
volatile uint32_t nRx = 0, nDrop = 0;
volatile int peers = 0;

// Runs in the Wi-Fi task: copy and return.
void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len <= 0 || len > (int)dash::kFrameMax) {
    nDrop++;
    return;
  }
  // Once peers are set, only they are heard.
  if (peers > 0 && !esp_now_is_peer_exist(mac)) {
    nDrop++;
    return;
  }
  portENTER_CRITICAL(&mux);
  int nextHead = (head + 1) % kSlots;
  if (nextHead == tail) {
    nDrop++;
  } else {
    Slot& s = ring[head];
    memcpy(s.from, mac, 6);
    memcpy(s.data, data, len);
    s.data[len] = 0;
    s.len = (uint8_t)len;
    head = nextHead;
    nRx++;
  }
  portEXIT_CRITICAL(&mux);
}
}  // namespace

bool parseMac(const char* t, uint8_t out[6]) {
  if (!t) return false;
  unsigned v[6];
  if (sscanf(t, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] > 255) return false;
    out[i] = (uint8_t)v[i];
  }
  return true;
}

bool begin(const char* key) {
  if (started) return true;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  encrypted = key && strlen(key) == 16;
  if (encrypted) {
    memcpy(lmk, key, 16);
    esp_now_set_pmk((const uint8_t*)key);
  }
  started = true;
  return true;
}

bool up() { return started; }

bool addPeer(const char* mac) {
  if (!started) return false;
  esp_now_peer_info_t p = {};
  if (!parseMac(mac, p.peer_addr)) return false;
  if (esp_now_is_peer_exist(p.peer_addr)) return true;
  p.channel = 0;  // whatever channel the Wi-Fi is on
  p.ifidx = WIFI_IF_STA;
  p.encrypt = encrypted;
  if (encrypted) memcpy(p.lmk, lmk, 16);
  if (esp_now_add_peer(&p) != ESP_OK) return false;
  peers++;
  return true;
}

bool next(char* buf, size_t n, size_t& len, uint8_t from[6]) {
  bool got = false;
  portENTER_CRITICAL(&mux);
  if (tail != head) {
    Slot& s = ring[tail];
    len = s.len < n ? s.len : n - 1;
    memcpy(buf, s.data, len);
    buf[len] = 0;
    memcpy(from, s.from, 6);
    tail = (tail + 1) % kSlots;
    got = true;
  }
  portEXIT_CRITICAL(&mux);
  return got;
}

bool send(const uint8_t* mac, const char* json, size_t len) {
  if (!started || len > dash::kFrameMax) return false;
  return esp_now_send(mac, (const uint8_t*)json, len) == ESP_OK;
}

uint32_t received() { return nRx; }
uint32_t dropped() { return nDrop; }

}  // namespace espnow
