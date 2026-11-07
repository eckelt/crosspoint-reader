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
  bool showNowLine = false;          // interactive view only; the sleep screen would go stale
};

// Tap target of a drawn event (day view), for the detail screen.
struct HitBox {
  int x, y, w, h;
  const CalEvent* event;
};

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
// Details of one event. Long notes continue on further pages: the first page
// passes firstNoteBlock -1 (title, time and fields, then notes), later pages
// the value the page before returned. Returns the first note block of the
// next page, or -1 on the last page.
int drawDetail(const GfxRenderer& r, const CalEvent& event, int top, int bottom, int firstNoteBlock);

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
