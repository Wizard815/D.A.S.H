// T.I.T.A.N: the homelab status board for friends and family. DASH reads
// its read-only summary, GET /api/glance with TITAN_GLANCE_TOKEN. The pure
// parts: parsing it and saying it in words.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

struct TitanDown {
  char name[32];
  char detail[56];
  char cause[32];  // down because this one is down; empty if not
};

struct TitanService {
  char name[24];
  char status;  // 'u' up, 'd' down, 'p' pending
};

struct TitanGlance {
  static constexpr int kDown = 6, kServices = 40;
  uint16_t up = 0, down = 0, pending = 0, notes = 0;
  uint32_t every = 0;  // T.I.T.A.N checks this often (seconds)
  TitanDown downs[kDown];
  uint8_t nDown = 0;
  TitanService services[kServices];
  uint8_t nServices = 0;
};

// False if it isn't a T.I.T.A.N glance; out is then left as it was.
bool parseTitan(const char* json, size_t len, TitanGlance& out);

// "All 12 up", "Plex is down", "2 down: Plex, Minecraft", "Checking 3...".
void titanSummary(const TitanGlance& g, char* buf, size_t n);

// One down service in words: "no answer" or "down because Outside access is down".
void titanDownWords(const TitanDown& d, char* buf, size_t n);

}  // namespace dash
