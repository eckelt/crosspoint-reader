#include "DeskleafCalendarService.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <sys/time.h>

#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <memory>

#include "CalendarView.h"
#include "DeskleafWifiSync.h"
#include "CrossPointSettings.h"
#include "util/TaskWatchdog.h"
#include "util/Timezones.h"
#include "fontIds.h"

namespace deskleaf {

const CalendarService CALENDAR;

namespace {

constexpr const char* kDir = "/.crosspoint/deskleaf";
constexpr const char* kWeeksDir = "/.crosspoint/deskleaf/weeks";
constexpr const char* kIndexPath = "/.crosspoint/deskleaf/weeks.idx";
constexpr const char* kIndexTmp = "/.crosspoint/deskleaf/weeks.idx.tmp";
// Single payload of builds before the week sync; removed after the first one.
constexpr const char* kLegacyPayloadPath = "/.crosspoint/deskleaf/calendar.json";
constexpr const char* kStatePath = "/.crosspoint/deskleaf/state.json";
// Battery readings of syncs that never reached the bridge: "epoch mV" lines.
constexpr const char* kHistoryPath = "/.crosspoint/deskleaf/battery.hist";
constexpr const char* kHistoryTmp = "/.crosspoint/deskleaf/battery.hist.tmp";
constexpr size_t kMaxHistory = 1000;  // ~10 days of 15-minute wakes
// Events the X4 Pro saw itself, sent as XACT: "epochMs type" lines.
constexpr const char* kActivityPath = "/.crosspoint/deskleaf/activity.hist";
constexpr const char* kActivityTmp = "/.crosspoint/deskleaf/activity.hist.tmp";
constexpr size_t kMaxActivities = 500;
constexpr size_t kMaxFile = 2 * 1024 * 1024;  // a week, the state or the legacy payload (PSRAM)
// First refresh after the reader goes to sleep; later ones follow the schedule.
constexpr uint32_t kFirstWakeMinutes = 2;
// Wi-Fi fallback (DeskleafWifiSync.h): at most hourly from timer wakes, last week to two weeks ahead.
constexpr int64_t kWifiFallbackIntervalSec = 60 * 60;
constexpr int kWifiWeeksBack = 1;
constexpr int kWifiWeeks = 4;

// The Monday `weeksOffset` weeks from today's week, as "YYYY-MM-DD".
std::string mondayIso(const Date today, const int weeksOffset) {
  return isoString(addDays(mondayOf(today), 7 * weeksOffset));
}

struct PsFree {
  void operator()(char* p) const { free(p); }
};
using PsBuffer = std::unique_ptr<char, PsFree>;

PsBuffer readFile(const char* path, size_t& len) {
  len = 0;
  HalFile f;
  if (!Storage.openFileForRead("DSK", path, f)) return PsBuffer();
  const size_t size = f.fileSize();
  if (size == 0 || size > kMaxFile) return PsBuffer();
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

bool writeFileAtomic(const char* path, const char* tmp, const char* data, size_t len) {
  HalFile f;
  if (!Storage.openFileForWrite("DSK", tmp, f)) return false;
  const size_t written = f.write(data, len);
  f.close();
  if (written != len) {
    Storage.remove(tmp);
    return false;
  }
  return Storage.replaceFile(tmp, path);
}

std::string weekPath(const std::string& monday) { return std::string(kWeeksDir) + "/" + monday + ".json"; }

// weeks.idx: one "YYYY-MM-DD HASH8" line per stored, non-empty week.
std::vector<WeekEntry> loadIndex() {
  std::vector<WeekEntry> index;
  size_t len = 0;
  PsBuffer buf = readFile(kIndexPath, len);
  if (!buf) return index;
  const char* p = buf.get();
  const char* end = p + len;
  while (p < end) {
    const char* nl = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
    const char* lineEnd = nl ? nl : end;
    if (lineEnd - p >= 19 && p[10] == ' ') {
      Date d{};
      const std::string monday(p, 10);
      if (parseIsoDate(monday, d)) {
        index.push_back({monday, static_cast<uint32_t>(strtoul(std::string(p + 11, 8).c_str(), nullptr, 16))});
      }
    }
    p = nl ? nl + 1 : end;
  }
  return index;
}

bool saveIndex(const std::vector<WeekEntry>& index) {
  std::string out;
  out.reserve(index.size() * 20);
  char line[32];
  for (const WeekEntry& w : index) {
    if (w.hash == 0) continue;
    snprintf(line, sizeof(line), "%s %08lX\n", w.monday.c_str(), static_cast<unsigned long>(w.hash));
    out += line;
  }
  return writeFileAtomic(kIndexPath, kIndexTmp, out.data(), out.size());
}

uint32_t indexHash(const std::vector<WeekEntry>& index, const std::string& monday) {
  for (const WeekEntry& w : index) {
    if (w.monday == monday) return w.hash;
  }
  return 0;
}

void setIndexHash(std::vector<WeekEntry>& index, const std::string& monday, uint32_t hash) {
  for (WeekEntry& w : index) {
    if (w.monday == monday) {
      w.hash = hash;
      return;
    }
  }
  index.push_back({monday, hash});
}

std::vector<BatteryReading> loadHistory() {
  std::vector<BatteryReading> history;
  size_t len = 0;
  PsBuffer buf = readFile(kHistoryPath, len);
  if (!buf) return history;
  const char* p = buf.get();
  const char* end = p + len;
  while (p < end) {
    char* next = nullptr;
    const long long epoch = strtoll(p, &next, 10);
    if (next == p) break;
    const unsigned long mv = strtoul(next, &next, 10);
    if (epoch > 1700000000LL && mv >= 1000 && mv <= 5000) {
      history.push_back({static_cast<int64_t>(epoch), static_cast<uint16_t>(mv)});
    }
    const char* nl = static_cast<const char*>(memchr(next, '\n', static_cast<size_t>(end - next)));
    p = nl ? nl + 1 : end;
  }
  return history;
}

void saveHistory(const std::vector<BatteryReading>& history) {
  if (history.empty()) {
    Storage.remove(kHistoryPath);
    return;
  }
  std::string out;
  out.reserve(history.size() * 16);
  char line[32];
  for (const BatteryReading& r : history) {
    snprintf(line, sizeof(line), "%lld %u\n", static_cast<long long>(r.epoch), static_cast<unsigned>(r.millivolts));
    out += line;
  }
  Storage.ensureDirectoryExists(kDir);
  writeFileAtomic(kHistoryPath, kHistoryTmp, out.data(), out.size());
}

int64_t nowMs() {
  struct timeval tv {};
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

std::vector<ActivityRecord> loadActivities() {
  std::vector<ActivityRecord> records;
  size_t len = 0;
  PsBuffer buf = readFile(kActivityPath, len);
  if (!buf) return records;
  const char* p = buf.get();
  const char* end = p + len;
  while (p < end) {
    char* next = nullptr;
    const long long ms = strtoll(p, &next, 10);
    if (next == p) break;
    const unsigned long type = strtoul(next, &next, 10);
    if (ms > 1700000000000LL && type >= 4 && type <= 6) {
      records.push_back({static_cast<int64_t>(ms), static_cast<ActivityType>(type)});
    }
    const char* nl = static_cast<const char*>(memchr(next, '\n', static_cast<size_t>(end - next)));
    p = nl ? nl + 1 : end;
  }
  return records;
}

void saveActivities(const std::vector<ActivityRecord>& records) {
  if (records.empty()) {
    Storage.remove(kActivityPath);
    return;
  }
  std::string out;
  char line[40];
  for (const ActivityRecord& r : records) {
    snprintf(line, sizeof(line), "%lld %u\n", static_cast<long long>(r.epochMs), static_cast<unsigned>(r.type));
    out += line;
  }
  Storage.ensureDirectoryExists(kDir);
  writeFileAtomic(kActivityPath, kActivityTmp, out.data(), out.size());
}

void noteActivity(const ActivityType type) {
  if (time(nullptr) < 1700000000) return;  // no clock, no useful time
  std::vector<ActivityRecord> records = loadActivities();
  records.push_back({nowMs(), type});
  if (records.size() > kMaxActivities) {
    records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(kMaxActivities));
  }
  saveActivities(records);
}

// The window around today that the device keeps; its set hash goes into WREQ.
std::vector<WeekEntry> windowOf(const Date today, const std::vector<WeekEntry>& index) {
  std::vector<WeekEntry> window;
  window.reserve(kWindowWeeks);
  const Date first = addDays(mondayOf(today), -7 * kWeeksBack);
  for (int i = 0; i < kWindowWeeks; ++i) {
    const std::string monday = isoString(addDays(first, 7 * i));
    window.push_back({monday, indexHash(index, monday)});
  }
  return window;
}

// Weeks that left the window: their files go, so the card does not fill up and
// old weeks do not reappear. True when anything was removed.
bool pruneOutside(const std::vector<WeekEntry>& window, std::vector<WeekEntry>& index) {
  bool removed = false;
  for (WeekEntry& w : index) {
    if (w.hash == 0) continue;
    bool inWindow = false;
    for (const WeekEntry& k : window) inWindow = inWindow || k.monday == w.monday;
    if (!inWindow) {
      Storage.remove(weekPath(w.monday).c_str());
      w.hash = 0;
      removed = true;
    }
  }
  return removed;
}

// Stores the frames of a WREQ answer and drops weeks outside the window.
// False when a frame or a file write failed; the stored weeks then no longer
// match the set hash, so the bridge resends everything next time.
bool applyWeekFrames(const char* body, size_t len, const std::vector<WeekEntry>& window,
                     std::vector<WeekEntry>& index) {
  std::vector<WeekFrame> frames;
  bool ok = parseWeekFrames(body, len, frames);
  if (!ok) LOG_ERR("DSK", "Malformed week frames after %u frames", static_cast<unsigned>(frames.size()));
  Storage.ensureDirectoryExists(kWeeksDir);
  for (const WeekFrame& frame : frames) {
    const std::string path = weekPath(frame.monday);
    if (frame.len == 0) {
      Storage.remove(path.c_str());
      setIndexHash(index, frame.monday, 0);
      continue;
    }
    std::vector<CalEvent> events;
    if (!parsePayload(frame.json, frame.len, events)) {
      LOG_ERR("DSK", "Week %s does not parse", frame.monday.c_str());
      ok = false;
      continue;
    }
    const std::string tmp = path + ".tmp";
    if (!writeFileAtomic(path.c_str(), tmp.c_str(), frame.json, frame.len)) {
      LOG_ERR("DSK", "Could not store week %s", frame.monday.c_str());
      setIndexHash(index, frame.monday, 0);
      ok = false;
      continue;
    }
    setIndexHash(index, frame.monday, weekHash(frame.json, frame.len));
  }
  pruneOutside(window, index);
  if (!saveIndex(index)) ok = false;
  return ok;
}

// The device clock keeps UTC until a zone is picked in Settings. While none
// was ever chosen, the bridge's zone (the Mac's) is adopted once and saved, so
// "today" and the sync time match the calendar the bridge sends.
void adoptBridgeTimezone(const char* posixTz) {
  if (!posixTz || !*posixTz) return;
  if (SETTINGS.clockTimezone != 255 || SETTINGS.clockUtcOffsetQ != 48) return;  // user's choice wins
  const TimezoneInfo* table = timezones::table();
  for (size_t i = 0; i < timezones::count(); ++i) {
    if (strcmp(table[i].posixTz, posixTz) == 0) {
      SETTINGS.clockTimezone = static_cast<uint8_t>(i);
      SETTINGS.saveToFile();
      timezones::applyToClock();
      LOG_INF("DSK", "Timezone set from bridge: %s", table[i].name);
      return;
    }
  }
  LOG_INF("DSK", "Bridge timezone %s not in the zone table", posixTz);
}

uint16_t batteryMillivolts() {
  const BatteryMonitor battery;
  return battery.readMillivolts();
}

// What the sleep screen shows for `today`: one day, or both weekend days.
uint32_t screenHash(const std::vector<CalEvent>& events, const Date today) {
  const std::string iso = isoString(today);
  if (!view::isWeekend(today)) return dayHash(eventsOnDate(events, iso), iso);
  const Date sat = addDays(today, 5 - weekdayMon0(today));
  const std::string satIso = isoString(sat);
  const std::string sunIso = isoString(addDays(sat, 1));
  // Sunday's hash is chained onto Saturday's, so the pair hashes in order.
  return dayHash(eventsOnDate(events, sunIso), sunIso) * 16777619u ^ dayHash(eventsOnDate(events, satIso), satIso) ^
         fnv1a(reinterpret_cast<const uint8_t*>(iso.data()), iso.size());
}

std::atomic<uint8_t> g_phase{0};

struct PhaseScope {
  PhaseScope() { g_phase = static_cast<uint8_t>(SyncPhase::Bluetooth); }
  ~PhaseScope() { g_phase = static_cast<uint8_t>(SyncPhase::Idle); }
};

view::Status viewStatus(const CalendarState& state) {
  view::Status s;
  s.lastSyncEpoch = state.lastSyncEpoch;
  s.lastAttemptEpoch = state.lastAttemptEpoch;
  s.via = state.lastVia == 1 ? "BT" : state.lastVia == 2 ? "WLAN" : nullptr;
  return s;
}

}  // namespace

SyncPhase syncPhase() { return static_cast<SyncPhase>(g_phase.load()); }

bool localToday(Date& out, struct tm* tmOut) {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;  // clock never set
  struct tm lt {};
  localtime_r(&now, &lt);
  out = dateFromTm(lt);
  if (tmOut) *tmOut = lt;
  return true;
}

bool CalendarService::loadState(CalendarState& state) const {
  state = CalendarState{};
  size_t len = 0;
  PsBuffer st = readFile(kStatePath, len);
  if (!st) return false;
  JsonDocument doc;
  if (deserializeJson(doc, st.get(), len)) return false;
  state.payloadHash = doc["payloadHash"] | 0u;
  state.sleepDayHash = doc["sleepDayHash"] | 0u;
  state.lastSyncEpoch = doc["lastSync"] | static_cast<int64_t>(0);
  state.lastStatus = doc["lastStatus"] | 0;
  state.firmware = doc["firmware"] | "";
  state.lastWifiEpoch = doc["lastWifi"] | static_cast<int64_t>(0);
  state.lastAttemptEpoch = doc["lastAttempt"] | static_cast<int64_t>(0);
  state.lastVia = doc["lastVia"] | 0;
  return true;
}

void CalendarService::loadRange(const Date first, const Date last, std::vector<CalEvent>& events) const {
  events.clear();
  const std::vector<WeekEntry> index = loadIndex();
  if (index.empty()) {
    // Before the first week sync (or with a bridge that only knows REQ), the
    // single payload of earlier builds still shows what it covers.
    size_t len = 0;
    PsBuffer legacy = readFile(kLegacyPayloadPath, len);
    if (legacy && parsePayload(legacy.get(), len, events)) return;
    events.clear();
  }
  const Date lastMonday = mondayOf(last);
  for (Date monday = mondayOf(first); isoString(monday) <= isoString(lastMonday); monday = addDays(monday, 7)) {
    const std::string key = isoString(monday);
    if (indexHash(index, key) == 0) continue;
    size_t len = 0;
    PsBuffer buf = readFile(weekPath(key).c_str(), len);
    std::vector<CalEvent> week;
    if (!buf || !parsePayload(buf.get(), len, week)) {
      LOG_ERR("DSK", "Stored week %s unreadable", key.c_str());
      continue;
    }
    appendUnique(events, week);
  }
  std::stable_sort(events.begin(), events.end(),
                   [](const CalEvent& a, const CalEvent& b) { return a.start < b.start; });
}

bool CalendarService::saveState(const CalendarState& state) const {
  JsonDocument doc;
  doc["payloadHash"] = state.payloadHash;
  doc["sleepDayHash"] = state.sleepDayHash;
  doc["lastSync"] = state.lastSyncEpoch;
  doc["lastStatus"] = state.lastStatus;
  doc["firmware"] = state.firmware;
  doc["lastWifi"] = state.lastWifiEpoch;
  doc["lastAttempt"] = state.lastAttemptEpoch;
  doc["lastVia"] = state.lastVia;
  String out;
  serializeJson(doc, out);
  Storage.ensureDirectoryExists(kDir);
  return Storage.writeFile(kStatePath, out);
}

SyncStatus CalendarService::sync(CalendarState& state, bool* forceRedraw, AbortCheck shouldAbort,
                                 char* bridgeTzOut, const WifiFallback wifi) const {
  const PhaseScope phase;
  state.lastAttemptEpoch = static_cast<int64_t>(time(nullptr));
  const auto takeTimezone = [&](const char* tz) {
    if (!tz || !*tz) return;  // a failed connection sends none; keep what we have
    if (!bridgeTzOut) {
      adoptBridgeTimezone(tz);
      return;
    }
    std::strncpy(bridgeTzOut, tz, 63);
    bridgeTzOut[63] = '\0';
  };
  char line[64];
  Date today{};
  if (!localToday(today)) {
    // The window is relative to today. Without a clock, a plain REQ fetches
    // the bridge's TIME first; its payload is not used. No battery field: the
    // bridge would file it under the ESP32-C3 display's telemetry.
    snprintf(line, sizeof(line), "REQ:00000000\n");
    SyncResult probe = syncWithBridge(line, shouldAbort);
    takeTimezone(probe.bridgeTz);
    freePayload(probe);
    if (probe.status == SyncStatus::Aborted) return probe.status;
    if (!localToday(today)) {
      state.lastStatus = static_cast<uint8_t>(probe.status);
      saveState(state);
      return probe.status;
    }
  }

  std::vector<WeekEntry> index = loadIndex();
  const std::vector<WeekEntry> window = windowOf(today, index);
  const uint16_t millivolts = batteryMillivolts();
  snprintf(line, sizeof(line), "WREQ:%08lX:%u:%s:%d\n", static_cast<unsigned long>(weekSetHash(window)),
           static_cast<unsigned>(millivolts), window.front().monday.c_str(), kWindowWeeks);
  // A new firmware since the last sync counts as installed now (an OTA
  // reboots into the calendar soon after); the first sync only notes it.
  if (state.firmware != CROSSPOINT_VERSION) {
    if (!state.firmware.empty()) noteActivity(ActivityType::FirmwareInstalled);
    state.firmware = CROSSPOINT_VERSION;
    saveState(state);  // an aborted sync must not count the install twice
  }
  // Before the WREQ: events the device saw itself (XACT), then readings of
  // syncs that missed the bridge (XHIS). An older bridge refuses the first
  // line; both lists then stay for later.
  std::vector<ActivityRecord> activities = loadActivities();
  const std::vector<std::string> activityLines = activityHistoryLines(activities);
  std::vector<BatteryReading> history = loadHistory();
  const std::vector<std::string> historyLines = batteryHistoryLines(history);
  std::vector<std::string> preamble = activityLines;
  preamble.insert(preamble.end(), historyLines.begin(), historyLines.end());
  SyncResult r = syncWithBridge(line, shouldAbort, 5, 45000, &preamble);
  const auto itemsIn = [](const std::vector<std::string>& lines, size_t count) {
    size_t items = 0;
    for (size_t i = 0; i < count && i < lines.size(); ++i) {
      items += static_cast<size_t>(std::count(lines[i].begin(), lines[i].end(), ';')) + 1;
    }
    return items;
  };
  const size_t activityLinesSent = std::min(r.preambleSent, activityLines.size());
  const size_t activitiesSent = itemsIn(activityLines, activityLinesSent);
  const size_t delivered = itemsIn(historyLines, r.preambleSent - activityLinesSent);
  if (activitiesSent > 0) {
    activities.erase(activities.begin(),
                     activities.begin() + static_cast<std::ptrdiff_t>(std::min(activitiesSent, activities.size())));
    saveActivities(activities);
  }
  const bool keepReading = !r.requestSent && r.status != SyncStatus::Aborted;
  if (delivered > 0 || keepReading) {
    history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(std::min(delivered, history.size())));
    // The WREQ carries the reading when it arrives; otherwise keep it for later.
    if (keepReading) history.push_back({static_cast<int64_t>(time(nullptr)), millivolts});
    if (history.size() > kMaxHistory) {
      history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(kMaxHistory));
    }
    saveHistory(history);
  }
  takeTimezone(r.bridgeTz);
  if (r.status == SyncStatus::BridgeNotFound) noteActivity(ActivityType::BridgeUnavailable);

