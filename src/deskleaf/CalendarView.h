#pragma once
// Drawing of the Deskleaf calendar views into the CrossPoint framebuffer
// (portrait 480x800, black/white with dithered grays). No panel refresh here.
#include <DeskleafCalendar.h>

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

// Full details of one event between `top` and `bottom`.
void drawDetail(const GfxRenderer& r, const CalEvent& event, int top, int bottom);

// Monday-based week containing `day`, one row per day.
void drawWeek(const GfxRenderer& r, const std::vector<CalEvent>& events, Date day, Date today, int top, int bottom);

// Month grid with ISO week numbers and event markers.
void drawMonth(const GfxRenderer& r, const std::vector<CalEvent>& events, int year, int month, Date today, int top,
               int bottom);

// Complete sleep screen: header, today's timeline and a footer with the sync time.
void drawSleepScreen(const GfxRenderer& r, const std::vector<CalEvent>& events, Date today, const Status& status);

}  // namespace deskleaf::view
