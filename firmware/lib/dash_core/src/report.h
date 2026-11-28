// Live reports to the phone over Bluetooth (docs/ble-reports.md): the pure
// parts. Each report is one dash-mod v1 frame of at most kFrameMax bytes.
// The firmware (src/ble_reports.cpp) does the radio.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

struct AtlasState;

// {"m":"<mod>","t":"sum","s":"<text>","ts":<unix>}. The text is JSON-escaped
// and cut (with "...") to keep the frame within kFrameMax bytes, never in the
// middle of an escape or a UTF-8 character. ts is left out when 0. Returns
// the length, 0 when it can't be built (no room, bad mod id).
size_t summaryFrame(const char* mod, const char* text, uint32_t ts, char* buf, size_t n);

// {"m":"dash","t":"bat","v":3.92,"pct":71,"ts":<unix>}: the cell's volts
// and percent. Returns the length, 0 if it doesn't fit.
size_t batteryFrame(uint32_t mv, uint8_t pct, uint32_t ts, char* buf, size_t n);

// An Atlas alert rebuilt from the state, for when the original frame was
// too long to pass through (MQTT can carry more than 240 bytes):
// {"m":"atlas","t":"alert","id":..,"k":..,"lvl":..,"msg":..,"snap":1,"ts":..}
// with msg cut to fit. Returns the length, 0 if there's no alert id.
size_t atlasAlertFrame(const AtlasState& s, uint32_t ts, char* buf, size_t n);

// The Bluetooth name: "DASH-" and the last 4 hex digits of the MAC, e.g. "DASH-A1B2".
void bleName(const uint8_t mac[6], char* buf, size_t n);

// Sends a mod's summary when it changed, at most once per kMinGapMs. A change
// inside the gap goes out (as the newest text) once the gap is over.
class SummaryGate {
 public:
  static constexpr uint32_t kMinGapMs = 10000;
  // The latest text. True: send it now (and it counts as sent).
  bool offer(const char* text, uint32_t nowMs);
  // A new phone: the next offer goes out at once.
  void reset() { sent_ = false; }

 private:
  uint32_t hash_ = 0, at_ = 0;
  bool sent_ = false;
};

}  // namespace dash
