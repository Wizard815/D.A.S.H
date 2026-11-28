// FAITH's family projects, as GET /api/projects/glance sends them: at most 8
// projects with 5 threads each, short lines. Fixed buffers, no heap kept.
// See FAITH's docs/projects.md. Who sees what is decided by FAITH from the
// token, never here.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

struct GlanceThread {
  char id[40];
  char title[36];
  char status[10];  // working, done, waiting, failed
  char stage[10];   // working, needs_you, review, done (newer FAITH; may be empty)
  bool throwaway;   // a private thread: FAITH sends only "Throwaway thread"
  char last[64];
  uint8_t project;  // index into Glance::projects
};

struct GlanceProject {
  char id[40];
  char name[36];  // "Sam", "Family", "Parents"
  uint8_t needsYou, working, review;
  uint8_t first, count;  // its threads in Glance::threads
};

struct Glance {
  static constexpr int kProjects = 8, kThreads = 40;
  GlanceProject projects[kProjects];
  GlanceThread threads[kThreads];
  uint8_t nProjects = 0, nThreads = 0;
  uint16_t needsYou = 0, working = 0, review = 0;
};

// Fills `out` from the JSON body. False (and `out` emptied) on anything that
// isn't a glance. Text is folded to plain ASCII for the screen's fonts.
bool parseGlance(const char* json, size_t len, Glance& out);

// Words for a status, so it never depends on colour alone.
const char* threadStatusText(const char* status);
// waiting or failed: the person should look.
bool threadNeedsYou(const char* status);
// Words for a thread: its stage when FAITH sends one ("Ready for review"),
// else its status.
const char* threadWords(const GlanceThread& t);

// "1 needs you, 2 working, 1 to review", or "Nothing going on".
void glanceSummary(const Glance& g, char* buf, size_t n);

// The list screen's rows: a heading per project, then its threads.
struct GlanceRow {
  bool heading;
  uint8_t index;  // project index for a heading, thread index otherwise
};
int glanceRows(const Glance& g, GlanceRow* rows, int max);

// Copies UTF-8 as ASCII: curly quotes, dashes and ellipses become their plain
// forms, other characters are dropped. Always ends with 0 when n > 0.
void asciiCopy(char* dst, size_t n, const char* src);

}  // namespace dash
