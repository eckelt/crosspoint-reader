#include "TodayActivity.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "components/UITheme.h"
#include "deskleaf/DeskleafCalendarService.h"
#include "deskleaf/DeskleafTodos.h"
#include "fontIds.h"

namespace {
constexpr int kMargin = 20;
constexpr int kBox = 22;
constexpr int kTop = 24;
constexpr unsigned long kSyncDelayMs = 4000;  // wait for more taps before joining Wi-Fi

const char* bucketTitle(const deskleaf::feed::TodoBucket b) {
  switch (b) {
    case deskleaf::feed::TodoBucket::Overdue:
      return "\xC3\x9C" "berf\xC3\xA4llig";
    case deskleaf::feed::TodoBucket::Today:
      return "Heute";
    case deskleaf::feed::TodoBucket::ThisWeek:
      return "Diese Woche";
  }
  return "";
}

// "2026-11-28" -> "Sa 28.11."
std::string headerDate(const std::string& iso) {
  static const char* kDays[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  if (iso.size() != 10) return {};
  const deskleaf::Date d{std::atoi(iso.substr(0, 4).c_str()), std::atoi(iso.substr(5, 2).c_str()),
                         std::atoi(iso.substr(8, 2).c_str())};
  return std::string(kDays[deskleaf::weekdayMon0(d)]) + " " + deskleaf::feed::shortGermanDate(iso);
}
}  // namespace

void TodayActivity::onEnter() {
  Activity::onEnter();
  reload();
  // An old list or waiting taps: fetch right away, the stored list shows meanwhile.
  deskleaf::Date today{};
  if (deskleaf::localToday(today) && deskleaf::todos::wanted(deskleaf::isoString(today))) syncSoon(0);
  requestUpdate();
}

void TodayActivity::reload() {
  hasList = deskleaf::todos::load(list);
  layout();
}

void TodayActivity::layout() {
  rows.clear();
  pageStarts.clear();
  page = 0;
  const int w = renderer.getScreenWidth();
  const int textW = w - 2 * kMargin - kBox - 12 - 56;  // room for the due date on the right
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  for (const auto bucket : {deskleaf::feed::TodoBucket::Overdue, deskleaf::feed::TodoBucket::Today,
                            deskleaf::feed::TodoBucket::ThisWeek}) {
    bool any = false;
    for (size_t i = 0; i < list.items.size(); ++i) {
      if (list.items[i].bucket != bucket) continue;
      if (!any) {
        rows.push_back({-1, 0, renderer.getLineHeight(UI_12_FONT_ID) + 14, {bucketTitle(bucket)}});
        any = true;
      }
      Row row{static_cast<int>(i), 0, 0, renderer.wrappedText(UI_10_FONT_ID, list.items[i].text.c_str(), textW, 3)};
      if (row.lines.empty()) row.lines.push_back(list.items[i].text);
      row.h = std::max(kBox + 14, static_cast<int>(row.lines.size()) * lh + 14);
      rows.push_back(row);
    }
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = kTop + renderer.getLineHeight(UI_12_FONT_ID) + 20;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 30;
  int y = top;
  pageStarts.push_back(0);
  for (size_t i = 0; i < rows.size(); ++i) {
    // A heading never ends a page.
    const int need = rows[i].h + (rows[i].item < 0 && i + 1 < rows.size() ? rows[i + 1].h : 0);
    if (y + need > bottom && static_cast<int>(i) > pageStarts.back()) {
      pageStarts.push_back(static_cast<int>(i));
      y = top;
    }
    rows[i].y = y;
    y += rows[i].h;
  }
}

void TodayActivity::syncSoon(const unsigned long delayMs) { syncAt = millis() + delayMs + 1; }

void TodayActivity::loop() {
  using B = MappedInputManager::Button;
  if (syncing && deskleaf::todos::background::finished()) {
    syncing = false;
    reload();
    requestUpdate();
  }
  if (!syncing && syncAt != 0 && static_cast<long>(millis() - syncAt) >= 0) {
    syncAt = 0;
    syncing = deskleaf::todos::background::start(/*inbox=*/false, /*todos=*/true);
    requestUpdate();
  }
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    onGoHome(HomeMenuItem::TODAY);
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    syncSoon(0);
    return;
  }
  const int pages = static_cast<int>(pageStarts.size());
  int step = 0;
  if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) || mappedInput.wasReleased(B::PageForward)) {
    step = 1;
  }
  if (mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::PageBack)) {
    step = -1;
  }
  if (mappedInput.hasTouch()) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) step = 1;
    if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) step = -1;
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      const int first = pageStarts.empty() ? 0 : pageStarts[page];
      const int last = page + 1 < pages ? pageStarts[page + 1] : static_cast<int>(rows.size());
      for (int r = first; r < last; ++r) {
        if (rows[r].item < 0 || ty < rows[r].y || ty >= rows[r].y + rows[r].h) continue;
        if (deskleaf::todos::toggle(list, list.items[rows[r].item].id)) {
          syncSoon(kSyncDelayMs);
          requestUpdate();
        }
        break;
      }
    }
  }
  if (step != 0 && page + step >= 0 && page + step < pages) {
    page += step;
    requestUpdate();
  }
}

void TodayActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const std::string title = hasList ? "Heute \xC2\xB7 " + headerDate(list.date) : std::string("Heute");
  renderer.drawText(UI_12_FONT_ID, kMargin, kTop, title.c_str(), true, EpdFontFamily::BOLD);
  const int lineY = kTop + renderer.getLineHeight(UI_12_FONT_ID) + 12;
  renderer.drawLine(kMargin, lineY, w - kMargin, lineY, true);

  if (!hasList) {
    renderer.drawText(UI_10_FONT_ID, kMargin, lineY + 20,
                      syncing ? "Hole die Liste\xE2\x80\xA6" : "Noch keine Liste. Confirm holt sie per WLAN.", true);
  } else if (list.items.empty()) {
    renderer.drawText(UI_10_FONT_ID, kMargin, lineY + 20, "Nichts offen f\xC3\xBCr heute.", true);
  }

  const int pages = static_cast<int>(pageStarts.size());
  const int first = pageStarts.empty() ? 0 : pageStarts[page];
  const int last = page + 1 < pages ? pageStarts[page + 1] : static_cast<int>(rows.size());
  for (int r = first; r < last; ++r) {
    const Row& row = rows[r];
    if (row.item < 0) {
      renderer.drawText(UI_12_FONT_ID, kMargin, row.y + 8, row.lines[0].c_str(), true, EpdFontFamily::BOLD);
      continue;
    }
    const auto& item = list.items[row.item];
    const int by = row.y + 7;
    renderer.drawRect(kMargin, by, kBox, kBox, 2, true);
    if (item.done) {
      for (int t = 0; t < 3; ++t) {
        renderer.drawLine(kMargin + 5, by + 11 + t, kMargin + 9, by + 15 + t, true);
        renderer.drawLine(kMargin + 9, by + 15 + t, kMargin + 18, by + 5 + t, true);
      }
    }
    const int tx = kMargin + kBox + 12;
    for (size_t i = 0; i < row.lines.size(); ++i) {
      const int ty = row.y + 7 + static_cast<int>(i) * lh;
      renderer.drawText(UI_10_FONT_ID, tx, ty, row.lines[i].c_str(), true);
      if (item.done) {
        const int sw = renderer.getTextWidth(UI_10_FONT_ID, row.lines[i].c_str());
        renderer.fillRect(tx, ty + lh / 2, sw, 2, true);
      }
    }
    // Due date only where it says something: overdue and later this week.
    if (!item.due.empty() && item.bucket != deskleaf::feed::TodoBucket::Today) {
      const std::string due = deskleaf::feed::shortGermanDate(item.due);
      const int dw = renderer.getTextWidth(SMALL_FONT_ID, due.c_str());
      renderer.drawText(SMALL_FONT_ID, w - kMargin - dw, row.y + 9, due.c_str(), true);
    }
    for (int x = tx; x < w - kMargin; x += 3) renderer.drawPixel(x, row.y + row.h - 1);
  }

  // Footer: what is waiting, the page.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int footY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 24;
  char foot[64] = "";
  const size_t waiting = deskleaf::todos::pending();
  if (syncing) {
    std::snprintf(foot, sizeof(foot), "Gleiche ab\xE2\x80\xA6");
  } else if (waiting > 0) {
    std::snprintf(foot, sizeof(foot), "%u %s aufs WLAN", static_cast<unsigned>(waiting),
                  waiting == 1 ? "Haken wartet" : "Haken warten");
  }
  if (pages > 1) {
    char pg[16];
    std::snprintf(pg, sizeof(pg), "  %d/%d", page + 1, pages);
    std::strncat(foot, pg, sizeof(foot) - std::strlen(foot) - 1);
  }
  if (foot[0]) renderer.drawCenteredText(SMALL_FONT_ID, footY, foot, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Abgleich", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
