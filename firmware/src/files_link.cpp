#include "files_link.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <TJpg_Decoder.h>
#include <qrcode.h>

#include "board.h"
#include "gfx.h"
#include "share.h"

using namespace gfx;

namespace {
constexpr char kDir[] = "/share";
constexpr char kSsid[] = "DASH-Share";
constexpr uint32_t kIdleStopMs = 30UL * 60UL * 1000UL;  // no guest for 30 min: stop sharing
constexpr uint64_t kKeepFree = 1024ULL * 1024ULL;        // refuse uploads past this
constexpr int kMaxGuests = 4;

SPIClass sdSpi(VSPI);
WebServer server(80);
FilesLink* self = nullptr;
File upload;
char uploadName[64] = "";
bool uploadFailed = false;
const char* uploadError = "";

// The share page: no outside fonts or scripts, works on any phone.
const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>DASH files</title>
<style>:root{--bg:#f6f1e7;--card:#fffaf0;--text:#2b2620;--muted:#6f665a;--gold:#9a7a12;--line:#e4dccb}
@media (prefers-color-scheme:dark){:root{--bg:#1b1a18;--card:#2a2823;--text:#efe9dc;--muted:#a0988a;--gold:#c9a227;--line:#3a372f}}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.5 system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}h1{font:600 1.6rem Georgia,serif;margin:.2em 0}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px;margin:12px 0}
ul{list-style:none;padding:0;margin:0}li{display:flex;justify-content:space-between;gap:8px;padding:10px 0;border-bottom:1px solid var(--line)}
li:last-child{border:0}a{color:var(--gold)}small,.m{color:var(--muted)}
button,label.b{display:inline-flex;align-items:center;min-height:44px;padding:10px 16px;border-radius:8px;border:0;background:var(--gold);color:var(--bg);font:inherit;cursor:pointer}
:focus-visible{outline:3px solid var(--gold);outline-offset:2px}input[type=file]{position:absolute;opacity:0;width:1px}</style></head>
<body><main><h1>DASH files</h1><p class="m" id="free">Loading...</p>
<div class="card" id="up"><label class="b" for="f">Choose files to upload</label>
<input id="f" type="file" multiple aria-label="Choose files to upload"> <span role="status" aria-live="polite" id="st"></span>
<p class="m"><small>Uploads are added, never replace a file. Only the owner can delete.</small></p></div>
<div class="card"><ul id="list" aria-live="polite"></ul></div></main>
<script>
const $=id=>document.getElementById(id);
function size(b){const u=['B','KB','MB','GB'];let i=0;while(b>=1024&&i<3){b/=1024;i++}return(i?b.toFixed(1):b)+' '+u[i]}
async function load(){try{const r=await fetch('/list');const j=await r.json();
$('free').textContent=size(j.free)+' free'+(j.uploads?'':'. Uploads are turned off on the device.');$('up').hidden=!j.uploads;
const l=$('list');l.innerHTML='';if(!j.files.length){l.innerHTML='<li class="m">No files yet.</li>'}
for(const f of j.files){const li=document.createElement('li');const a=document.createElement('a');
a.href='/f?n='+encodeURIComponent(f.n);a.textContent=f.n;
const d=document.createElement('a');d.href=a.href+'&dl=1';d.textContent='Save';d.setAttribute('aria-label','Save '+f.n);
const s=document.createElement('small');s.textContent=size(f.s)+' ';s.append(d);li.append(a,s);l.append(li)}}
catch(e){$('free').textContent="DASH isn't answering. Check you're still on the DASH-Share Wi-Fi."}}
$('f').onchange=async e=>{for(const file of e.target.files){await new Promise(res=>{const x=new XMLHttpRequest();
const fd=new FormData();fd.append('file',file,file.name);x.upload.onprogress=p=>{$('st').textContent='Uploading '+file.name+': '+Math.round(100*p.loaded/p.total)+'%'};
x.onload=()=>{let m='Done';try{const j=JSON.parse(x.responseText);m=j.ok?'Uploaded '+j.name:"Didn't upload: "+j.error}catch(_){}$('st').textContent=m;res()};
x.onerror=()=>{$('st').textContent="Upload stopped. Move closer to DASH and try again.";res()};x.open('POST','/upload');x.send(fd)})}
e.target.value='';load()};load();
</script></body></html>)HTML";

bool validName(const String& n, char* out, size_t size) {
  return dash::safeFileName(n.c_str(), out, size) && n == out;
}

void handleList() {
  self->noteGuest();
  String j = "{\"free\":";
  j += String((double)(SD.totalBytes() - SD.usedBytes()), 0);
  j += ",\"uploads\":";
  j += self->uploadsOn() ? "true" : "false";
  j += ",\"files\":[";
  File dir = SD.open(kDir);
  bool first = true;
  int n = 0;
  for (File f = dir.openNextFile(); f && n < 500; f = dir.openNextFile(), n++) {
    if (f.isDirectory()) continue;
    char safe[64];
    const char* nm = f.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;
    if (!validName(base, safe, sizeof safe)) continue;  // only names the page can ask for back
    if (!first) j += ",";
    first = false;
    j += "{\"n\":\"";
    j += safe;  // safe names have no quotes or backslashes
    j += "\",\"s\":";
    j += String((double)f.size(), 0);
    j += "}";
  }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void handleFile() {
  self->noteGuest();
  char safe[64];
  if (!validName(server.arg("n"), safe, sizeof safe)) {
    server.send(400, "text/plain", "That file name isn't allowed.");
    return;
  }
  String path = String(kDir) + "/" + safe;
  File f = SD.open(path);
  if (!f || f.isDirectory()) {
    server.send(404, "text/plain", "That file isn't on DASH any more.");
    return;
  }
  // Range requests let a phone seek in a video and play it while it loads.
  size_t size = f.size(), from = 0, to = size ? size - 1 : 0;
  bool partial = false;
  String range = server.header("Range");
  if (range.startsWith("bytes=") && size) {
    int dash = range.indexOf('-');
    String a = range.substring(6, dash), b = range.substring(dash + 1);
    if (a.length()) {
      from = (size_t)a.toInt();
      if (b.length()) to = (size_t)b.toInt();
    } else if (b.length()) {  // the last N bytes
      size_t n = (size_t)b.toInt();
      from = n < size ? size - n : 0;
    }
    if (to >= size) to = size - 1;
    if (from > to) {
      server.sendHeader("Content-Range", String("bytes */") + size);
      server.send(416, "text/plain", "That part of the file doesn't exist.");
      f.close();
      return;
    }
    partial = true;
  }
  const char* type = "application/octet-stream";
  String lower = String(safe);
  lower.toLowerCase();
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) type = "image/jpeg";
  else if (lower.endsWith(".png")) type = "image/png";
  else if (lower.endsWith(".mp4") || lower.endsWith(".m4v")) type = "video/mp4";
  else if (lower.endsWith(".mov")) type = "video/quicktime";
  else if (lower.endsWith(".webm")) type = "video/webm";
  else if (lower.endsWith(".mp3")) type = "audio/mpeg";
  else if (lower.endsWith(".pdf")) type = "application/pdf";
  else if (lower.endsWith(".txt")) type = "text/plain";
  // ?dl=1 saves it; otherwise the phone shows or plays it.
  server.sendHeader("Content-Disposition", String(server.hasArg("dl") ? "attachment" : "inline") + "; filename=\"" + safe + "\"");
  server.sendHeader("Accept-Ranges", "bytes");
  if (partial) server.sendHeader("Content-Range", String("bytes ") + from + "-" + to + "/" + size);
  server.setContentLength(to - from + 1);
  server.send(partial ? 206 : 200, type, "");
  f.seek(from);
  uint8_t chunk[2048];
  size_t left = to - from + 1;
  WiFiClient client = server.client();
  while (left && client.connected()) {
    size_t n = f.read(chunk, min(left, sizeof chunk));
    if (!n) break;
    client.write(chunk, n);
    left -= n;
  }
  f.close();
}

void handleUploadData() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    self->noteGuest();
    uploadFailed = false;
    uploadError = "";
    char base[56];
    if (!self->uploadsOn()) {
      uploadFailed = true;
      uploadError = "uploads are turned off on DASH";
      return;
    }
    if (!dash::safeFileName(u.filename.c_str(), base, sizeof base)) {
      uploadFailed = true;
      uploadError = "that file name isn't allowed";
      return;
    }
    // Never overwrite: photo.jpg, photo (2).jpg, ...
    for (int i = 1; i < 100; i++) {
      dash::numberedName(base, i, uploadName, sizeof uploadName);
      if (!SD.exists(String(kDir) + "/" + uploadName)) break;
    }
    upload = SD.open(String(kDir) + "/" + uploadName, FILE_WRITE);
    if (!upload) {
      uploadFailed = true;
      uploadError = "the SD card refused it";
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed || !upload) return;
    if (SD.totalBytes() - SD.usedBytes() < kKeepFree + u.currentSize || upload.write(u.buf, u.currentSize) != u.currentSize) {
      uploadFailed = true;
      uploadError = "the SD card is full";
      upload.close();
      SD.remove(String(kDir) + "/" + uploadName);
    }
  } else if (u.status == UPLOAD_FILE_END) {
    if (upload) upload.close();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upload) upload.close();
    SD.remove(String(kDir) + "/" + uploadName);  // keep no half files
    uploadFailed = true;
    uploadError = "the upload stopped part way";
  }
}

