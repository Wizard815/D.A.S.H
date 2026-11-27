#include "share.h"

#include <stdio.h>
#include <string.h>

namespace dash {

bool safeFileName(const char* in, char* out, size_t n) {
  if (!n) return false;
  out[0] = 0;
  if (!in) return false;
  // Browsers may send a full path: keep the last part.
  const char* base = in;
  for (const char* p = in; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  char tmp[128];
  size_t o = 0;
  for (const char* p = base; *p && o + 1 < sizeof tmp; p++) {
    unsigned char c = (unsigned char)*p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')';
    tmp[o++] = ok ? (char)c : '_';
  }
  tmp[o] = 0;
  // No leading dots or spaces (hidden files, "..").
  char* s = tmp;
  while (*s == '.' || *s == ' ') s++;
  size_t len = strlen(s);
  while (len && (s[len - 1] == ' ' || s[len - 1] == '.')) s[--len] = 0;
  if (!len) return false;
  if (len + 1 > n) {
    // Too long: keep the extension when it is short.
    const char* dot = strrchr(s, '.');
    size_t ext = dot ? strlen(dot) : 0;
    if (ext > 8 || ext + 2 > n) ext = 0;
    size_t keep = n - 1 - ext;
    memmove(out, s, keep);
    if (ext) memcpy(out + keep, dot, ext);
    out[keep + ext] = 0;
    return true;
  }
  memcpy(out, s, len + 1);
  return true;
}

void numberedName(const char* name, int attempt, char* out, size_t n) {
  if (!n) return;
  if (attempt <= 1) {
    snprintf(out, n, "%s", name);
    return;
  }
  const char* dot = strrchr(name, '.');
  if (!dot || dot == name) dot = name + strlen(name);
  char suffix[16];
  snprintf(suffix, sizeof suffix, " (%d)", attempt);
  size_t ext = strlen(dot), sl = strlen(suffix);
  size_t stem = (size_t)(dot - name);
  if (stem + sl + ext + 1 > n) stem = n > sl + ext + 1 ? n - sl - ext - 1 : 0;
  snprintf(out, n, "%.*s%s%s", (int)stem, name, suffix, dot);
}

void fmtBytes(uint64_t b, char* buf, size_t n) {
  const char* u[] = {"KB", "MB", "GB", "TB"};
  if (b < 1024) {
    snprintf(buf, n, "%u B", (unsigned)b);
    return;
  }
  double v = (double)b / 1024.0;
  int i = 0;
  while (v >= 1024.0 && i < 3) {
    v /= 1024.0;
    i++;
  }
  snprintf(buf, n, "%.1f %s", v, u[i]);
}

namespace {
size_t escaped(char* out, size_t o, size_t n, const char* s) {
  for (; s && *s && o + 2 < n; s++) {
    if (strchr("\\;,:\"", *s)) out[o++] = '\\';
    out[o++] = *s;
  }
  return o;
}
}  // namespace

void wifiJoinCode(const char* ssid, const char* pass, char* buf, size_t n) {
  if (!n) return;
  size_t o = 0;
  bool open = !pass || !*pass;
  int w = snprintf(buf, n, "WIFI:T:%s;S:", open ? "nopass" : "WPA");
  o = w > 0 ? (size_t)w : 0;
  if (o >= n) { buf[n - 1] = 0; return; }
  o = escaped(buf, o, n, ssid);
  if (!open) {
    if (o + 3 < n) { memcpy(buf + o, ";P:", 3); o += 3; }
    o = escaped(buf, o, n, pass);
  }
  if (o + 3 < n) { memcpy(buf + o, ";;", 2); o += 2; }
  buf[o < n ? o : n - 1] = 0;
}

void sharePassword(const uint8_t* r, char* out, size_t n) {
  static const char kSet[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0 O 1 l I i o
  size_t len = n > 11 ? 10 : (n ? n - 1 : 0);
  for (size_t i = 0; i < len; i++) out[i] = kSet[r[i] % (sizeof kSet - 1)];
  if (n) out[len] = 0;
}

Media mediaKind(const char* name) {
  const char* dot = name ? strrchr(name, '.') : nullptr;
  if (!dot) return Media::None;
  char ext[8];
  size_t n = 0;
  for (const char* p = dot + 1; *p && n + 1 < sizeof ext; p++) ext[n++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
  ext[n] = 0;
  if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) return Media::Photo;
  if (!strcmp(ext, "mjpeg") || !strcmp(ext, "mjpg")) return Media::Video;
  const char* other[] = {"mp4", "mov", "m4v", "avi", "mkv", "webm", "3gp"};
  for (const char* o : other)
    if (!strcmp(ext, o)) return Media::OtherVideo;
  return Media::None;
}

uint8_t jpegScale(uint16_t w, uint16_t h, int maxW, int maxH) {
  for (uint8_t s = 1; s <= 8; s *= 2)
    if (w / s <= maxW && h / s <= maxH) return s;
  return 8;
}

bool mjpegFrame(const uint8_t* b, size_t len, size_t* start, size_t* end) {
  size_t i = 0;
  while (i + 1 < len && !(b[i] == 0xFF && b[i + 1] == 0xD8)) i++;
  *start = i;
  if (i + 1 >= len) return false;
  for (size_t j = i + 2; j + 1 < len; j++)
    if (b[j] == 0xFF && b[j + 1] == 0xD9) {
      *end = j + 2;
      return true;
    }
  return false;
}

bool urlPathPart(const char* name, char* out, size_t n) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
    bool keep = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' ||
                *p == '.' || *p == '_' || *p == '~';
    size_t need = keep ? 1 : 3;
    if (o + need >= n) {
      if (n) out[0] = 0;
      return false;
    }
    if (keep) {
      out[o++] = (char)*p;
    } else {
      out[o++] = '%';
      out[o++] = hex[*p >> 4];
      out[o++] = hex[*p & 15];
    }
  }
  if (!n) return false;
  out[o] = 0;
  return true;
}

uint32_t syncKey(const char* name, uint64_t size) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const unsigned char* p = (const unsigned char*)name; *p; p++) h = (h ^ *p) * 16777619u;
  for (int i = 0; i < 8; i++) h = (h ^ (uint8_t)(size >> (8 * i))) * 16777619u;
  return h ? h : 1;  // 0 means "empty slot"
}

int httpStatus(const char* line) {
  if (strncmp(line, "HTTP/", 5)) return 0;
  const char* sp = strchr(line, ' ');
  if (!sp) return 0;
  int code = 0;
  for (int i = 1; i <= 3; i++) {
    char c = sp[i];
    if (c < '0' || c > '9') return 0;
    code = code * 10 + (c - '0');
  }
  return code;
}

}  // namespace dash
