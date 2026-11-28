#pragma once
// "Heute": the daily to-do list from the Deskleaf worker, grouped into
// Überfällig / Heute / Diese Woche. A tap strikes an item through (or opens it
// again); the change is stored at once and sent on the next Wi-Fi visit, which
// starts a few seconds after the last tap. Confirm syncs now, Left/Right page,
// Back goes home.
#include <string>
#include <vector>

#include "DeskleafFeed.h"
#include "activities/Activity.h"

class TodayActivity final : public Activity {
 public:
  TodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Today", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  struct Row {
    int item;        // index into list.items, -1 for a group heading
    int y, h;        // set while laying out
    std::vector<std::string> lines;
  };

 private:
  void reload();
  void layout();
  void syncSoon(unsigned long delayMs);

  deskleaf::feed::TodoList list;
  bool hasList = false;
  std::vector<Row> rows;
  std::vector<int> pageStarts;  // index into rows
  int page = 0;
  unsigned long syncAt = 0;     // millis() when a debounced sync should start, 0 = none
  bool syncing = false;
};
