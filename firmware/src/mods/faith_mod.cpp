#include "faith_mod.h"

#include "config.h"
#include "gfx.h"

using namespace gfx;

namespace {
ProjectsLink* projects = nullptr;  // the mod's, for the screen code below

// ---------- Projects ----------
// FAITH's family projects, read-only: a list by project, and one thread's
// latest line. Tap a thread to open it. Pages, not scrolling, until the knob (M3).
constexpr int kListY = kTop + 52;
constexpr int kHeadRowH = 28, kThreadRowH = 56;
constexpr int kMaxRows = dash::Glance::kProjects + dash::Glance::kThreads;
constexpr int kMaxPages = 16;
dash::GlanceRow rows[kMaxRows];
int nRows = 0, nPages = 1, page = 0;
int pageStart[kMaxPages + 1] = {0};
char openThread[40] = "";  // the thread being read, by id; empty: the list
uint32_t drawnProjectsRev = UINT32_MAX;
uint32_t drawnProjectsLinks = UINT32_MAX;

uint16_t statusColour(const dash::GlanceThread& t) {
  const char* st = t.status;
  if (dash::threadNeedsYou(st)) return BAD;
  if (!strcmp(t.stage, "review")) return GOLD;
  if (!strcmp(st, "working")) return GOLD;
  return MUTED;
}

void layoutPages() {
  nRows = dash::glanceRows(projects->glance(), rows, kMaxRows);
  nPages = 0;
  int y = kListY, bottom = kNavY - 6;
  for (int i = 0; i < nRows; i++) {
    int h = rows[i].heading ? kHeadRowH : kThreadRowH;
    // Keep a heading with its first thread.
    int need = h + (rows[i].heading && i + 1 < nRows && !rows[i + 1].heading ? kThreadRowH : 0);
    if (i == 0 || y + need > bottom) {
      if (nPages >= kMaxPages) { nRows = i; break; }
      pageStart[nPages++] = i;
      y = kListY;
    }
    y += h;
  }
  if (nPages == 0) pageStart[nPages++] = 0;
  pageStart[nPages] = nRows;
  if (page >= nPages) page = nPages - 1;
}

// Where a row of the current page is drawn, or -1 past the page.
int rowY(int i) {
  int y = kListY;
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    if (k == i) return y;
    y += rows[k].heading ? kHeadRowH : kThreadRowH;
  }
  return -1;
}

int findThread(const char* id) {
  const dash::Glance& g = projects->glance();
  for (int i = 0; i < g.nThreads; i++)
    if (!strcmp(g.threads[i].id, id)) return i;
  return -1;
}

void drawProjectsMessage(const char* a, const char* b) {
  text(a, 16, kListY + 8, 4, TEXT);
  wrap(b, 16, kListY + 48, W - 32, 2, MUTED, BG, 4);
}

void drawThread(int ti) {
  const dash::Glance& g = projects->glance();
  const dash::GlanceThread& t = g.threads[ti];
  char buf[64];
  text(g.projects[t.project].name, 16, kTop + 16, 2, MUTED);
  int y = wrap(t.title, 16, kTop + 38, W - 32, 4, TEXT, BG, 2) + 6;
  // Status as a badge with words.
  const char* words = dash::threadWords(t);
  int bw = tft.textWidth(words, 2) + 20;
  tft.fillRoundRect(16, y, bw, 26, 13, CARD);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(statusColour(t), CARD);
  tft.drawString(words, 26, y + 13, 2);
  y += 42;
  if (*t.last) {
    text("Latest", 16, y, 2, MUTED);
    y = wrap(t.last, 16, y + 22, W - 32, 4, TEXT, BG, 4) + 12;
  }
  snprintf(buf, sizeof buf, "%s", t.throwaway ? "A private thread: open it in the FAITH app." : dash::threadNeedsYou(t.status) ? "Answer it in the FAITH app or a chat." : "Read it all or send it more in the FAITH app.");
  wrap(buf, 16, y, W - 32, 2, MUTED, BG, 2);
  navButton("Back", 12, W - 24, false);
}