void handleUploadDone() {
  self->noteGuest();
  self->changed();
  if (uploadFailed) {
    server.send(200, "application/json", String("{\"ok\":false,\"error\":\"") + uploadError + "\"}");
    return;
  }
  self->uploaded(uploadName);
  server.send(200, "application/json", String("{\"ok\":true,\"name\":\"") + uploadName + "\"}");
}
}  // namespace

bool jpgOutCb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp);

bool FilesLink::mount() {
  sdSpi.begin(board::kSdSck, board::kSdMiso, board::kSdMosi, board::kSdCs);
  if (!SD.begin(board::kSdCs, sdSpi, 20000000)) {
    SD.end();
    return false;
  }
  if (!SD.exists(kDir)) SD.mkdir(kDir);
  return true;
}

void FilesLink::begin() {
  self = this;
  TJpgDec.setCallback(jpgOutCb);
  tft.setSwapBytes(true);  // TJpg_Decoder gives byte-swapped pixels
  mounted_ = mount();
  if (mounted_) scan();
  server.on("/", HTTP_GET, [] {
    self->noteGuest();
    server.send_P(200, "text/html", kPage);
  });
  server.on("/list", HTTP_GET, handleList);
  server.on("/f", HTTP_GET, handleFile);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  static const char* hdrs[] = {"Range"};
  server.collectHeaders(hdrs, 1);
  server.onNotFound([] { server.send(404, "text/plain", "Not here. Open the page at http://192.168.4.1"); });
  updateDetail();
}

