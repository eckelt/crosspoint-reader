#pragma once
// Drawing of the Deskleaf calendar views into the CrossPoint framebuffer
// (portrait 480x800, black/white with dithered grays). No panel refresh here.
#include <DeskleafCalendar.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class GfxRenderer;

namespace deskleaf::view {

struct Status {
  int64_t lastSyncEpoch = 0;
  const char* statusText = nullptr;  // e.g. "Synchronisiere…"; nullptr = none
  uint8_t via = 0;                   // how the shown data arrived: 0 unknown, 1 Bluetooth, 2 Wi-Fi
  uint8_t active = 0;                // a sync running now over: 0 none, 1 Bluetooth, 2 Wi-Fi
  int64_t lastAttemptEpoch = 0;      // a later failed attempt is shown below the sync time
  bool showNowLine = false;          // interactive view only; the sleep screen would go stale
  bool peekNextDay = false;          // day view: a narrow column with the next working day on the right
};

// Tap target of a drawn event (day view), for the detail screen.
struct HitBox {
  int x, y, w, h;
  const CalEvent* event;  // nullptr: the peek column of the next working day
};

// The day the day view peeks at: the next day, skipping Saturday and Sunday.
Date peekDay(Date day);

// Header band shared by all views. Returns the y below it.
int drawHeader(const GfxRenderer& r, const char* title, const char* subtitle, const Status& status);

// Timeline of one day between `top` and `bottom`.
void drawDay(const GfxRenderer& r, const std::vector<CalEvent>& events, Date day, Date today, const Status& status,
             int top, int bottom, std::vector<HitBox>* hits = nullptr);

// Saturday and Sunday side by side on a shared hour axis; drawDay uses it for
// any weekend day.
void drawWeekend(const GfxRenderer& r, const std::vector<CalEvent>& events, Date anyWeekendDay, Date today,
                 const Status& status, int top, int bottom, std::vector<HitBox>* hits = nullptr);

bool isWeekend(Date d);

// Free-day motif plus caption, filling [top, bottom).
void drawFreeDay(const GfxRenderer& r, const char* caption, int top, int bottom);

// Header texts for a day ("Freitag" / "6. November 2026 · KW 45"), or for the
// weekend it belongs to ("Wochenende" / "7.–8. November · KW 45").
void dayTitle(Date day, char* title, size_t titleSize, char* subtitle, size_t subtitleSize, bool withYear);

// Full details of one event between `top` and `bottom`.
// Fades what is on screen: every other dot white.
void fadeScreen(const GfxRenderer& r);

// The popup for one event over the (faded) view drawn before: when and
// calendar, title, icon chips, place, organizer and a notes preview; as tall
// as its content. Returns the card's rectangle for tap tests.
HitBox drawPopup(const GfxRenderer& r, const CalEvent& event, int top, int bottom);

// The full detail page with its own header (calendar · date, title, time).
// Long notes continue on further pages: the first page passes
// firstNoteBlock -1, later pages the value the page before returned.
// Returns the first note block of the next page, or -1 on the last page.
int drawDetail(const GfxRenderer& r, const CalEvent& event, int bottom, int firstNoteBlock);

// Monday-based week containing `day`, one row per day.
void drawWeek(const GfxRenderer& r, const std::vector<CalEvent>& events, Date day, Date today, int top, int bottom);

// Month grid with ISO week numbers and event markers.
// Month grid: seven equal columns between these side margins (tap mapping uses it too).
constexpr int kMonthGridX = 4;
inline int monthColumnWidth(int screenW) { return (screenW - 2 * kMonthGridX) / 7; }

void drawMonth(const GfxRenderer& r, const std::vector<CalEvent>& events, int year, int month, Date today, int top,
               int bottom);

// Complete sleep screen: header, today's timeline and a footer with the sync time.
void drawSleepScreen(const GfxRenderer& r, const std::vector<CalEvent>& events, Date today, const Status& status);

}  // namespace deskleaf::view
