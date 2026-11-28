// The Atlas mod: DASH and the truck homelab (Atlas) talking to each other.
// Atlas's brain (truckd on a Pi) sends dash-mod frames over MQTT on the
// truck Wi-Fi, and over ESP-NOW through its ESP32-C3 bridge (or any ESP
// peer). Shows OBD2 gauges, check-engine codes in plain words, sync,
// weather, the brain's battery and GPS. The Fob page (remote start, lock,
// unlock, skip and pause) goes over truckd's HTTP API with an owner token.
// Protocol: docs/mods/atlas-protocol.md. Setup: docs/mods/atlas.md.
#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "atlas.h"
#include "backoff.h"
#include "fob.h"
#include "mod.h"
#include "view.h"

class AtlasMod : public dash::Mod, public dash::ModView {
 public:
  AtlasMod();
  const char* id() const override { return "atlas"; }
  const char* name() const override { return "Atlas"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override;
  const char* detail() const override { return detail_; }
  bool problem() const override { return configError_; }

  bool hasScreen() const override { return true; }
  dash::ModView* view() override { return this; }
  int settingCount() const override { return 2; }
  dash::ModSetting* setting(int i) override { return i == 0 ? &units_ : i == 1 ? &link_ : nullptr; }
  void settingChanged(int i) override;
  bool homeCard(dash::HomeCard& c, uint32_t nowMs) const override;
  void onFrame(const char* json, size_t len, dash::Via via, uint32_t nowMs) override;

  // ModView
  void opened() override;
  bool dirty(uint32_t nowMs) override;
  void draw(uint32_t nowMs, bool full) override;
  void tap(int x, int y, uint32_t nowMs) override;
  void closed() override { open_ = false; }

  const dash::AtlasState& data() const { return st_; }

 private:
  enum Link : uint8_t { Auto, EspNowOnly, WifiOnly };
  bool useEspNow() const { return link_.value != WifiOnly; }
  bool useMqtt() const { return link_.value != EspNowOnly; }
  dash::Units units() const { return units_.value == 1 ? dash::Units::Metric : dash::Units::US; }
  void mqttLoop(uint32_t now, bool netUp);
  void request(uint32_t now);
  void updateDetail(uint32_t now);
  uint32_t unixNow(uint32_t now) const;
  void drawHeader(uint32_t now);
  void drawGauges(uint32_t now, bool full);
  void drawCodes(uint32_t now);
  void drawTrip(uint32_t now);
  // Fob page: truckd's HTTP API with the owner token (never ESP-NOW frames,
  // so a spoofed frame can't start the truck).
  bool fobReachable() const;
  int fobHttp(const char* method, const char* path, const String& body, String* reply);
  void fobFetch(uint32_t now);
  void fobCommand(const char* c, uint32_t now);
  void fobPause(const char* body, uint32_t now);
  void drawFob(uint32_t now);

  static void onMqtt(char* topic, uint8_t* payload, unsigned int len);
  static AtlasMod* self_;

  dash::AtlasState st_;
  dash::ModSetting units_, link_;
  dash::Via lastVia_ = dash::Via::Other;
  WiFiClient tcp_;
  PubSubClient mqtt_;
  dash::Backoff retry_{2000, 30000};
  uint32_t nextMqtt_ = 0, lastReq_ = 0, seq_ = 0;
  bool mqttUp_ = false, espnowOk_ = false, configError_ = false, peerSet_ = false;
  uint8_t peer_[6] = {};
  char detail_[56] = "";
  // Screen
  uint8_t page_ = 0, codePage_ = 0;
  uint32_t drawnRev_ = UINT32_MAX, drawnTick_ = 0;
  bool drawnLive_ = false;
  bool open_ = false;
  dash::FobStatus fob_;
  bool fobHave_ = false;
  uint32_t fobNext_ = 0, armUntil_ = 0, fobRev_ = 0, drawnFobRev_ = UINT32_MAX;
  char fobMsg_[64] = "";
};
