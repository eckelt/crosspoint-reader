#pragma once
// QR codes to show people: ecke.lt, a profile, the current slides. Each page
// is one code with its title and where it leads; Left/Right or a swipe pages,
// Back leaves. Confirm or the pencil edits a code (title, description, link;
// an empty link deletes it); the page after the last adds one. The list is
// /qr-codes.txt on the SD card ("Title | Link" or "Title | Description | Link"
// per line), created with examples on first use.
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
  void startEdit();
  void askNext();
  void finishEdit();
  void save() const;
  bool onAddPage() const { return index == static_cast<int>(entries.size()); }

  std::vector<Entry> entries;
  int index = 0;  // entries.size() = the page that adds one
  // Edit flow: the keyboard asks title, description and link in turn.
  enum class Ask : uint8_t { None, Title, Description, Link, Done };
  Ask ask = Ask::None;
  bool askPending = false;
  Entry draft;
};