void FilesLink::scan() {
  files_ = 0;
  nMedia_ = 0;
  File dir = SD.open(kDir);
  for (File f = dir.openNextFile(); f && files_ < 9999; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    files_++;
    const char* nm = f.name();
    const char* base = strrchr(nm, '/');
    base = base ? base + 1 : nm;
    if (nMedia_ < kMaxMedia && dash::mediaKind(base) != dash::Media::None && strlen(base) < sizeof media_[0]) strcpy(media_[nMedia_++], base);
  }
  used_ = SD.usedBytes();
  total_ = SD.totalBytes();
  rev_++;
}

void FilesLink::loop(uint32_t now, bool) {
  if (playing_) videoFrame(now);
  if (!sharing_) return;
  server.handleClient();
  if (now - lastClientCheck_ >= 2000) {
    lastClientCheck_ = now;
    uint8_t c = WiFi.softAPgetStationNum();
    if (c) lastActive_ = now;
    if (c != clients_) {
      clients_ = c;
      rev_++;
      updateDetail();
    }
  }
  if (now - lastActive_ > kIdleStopMs) stopShare();
}

bool FilesLink::startShare() {
  if (sharing_ || !mounted_) return sharing_;
  uint8_t r[10];
  esp_fill_random(r, sizeof r);
  dash::sharePassword(r, pass_, sizeof pass_);
  WiFi.mode(WIFI_AP_STA);
  // The share must sit on the same channel as the network DASH is on.
  int ch = WiFi.status() == WL_CONNECTED ? WiFi.channel() : 6;
  if (!WiFi.softAP(kSsid, pass_, ch, 0, kMaxGuests)) {
    WiFi.mode(WIFI_STA);
    snprintf(detail_, sizeof detail_, "couldn't start the share Wi-Fi");
    return false;
  }
  server.begin();
  sharing_ = true;
  showLink_ = false;
  lastActive_ = millis();
  rev_++;
  updateDetail();
  return true;
}

