// Files: the microSD card, and a Wi-Fi share so people nearby can view,
// download and upload files from a browser. Built in, not a mod.
// Guests can't delete or overwrite anything. See docs/files.md.
#pragma once
#include <FS.h>
#include <stdint.h>

#include "connector.h"
#include "mod.h"
#include "mods/view.h"

class FilesLink : public dash::Connector, public dash::ModView {
 public:
  const char* name() const override { return "Files"; }
  void begin() override;
  void loop(uint32_t nowMs, bool netUp) override;
  dash::LinkState state() const override;
  const char* detail() const override { return detail_; }
  bool problem() const override { return false; }  // no card is a choice, not a fault

  bool homeCard(dash::HomeCard& c) const;

  // ModView
  void opened() override;
  bool dirty(uint32_t nowMs) override;
  void draw(uint32_t nowMs, bool full) override;
  void tap(int x, int y, uint32_t nowMs) override;
  void release(int x, int y, int dx, uint32_t nowMs) override;
  void closed() override;

  bool sharing() const { return sharing_; }
  bool startShare();
  void stopShare();
  bool uploadsOn() const { return uploads_; }
  void noteGuest();  // a guest did something: keeps the share from timing out
  void changed();    // a file was added
  void uploaded(const char* name);  // a guest uploaded this file
  void setUploads(bool on);
  void stopSoon();                  // stop sharing in a moment (after a reply)
  const char* adminKey() const { return admin_; }
  // The quick QR button: starts sharing if needed and shows the big code.
  void showQr();

 private:
  enum class Mode : uint8_t { Main, Qr, Viewer };
  bool mount();
  void scan();  // counts files, space and pictures; slow on big cards, so only on change
  void drawQr(const char* text, int x, int y, int maxPx);
  void updateDetail();
  void drawMain();
  void drawQrScreen();
  void drawViewer();
  void openViewer(int index);
  void step(int dir);
  void play();
  void stopVideo();
  void videoFrame(uint32_t now);
  int mediaIndex(const char* name) const;

  Mode mode_ = Mode::Main;
  static constexpr int kMaxMedia = 120;
  char media_[kMaxMedia][48];
  uint16_t nMedia_ = 0, at_ = 0;
  // The last upload, shown until Files is opened.
  char lastUpload_[64] = "";
  uint32_t lastUploadMs_ = 0;
  bool unseen_ = false;
  // Video
  fs::File video_;
  uint8_t* vbuf_ = nullptr;
  size_t vlen_ = 0;
  bool playing_ = false;
  uint32_t nextFrame_ = 0, frames_ = 0;
  char videoNote_[48] = "";

  bool mounted_ = false, sharing_ = false, uploads_ = true;
  uint8_t qrKind_ = 0;  // 0 Wi-Fi join, 1 guest page, 2 admin page
  char admin_[17] = "";  // owner key for this share session
  uint32_t stopAt_ = 0;
  uint16_t files_ = 0;
  uint64_t used_ = 0, total_ = 0;
  char pass_[12] = "";
  uint8_t clients_ = 0;
  uint32_t lastActive_ = 0, lastClientCheck_ = 0, rev_ = 0, drawnRev_ = UINT32_MAX;
  char detail_[56] = "";
};
