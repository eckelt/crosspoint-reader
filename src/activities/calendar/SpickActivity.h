#pragma once
// Spick: notes sent from the chat (show_on_display) to read at a glance, e.g.
// a workshop agenda. They arrive through the display inbox as Markdown in
// /Spick, named by arrival so the newest sorts last. Opens on the newest note;
// Left/Right or swipe pages, Back shows the list, Confirm fetches new notes
// over Wi-Fi now. In the list a tap opens a note, two long presses delete it.
#include <string>
#include <vector>

#include "activities/Activity.h"

class SpickActivity final : public Activity {
 public:
  SpickActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Spick", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return open >= 0; }

  // One laid-out line of a note.
  struct Line {
    std::string text;
    uint8_t style;  // 0 text, 1 heading, 2 title
    int indent;
    int gapBefore;
  };

 private:
  void loadNotes();
  void openNote(int index);
  void layout();
  void fetchNow();

  std::vector<std::string> files;   // newest first
  std::vector<std::string> titles;
  int open = -1;                    // index into files, -1 = list
  std::vector<Line> lines;
  std::vector<int> pageStarts;
  int page = 0;
  int armedDelete = -1;             // a first long press marks the note
  bool fetching = false;
  std::string message;              // shown once, e.g. "Keine neuen Zettel"
  int rowsTop = 0, rowH = 0;
};
