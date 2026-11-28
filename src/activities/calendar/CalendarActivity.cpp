#include "CalendarActivity.h"

#include <HalStorage.h>
#include <esp_system.h>

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>

#include "components/UITheme.h"
#include "deskleaf/CalendarView.h"
#include "deskleaf/DeskleafAutoUpdate.h"
#include "deskleaf/DeskleafHirn.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

namespace {
// A sync on entry only when the stored data is older than this.
constexpr int64_t kAutoSyncAgeSeconds = 60 * 60;
constexpr unsigned long kSyncLongPressMs = 800;
}  // namespace

void CalendarActivity::onEnter() {
  Activity::onEnter();
  deskleaf::CALENDAR.loadState(state);
  clockValid = deskleaf::localToday(today);
  cursor = today;
  ensureLoaded(true);
  const int64_t now = static_cast<int64_t>(time(nullptr));
  syncPending = !clockValid || state.lastSyncEpoch == 0 || now - state.lastSyncEpoch > kAutoSyncAgeSeconds;
  requestUpdate();
}

void CalendarActivity::onExit() {
  deskleaf::bgsync::stop();
  syncing = false;
  events.clear();
  events.shrink_to_fit();
  Activity::onExit();
}

void CalendarActivity::startSync() {
  if (syncing) return;
  if (!deskleaf::bgsync::start(state)) return;
  syncing = true;
  shownPhase = deskleaf::SyncPhase::Bluetooth;
  requestUpdate();
}

void CalendarActivity::finishSync() {
  deskleaf::CalendarState newState;
  auto status = deskleaf::SyncStatus::Unchanged;
  if (!deskleaf::bgsync::collect(newState, status)) return;
  {
    RenderLock lock;
    state = newState;
  }
  syncing = false;
  const bool hadClock = clockValid;
  clockValid = deskleaf::localToday(today);
  if (!hadClock && clockValid) cursor = today;
  // StoreFailed may still have stored some weeks.
  const bool weeksChanged =
      status == deskleaf::SyncStatus::NewPayload || status == deskleaf::SyncStatus::StoreFailed;
  ensureLoaded(weeksChanged || !hadClock);
  if (view == View::Detail) loadHirn();  // the sync may have brought the Brain's notes
  requestUpdate();
  // A newer firmware the bridge announced: install it now, the calendar is idle.
  std::string version;
  if (deskleaf::CALENDAR.takeDueUpdate(state, version)) {
    requestUpdateAndWait();  // the current view first; install() then shows its notice
    bool installed = false;
    {
      RenderLock lock;  // install() draws its notice directly
      installed = deskleaf::autoupdate::install(&renderer, version, nullptr);
    }
    if (installed) {
      Storage.prepareForDeepSleep();
      esp_restart();
    }
    requestUpdate();  // failed: back to the calendar, retried in an hour
  }
}

void CalendarActivity::rangeFor(deskleaf::Date& first, deskleaf::Date& last) const {
  if (view == View::Month) {
    first = {cursor.year, cursor.month, 1};
    last = {cursor.year, cursor.month, deskleaf::daysInMonth(cursor.year, cursor.month)};
    return;
  }
  first = deskleaf::mondayOf(cursor);  // day, weekend, week and detail: the cursor's week
  last = deskleaf::addDays(first, 6);
  // The day view peeks at the next day, which after Sunday is next week's Monday.
  if (view != View::Week) {
    const deskleaf::Date next = deskleaf::view::peekDay(cursor);
    if (deskleaf::isoString(next) > deskleaf::isoString(last)) last = next;
  }
}

void CalendarActivity::ensureLoaded(const bool force) {
  deskleaf::Date first{};
  deskleaf::Date last{};
  rangeFor(first, last);
  if (!force && loadedValid && deskleaf::sameDate(first, loadedFirst) && deskleaf::sameDate(last, loadedLast)) return;
  std::vector<deskleaf::CalEvent> incoming;
  deskleaf::CALENDAR.loadRange(first, last, incoming);
  RenderLock lock;
  // Keep an opened event open if it is still there; indices change.
  int reopened = -1;
  const bool eventOpen = view == View::Detail || view == View::Popup || view == View::Note;
  if (eventOpen && detailIndex >= 0 && detailIndex < static_cast<int>(events.size())) {
    const auto& open = events[detailIndex];
    for (size_t i = 0; i < incoming.size(); ++i) {
      if (incoming[i].subject == open.subject && incoming[i].start == open.start) {
        reopened = static_cast<int>(i);
        break;
      }
    }
  }
  events.swap(incoming);
  hits.clear();
  detailIndex = reopened;
  // The notes may have changed: start the opened event on its first page again.
  detailPages.assign(1, -1);
  detailPage = 0;
  detailNext = -1;
  if (eventOpen && reopened < 0) view = View::Day;
  hasHirn = reopened >= 0 && deskleaf::hirn::forEvent(events[reopened], hirnEntry);
  linkBoxes.clear();
  loadedFirst = first;
  loadedLast = last;
  loadedValid = true;
}

