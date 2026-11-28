// Atlas's fob module (remote start, lock, unlock and their schedule), as
// truckd reports it on GET /api/fob. The pure parts: parsing and words.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

struct FobEntry {
  char days[16];  // "mon-fri", "sat,sun", "daily"
  char at[6];     // "07:40"
  char c[16];     // start, lock, unlock_driver, unlock_all
};

struct FobStatus {
  static constexpr int kEntries = 8, kSkips = 4;
  int battery = -1;  // %; -1 not known
  char lastC[16] = "";
  uint32_t lastAt = 0;
  bool lastOk = false;
  char lastErr[48] = "";
  FobEntry entries[kEntries];
  uint8_t n = 0;
  bool skipNext = false;
  char until[11] = "";  // paused until this date, "" if not
  char skips[kSkips][11];
  uint8_t nSkips = 0;
  int32_t clockAgeS = -1;
};

// False if it isn't a fob status; out is then left as it was.
bool parseFob(const char* json, size_t len, FobStatus& out);

// "Start", "Lock", "Unlock driver", "Unlock all"; the raw value otherwise.
const char* fobActionWords(const char* c);

// "Paused until 2026-12-28", "Skips the next one", "Skips 2026-12-04", or ""
// when nothing is paused.
void fobPauseWords(const FobStatus& f, char* buf, size_t n);

// "Mon-Fri 07:40 Start"
void fobEntryWords(const FobEntry& e, char* buf, size_t n);

}  // namespace dash