void FilesLink::stopShare() {
  if (!sharing_) return;
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  sharing_ = false;
  clients_ = 0;
  pass_[0] = 0;  // a new password next time: old guests can't rejoin
  rev_++;
  updateDetail();
}

void FilesLink::noteGuest() { lastActive_ = millis(); }
void FilesLink::changed() {
  scan();
  updateDetail();
}

void FilesLink::uploaded(const char* name) {
  snprintf(lastUpload_, sizeof lastUpload_, "%s", name);
  lastUploadMs_ = millis();
  unseen_ = true;
  rev_++;
}

int FilesLink::mediaIndex(const char* name) const {
  for (int i = 0; i < nMedia_; i++)
    if (!strcmp(media_[i], name)) return i;
  return -1;
}

dash::LinkState FilesLink::state() const { return mounted_ ? dash::LinkState::Up : dash::LinkState::Waiting; }

void FilesLink::updateDetail() {
  char freeText[16];
  if (!mounted_) {
    snprintf(detail_, sizeof detail_, "no SD card");
    return;
  }
  dash::fmtBytes(total_ - used_, freeText, sizeof freeText);
  if (sharing_) snprintf(detail_, sizeof detail_, "sharing, %u connected", clients_);
  else snprintf(detail_, sizeof detail_, "%u files, %s free", files_, freeText);
}

bool FilesLink::homeCard(dash::HomeCard& c) const {
  snprintf(c.title, sizeof c.title, "Files");
  if (!mounted_) snprintf(c.line, sizeof c.line, "No SD card");
  else if (unseen_) snprintf(c.line, sizeof c.line, "New upload: %.40s", lastUpload_);
  else if (sharing_) snprintf(c.line, sizeof c.line, "Shared: %u connected", clients_);
  else {
    char f[16];
    dash::fmtBytes(total_ - used_, f, sizeof f);
    snprintf(c.line, sizeof c.line, "Not shared. %u files, %s free", files_, f);
  }
  c.attention = unseen_;
  return true;
}

// ---------- screen ----------
// Main: card, share state in words, four buttons, the newest upload.
// Qr: the big join code (the quick QR button). Viewer: photos and videos,
// one at a time; swipe or tap the sides to move.
namespace {
constexpr int kCardY = kTop + 52, kNewY = kTop + 122, kBtnY = kTop + 152, kBtnH = 52, kBtnGap = 8;
constexpr int kBarY = kTop, kBarH2 = 44, kImgY = kTop + 48, kImgH = kBottom - kImgY;
constexpr int kEdge = 80;              // tap this close to a side to move
constexpr size_t kVideoBuf = 48 * 1024;  // one frame at 320 wide fits easily
constexpr uint32_t kFrameMs = 1000 / 12;

int btnX(int col) { return 12 + col * ((W - 24 - kBtnGap) / 2 + kBtnGap); }
constexpr int kBtnW = (W - 24 - kBtnGap) / 2;
int btnY(int row) { return kBtnY + row * (kBtnH + kBtnGap); }

}  // namespace

// TJpg_Decoder hands over blocks; keep them inside the picture area.
bool jpgOutCb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  if (y >= kBottom) return false;
  if (y < kImgY) return true;
  if (y + h > kBottom) h = kBottom - y;
  tft.pushImage(x, y, w, h, bmp);
  return true;
}

void FilesLink::opened() {
  if (!mounted_) {
    mounted_ = mount();  // a card may have gone in since boot
    if (mounted_) scan();
    updateDetail();
  }
  mode_ = Mode::Main;
  drawnRev_ = UINT32_MAX;
}

void FilesLink::closed() {
  stopVideo();
  if (unseen_ && mode_ != Mode::Qr) unseen_ = false;  // they've been to Files
}

void FilesLink::showQr() {
  if (!sharing_) startShare();
  mode_ = sharing_ ? Mode::Qr : Mode::Main;  // no card: main says why
  showLink_ = false;
  rev_++;
}

bool FilesLink::dirty(uint32_t) { return rev_ != drawnRev_; }

