#include "DeskleafCalendarService.h"

#if DESKLEAF_CALENDAR

#include <ArduinoJson.h>
#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include <memory>

#include "CalendarView.h"
#include "fontIds.h"

namespace deskleaf {

const CalendarService CALENDAR;

namespace {

constexpr const char* kDir = "/.crosspoint/deskleaf";
constexpr const char* kPayloadPath = "/.crosspoint/deskleaf/calendar.json";
constexpr const char* kPayloadTmp = "/.crosspoint/deskleaf/calendar.json.tmp";
constexpr const char* kStatePath = "/.crosspoint/deskleaf/state.json";
constexpr size_t kMaxPayload = 200000;
// First refresh after the reader goes to sleep; later ones follow the schedule.
constexpr uint32_t kFirstWakeMinutes = 2;

struct PsFree {
  void operator()(char* p) const { free(p); }
};
using PsBuffer = std::unique_ptr<char, PsFree>;

PsBuffer readFile(const char* path, size_t& len) {
  len = 0;
  HalFile f;
  if (!Storage.openFileForRead("DSK", path, f)) return PsBuffer();
  const size_t size = f.fileSize();
  if (size == 0 || size > kMaxPayload) return PsBuffer();
  // Payload lives in PSRAM; the internal heap stays free for the reader.
  auto* buf = static_cast<char*>(heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) buf = static_cast<char*>(malloc(size + 1));
  if (!buf) return PsBuffer();
  const int n = f.read(buf, size);
  f.close();
  if (n != static_cast<int>(size)) {
    free(buf);
    return PsBuffer();
  }
  buf[size] = '\0';
  len = size;
  return PsBuffer(buf);
}

bool writePayload(const char* data, size_t len) {
  Storage.ensureDirectoryExists(kDir);
  HalFile f;
  if (!Storage.openFileForWrite("DSK", kPayloadTmp, f)) return false;
  const size_t written = f.write(data, len);
  f.close();
  if (written != len) {
    Storage.remove(kPayloadTmp);
    return false;
  }
  return Storage.replaceFile(kPayloadTmp, kPayloadPath);
}

uint16_t batteryMillivolts() {
  const BatteryMonitor battery;
  return battery.readMillivolts();
}

view::Status viewStatus(const CalendarState& state) {
  view::Status s;
  s.lastSyncEpoch = state.lastSyncEpoch;
  return s;
}

}  // namespace

bool localToday(Date& out, struct tm* tmOut) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // clock never set
  struct tm lt {};
  localtime_r(&now, &lt);
  out = dateFromTm(lt);
  if (tmOut) *tmOut = lt;
  return true;
}

bool CalendarService::load(std::vector<CalEvent>& events, CalendarState& state) const {
  state = CalendarState{};
  size_t len = 0;
  if (PsBuffer st = readFile(kStatePath, len)) {
    JsonDocument doc;
    if (!deserializeJson(doc, st.get(), len)) {
      state.payloadHash = doc["payloadHash"] | 0u;
      state.sleepDayHash = doc["sleepDayHash"] | 0u;
      state.lastSyncEpoch = doc["lastSync"] | static_cast<int64_t>(0);
      state.lastStatus = doc["lastStatus"] | 0;
    }
  }
  events.clear();
  PsBuffer payload = readFile(kPayloadPath, len);
  if (!payload) return false;
  if (!parsePayload(payload.get(), len, events)) {
    LOG_ERR("DSK", "Stored payload does not parse");
    state.payloadHash = 0;  // ask the bridge for a fresh copy
    return false;
  }
  return true;
}

bool CalendarService::saveState(const CalendarState& state) const {
  JsonDocument doc;
  doc["payloadHash"] = state.payloadHash;
  doc["sleepDayHash"] = state.sleepDayHash;
  doc["lastSync"] = state.lastSyncEpoch;
  doc["lastStatus"] = state.lastStatus;
  String out;
  serializeJson(doc, out);
  Storage.ensureDirectoryExists(kDir);
  return Storage.writeFile(kStatePath, out);
}

SyncStatus CalendarService::sync(std::vector<CalEvent>& events, CalendarState& state, bool* forceRedraw,
                                 AbortCheck shouldAbort) const {
  SyncResult r = syncWithBridge(state.payloadHash, batteryMillivolts(), shouldAbort);
  if (forceRedraw) *forceRedraw = r.forceRedraw;
  if (r.status == SyncStatus::NewPayload) {
    std::vector<CalEvent> fresh;
    if (parsePayload(r.payload, r.payloadLen, fresh)) {
      events.swap(fresh);
      if (writePayload(r.payload, r.payloadLen)) {
        state.payloadHash = semanticPayloadHash(events);
      } else {
        // Shown now; the old hash makes the bridge resend it next time.
        LOG_ERR("DSK", "Could not store payload");
      }
    } else {
      LOG_ERR("DSK", "Received payload does not parse");
      r.status = SyncStatus::Timeout;
    }
  }
  if (r.status == SyncStatus::NewPayload || r.status == SyncStatus::Unchanged) {
    state.lastSyncEpoch = static_cast<int64_t>(time(nullptr));
  }
  state.lastStatus = static_cast<uint8_t>(r.status);
  freePayload(r);
  if (r.status != SyncStatus::Aborted) saveState(state);
  return r.status;
}

void CalendarService::renderSleepScreen(GfxRenderer& renderer) const {
  std::vector<CalEvent> events;
  CalendarState state;
  load(events, state);
  Date today{};
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (!localToday(today)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, "Kalender: Uhrzeit noch nicht gesetzt");
  } else {
    view::drawSleepScreen(renderer, events, today, viewStatus(state));
    state.sleepDayHash = dayHash(eventsOnDate(events, isoString(today)), isoString(today));
    saveState(state);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool CalendarService::backgroundRefresh(GfxRenderer& renderer, const AbortCheck shouldAbort) const {
  std::vector<CalEvent> events;
  CalendarState state;
  load(events, state);
  bool force = false;
  if (sync(events, state, &force, shouldAbort) == SyncStatus::Aborted) return false;
  Date today{};
  if (!localToday(today)) return true;
  const std::string iso = isoString(today);
  const uint32_t h = dayHash(eventsOnDate(events, iso), iso);
  if (!force && h == state.sleepDayHash) {
    LOG_INF("DSK", "Sleep screen unchanged");
    return true;
  }
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  view::drawSleepScreen(renderer, events, today, viewStatus(state));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  state.sleepDayHash = h;
  saveState(state);
  LOG_INF("DSK", "Sleep screen redrawn");
  return true;
}

void CalendarService::armWakeTimer(const bool firstAfterSleep) const {
  uint32_t minutes = kFirstWakeMinutes;
  struct tm lt {};
  Date today{};
  if (!firstAfterSleep) minutes = localToday(today, &lt) ? nextWakeMinutes(lt) : 60;
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(minutes) * 60ULL * 1000000ULL);
  LOG_INF("DSK", "Next calendar refresh in %lu min", static_cast<unsigned long>(minutes));
}

}  // namespace deskleaf

#endif  // DESKLEAF_CALENDAR
