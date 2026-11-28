#include "ble_reports.h"

#if DASH_BLE_REPORTS
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <string.h>
#include <time.h>

#include "battery.h"
#include "hw.h"
#include "mod.h"
#include "net.h"
#include "report.h"

namespace ble_reports {

namespace {
// The contract with the phone app (docs/ble-reports.md). Keep these fixed.
constexpr char kSvcUuid[] = "6a7c0101-4a1e-4d4b-9a54-44415348aa01";
constexpr char kChrUuid[] = "6a7c0102-4a1e-4d4b-9a54-44415348aa01";
constexpr uint32_t kPairWithinMs = 30000;  // connected but not paired by then: dropped
constexpr int kSlots = 4;                  // mods with a summary

struct Slot {
  char mod[8] = "";
  char text[96] = "";
  dash::SummaryGate gate;
};
Slot slots[kSlots];
dash::SummaryGate batGate;
char devName[12] = "";
char statusBuf[64] = "";
char lastAlert[dash::kFrameMax + 1] = "";  // the newest open alert, for a phone that subscribes later
size_t lastAlertLen = 0;

NimBLEServer* srv = nullptr;
NimBLECharacteristic* chr = nullptr;
bool started = false;

// Written by the NimBLE host task, read by the loop.
volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
volatile uint32_t connAt = 0;
volatile bool secure = false;      // encrypted with a MITM-protected (passkey) key
volatile bool subscribed = false;  // notifications on
volatile bool fresh = false;       // a phone just paired or subscribed: send everything
volatile bool kicked = false;      // a disconnect is on its way
volatile uint32_t pk = 0;

uint32_t unixNow() { return net::timeValid() ? (uint32_t)time(nullptr) : 0; }

void send(const char* f, size_t n) {
  chr->setValue((const uint8_t*)f, n);  // a read returns the newest frame
  chr->notify();
}

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, ble_gap_conn_desc* d) override {
    conn = d->conn_handle;
    connAt = millis();
    secure = d->sec_state.encrypted && d->sec_state.authenticated;
    subscribed = false;
    kicked = false;
    // Ask to pair (or to restore the bond) at once, before any read.
    if (!secure) NimBLEDevice::startSecurity(d->conn_handle);
  }
  void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
    conn = BLE_HS_CONN_HANDLE_NONE;
    secure = subscribed = kicked = false;
    pk = 0;
  }
  // Display-only: DASH makes up the code and shows it; the phone types it.
  uint32_t onPassKeyRequest() override {
    pk = 100000 + esp_random() % 900000;
    return pk;
  }
  void onAuthenticationComplete(ble_gap_conn_desc* d) override {
    pk = 0;
    // Only a passkey (MITM) pairing counts: "just works" from a phone
    // without a keyboard gets encrypted but not authenticated.
    if (d->sec_state.encrypted && d->sec_state.authenticated) {
      secure = true;
      fresh = true;
    } else {
      kicked = true;
      NimBLEDevice::getServer()->disconnect(d->conn_handle);
    }
  }
};

class ChrCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*, uint16_t v) override {
    subscribed = v != 0;
    if (subscribed) fresh = true;
  }
};

ServerCb serverCb;
ChrCb chrCb;

void updateStatus() {
  if (!started) snprintf(statusBuf, sizeof statusBuf, "Bluetooth didn't start");
  else if (pk) snprintf(statusBuf, sizeof statusBuf, "%s, pairing: code %06u", devName, (unsigned)pk);
  else if (conn == BLE_HS_CONN_HANDLE_NONE) snprintf(statusBuf, sizeof statusBuf, "%s, waiting for a phone", devName);
  else if (!secure) snprintf(statusBuf, sizeof statusBuf, "%s, phone not paired yet", devName);
  else if (!subscribed) snprintf(statusBuf, sizeof statusBuf, "%s, phone paired", devName);
  else snprintf(statusBuf, sizeof statusBuf, "%s, sending to phone", devName);
}
}  // namespace

void begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  dash::bleName(mac, devName, sizeof devName);
  NimBLEDevice::init(devName);
  // Bonding, MITM (passkey), LE Secure Connections; DASH shows the code.
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setMTU(247);  // room for a 240-byte frame in one notify

  srv = NimBLEDevice::createServer();
  srv->setCallbacks(&serverCb, false);
  NimBLEService* svc = srv->createService(kSvcUuid);
  // Reads need an authenticated (passkey) link; notify() itself skips
  // unencrypted peers, and loop() only notifies a passkey-paired one.
  chr = svc->createCharacteristic(kChrUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN |
                                                NIMBLE_PROPERTY::NOTIFY);
  chr->setCallbacks(&chrCb);
  char f[dash::kFrameMax + 1];
  uint32_t mv = hw::batteryMv();
  size_t n = dash::batteryFrame(mv, dash::batteryPercent(mv), 0, f, sizeof f);
  chr->setValue((const uint8_t*)f, n);
  svc->start();

  // The 128-bit service UUID fills the advertisement; the name goes in the
  // scan response.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setCompleteServices(NimBLEUUID(kSvcUuid));
  sr.setName(devName);
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  started = adv->start();
  Serial.printf("ble: %s %s, %d bonded phone(s)\n", devName, started ? "advertising" : "didn't start", NimBLEDevice::getNumBonds());
  updateStatus();
}

void loop(uint32_t now) {
  if (!started) return;
  // A stranger can't sit on the one connection: pair within 30 s or go.
  if (conn != BLE_HS_CONN_HANDLE_NONE && !secure && !kicked && now - connAt > kPairWithinMs) {
    kicked = true;
    srv->disconnect(conn);
  }
  updateStatus();
  if (!listening()) return;
  if (fresh) {
    fresh = false;
    for (Slot& s : slots) s.gate.reset();
    batGate.reset();
    if (lastAlertLen) send(lastAlert, lastAlertLen);
  }
  uint32_t ts = unixNow();
  char f[dash::kFrameMax + 1];
  int sent = 0;
  for (Slot& s : slots) {
    if (!*s.mod || sent >= 2) continue;  // two a pass at most; the rest next pass
    if (!s.gate.offer(s.text, now)) continue;
    size_t n = dash::summaryFrame(s.mod, s.text, ts, f, sizeof f);
    if (n) send(f, n), sent++;
  }
  uint32_t mv = hw::batteryMv();
  uint8_t pct = dash::batteryPercent(mv);
  char p[8];
  snprintf(p, sizeof p, "%u", pct);
  if (sent < 2 && batGate.offer(p, now)) {
    size_t n = dash::batteryFrame(mv, pct, ts, f, sizeof f);
    if (n) send(f, n);
  }
}

void summary(const char* mod, const char* text) {
  if (!mod || !*mod) return;
  Slot* spare = nullptr;
  for (Slot& s : slots) {
    if (!strcmp(s.mod, mod)) {
      snprintf(s.text, sizeof s.text, "%s", text ? text : "");
      return;
    }
    if (!*s.mod && !spare) spare = &s;
  }
  if (!spare || strlen(mod) >= sizeof spare->mod) return;
  snprintf(spare->mod, sizeof spare->mod, "%s", mod);
  snprintf(spare->text, sizeof spare->text, "%s", text ? text : "");
}

void frame(const char* json, size_t len) {
  if (!json || !len || len > dash::kFrameMax || !listening()) return;
  send(json, len);
}

void atlasAlert(const char* json, size_t len, const dash::AtlasState& s) {
  char f[dash::kFrameMax + 1];
  size_t n = 0;
  if (json && len && len <= dash::kFrameMax) {
    memcpy(f, json, len);
    n = len;
  } else {
    n = dash::atlasAlertFrame(s, unixNow(), f, sizeof f);
  }
  if (!n) return;
  f[n] = 0;
  // Keep the newest open alert for a phone that subscribes later.
  if (s.alertLvl) {
    memcpy(lastAlert, f, n + 1);
    lastAlertLen = n;
  } else {
    lastAlertLen = 0;
  }
  frame(f, n);
}

bool listening() { return started && conn != BLE_HS_CONN_HANDLE_NONE && secure && subscribed; }

uint32_t passkey() { return pk; }

void rejectPairing() {
  pk = 0;
  if (started && conn != BLE_HS_CONN_HANDLE_NONE && !secure) {
    kicked = true;
    srv->disconnect(conn);
  }
}

void forgetPhones() {
  if (!started) return;
  if (conn != BLE_HS_CONN_HANDLE_NONE) {
    kicked = true;
    srv->disconnect(conn);
  }
  NimBLEDevice::deleteAllBonds();
}

const char* status() { return statusBuf; }

}  // namespace ble_reports

#endif  // DASH_BLE_REPORTS
