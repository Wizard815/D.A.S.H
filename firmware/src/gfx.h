// The screen and its drawing helpers, shared by the built-in screens and by
// mods. Portrait 320x480: status bar on top, tab bar at the bottom, content
// between kTop and kBottom.
#pragma once
#include <TFT_eSPI.h>

namespace gfx {

extern TFT_eSPI tft;
// Neutral dark with a gold accent, to sit next to FAITH's look.
extern uint16_t BG, CARD, TEXT, MUTED, GOLD, GOOD, BAD;

constexpr int W = 320, H = 480;
constexpr int kBarH = 32;  // status bar
constexpr int kTabH = 56;  // tab bar, comfortably over 44 px
constexpr int kTabY = H - kTabH;
constexpr int kTop = kBarH, kBottom = kTabY;
constexpr int kNavH = 48, kNavY = kBottom - kNavH - 6;  // a bottom button row

void initPalette();

void text(const char* s, int x, int y, uint8_t font, uint16_t fg, uint8_t datum = TL_DATUM, int pad = 0);
// Text on a card (or any other background).
void textOn(const char* s, int x, int y, uint8_t font, uint16_t fg, uint16_t bg, uint8_t datum = TL_DATUM);
void clearContent();
void heading(const char* s);

// Cuts `s` to fit `maxW` pixels in `font`, ending with "..." when cut.
void fit(char* out, size_t n, const char* s, uint8_t font, int maxW);
// Draws `s` wrapped at spaces, from y, at most `lines` lines. Returns the next y.
int wrap(const char* s, int x, int y, int maxW, uint8_t font, uint16_t fg, uint16_t bg, int lines);

// A button at least 44 px tall. Primary: gold fill; otherwise a card.
void button(const char* label, int x, int y, int w, int h, bool primary);
void navButton(const char* label, int x, int w, bool primary);
bool hit(int x, int y, int bx, int by, int bw, int bh);

// A row of segment buttons (sub-pages). The selected one is filled and underlined.
void segments(const char* const* labels, int n, int selected, int y, int h = 44);
int segmentAt(int x, int n);  // which segment a tap at x is in

// A QR code of `s`, black on white with a quiet zone, in a square of at most
// maxPx at (x, y). Picks the smallest QR version that holds it (up to 10,
// 271 bytes). Returns the side drawn, 0 if `s` is too long.
int qr(const char* s, int x, int y, int maxPx);

}  // namespace gfx