  // No bridge: the same week frames over Wi-Fi from the Deskleaf worker, for the
  // weeks around today. The bridge resends any week that differs next time.
  const int64_t now = static_cast<int64_t>(time(nullptr));
  const bool bridgeMissing = r.status == SyncStatus::BridgeNotFound || r.status == SyncStatus::ConnectFailed;
  const bool wifiDue = wifi == WifiFallback::Now ||
                       (wifi == WifiFallback::Hourly && now - state.lastWifiEpoch >= kWifiFallbackIntervalSec);
  if (bridgeMissing && wifiDue && wifisync::configured()) {
    state.lastWifiEpoch = now;
    std::string body;
    SyncStatus viaWifi = SyncStatus::BridgeNotFound;
    g_phase = static_cast<uint8_t>(SyncPhase::Wifi);
    if (wifisync::join(shouldAbort)) {
      viaWifi = wifisync::fetchWeeks(mondayIso(today, -kWifiWeeksBack), kWifiWeeks, body);
    }
    wifisync::leave();
    LOG_INF("DSK", "Wi-Fi fallback: %s", syncStatusText(viaWifi));
    if (viaWifi == SyncStatus::NewPayload) {
      if (!applyWeekFrames(body.data(), body.size(), window, index)) viaWifi = SyncStatus::StoreFailed;
      freePayload(r);
      r = SyncResult{};
      r.status = viaWifi;
      if (forceRedraw) *forceRedraw = true;
      if (viaWifi == SyncStatus::NewPayload) {
        state.lastSyncEpoch = now;
        state.lastVia = 2;
        state.payloadHash = weekSetHash(windowOf(today, index));
        state.lastStatus = static_cast<uint8_t>(viaWifi);
        saveState(state);
        return viaWifi;
      }
    }
  }
  if (forceRedraw) *forceRedraw = r.forceRedraw;
  if (r.status == SyncStatus::NewPayload) {
    if (!applyWeekFrames(r.payload, r.payloadLen, window, index)) r.status = SyncStatus::StoreFailed;
    Storage.remove(kLegacyPayloadPath);
    LOG_INF("DSK", "Stored week frames, %u bytes", static_cast<unsigned>(r.payloadLen));
  } else if (r.status == SyncStatus::Unchanged && pruneOutside(window, index)) {
    saveIndex(index);
  }
  if (r.status == SyncStatus::NewPayload || r.status == SyncStatus::Unchanged) {
    state.lastSyncEpoch = static_cast<int64_t>(time(nullptr));
    state.lastVia = 1;
  }
  state.payloadHash = weekSetHash(windowOf(today, index));
  state.lastStatus = static_cast<uint8_t>(r.status);
  freePayload(r);
  if (r.status != SyncStatus::Aborted) saveState(state);
  return r.status;
}

