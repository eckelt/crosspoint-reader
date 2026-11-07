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
  uint32_t payloadHash = 0;    // semantic hash of the stored payload (sent in REQ)
  uint32_t sleepDayHash = 0;   // what the sleep screen currently shows
  int64_t lastSyncEpoch = 0;   // last successful contact with the bridge
  uint8_t lastStatus = 0;      // SyncStatus of the last attempt
};

class CalendarService {
 public:
  // Loads the stored payload and state from SD. Cheap when nothing is stored.
  bool load(std::vector<CalEvent>& events, CalendarState& state) const;
  bool saveState(const CalendarState& state) const;

  // One BLE round trip. On a new payload it is stored, `events` replaced and
  // state.payloadHash updated. Blocks for up to ~50 s (scan + transfer).
  // The bridge's timezone is adopted right away, or copied to bridgeTzOut
  // (64 bytes) for a caller that must apply it on another task.
  SyncStatus sync(std::vector<CalEvent>& events, CalendarState& state, bool* forceRedraw = nullptr,
                  AbortCheck shouldAbort = nullptr, char* bridgeTzOut = nullptr) const;

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
// True once the worker has finished: moves its result out and releases it.
// The bridge's timezone is adopted here, on the calling task.
bool collect(std::vector<CalEvent>& events, CalendarState& state, SyncStatus& status);
// Aborts a running sync, waits for the worker and drops its result.
void stop();
}  // namespace bgsync

// Current local date, or false when the clock is not set yet.
bool localToday(Date& out, struct tm* tmOut = nullptr);

extern const CalendarService CALENDAR;

}  // namespace deskleaf
