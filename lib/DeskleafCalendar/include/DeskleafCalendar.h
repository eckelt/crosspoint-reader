#pragma once
// Deskleaf calendar model: the JSON payload the Deskleaf bridge (macOS) sends
// over BLE, the hashes that decide whether anything changed, the overlap
// layout of a day timeline and the wake schedule of the sleep-screen calendar.
//
// Plain C++ without Arduino types, so the logic runs in host tests
// (test/deskleaf_calendar). The payload contract is calendar.payload.v1 from
// the deskleaf-display repository; the semantic hash must stay bit-identical
// to einkalSemanticHash() in the Swift bridge.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace deskleaf {

enum class MeetingPlatform : uint8_t { None, Teams, Meet, Ne, LinkedIn };

struct CalEvent {
  // Raw payload fields (also the input of the semantic hash).
  std::string start;  // "YYYY-MM-DDTHH:MM:SS" local time
  std::string end;
  std::string subject;
  std::string location;
  std::string organizer;
  std::string notes;
  std::string importance;
  std::string meetingPlatformRaw;
  std::string calendar;
  bool hasAttachments = false;
  bool isRecurring = false;
  bool isMoved = false;
  bool isCancelled = false;
  bool isAllDay = false;
  bool hasObsidianNote = false;
  int numAttendees = 0;

  // Derived for display.
  MeetingPlatform platform() const;
  bool isImportant() const { return importance == "high"; }
  // Location without the bridge's room prefixes ("DE-", "; " ...).
  std::string displayLocation() const;
  int startMinutes() const;  // minutes after local midnight, -1 if unparsable
  int endMinutes() const;    // falls back to start + 60
  std::string startDate() const { return start.size() >= 10 ? start.substr(0, 10) : std::string(); }
};

// Parses the payload array. Unknown fields are ignored. Returns false on a JSON
// error; `out` is then left empty.
bool parsePayload(const char* json, size_t len, std::vector<CalEvent>& out);

// FNV-1a over the display-relevant fields, sorted, as the bridge computes it.
// Sent in REQ:<hash> so the bridge can answer SAME.
uint32_t semanticPayloadHash(const std::vector<CalEvent>& events);

// FNV-1a over the raw payload bytes (fallback when the payload does not parse).
uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t seed = 2166136261u);

// Events whose start falls on `isoDate` ("YYYY-MM-DD"), timed events sorted by
// start, all-day events first.
std::vector<const CalEvent*> eventsOnDate(const std::vector<CalEvent>& events, const std::string& isoDate);

// Hash of what one day shows; a sleep-screen redraw is needed only when it changes.
uint32_t dayHash(const std::vector<const CalEvent*>& dayEvents, const std::string& isoDate);

// --- Day layout ----------------------------------------------------------

struct LayoutBox {
  size_t eventIndex;  // index into the input vector
  int column;         // leftmost column
  int groupColumns;   // columns of the overlap group
  int colSpan;        // >= 1
  int startMin;
  int endMin;
};

struct LayoutInput {
  int startMin;
  int endMin;
};

// Overlapping events share equal-width columns; an event widens to the right
// while the neighbouring column is free for its whole time span.
std::vector<LayoutBox> layoutDay(const std::vector<LayoutInput>& inputs);

// --- Dates ---------------------------------------------------------------

struct Date {
  int year;
  int month;  // 1..12
  int day;    // 1..31
};

Date dateFromTm(const struct tm& t);
Date addDays(Date d, int days);
int daysInMonth(int year, int month);
int weekdayMon0(Date d);  // 0 = Monday .. 6 = Sunday
int isoWeek(Date d);
std::string isoString(Date d);  // "YYYY-MM-DD"
bool sameDate(Date a, Date b);

// German labels for the UI.
const char* weekdayNameDe(int mon0);
const char* weekdayShortDe(int mon0);
const char* monthNameDe(int month);

// --- Sleep-screen wake schedule ------------------------------------------

// Minutes until the next background refresh, given local time `now`.
// Workdays 07:00-19:00 every `dayIntervalMin`; otherwise until 07:00 (or
// 00:05 for the date change); weekends once at noon and after midnight.
uint32_t nextWakeMinutes(const struct tm& now, uint32_t dayIntervalMin = 15);

}  // namespace deskleaf