void CalendarActivity::openDetailPage() {
  detailPages.assign(1, -1);
  detailPage = 0;
  detailNext = -1;
  loadHirn();
  view = View::Detail;
  show();
}

void CalendarActivity::loadHirn() {
  deskleaf::knowledge::Entry entry;
  const bool found =
      detailIndex >= 0 && detailIndex < static_cast<int>(events.size()) && deskleaf::hirn::forEvent(events[detailIndex], entry);
  RenderLock lock;
  hasHirn = found;
  hirnEntry = std::move(entry);
  linkBoxes.clear();
}

void CalendarActivity::openNote(const int link) {
  deskleaf::knowledge::Entry entry;
  if (link < 0 || link >= static_cast<int>(hirnEntry.links.size())) return;
  if (!deskleaf::hirn::note(hirnEntry.links[link], entry)) {
    entry.title = hirnEntry.links[link];  // listed but not fetched: show its name
  }
  {
    RenderLock lock;
    noteEntry = std::move(entry);
    notePages.assign(1, -1);
    notePage = 0;
    noteNext = -1;
  }
  view = View::Note;
  requestUpdate();
}

void CalendarActivity::show() {
  ensureLoaded(false);
  requestUpdate();
}

void CalendarActivity::step(const int direction) {
  switch (view) {
    case View::Popup:
    case View::Detail:
    case View::Note:
      return;
    case View::Day:
      if (deskleaf::view::isWeekend(cursor)) {
        // The weekend is one page: step to Friday or Monday.
        const deskleaf::Date sat = deskleaf::addDays(cursor, 5 - deskleaf::weekdayMon0(cursor));
        cursor = deskleaf::addDays(sat, direction > 0 ? 2 : -1);
      } else {
        cursor = deskleaf::addDays(cursor, direction);
      }
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
  show();
}

void CalendarActivity::cycleView() {
  view = view == View::Day ? View::Week : view == View::Week ? View::Month : View::Day;
  detailIndex = -1;
  show();
}

int CalendarActivity::contentTop() const { return headerHeight; }

void CalendarActivity::loop() {
  if (syncing) {
    // Bluetooth, then maybe Wi-Fi: the header says which.
    const deskleaf::SyncPhase phase = deskleaf::syncPhase();
    if (phase != deskleaf::SyncPhase::Idle && phase != shownPhase) {
      shownPhase = phase;
      requestUpdate();
    }
    finishSync();
  }
  if (syncPending) {
    syncPending = false;
    startSync();
  }

  using Button = MappedInputManager::Button;
  if (view == View::Popup) {
    // Back or a tap beside the card closes it; Confirm or a tap on it opens
    // the full page.
    int tx = 0;
    int ty = 0;
    const bool tapped = mappedInput.wasScreenTapped(tx, ty);
    deskleaf::view::HitBox box{};
    {
      RenderLock lock;
      box = popupBox;
    }
    const bool onCard = tapped && tx >= box.x && tx < box.x + box.w && ty >= box.y && ty < box.y + box.h;
    if (mappedInput.wasReleased(Button::Confirm) || onCard) {
      openDetailPage();
    } else if (tapped || mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture()) {
      view = View::Day;
      detailIndex = -1;
      show();
    }
    return;
  }
  if (view == View::Note) {
    // Back, Confirm or a tap returns to the event; Left/Right and swipes page.
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture() ||
        mappedInput.wasReleased(Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      view = View::Detail;
      requestUpdate();
      return;
    }
    const auto swipe = mappedInput.hasTouch() ? mappedInput.wasSwipe() : MappedInputManager::SwipeDir::None;
    const bool forward = mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down) ||
                         mappedInput.wasReleased(Button::PageForward) ||
                         swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up;
    const bool backward = mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up) ||
                          mappedInput.wasReleased(Button::PageBack) ||
                          swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down;
    int next = -1;
    {
      RenderLock lock;
      next = noteNext;
    }
    if (forward && next >= 0) {
      if (notePage + 1 >= static_cast<int>(notePages.size())) notePages.push_back(next);
      ++notePage;
      requestUpdate();
    } else if (backward && notePage > 0) {
      --notePage;
      requestUpdate();
    }
    return;
  }
  if (view == View::Detail) {
    // A tap on a linked note opens it. Back, Confirm or any other tap closes
    // the details; Left/Right and swipes page through long notes.
    int tx = 0;
    int ty = 0;
    const bool tapped = mappedInput.wasScreenTapped(tx, ty);
    if (tapped) {
      int link = -1;
      {
        RenderLock lock;
        for (const auto& b : linkBoxes) {
          if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h) link = b.link;
        }
      }
      if (link >= 0) {
        openNote(link);
        return;
      }
    }
    if (mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture() ||
        mappedInput.wasReleased(Button::Confirm) || tapped) {
      view = View::Day;
      detailIndex = -1;
      show();
      return;
    }
    const auto swipe = mappedInput.hasTouch() ? mappedInput.wasSwipe() : MappedInputManager::SwipeDir::None;
    const bool forward = mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down) ||
                         mappedInput.wasReleased(Button::PageForward) ||
                         swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up;
    const bool backward = mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up) ||
                          mappedInput.wasReleased(Button::PageBack) ||
                          swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down;
    int next = -1;
    {
      RenderLock lock;
      next = detailNext;
    }
    if (forward && next >= 0) {
      if (detailPage + 1 >= static_cast<int>(detailPages.size())) detailPages.push_back(next);
      ++detailPage;
      requestUpdate();
    } else if (backward && detailPage > 0) {
      --detailPage;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasBackGesture()) {
    onGoHome();
    return;
  }
  if (mappedInput.wasLongPressed(Button::Confirm, kSyncLongPressMs)) {
    startSync();
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
    startSync();
    return;
  }
  if (!mappedInput.wasScreenTapped(tx, ty)) return;
  if (ty < headerHeight) {
    // Tapping the header switches to the next view when today is already shown,
    // otherwise it returns to today in the current view.
    const auto weekMonday = [](deskleaf::Date d) { return deskleaf::addDays(d, -deskleaf::weekdayMon0(d)); };
    bool onToday = false;
    switch (view) {
      case View::Day:
        onToday = deskleaf::sameDate(cursor, today) ||
                  (deskleaf::view::isWeekend(cursor) && deskleaf::view::isWeekend(today) &&
                   deskleaf::sameDate(weekMonday(cursor), weekMonday(today)));
        break;
      case View::Week:
        onToday = deskleaf::sameDate(weekMonday(cursor), weekMonday(today));
        break;
      case View::Month:
        onToday = cursor.year == today.year && cursor.month == today.month;
        break;
      case View::Popup:
      case View::Detail:
      case View::Note:
        break;
    }
    cursor = today;
    if (onToday) {
      cycleView();
    } else {
      show();
    }
    return;
  }
  if (view == View::Day) {
    int picked = -1;
    {
      RenderLock lock;  // the render task rebuilds `hits`
      for (const auto& hit : hits) {
        if (tx >= hit.x && tx < hit.x + hit.w && ty >= hit.y && ty < hit.y + hit.h) {
          picked = hit.event ? static_cast<int>(hit.event - events.data()) : -2;
          break;
        }
      }
    }
    if (picked == -2) {  // the peek column: open that day
      cursor = deskleaf::view::peekDay(cursor);
      show();
    } else if (picked >= 0) {
      detailIndex = picked;
      view = View::Popup;
      show();
    }
  } else if (view == View::Month) {
    // Same grid geometry as view::drawMonth.
    const int colW = deskleaf::view::monthColumnWidth(renderer.getScreenWidth());
    const int gridX = deskleaf::view::kMonthGridX;
    const int headH = renderer.getLineHeight(SMALL_FONT_ID) + 6;
    const deskleaf::Date first{cursor.year, cursor.month, 1};
    const int lead = deskleaf::weekdayMon0(first);
    const int rows = (lead + deskleaf::daysInMonth(cursor.year, cursor.month) + 6) / 7;
    const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
    const int cellH = (bottom - headerHeight - headH) / rows;
    const int col = (tx - gridX) / colW;
    const int row = (ty - headerHeight - headH) / cellH;
    if (tx >= gridX && col >= 0 && col < 7 && row >= 0 && row < rows) {
      const deskleaf::Date d = deskleaf::addDays(first, row * 7 + col - lead);
      if (d.month == cursor.month) {
        cursor = d;
        view = View::Day;
        show();
      }
    }
  } else if (view == View::Week) {
    const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
    const int rowH = (bottom - headerHeight) / 7;
    const int row = (ty - headerHeight) / rowH;
    if (row >= 0 && row < 7) {
      cursor = deskleaf::addDays(cursor, row - deskleaf::weekdayMon0(cursor));
      view = View::Day;
      show();
    }
  }
}

