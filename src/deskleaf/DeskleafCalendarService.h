#pragma once
// Deskleaf calendar on CrossPoint: SD storage of the bridge payload, BLE sync,
// and the sleep-screen calendar with its timer-driven background refresh.
// Built only with -DDESKLEAF_CALENDAR=1 (x4pro profile).
#include <DeskleafCalendar.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "DeskleafBle.h"

class GfxRenderer;

namespace deskleaf {

struct CalendarState {
  uint32_t payloadHash = 0;    // set hash of the stored week window (last WREQ)
  uint32_t sleepDayHash = 0;   // what the sleep screen currently shows
  int64_t lastSyncEpoch = 0;   // last successful contact with the bridge
  uint8_t lastStatus = 0;      // SyncStatus of the last attempt
  std::string firmware;        // firmware at the last sync; a change is an install
  int64_t lastWifiEpoch = 0;   // last Wi-Fi fallback attempt (rate limit for timer wakes)
};

// When a sync may fall back to Wi-Fi (DeskleafWifiSync.h) after missing the bridge.
enum class WifiFallback : uint8_t {
  Never,
  Hourly,  // timer wakes: at most one attempt per hour
  Now,     // the user asked: open calendar, long press
};

class CalendarService {
 public:
  // Sync state from SD; false (and a default state) when none is stored.
  bool loadState(CalendarState& state) const;
  // Events of the stored weeks that overlap [first, last], in start order.
  // A multi-day event appears once even when several of those weeks list it.
  void loadRange(Date first, Date last, std::vector<CalEvent>& events) const;
  bool saveState(const CalendarState& state) const;

  // One WREQ round trip for the week window around today (DeskleafCalendar.h):
  // changed weeks are stored, weeks outside the window removed. Reload with
  // loadRange() afterwards. Blocks for the scan, the bridge's calendar fetch
  // and the transfer. The bridge's timezone is adopted right away, or copied
  // to bridgeTzOut (64 bytes) for a caller that applies it on another task.
  SyncStatus sync(CalendarState& state, bool* forceRedraw = nullptr, AbortCheck shouldAbort = nullptr,
                  char* bridgeTzOut = nullptr, WifiFallback wifi = WifiFallback::Never) const;

  // Sleep screen: draws today's view into the framebuffer and refreshes the panel.
  void renderSleepScreen(GfxRenderer& renderer) const;

  // Timer wake: sync, then redraw only if today's view changed. Returns false
  // when shouldAbort cancelled it (the user pressed a button).
  bool backgroundRefresh(GfxRenderer& renderer, AbortCheck shouldAbort) const;

  // Arms the RTC timer for the next background refresh. firstAfterSleep uses a
  // short delay so a stale screen is refreshed soon after the reader sleeps.
  void armWakeTimer(bool firstAfterSleep) const;
};

// The calendar app's sync on a worker task, so the stored calendar stays usable
// meanwhile. Only the loop task calls these. Anything else that drives the BLE
// radio (the page turner) must call bgsync::stop() before it does.
namespace bgsync {
bool start(const CalendarState& from);  // false when one runs or the task cannot start
bool running();                         // started and not yet collected or stopped
// True once the worker has finished: hands out its state and releases it.
// The bridge's timezone is adopted here, on the calling task.
bool collect(CalendarState& state, SyncStatus& status);
// Aborts a running sync, waits for the worker and drops its result.
void stop();
}  // namespace bgsync

// Current local date, or false when the clock is not set yet.
bool localToday(Date& out, struct tm* tmOut = nullptr);

extern const CalendarService CALENDAR;

}  // namespace deskleaf
