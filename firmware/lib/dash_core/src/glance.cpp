#include "glance.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace dash {

void asciiCopy(char* dst, size_t n, const char* src) {
  if (!n) return;
  size_t o = 0;
  const unsigned char* s = (const unsigned char*)(src ? src : "");
  auto put = [&](const char* t) {
    while (*t && o + 1 < n) dst[o++] = *t++;
  };
  while (*s && o + 1 < n) {
    unsigned char c = *s;
    if (c < 0x80) {
      dst[o++] = (c == '\n' || c == '\t' || c == '\r') ? ' ' : (char)c;
      s++;
      continue;
    }
    // E2 80 xx: quotes, dashes, ellipsis, bullet.
    if (c == 0xE2 && s[1] == 0x80 && s[2]) {
      switch (s[2]) {
        case 0x98: case 0x99: put("'"); break;
        case 0x9C: case 0x9D: put("\""); break;
        case 0x93: case 0x94: put("-"); break;
        case 0xA6: put("..."); break;
        case 0xA2: put("*"); break;
        default: break;
      }
      s += 3;
      continue;
    }
    if (c == 0xC2 && s[1] == 0xA0) { put(" "); s += 2; continue; }  // no-break space
    // Skip any other character: its lead byte, then continuation bytes.
    s++;
    while ((*s & 0xC0) == 0x80) s++;
  }
  dst[o] = 0;
}

bool threadNeedsYou(const char* st) { return st && (!strcmp(st, "waiting") || !strcmp(st, "failed")); }

const char* threadStatusText(const char* st) {
  if (!st) return "Unknown";
  if (!strcmp(st, "working")) return "Working";
  if (!strcmp(st, "done")) return "Done";
  if (!strcmp(st, "waiting")) return "Waiting for approval";
  if (!strcmp(st, "failed")) return "Didn't work";
  return "Unknown";
}

bool parseGlance(const char* json, size_t len, Glance& out) {
  out = Glance();
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonArrayConst ps = doc["projects"].as<JsonArrayConst>();
  if (ps.isNull()) return false;
  for (JsonObjectConst p : ps) {
    if (out.nProjects >= Glance::kProjects) break;
    GlanceProject& gp = out.projects[out.nProjects];
    asciiCopy(gp.id, sizeof gp.id, p["id"] | "");
    asciiCopy(gp.name, sizeof gp.name, p["name"] | "");
    gp.needsYou = p["needs_you"] | 0;
    gp.working = p["working"] | 0;
    gp.first = out.nThreads;
    for (JsonObjectConst t : p["threads"].as<JsonArrayConst>()) {
      if (out.nThreads >= Glance::kThreads) break;
      GlanceThread& gt = out.threads[out.nThreads];
      asciiCopy(gt.id, sizeof gt.id, t["id"] | "");
      asciiCopy(gt.title, sizeof gt.title, t["title"] | "");
      asciiCopy(gt.status, sizeof gt.status, t["status"] | "");
      asciiCopy(gt.last, sizeof gt.last, t["last"] | "");
      gt.project = out.nProjects;
      out.nThreads++;
      gp.count++;
    }
    out.needsYou += gp.needsYou;
    out.working += gp.working;
    out.nProjects++;
  }
  return true;
}

void glanceSummary(const Glance& g, char* buf, size_t n) {
  if (!n) return;
  if (g.needsYou && g.working) snprintf(buf, n, "%u need%s you, %u working", g.needsYou, g.needsYou == 1 ? "s" : "", g.working);
  else if (g.needsYou) snprintf(buf, n, "%u need%s you", g.needsYou, g.needsYou == 1 ? "s" : "");
  else if (g.working) snprintf(buf, n, "%u working", g.working);
  else snprintf(buf, n, "Nothing going on");
}

int glanceRows(const Glance& g, GlanceRow* rows, int max) {
  int k = 0;
  for (int p = 0; p < g.nProjects && k < max; p++) {
    rows[k++] = {true, (uint8_t)p};
    for (int t = 0; t < g.projects[p].count && k < max; t++) rows[k++] = {false, (uint8_t)(g.projects[p].first + t)};
  }
  return k;
}

}  // namespace dash
