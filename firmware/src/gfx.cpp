#include "gfx.h"

#include <qrcode.h>

namespace gfx {

TFT_eSPI tft;
uint16_t BG, CARD, TEXT, MUTED, GOLD, GOOD, BAD;

void initPalette() {
  BG = tft.color565(27, 26, 24);
  CARD = tft.color565(42, 40, 35);
  TEXT = tft.color565(239, 233, 220);
  MUTED = tft.color565(160, 152, 138);
  GOLD = tft.color565(201, 162, 39);
  GOOD = tft.color565(95, 163, 106);
  BAD = tft.color565(220, 110, 90);
}

void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum, int pad) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, BG);
  tft.setTextPadding(pad);
  tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
}

void textOn(const char* s, int x, int y, uint8_t font, uint16_t fg, uint16_t bg, uint8_t datum) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, font);
}

void clearContent() { tft.fillRect(0, kTop, W, kBottom - kTop, BG); }

void heading(const char* s) { text(s, 16, kTop + 14, 4, TEXT); }

void fit(char* out, size_t n, const char* s, uint8_t font, int maxW) {
  snprintf(out, n, "%s", s);
  if (tft.textWidth(out, font) <= maxW) return;
  size_t len = strlen(out);
  while (len > 0) {
    out[--len] = 0;
    char tmp[80];
    snprintf(tmp, sizeof tmp, "%s...", out);
    if (tft.textWidth(tmp, font) <= maxW) {
      snprintf(out, n, "%s", tmp);
      return;
    }
  }
}

int wrap(const char* s, int x, int y, int maxW, uint8_t font, uint16_t fg, uint16_t bg, int lines) {
  char line[80];
  const char* p = s;
  int lh = tft.fontHeight(font) + 4;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, bg);
  while (*p && lines-- > 0) {
    while (*p == ' ') p++;
    size_t take = 0, lastSpace = 0;
    while (p[take]) {
      snprintf(line, sizeof line, "%.*s", (int)(take + 1), p);
      if (tft.textWidth(line, font) > maxW) break;
      if (p[take] == ' ') lastSpace = take;
      take++;
    }
    if (p[take] && lastSpace && lines > 0) take = lastSpace;  // break at a word
    if (p[take] && lines == 0) {                               // last line: cut with "..."
      char cut[80];
      fit(cut, sizeof cut, p, font, maxW);
      tft.drawString(cut, x, y, font);
      return y + lh;
    }
    snprintf(line, sizeof line, "%.*s", (int)take, p);
    tft.drawString(line, x, y, font);
    p += take;
    y += lh;
  }
  return y;
}

void button(const char* label, int x, int y, int w, int h, bool primary) {
  tft.fillRoundRect(x, y, w, h, 8, primary ? GOLD : CARD);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(primary ? BG : TEXT, primary ? GOLD : CARD);
  uint8_t font = tft.textWidth(label, 4) <= w - 12 ? 4 : 2;
  tft.drawString(label, x + w / 2, y + h / 2, font);
}

void navButton(const char* label, int x, int w, bool primary) { button(label, x, kNavY, w, kNavH, primary); }

bool hit(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

void segments(const char* const* labels, int n, int selected, int y, int h) {
  if (n <= 0) return;
  int sw = (W - 24) / n;
  for (int i = 0; i < n; i++) {
    int x = 12 + i * sw;
    bool on = i == selected;
    tft.fillRoundRect(x + 2, y, sw - 4, h, 8, on ? CARD : BG);
    tft.drawRoundRect(x + 2, y, sw - 4, h, 8, CARD);
    if (on) tft.fillRect(x + 14, y + h - 6, sw - 28, 3, GOLD);  // underline: not colour alone
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? TEXT : MUTED, on ? CARD : BG);
    tft.drawString(labels[i], x + sw / 2, y + h / 2 - 2, 2);
  }
}

int segmentAt(int x, int n) {
  if (n <= 0) return 0;
  int i = (x - 12) / ((W - 24) / n);
  return i < 0 ? 0 : i >= n ? n - 1 : i;
}

int qr(const char* s, int x, int y, int maxPx) {
  constexpr int kMaxVersion = 10;
  constexpr int kSide = 4 * kMaxVersion + 17;
  static uint8_t buf[(kSide * kSide + 7) / 8];
  QRCode code;
  int v = 2;
  while (v <= kMaxVersion && qrcode_initText(&code, buf, v, ECC_LOW, s) != 0) v++;
  if (v > kMaxVersion) return 0;
  int scale = maxPx / (code.size + 8);  // with a 4-module quiet zone each side
  if (scale < 2) scale = 2;
  int side = (code.size + 8) * scale;
  tft.fillRect(x, y, side, side, TFT_WHITE);
  for (uint8_t my = 0; my < code.size; my++)
    for (uint8_t mx = 0; mx < code.size; mx++)
      if (qrcode_getModule(&code, mx, my)) tft.fillRect(x + (mx + 4) * scale, y + (my + 4) * scale, scale, scale, TFT_BLACK);
  return side;
}

}  // namespace gfx
