#include "report.h"

#include <stdio.h>
#include <string.h>

#include "atlas.h"
#include "mod.h"

namespace dash {

namespace {
// Appends s as JSON string content (no quotes) to out[o..], using at most
// room bytes. Cuts with "..." when it doesn't all fit, never splitting an
// escape or a UTF-8 character. Control characters become spaces. Returns
// the new end.
size_t putEscaped(char* out, size_t o, size_t room, const char* s) {
  if (!s) return o;
  // First: does all of it fit?
  size_t need = 0;
  for (const char* p = s; *p; p++) need += (*p == '"' || *p == '\\') ? 2 : 1;
  bool cut = need > room;
  size_t limit = cut ? (room >= 3 ? room - 3 : 0) : room;
  size_t used = 0;
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    unsigned char c = *p;
    size_t clen = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    size_t w = (c == '"' || c == '\\') ? 2 : clen;
    if (used + w > limit) break;
    bool whole = true;
    for (size_t k = 1; k < clen && whole; k++)
      if ((p[k] & 0xC0) != 0x80) whole = false;  // stops at the NUL too
    if (!whole) {  // a broken UTF-8 sequence: one '?' instead
      clen = 1;
      w = 1;
      out[o++] = '?';
    } else if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c < 0x20) {
      out[o++] = ' ';
    } else {
      memcpy(out + o, p, clen);
      o += clen;
    }
    used += w;
    p += clen;
  }
  if (cut && room >= 3) {
    memcpy(out + o, "...", 3);
    o += 3;
  }
  return o;
}

bool plainId(const char* s, size_t max) {
  if (!s || !*s || strlen(s) > max) return false;
  for (; *s; s++)
    if (*s == '"' || *s == '\\' || (unsigned char)*s < 0x20) return false;
  return true;
}

size_t cap(size_t n) { return n - 1 < kFrameMax ? n - 1 : kFrameMax; }
}  // namespace

size_t summaryFrame(const char* mod, const char* text, uint32_t ts, char* buf, size_t n) {
  if (!buf || n < 2) return 0;
  buf[0] = 0;
  if (!plainId(mod, 15)) return 0;
  char tail[24];
  int tl = ts ? snprintf(tail, sizeof tail, "\",\"ts\":%lu}", (unsigned long)ts) : snprintf(tail, sizeof tail, "\"}");
  char head[48];
  int hl = snprintf(head, sizeof head, "{\"m\":\"%s\",\"t\":\"sum\",\"s\":\"", mod);
  size_t max = cap(n);
  if ((size_t)(hl + tl) > max) return 0;
  memcpy(buf, head, hl);
  size_t o = putEscaped(buf, hl, max - hl - tl, text);
  memcpy(buf + o, tail, tl);
  o += tl;
  buf[o] = 0;
  return o;
}

size_t batteryFrame(uint32_t mv, uint8_t pct, uint32_t ts, char* buf, size_t n) {
  if (!buf || !n) return 0;
  char t[24] = "";
  if (ts) snprintf(t, sizeof t, ",\"ts\":%lu", (unsigned long)ts);
  uint32_t cv = (mv + 5) / 10;  // centivolts, rounded
  int w = snprintf(buf, n, "{\"m\":\"dash\",\"t\":\"bat\",\"v\":%u.%02u,\"pct\":%u%s}", (unsigned)(cv / 100), (unsigned)(cv % 100),
                   (unsigned)pct, t);
  if (w < 0 || (size_t)w >= n || (size_t)w > kFrameMax) {
    buf[0] = 0;
    return 0;
  }
  return (size_t)w;
}

size_t atlasAlertFrame(const AtlasState& s, uint32_t ts, char* buf, size_t n) {
  if (!buf || n < 2) return 0;
  buf[0] = 0;
  if (!*s.alertId) return 0;
  char head[96], tail[40];
  char id[sizeof s.alertId * 2], k[sizeof s.alertK * 2];
  size_t il = putEscaped(id, 0, sizeof id - 1, s.alertId);
  id[il] = 0;
  size_t kl = putEscaped(k, 0, sizeof k - 1, s.alertK);
  k[kl] = 0;
  int hl = snprintf(head, sizeof head, "{\"m\":\"atlas\",\"t\":\"alert\",\"id\":\"%s\",\"k\":\"%s\",\"lvl\":%u,\"msg\":\"", id, k,
                    (unsigned)s.alertLvl);
  int tl = snprintf(tail, sizeof tail, "\"%s", s.alertSnap ? ",\"snap\":1" : "");
  if (ts) tl += snprintf(tail + tl, sizeof tail - tl, ",\"ts\":%lu", (unsigned long)ts);
  tl += snprintf(tail + tl, sizeof tail - tl, "}");
  size_t max = cap(n);
  if (hl < 0 || hl >= (int)sizeof head || (size_t)(hl + tl) > max) return 0;
  memcpy(buf, head, hl);
  size_t o = putEscaped(buf, hl, max - hl - tl, s.alertMsg);
  memcpy(buf + o, tail, tl);
  o += tl;
  buf[o] = 0;
  return o;
}

void bleName(const uint8_t mac[6], char* buf, size_t n) { snprintf(buf, n, "DASH-%02X%02X", mac[4], mac[5]); }

bool SummaryGate::offer(const char* text, uint32_t now) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const char* p = text ? text : ""; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  if (sent_ && h == hash_) return false;
  if (sent_ && now - at_ < kMinGapMs) return false;
  sent_ = true;
  hash_ = h;
  at_ = now;
  return true;
}

}  // namespace dash
