// Files: the microSD card, and a Wi-Fi share so people nearby can view,
// download and upload files from a browser. Built in, not a mod.
// Guests can't delete or overwrite anything. See docs/files.md.
#pragma once
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

  bool sharing() const { return sharing_; }
  bool startShare();
  void stopShare();
  bool uploadsOn() const { return uploads_; }
  void noteGuest();  // a guest did something: keeps the share from timing out
  void changed();    // a file was added

 private:
  bool mount();
  void scan();  // counts files and space; slow on big cards, so only on change
  void drawQr(const char* text, int x, int y, int maxPx);
  void updateDetail();

  bool mounted_ = false, sharing_ = false, showLink_ = false, uploads_ = true;
  uint16_t files_ = 0;
  uint64_t used_ = 0, total_ = 0;
  char pass_[12] = "";
  uint8_t clients_ = 0;
  uint32_t lastActive_ = 0, lastClientCheck_ = 0, rev_ = 0, drawnRev_ = UINT32_MAX;
  char detail_[56] = "";
};
