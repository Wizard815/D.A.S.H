// Files on the SD card and the Wi-Fi share: the pure parts (names, sizes,
// the Wi-Fi join code). The firmware does the card and the web page.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dash {

// Makes an uploaded file name safe for one flat folder: no paths, no "..",
// no control or odd characters (they become "_"), at most n-1 bytes keeping
// the extension. False if nothing usable is left.
bool safeFileName(const char* in, char* out, size_t n);

// "name.txt" -> "name (2).txt" for try = 2. Guests never overwrite a file.
void numberedName(const char* name, int attempt, char* out, size_t n);

// "512 B", "3.4 KB", "12.0 MB", "1.5 GB".
void fmtBytes(uint64_t bytes, char* buf, size_t n);

// The text of a Wi-Fi join QR code: WIFI:T:WPA;S:<ssid>;P:<pass>;;
// with \ ; , : " escaped.
void wifiJoinCode(const char* ssid, const char* pass, char* buf, size_t n);

// A share password from random bytes: 10 letters and digits that are easy
// to read out (no 0/O or 1/l/I).
void sharePassword(const uint8_t* random10, char* out, size_t n);

// What DASH can show for a file, by its name.
enum class Media : uint8_t {
  None,        // not a picture or video
  Photo,       // .jpg / .jpeg: shown on the screen
  Video,       // .mjpeg / .mjpg: played on the screen (no sound)
  OtherVideo,  // .mp4, .mov, ...: shared, but too heavy for this chip to play
};
Media mediaKind(const char* name);

// The smallest JPEG scale (1, 2, 4 or 8) that fits w x h inside maxW x maxH.
uint8_t jpegScale(uint16_t w, uint16_t h, int maxW, int maxH);

// Finds one whole JPEG frame (FF D8 ... FF D9) in an MJPEG stream buffer.
// Returns true with [start, end) when one is complete; `end` is where the
// next search starts. False: read more (keep the bytes from `start` on).
bool mjpegFrame(const uint8_t* buf, size_t len, size_t* start, size_t* end);

}  // namespace dash