void FilesLink::drawQr(const char* s, int x, int y, int maxPx) {
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(4)];
  if (qrcode_initText(&qr, buf, 4, ECC_LOW, s) != 0) return;
  int scale = maxPx / (qr.size + 8);  // with a 4-module quiet zone each side
  if (scale < 2) scale = 2;
  int side = (qr.size + 8) * scale;
  tft.fillRect(x, y, side, side, TFT_WHITE);
  for (uint8_t my = 0; my < qr.size; my++)
    for (uint8_t mx = 0; mx < qr.size; mx++)
      if (qrcode_getModule(&qr, mx, my)) tft.fillRect(x + (mx + 4) * scale, y + (my + 4) * scale, scale, scale, TFT_BLACK);
}

void FilesLink::draw(uint32_t, bool) {
  drawnRev_ = rev_;
  if (mode_ == Mode::Qr) drawQrScreen();
  else if (mode_ == Mode::Viewer) drawViewer();
  else drawMain();
}

void FilesLink::drawMain() {
  clearContent();
  heading("Files");
  char a[128], b[64], f[16], t[16];
  tft.fillRoundRect(12, kCardY, W - 24, 62, 8, CARD);
  if (!enabled) {
    textOn("Files is off", 24, kCardY + 8, 4, TEXT, CARD);
    textOn("Turn it on in Links.", 24, kCardY + 38, 2, MUTED, CARD);
    return;
  }
  if (!mounted_) {
    textOn("No SD card", 24, kCardY + 8, 4, TEXT, CARD);
    textOn("Put in a FAT32 card, then tap here.", 24, kCardY + 38, 2, MUTED, CARD);
    return;
  }
  // Shared or not, in words, top right.
  text(sharing_ ? "Shared" : "Not shared", W - 16, kTop + 20, 2, sharing_ ? GOOD : MUTED, TR_DATUM);
  dash::fmtBytes(total_ - used_, f, sizeof f);
  dash::fmtBytes(total_, t, sizeof t);
  snprintf(a, sizeof a, "%u file%s, %u to view", files_, files_ == 1 ? "" : "s", nMedia_);
  snprintf(b, sizeof b, "%s free of %s", f, t);
  textOn(a, 24, kCardY + 8, 4, TEXT, CARD);
  textOn(b, 24, kCardY + 38, 2, MUTED, CARD);

  if (*lastUpload_) {
    char cut[80];
    snprintf(a, sizeof a, "%s: %s  (tap to view)", unseen_ ? "New upload" : "Last upload", lastUpload_);
    fit(cut, sizeof cut, a, 2, W - 32);
    text(cut, 16, kNewY, 2, unseen_ ? GOLD : MUTED);
  }

  button(sharing_ ? "Stop sharing" : "Share", btnX(0), btnY(0), kBtnW, kBtnH, !sharing_);
  button("QR code", btnX(1), btnY(0), kBtnW, kBtnH, sharing_);
  snprintf(a, sizeof a, "View (%u)", nMedia_);
  button(a, btnX(0), btnY(1), kBtnW, kBtnH, false);
  button(uploads_ ? "Uploads: on" : "Uploads: off", btnX(1), btnY(1), kBtnW, kBtnH, false);

  int y = btnY(2) + 4;
  if (sharing_) {
    snprintf(a, sizeof a, "Everyone on DASH-Share can see and download every file here. %u connected.", clients_);
    wrap(a, 16, y, W - 32, 2, MUTED, BG, 4);
  } else {
    wrap("Not shared: only you can see these. Share or QR code turns on a DASH-Share Wi-Fi with a new password. Nobody but you can delete.", 16, y, W - 32, 2, MUTED, BG, 5);
  }
}

