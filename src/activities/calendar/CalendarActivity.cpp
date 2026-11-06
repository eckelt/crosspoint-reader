#include "CalendarActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "components/UITheme.h"
#include "deskleaf/CalendarView.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
// A sync on entry only when the stored data is older than this.
constexpr int64_t kAutoSyncAgeSeconds = 10 * 60;
constexpr unsigned long kSyncLongPressMs = 800;
}  // namespace

void CalendarActivity::onEnter() {
  Activity::onEnter();
  deskleaf::CALENDAR.load(events, state);
  clockValid = deskleaf::localToday(today);
  cursor = today;
  const int64_t now = static_cast<int64_t>(time(nullptr));
  syncPending = !clockValid || state.lastSyncEpoch == 0 || now - state.lastSyncEpoch > kAutoSyncAgeSeconds;
  requestUpdate();
}

void CalendarActivity::onExit() {
  events.clear();
  events.shrink_to_fit();
  Activity::onExit();
}

void CalendarActivity::syncNow() {
  // Blocking by design: the BLE round trip owns the radio and finishes within
  // the scan + transfer timeouts; the header shows the state meanwhile.
  syncing = true;
  requestUpdateAndWait();
  // A held long-press would otherwise cancel the sync right away.
  while (gpio.rawInputActive()) {
    resetTaskWatchdogIfSubscribed();
    delay(10);
  }
  std::vector<deskleaf::CalEvent> incoming;
  deskleaf::CalendarState newState = state;
  const auto status =
      deskleaf::CALENDAR.sync(incoming, newState, nullptr, [] { return gpio.rawInputActive(); });
  {
    RenderLock lock;
    if (status == deskleaf::SyncStatus::NewPayload) {
      events.swap(incoming);
      // Tap targets and the opened event point into the old vector.
      hits.clear();
      detailIndex = -1;
      if (view == View::Detail) view = View::Day;
    }
    state = newState;
  }
  syncing = false;
  // The press that cancelled the sync must not also act on the calendar.
  if (status == deskleaf::SyncStatus::Aborted) {
    while (gpio.rawInputActive()) {
      resetTaskWatchdogIfSubscribed();
      delay(10);
    }
    mappedInput.update();
  }
  const bool hadClock = clockValid;
  clockValid = deskleaf::localToday(today);
  if (!hadClock && clockValid) cursor = today;
  requestUpdate();
}

void CalendarActivity::step(const int direction) {
  switch (view) {
    case View::Detail:
      return;
    case View::Day:
      cursor = deskleaf::addDays(cursor, direction);
      break;
    case View::Week:
      cursor = deskleaf::addDays(cursor, 7 * direction);
      break;
    case View::Month: {
      int m = cursor.month + direction;
      int y = cursor.year;
      if (m < 1) {
        m = 12;
        --y;
      } else if (m > 12) {
        m = 1;
        ++y;
      }
      cursor = {y, m, std::min(cursor.day, deskleaf::daysInMonth(y, m))};
      break;
    }
  }
  requestUpdate();
}

void CalendarActivity::cycleView() {
  view = view == View::Day ? View::Week : view == View::Week ? View::Month : View::Day;
  detailIndex = -1;
  requestUpdate();
}

int CalendarActivity::contentTop() const { return headerHeight; }

void CalendarActivity::loop() {
  if (syncPending) {
    syncPending = false;
    syncNow();
    return;
  }

  using Button = MappedInputManager::Button;
  if (view == View::Detail) {
    // Back, Confirm or any tap closes the details and returns to the day.
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture() ||
        mappedInput.wasReleased(Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      view = View::Day;
      detailIndex = -1;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture()) {
    onGoHome();
    return;
  }
  if (mappedInput.wasLongPressed(Button::Confirm, kSyncLongPressMs)) {
    syncNow();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    cycleView();
    return;
  }
  if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up) ||
      mappedInput.wasReleased(Button::PageBack)) {
    step(-1);
    return;
  }
  if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down) ||
      mappedInput.wasReleased(Button::PageForward)) {
    step(+1);
    return;
  }

  if (!mappedInput.hasTouch()) return;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) {
    step(+1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) {
    step(-1);
    return;
  }
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenLongPress(tx, ty)) {
    syncNow();
    return;
  }
  if (!mappedInput.wasScreenTapped(tx, ty)) return;
  if (ty < headerHeight) {
    // Tapping the header returns to today in the day view, or cycles views when already there.
    if (view == View::Day && deskleaf::sameDate(cursor, today)) {
      cycleView();
    } else {
      view = View::Day;
      cursor = today;
      requestUpdate();
    }
    return;
  }
  if (view == View::Day) {
    int picked = -1;
    {
      RenderLock lock;  // the render task rebuilds `hits`
      for (const auto& hit : hits) {
        if (tx >= hit.x && tx < hit.x + hit.w && ty >= hit.y && ty < hit.y + hit.h) {
          picked = static_cast<int>(hit.event - events.data());
          break;
        }
      }
    }
    if (picked >= 0) {
      detailIndex = picked;
      view = View::Detail;
      requestUpdate();
    }
  } else if (view == View::Month) {
    // Same grid geometry as view::drawMonth.
    const int w = renderer.getScreenWidth();
    const int kwW = 34;
    const int colW = (w - kwW - 8) / 7;
    const int headH = renderer.getLineHeight(SMALL_FONT_ID) + 6;
    const deskleaf::Date first{cursor.year, cursor.month, 1};
    const int lead = deskleaf::weekdayMon0(first);
    const int rows = (lead + deskleaf::daysInMonth(cursor.year, cursor.month) + 6) / 7;
    const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
    const int cellH = (bottom - headerHeight - headH) / rows;
    const int col = (tx - kwW) / colW;
    const int row = (ty - headerHeight - headH) / cellH;
    if (tx >= kwW && col >= 0 && col < 7 && row >= 0 && row < rows) {
      const deskleaf::Date d = deskleaf::addDays(first, row * 7 + col - lead);
      if (d.month == cursor.month) {
        cursor = d;
        view = View::Day;
        requestUpdate();
      }
    }
  } else if (view == View::Week) {
    const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
    const int rowH = (bottom - headerHeight) / 7;
    const int row = (ty - headerHeight) / rowH;
    if (row >= 0 && row < 7) {
      cursor = deskleaf::addDays(cursor, row - deskleaf::weekdayMon0(cursor));
      view = View::Day;
      requestUpdate();
    }
  }
}

