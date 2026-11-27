#include "net.h"

#include <WiFi.h>
#include <time.h>

#include "backoff.h"
#include "config.h"

namespace net {

namespace {
// Home first, then Atlas's truck network if one is set. Each failed try
// moves to the other, so in the truck DASH joins Atlas within about 30 s.
struct Network {
  const char* ssid;
  const char* pass;
  const char* label;  // for the status line
};
const Network kNets[] = {{DASH_WIFI_SSID, DASH_WIFI_PASS, "Wi-Fi"}, {DASH_ATLAS_WIFI_SSID, DASH_ATLAS_WIFI_PASS, "truck Wi-Fi"}};
int nNets() { return *DASH_ATLAS_WIFI_SSID ? 2 : 1; }
int cur = 0;
char statusBuf[40] = "";

dash::Backoff retry(2000, 60000);
uint32_t nextTry = 0;
bool wasUp = false;
char ipText[16] = "";
const char* status = "starting";
}  // namespace

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // we retry with backoff ourselves
  WiFi.setHostname("dash");
  WiFi.begin(kNets[0].ssid, kNets[0].pass);
  status = "joining Wi-Fi";
  nextTry = millis() + 15000;
}

void loop(uint32_t now) {
  bool isUp = WiFi.status() == WL_CONNECTED;
  if (isUp && !wasUp) {
    retry.reset();
    snprintf(ipText, sizeof ipText, "%s", WiFi.localIP().toString().c_str());
    configTzTime(DASH_TZ, DASH_NTP_SERVER);
    snprintf(statusBuf, sizeof statusBuf, "on %s (%s)", kNets[cur].label, kNets[cur].ssid);
    status = statusBuf;
  }
  if (!isUp && wasUp) {
    ipText[0] = 0;
    status = "Wi-Fi lost, retrying";
    nextTry = now + retry.fail(esp_random() % 1000);
  }
  wasUp = isUp;
  if (!isUp && (int32_t)(now - nextTry) >= 0) {
    WiFi.disconnect();
    cur = (cur + 1) % nNets();
    WiFi.begin(kNets[cur].ssid, kNets[cur].pass);
    // Allow 15 s to join, then wait out the backoff before the next attempt.
    // With two networks, the backoff only grows after both were tried.
    nextTry = now + 15000 + (cur == 0 ? retry.fail(esp_random() % 1000) : 0);
    if (retry.failures() > 3) status = "can't join Wi-Fi: check secrets.h";
    else status = cur == 0 ? "joining Wi-Fi" : "joining truck Wi-Fi";
  }
}

bool up() { return wasUp; }
bool onAtlasWifi() { return wasUp && cur == 1; }
uint32_t gateway() { return wasUp ? (uint32_t)WiFi.gatewayIP() : 0; }
int rssi() { return wasUp ? WiFi.RSSI() : 0; }
const char* ip() { return ipText; }
const char* statusText() { return status; }

bool timeValid() {
  time_t t = time(nullptr);
  return t > 1700000000;  // after Nov 2023, so NTP has answered
}

}  // namespace net