void FilesLink::drawQrScreen() {
  clearContent();
  heading(showLink_ ? "Open the page" : "Scan to join");
  text("Shared", W - 16, kTop + 20, 2, GOOD, TR_DATUM);
  char code[96], a[64];
  if (showLink_) snprintf(code, sizeof code, "http://%s/", WiFi.softAPIP().toString().c_str());
  else dash::wifiJoinCode(kSsid, pass_, code, sizeof code);
  int side = 220;
  drawQr(code, (W - side) / 2, kTop + 50, side);
  int y = kTop + 50 + side + 8;
  if (showLink_) snprintf(a, sizeof a, "http://%s", WiFi.softAPIP().toString().c_str());
  else snprintf(a, sizeof a, "%s, password %s", kSsid, pass_);
  text(a, W / 2, y, 2, TEXT, TC_DATUM);
  snprintf(a, sizeof a, "%u connected", clients_);
  text(a, W / 2, y + 22, 2, MUTED, TC_DATUM);
  int bw = (W - 24 - 8) / 2;
  button(showLink_ ? "Wi-Fi code" : "Page link", 12, kNavY, bw, kNavH, false);
  button("Done", 12 + bw + 8, kNavY, bw, kNavH, true);
}

void FilesLink::openViewer(int i) {
  if (nMedia_ == 0) return;
  stopVideo();
  at_ = (uint16_t)((i % nMedia_ + nMedia_) % nMedia_);
  mode_ = Mode::Viewer;
  if (!strcmp(media_[at_], lastUpload_)) unseen_ = false;
  rev_++;
}

void FilesLink::step(int dir) { openViewer((int)at_ + dir); }

void FilesLink::drawViewer() {
  clearContent();
  if (nMedia_ == 0) {
    mode_ = Mode::Main;
    drawMain();
    return;
  }
  const char* name = media_[at_];
  // Top bar: Back, the name, "3 of 12".
  button("Back", 8, kBarY + 2, 76, kBarH2 - 4, false);
  char pos[16], cut[48];
  snprintf(pos, sizeof pos, "%u of %u", at_ + 1, nMedia_);
  text(pos, W - 10, kBarY + 14, 2, MUTED, TR_DATUM);
  fit(cut, sizeof cut, name, 2, W - 90 - tft.textWidth(pos, 2) - 18);
  text(cut, 92, kBarY + 14, 2, TEXT);

  dash::Media kind = dash::mediaKind(name);
  String path = String(kDir) + "/" + name;
  if (kind == dash::Media::OtherVideo) {
    int y = kImgY + 40;
    text("Can't play this here", 16, y, 4, TEXT);
    wrap("This chip can only play MJPEG video. The file is still shared. To watch it on DASH, convert it on a computer with ffmpeg (see docs/files.md) and upload the .mjpeg.",
         16, y + 40, W - 32, 2, MUTED, BG, 6);
    text("Swipe or tap the sides for the next one", W / 2, kBottom - 24, 2, MUTED, TC_DATUM);
    return;
  }
  uint16_t w = 0, h = 0;
  if (kind == dash::Media::Photo) {
    if (TJpgDec.getFsJpgSize(&w, &h, path, SD) != JDR_OK || !w) {
      text("Can't open this picture", 16, kImgY + 40, 4, TEXT);
      wrap("It may be a progressive JPEG, which this chip can't decode. Save it as a normal (baseline) JPEG.", 16, kImgY + 80, W - 32, 2, MUTED, BG, 4);
      return;
    }
    uint8_t sc = dash::jpegScale(w, h, W, kImgH);
    TJpgDec.setJpgScale(sc);
    int dw = w / sc, dh = h / sc;
    TJpgDec.drawFsJpg((W - dw) / 2, kImgY + (kImgH - dh) / 2, path, SD);
    return;
  }
  // Video: its first frame, and Play.
  videoNote_[0] = 0;
  play();
  if (playing_) {
    videoFrame(millis());
    playing_ = false;  // paused on the first frame
  }
  int bw = 120;
  button("Play", (W - bw) / 2, kBottom - 60, bw, 48, true);
  if (*videoNote_) wrap(videoNote_, 16, kImgY + 40, W - 32, 2, MUTED, BG, 3);
}

void FilesLink::play() {
  if (!video_) {
    video_ = SD.open(String(kDir) + "/" + media_[at_]);
    if (!video_) {
      snprintf(videoNote_, sizeof videoNote_, "Can't open this video.");
      return;
    }
    vlen_ = 0;
    frames_ = 0;
  }
  if (!vbuf_) vbuf_ = (uint8_t*)malloc(kVideoBuf);
  if (!vbuf_) {
    snprintf(videoNote_, sizeof videoNote_, "Not enough memory to play.");
    stopVideo();
    return;
  }
  TJpgDec.setJpgScale(1);
  playing_ = true;
  nextFrame_ = millis();
}