void CalendarService::renderSleepScreen(GfxRenderer& renderer) const {
  std::vector<CalEvent> events;
  CalendarState state;
  loadState(state);
  Date today{};
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (!localToday(today)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, "Kalender: Uhrzeit noch nicht gesetzt");
  } else {
    // The week of today holds every event of today and of the weekend pair.
    loadRange(today, today, events);
    view::drawSleepScreen(renderer, events, today, viewStatus(state));
    state.sleepDayHash = screenHash(events, today);
    saveState(state);
    noteActivity(ActivityType::DisplayRefreshed);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool CalendarService::backgroundRefresh(GfxRenderer& renderer, const AbortCheck shouldAbort) const {
  CalendarState state;
  loadState(state);
  bool force = false;
  if (sync(state, &force, shouldAbort, nullptr, WifiFallback::Hourly) == SyncStatus::Aborted) return false;
  Date today{};
  if (!localToday(today)) return true;
  std::vector<CalEvent> events;
  loadRange(today, today, events);
  const uint32_t h = screenHash(events, today);
  if (!force && h == state.sleepDayHash) {
    LOG_INF("DSK", "Sleep screen unchanged");
    return true;
  }
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  view::drawSleepScreen(renderer, events, today, viewStatus(state));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  state.sleepDayHash = h;
  saveState(state);
  noteActivity(ActivityType::DisplayRefreshed);
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

namespace bgsync {
namespace {
// NimBLE client calls block on semaphores while the host task does the work;
// the body sits in PSRAM and each week's JSON document on the heap. The Wi-Fi
// fallback adds a TLS handshake on this stack, hence 16 KB (unmeasured: the
// worker logs its stack high-water mark after every sync).
constexpr uint32_t kStackBytes = 16384;

// Owned by the worker between start() and `done`; by the loop task otherwise.
struct Job {
  CalendarState state;
  SyncStatus status = SyncStatus::Unchanged;
  char bridgeTz[64] = "";
};
Job* job = nullptr;  // non-null from start() until collect()/stop()
std::atomic<bool> done{false};
std::atomic<bool> abortFlag{false};

void worker(void* param) {
  auto* j = static_cast<Job*>(param);
  // The user is looking: without the bridge, go to Wi-Fi right away.
  j->status = CALENDAR.sync(j->state, nullptr, [] { return abortFlag.load(); }, j->bridgeTz, WifiFallback::Now);
  LOG_DBG("DSK", "Sync task stack free: %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  done = true;  // the worker touches nothing after this
  vTaskDelete(nullptr);
}

void waitDone() {
  while (!done) {
    resetTaskWatchdogIfSubscribed();
    delay(10);
  }
}
}  // namespace

bool start(const CalendarState& from) {
  if (job) return false;
  job = new (std::nothrow) Job();
  if (!job) return false;
  job->state = from;
  done = false;
  abortFlag = false;
  if (xTaskCreate(&worker, "DeskleafSync", kStackBytes, job, 1, nullptr) != pdPASS) {
    LOG_ERR("DSK", "OOM: sync task");
    delete job;
    job = nullptr;
    return false;
  }
  return true;
}

bool running() { return job != nullptr; }

bool collect(CalendarState& state, SyncStatus& status) {
  if (!job || !done) return false;
  state = job->state;
  status = job->status;
  adoptBridgeTimezone(job->bridgeTz);
  delete job;
  job = nullptr;
  return true;
}

void stop() {
  if (!job) return;
  abortFlag = true;
  waitDone();
  delete job;
  job = nullptr;
}
}  // namespace bgsync

}  // namespace deskleaf

#endif  // DESKLEAF_CALENDAR