void CalendarActivity::render(RenderLock&&) {
  namespace view_ns = deskleaf::view;
  renderer.clearScreen();

  view_ns::Status status;
  status.lastSyncEpoch = state.lastSyncEpoch;
  status.lastAttemptEpoch = state.lastAttemptEpoch;
  status.via = state.lastVia;
  status.wifiResult = state.lastWifiResult;
  const std::string notice = deskleaf::updateNotice(state);
  if (!notice.empty()) status.notice = notice.c_str();
  status.showNowLine = true;
  status.peekNextDay = true;
  if (syncing) {
    status.active = shownPhase == deskleaf::SyncPhase::Wifi ? 2 : 1;
  } else if (state.lastStatus != static_cast<uint8_t>(deskleaf::SyncStatus::Unchanged) &&
             state.lastStatus != static_cast<uint8_t>(deskleaf::SyncStatus::NewPayload) && state.lastSyncEpoch == 0) {
    status.statusText = deskleaf::syncStatusText(static_cast<deskleaf::SyncStatus>(state.lastStatus));
  }

  char title[40];
  char subtitle[48];
  switch (view) {
    case View::Day:
      view_ns::dayTitle(cursor, title, sizeof(title), subtitle, sizeof(subtitle), true);
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
    case View::Popup:
      view_ns::dayTitle(cursor, title, sizeof(title), subtitle, sizeof(subtitle), true);
      break;
    case View::Detail:
    case View::Note:
      title[0] = subtitle[0] = '\0';  // these pages draw their own header
      break;
  }

  // The detail page draws its own header; the others share the common one.
  headerHeight =
      view == View::Detail || view == View::Note ? 0 : view_ns::drawHeader(renderer, title, subtitle, status);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;

  if (!clockValid) {
    renderer.drawCenteredText(UI_10_FONT_ID, (headerHeight + bottom) / 2,
                              "Uhrzeit unbekannt – Bridge auf dem Mac starten", true);
  } else if (view == View::Detail && detailIndex >= 0 && detailIndex < static_cast<int>(events.size())) {
    const int page = std::min(detailPage, static_cast<int>(detailPages.size()) - 1);
    detailNext = view_ns::drawDetail(renderer, events[detailIndex], bottom, detailPages[page],
                                     hasHirn ? &hirnEntry : nullptr, page == 0 ? &linkBoxes : nullptr);
    if (page != 0) linkBoxes.clear();
  } else if (view == View::Note) {
    const int page = std::min(notePage, static_cast<int>(notePages.size()) - 1);
    noteNext = view_ns::drawNoteCard(renderer, noteEntry, bottom, notePages[page]);
  } else if (view == View::Day || view == View::Popup || view == View::Detail) {
    view_ns::drawDay(renderer, events, cursor, today, status, headerHeight, bottom, &hits);
    if (view == View::Popup && detailIndex >= 0 && detailIndex < static_cast<int>(events.size())) {
      popupBox = view_ns::drawPopup(renderer, events[detailIndex], headerHeight, bottom);
    }
  } else if (view == View::Week) {
    view_ns::drawWeek(renderer, events, cursor, today, headerHeight, bottom);
  } else {
    view_ns::drawMonth(renderer, events, cursor.year, cursor.month, today, headerHeight, bottom);
  }

  const char* nextView = view == View::Day ? "Woche" : view == View::Week ? "Monat" : "Tag";
  const auto labels = view == View::Detail || view == View::Note ? mappedInput.mapLabels(tr(STR_BACK), "", "<", ">")
                      : view == View::Popup ? mappedInput.mapLabels(tr(STR_BACK), "Details", "", "")
                                            : mappedInput.mapLabels(tr(STR_BACK), nextView, "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