void FilesLink::stopVideo() {
  playing_ = false;
  if (video_) video_.close();
  if (vbuf_) {
    free(vbuf_);
    vbuf_ = nullptr;
  }
  vlen_ = 0;
}

void FilesLink::videoFrame(uint32_t now) {
  if ((int32_t)(now - nextFrame_) < 0 || !video_ || !vbuf_) return;
  nextFrame_ = now + kFrameMs;
  size_t start, end;
  for (int tries = 0; tries < 16; tries++) {
    if (dash::mjpegFrame(vbuf_, vlen_, &start, &end)) {
      uint16_t w = 0, h = 0;
      if (TJpgDec.getJpgSize(&w, &h, vbuf_ + start, end - start) == JDR_OK && w) {
        uint8_t sc = dash::jpegScale(w, h, W, kImgH);
        TJpgDec.setJpgScale(sc);
        int dw = w / sc, dh = h / sc;
        TJpgDec.drawJpg((W - dw) / 2, kImgY + (kImgH - dh) / 2, vbuf_ + start, end - start);
      }
      memmove(vbuf_, vbuf_ + end, vlen_ - end);
      vlen_ -= end;
      frames_++;
      return;
    }
    // Keep the partial frame, drop what's before it, read more.
    if (start > 0) {
      memmove(vbuf_, vbuf_ + start, vlen_ - start);
      vlen_ -= start;
    }
    if (vlen_ >= kVideoBuf) {  // a frame bigger than the buffer: can't play it
      snprintf(videoNote_, sizeof videoNote_, "Frames too big: convert at 320 wide.");
      stopVideo();
      rev_++;
      return;
    }
    size_t got = video_.read(vbuf_ + vlen_, min((size_t)4096, kVideoBuf - vlen_));
    if (got == 0) {  // the end: back to the start, paused
      video_.seek(0);
      vlen_ = 0;
      playing_ = false;
      rev_++;
      return;
    }
    vlen_ += got;
  }
}

void FilesLink::tap(int x, int y, uint32_t now) {
  if (!enabled) return;
  if (!mounted_) {
    opened();
    rev_++;
    return;
  }
  if (mode_ == Mode::Qr) {
    if (y < kNavY) return;
    if (x < W / 2) showLink_ = !showLink_;
    else mode_ = Mode::Main;
    rev_++;
    return;
  }
  if (mode_ == Mode::Viewer) {
    if (y < kBarY + kBarH2 && x < 90) {  // Back
      stopVideo();
      mode_ = Mode::Main;
      rev_++;
      return;
    }
    // Play/pause on the middle of a video; sides and swipes move (on release).
    if (dash::mediaKind(media_[at_]) == dash::Media::Video && x >= kEdge && x < W - kEdge && y >= kImgY) {
      if (playing_) playing_ = false;
      else {
        if (!video_) {
          clearContent();
          drawnRev_ = rev_;
        }
        play();
      }
    }
    return;
  }
  // Main
  if (*lastUpload_ && y >= kNewY - 6 && y < kNewY + 22) {
    int i = mediaIndex(lastUpload_);
    unseen_ = false;
    if (i >= 0) openViewer(i);
    else rev_++;
    return;
  }
  if (hit(x, y, btnX(0), btnY(0), kBtnW, kBtnH)) {
    if (sharing_) stopShare();
    else startShare();
  } else if (hit(x, y, btnX(1), btnY(0), kBtnW, kBtnH)) {
    showQr();
  } else if (hit(x, y, btnX(0), btnY(1), kBtnW, kBtnH)) {
    if (nMedia_) openViewer(*lastUpload_ && mediaIndex(lastUpload_) >= 0 ? mediaIndex(lastUpload_) : 0);
  } else if (hit(x, y, btnX(1), btnY(1), kBtnW, kBtnH)) {
    uploads_ = !uploads_;
    rev_++;
  }
}

void FilesLink::release(int x, int y, int dx, uint32_t now) {
  if (mode_ != Mode::Viewer || y < kImgY) return;
  if (dx <= -60 || (abs(dx) < 20 && x >= W - kEdge)) step(1);       // swipe left or tap right: next
  else if (dx >= 60 || (abs(dx) < 20 && x < kEdge)) step(-1);       // swipe right or tap left: back
}
