#include "files_link.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
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
a.href='/f?n='+encodeURIComponent(f.n);a.textContent=f.n;a.setAttribute('download',f.n);
const s=document.createElement('small');s.textContent=size(f.s);li.append(a,s);l.append(li)}}
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
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + safe + "\"");
  server.streamFile(f, "application/octet-stream");
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
  server.send(200, "application/json", String("{\"ok\":true,\"name\":\"") + uploadName + "\"}");
}
}  // namespace

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
  mounted_ = mount();
  if (mounted_) scan();
  server.on("/", HTTP_GET, [] {
    self->noteGuest();
    server.send_P(200, "text/html", kPage);
  });
  server.on("/list", HTTP_GET, handleList);
  server.on("/f", HTTP_GET, handleFile);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.onNotFound([] { server.send(404, "text/plain", "Not here. Open the page at http://192.168.4.1"); });
  updateDetail();
}

void FilesLink::scan() {
  files_ = 0;
  File dir = SD.open(kDir);
  for (File f = dir.openNextFile(); f && files_ < 9999; f = dir.openNextFile())
    if (!f.isDirectory()) files_++;
  used_ = SD.usedBytes();
  total_ = SD.totalBytes();
  rev_++;
}

void FilesLink::loop(uint32_t now, bool) {
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
  else if (sharing_) snprintf(c.line, sizeof c.line, "Sharing: %u connected", clients_);
  else {
    char f[16];
    dash::fmtBytes(total_ - used_, f, sizeof f);
    snprintf(c.line, sizeof c.line, "%u files, %s free. Tap to share", files_, f);
  }
  c.attention = false;
  return true;
}

// ---------- screen ----------
namespace {
constexpr int kCardY = kTop + 52, kBtnY = kTop + 124, kQrY = kTop + 186;
}

void FilesLink::opened() {
  if (!mounted_) {
    mounted_ = mount();  // a card may have gone in since boot
    if (mounted_) scan();
    updateDetail();
  }
  drawnRev_ = UINT32_MAX;
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

void FilesLink::draw(uint32_t, bool full) {
  drawnRev_ = rev_;
  clearContent();
  heading("Files");
  char a[64], b[64], f[16], t[16];
  tft.fillRoundRect(12, kCardY, W - 24, 62, 8, CARD);
  if (!enabled) {
    textOn("Files is off", 24, kCardY + 8, 4, TEXT, CARD);
    textOn("Turn it on in Links.", 24, kCardY + 38, 2, MUTED, CARD);
    return;
  }
  if (!mounted_) {
    textOn("No SD card", 24, kCardY + 8, 4, TEXT, CARD);
    textOn("Put in a FAT32 card, then open Files again.", 24, kCardY + 38, 2, MUTED, CARD);
    return;
  }
  dash::fmtBytes(total_ - used_, f, sizeof f);
  dash::fmtBytes(total_, t, sizeof t);
  snprintf(a, sizeof a, "%u file%s", files_, files_ == 1 ? "" : "s");
  snprintf(b, sizeof b, "%s free of %s", f, t);
  textOn(a, 24, kCardY + 8, 4, TEXT, CARD);
  textOn(b, 24, kCardY + 38, 2, MUTED, CARD);

  int bw = (W - 24 - 8) / 2;
  button(sharing_ ? "Stop sharing" : "Share", 12, kBtnY, bw, 52, !sharing_);
  button(uploads_ ? "Uploads: on" : "Uploads: off", 12 + bw + 8, kBtnY, bw, 52, false);

  if (!sharing_) {
    wrap("Share turns on a DASH-Share Wi-Fi with a new password. People nearby scan the code, then view, download and upload. Nobody but you can delete.",
         16, kQrY, W - 32, 2, MUTED, BG, 6);
    return;
  }
  char code[96];
  if (showLink_) snprintf(code, sizeof code, "http://%s/", WiFi.softAPIP().toString().c_str());
  else dash::wifiJoinCode(kSsid, pass_, code, sizeof code);
  drawQr(code, 12, kQrY, 160);
  int tx = 12 + 164, tw = W - tx - 12;
  int y = kQrY;
  text(showLink_ ? "2. Open the page" : "1. Join the Wi-Fi", tx, y, 2, TEXT);
  y += 22;
  if (showLink_) {
    snprintf(a, sizeof a, "http://%s", WiFi.softAPIP().toString().c_str());
    y = wrap(a, tx, y, tw, 2, GOLD, BG, 2);
  } else {
    y = wrap(kSsid, tx, y, tw, 2, MUTED, BG, 1);
    snprintf(a, sizeof a, "Password %s", pass_);
    y = wrap(a, tx, y, tw, 2, MUTED, BG, 2);
  }
  snprintf(a, sizeof a, "%u connected", clients_);
  text(a, tx, y + 6, 2, MUTED);
  navButton(showLink_ ? "Show Wi-Fi code" : "Show page link", 12, W - 24, false);
}

void FilesLink::tap(int x, int y, uint32_t now) {
  if (!enabled) return;
  if (!mounted_) {
    opened();
    draw(now, true);
    return;
  }
  int bw = (W - 24 - 8) / 2;
  if (hit(x, y, 12, kBtnY, bw, 52)) {
    if (sharing_) stopShare();
    else startShare();
  } else if (hit(x, y, 12 + bw + 8, kBtnY, bw, 52)) {
    uploads_ = !uploads_;
    rev_++;
  } else if (sharing_ && y >= kNavY) {
    showLink_ = !showLink_;
    rev_++;
  }
}
