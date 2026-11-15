#pragma once
// QR codes to show people: ecke.lt, a profile, the current slides. Each page
// is one code with its title and where it leads; Left/Right or a swipe pages,
// Back leaves. The list is /qr-codes.txt on the SD card ("Title | Link" or
// "Title | Description | Link" per line), created with examples on first use.
#include <string>
#include <vector>

#include "activities/Activity.h"

class QrCodesActivity final : public Activity {
 public:
  QrCodesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("QrCodes", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Someone is scanning: do not sleep away from the code.
  bool preventAutoSleep() override { return true; }

  struct Entry {
    std::string title;
    std::string description;
    std::string link;
  };
  // Parses the file's lines; blank lines and '#' comments are skipped.
  static std::vector<Entry> parse(const std::string& text);

 private:
  void step(int direction);

  std::vector<Entry> entries;
  int index = 0;
};
