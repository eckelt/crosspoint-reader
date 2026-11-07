#pragma once
// Deskleaf calendar: day, week and month views of the payload the Deskleaf
// bridge sends over BLE. Left/Right (or swipes) page, Confirm (or a tap on the
// header) switches the view, a long press on Confirm or the screen syncs, Back
// goes home. In the month view a tap on a day opens that day. The sync
// runs on a worker task, so the stored calendar stays usable meanwhile.
#include <DeskleafCalendar.h>

#include <vector>

#include "activities/Activity.h"
#include "deskleaf/CalendarView.h"
#include "deskleaf/DeskleafCalendarService.h"

class CalendarActivity final : public Activity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calendar", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return syncing; }

 private:
  enum class View : uint8_t { Day, Week, Month, Detail };

  void step(int direction);
  void cycleView();
  void startSync();
  void finishSync();
  // Events are loaded per view: the cursor's week, or the weeks of its month.
  void rangeFor(deskleaf::Date& first, deskleaf::Date& last) const;
  void ensureLoaded(bool force);
  void show();  // ensureLoaded + requestUpdate after a view or cursor change
  int contentTop() const;

  std::vector<deskleaf::CalEvent> events;
  deskleaf::CalendarState state;
  View view = View::Day;
  deskleaf::Date today{2026, 1, 1};
  deskleaf::Date cursor{2026, 1, 1};  // shown day; week and month derive from it
  bool clockValid = false;
  bool loadedValid = false;
  deskleaf::Date loadedFirst{2026, 1, 1};
  deskleaf::Date loadedLast{2026, 1, 1};
  bool syncing = false;
  bool syncPending = false;

  int headerHeight = 0;
  // Day view tap targets (rebuilt on every day render) and the opened event.
  std::vector<deskleaf::view::HitBox> hits;
  int detailIndex = -1;  // index into `events`
  // Pages of the opened event: first note block of each page (-1 = page 1 with
  // the fields). detailNext is what the render of the current page reported.
  std::vector<int> detailPages{-1};
  int detailPage = 0;
  int detailNext = -1;
};