void drawProjects(bool full) {
  uint32_t rev = projects ? projects->revision() : 0;
  uint32_t lrev = (uint32_t)projects->state() + (projects->enabled ? 16u : 0u);
  if (!full && rev == drawnProjectsRev && lrev == drawnProjectsLinks) return;
  drawnProjectsRev = rev;
  drawnProjectsLinks = lrev;
  clearContent();

  if (!projects || !projects->enabled) {
    heading("Projects");
    drawProjectsMessage("Projects are off", "Add DASH_FAITH_TOKEN to secrets.h (see the firmware README), or turn FAITH on in Links.");
    return;
  }
  if (!projects->hasGlance()) {
    heading("Projects");
    const char* d = projects->detail();
    drawProjectsMessage("Asking FAITH...", projects->state() == dash::LinkState::Error && *d ? d : "Your projects show here in a moment.");
    return;
  }
  layoutPages();
  if (*openThread) {
    int ti = findThread(openThread);
    if (ti >= 0) {
      drawThread(ti);
      return;
    }
    openThread[0] = 0;  // put away or gone: back to the list
  }

  heading("Projects");
  const dash::Glance& g = projects->glance();
  char sum[40];
  dash::glanceSummary(g, sum, sizeof sum);
  text(sum, W - 16, kTop + 20, 2, g.needsYou ? GOLD : MUTED, TR_DATUM);
  if (nRows == 0) {
    drawProjectsMessage("No projects", "FAITH shows no projects for this token.");
    return;
  }
  char line[80];
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    int y = rowY(k);
    if (rows[k].heading) {
      const dash::GlanceProject& p = g.projects[rows[k].index];
      text(p.name, 16, y + 8, 2, MUTED);
      if (p.count == 0) text("No threads yet", W - 16, y + 8, 2, MUTED, TR_DATUM);
      continue;
    }
    const dash::GlanceThread& t = g.threads[rows[k].index];
    tft.fillRoundRect(12, y, W - 24, kThreadRowH - 6, 8, CARD);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TEXT, CARD);
    fit(line, sizeof line, t.title, 2, W - 56);
    tft.drawString(line, 24, y + 7, 2);
    tft.setTextColor(TEXT, CARD);
    tft.drawString(">", W - 32, y + 7, 2);
    const char* words = dash::threadWords(t);
    tft.setTextColor(statusColour(t), CARD);
    tft.drawString(words, 24, y + 27, 2);
    int x = 24 + tft.textWidth(words, 2) + 10;
    if (*t.last && strcmp(t.last, words)) {
      fit(line, sizeof line, t.last, 2, W - 24 - x - 12);
      tft.setTextColor(MUTED, CARD);
      tft.drawString(line, x, y + 27, 2);
    }
  }
  if (nPages > 1) {
    char more[32];
    snprintf(more, sizeof more, "Page %d of %d", page + 1, nPages);
    text(more, 16, kNavY + 16, 2, MUTED);
    navButton(page + 1 < nPages ? "Next" : "First", W - 12 - 128, 128, true);
  }
}

void tapProjects(int x, int y) {
  if (!projects || !projects->hasGlance()) return;
  if (*openThread) {
    if (y >= kNavY) {
      openThread[0] = 0;
      drawProjects(true);
    }
    return;
  }
  if (nPages > 1 && y >= kNavY && x >= W - 12 - 128 - 12) {
    page = (page + 1) % nPages;
    drawProjects(true);
    return;
  }
  for (int k = pageStart[page]; k < pageStart[page + 1]; k++) {
    int ry = rowY(k);
    if (rows[k].heading || y < ry || y >= ry + kThreadRowH - 6) continue;
    snprintf(openThread, sizeof openThread, "%s", projects->glance().threads[rows[k].index].id);
    drawProjects(true);
    return;
  }
}

}  // namespace

FaithMod::FaithMod() : health_(DASH_FAITH_URL), projects_(DASH_FAITH_URL, DASH_FAITH_TOKEN) { projects = &projects_; }

void FaithMod::begin() {
  health_.enabled = true;
  projects_.enabled = true;
  health_.begin();
  projects_.begin();  // turns itself off without a token
}

void FaithMod::loop(uint32_t now, bool netUp) {
  if (health_.enabled) health_.loop(now, netUp);
  if (projects_.enabled) projects_.loop(now, netUp);
}

const char* FaithMod::detail() const {
  const char* h = health_.detail();
  if (!projects_.enabled) snprintf(detail_, sizeof detail_, "%.30s%sprojects off (no token)", h, *h ? "; " : "");
  else if (projects_.hasGlance()) {
    char sum[40];
    dash::glanceSummary(projects_.glance(), sum, sizeof sum);
    snprintf(detail_, sizeof detail_, "%s", sum);
  } else snprintf(detail_, sizeof detail_, "%s", h);
  return detail_;
}

bool FaithMod::homeCard(dash::HomeCard& c, uint32_t) const {
  snprintf(c.title, sizeof c.title, "Projects");
  if (!projects_.enabled) {
    snprintf(c.title, sizeof c.title, "FAITH");
    snprintf(c.line, sizeof c.line, "%s", dash::linkStateText(health_.state()));
    c.attention = health_.problem();
    return true;
  }
  if (projects_.hasGlance()) dash::glanceSummary(projects_.glance(), c.line, sizeof c.line);
  else snprintf(c.line, sizeof c.line, "Asking FAITH...");
  c.attention = projects_.hasGlance() && projects_.glance().needsYou;
  return true;
}

void FaithMod::opened() {
  page = 0;
  openThread[0] = 0;
  drawnProjectsRev = UINT32_MAX;
  projects_.refreshSoon();
}

bool FaithMod::dirty(uint32_t) {
  uint32_t lrev = (uint32_t)projects_.state() + (projects_.enabled ? 16u : 0u);
  return projects_.revision() != drawnProjectsRev || lrev != drawnProjectsLinks;
}

void FaithMod::draw(uint32_t, bool full) { drawProjects(full); }

void FaithMod::tap(int x, int y, uint32_t) { tapProjects(x, y); }