void CalendarActivity::render(RenderLock&&) {
  namespace view_ns = deskleaf::view;
  renderer.clearScreen();

  view_ns::Status status;
  status.lastSyncEpoch = state.lastSyncEpoch;
  status.showNowLine = true;
  if (syncing) {
    status.statusText = "Synchronisiere…";
  } else if (state.lastStatus != static_cast<uint8_t>(deskleaf::SyncStatus::Unchanged) &&
             state.lastStatus != static_cast<uint8_t>(deskleaf::SyncStatus::NewPayload) && state.lastSyncEpoch == 0) {
    status.statusText = deskleaf::syncStatusText(static_cast<deskleaf::SyncStatus>(state.lastStatus));
  }

  char title[40];
  char subtitle[48];
  switch (view) {
    case View::Day:
      std::snprintf(title, sizeof(title), "%s", deskleaf::weekdayNameDe(deskleaf::weekdayMon0(cursor)));
      std::snprintf(subtitle, sizeof(subtitle), "%d. %s %d · KW %d", cursor.day, deskleaf::monthNameDe(cursor.month),
                    cursor.year, deskleaf::isoWeek(cursor));
      break;
    case View::Week: {
      const deskleaf::Date mon = deskleaf::addDays(cursor, -deskleaf::weekdayMon0(cursor));
      const deskleaf::Date sun = deskleaf::addDays(mon, 6);
      std::snprintf(title, sizeof(title), "KW %d", deskleaf::isoWeek(cursor));
      std::snprintf(subtitle, sizeof(subtitle), "%d.%d. – %d.%d.%d", mon.day, mon.month, sun.day, sun.month,
                    sun.year);
      break;
    }
    case View::Month:
      std::snprintf(title, sizeof(title), "%s", deskleaf::monthNameDe(cursor.month));
      std::snprintf(subtitle, sizeof(subtitle), "%d", cursor.year);
      break;
    case View::Detail:
      std::snprintf(title, sizeof(title), "Termin");
      std::snprintf(subtitle, sizeof(subtitle), "%s, %d. %s", deskleaf::weekdayNameDe(deskleaf::weekdayMon0(cursor)),
                    cursor.day, deskleaf::monthNameDe(cursor.month));
      break;
  }

  headerHeight = view_ns::drawHeader(renderer, title, subtitle, status);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;

  if (!clockValid) {
    renderer.drawCenteredText(UI_10_FONT_ID, (headerHeight + bottom) / 2,
                              "Uhrzeit unbekannt – Bridge auf dem Mac starten", true);
  } else if (view == View::Detail && detailIndex >= 0 && detailIndex < static_cast<int>(events.size())) {
    view_ns::drawDetail(renderer, events[detailIndex], headerHeight, bottom);
  } else if (view == View::Day || view == View::Detail) {
    view_ns::drawDay(renderer, events, cursor, today, status, headerHeight, bottom, &hits);
  } else if (view == View::Week) {
    view_ns::drawWeek(renderer, events, cursor, today, headerHeight, bottom);
  } else {
    view_ns::drawMonth(renderer, events, cursor.year, cursor.month, today, headerHeight, bottom);
  }

  const char* nextView = view == View::Day ? "Woche" : view == View::Week ? "Monat" : "Tag";
  const auto labels = view == View::Detail ? mappedInput.mapLabels(tr(STR_BACK), "", "", "")
                                           : mappedInput.mapLabels(tr(STR_BACK), nextView, "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
