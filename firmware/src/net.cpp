#include "net.h"

#include <WiFi.h>
#include <time.h>

#include "backoff.h"
#include "config.h"

namespace net {

namespace {
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
  WiFi.begin(DASH_WIFI_SSID, DASH_WIFI_PASS);
  status = "joining Wi-Fi";
  nextTry = millis() + 15000;
}

void loop(uint32_t now) {
  bool isUp = WiFi.status() == WL_CONNECTED;
  if (isUp && !wasUp) {
    retry.reset();
    snprintf(ipText, sizeof ipText, "%s", WiFi.localIP().toString().c_str());
    configTzTime(DASH_TZ, DASH_NTP_SERVER);
    status = "Wi-Fi on";
  }
  if (!isUp && wasUp) {
    ipText[0] = 0;
    status = "Wi-Fi lost, retrying";
    nextTry = now + retry.fail(esp_random() % 1000);
  }
  wasUp = isUp;
  if (!isUp && (int32_t)(now - nextTry) >= 0) {
    WiFi.disconnect();
    WiFi.begin(DASH_WIFI_SSID, DASH_WIFI_PASS);
    // Allow 15 s to join, then wait out the backoff before the next attempt.
    nextTry = now + 15000 + retry.fail(esp_random() % 1000);
    status = retry.failures() > 3 ? "can't join Wi-Fi: check secrets.h" : "joining Wi-Fi";
  }
}

bool up() { return wasUp; }
int rssi() { return wasUp ? WiFi.RSSI() : 0; }
const char* ip() { return ipText; }
const char* statusText() { return status; }

bool timeValid() {
  time_t t = time(nullptr);
  return t > 1700000000;  // after Nov 2023, so NTP has answered
}

}  // namespace net
