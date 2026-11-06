#include "DeskleafCalendar.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

namespace deskleaf {

namespace {

constexpr uint32_t kFnvPrime = 16777619u;

uint32_t fnvAdd(uint32_t h, const std::string& s) {
  for (const char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= kFnvPrime;
  }
  return h;
}

uint32_t fnvAddByte(uint32_t h, uint8_t b) {
  h ^= b;
  return h * kFnvPrime;
}

// CRLF -> LF and ASCII whitespace trimmed, like the bridge's text().
std::string normalizeHashText(const std::string& in) {
  std::string s;
  s.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\r' && i + 1 < in.size() && in[i + 1] == '\n') continue;
    s += in[i];
  }
  const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  size_t b = 0;
  size_t e = s.size();
  while (b < e && isSpace(s[b])) ++b;
  while (e > b && isSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

int parseHhMm(const std::string& iso) {
  const size_t t = iso.find('T');
  if (t == std::string::npos || iso.size() < t + 6) return -1;
  const int h = (iso[t + 1] - '0') * 10 + (iso[t + 2] - '0');
  const int m = (iso[t + 4] - '0') * 10 + (iso[t + 5] - '0');
  if (h < 0 || h > 24 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void replaceAll(std::string& s, const char* what, const char* with) {
  const size_t wl = std::strlen(what);
  const size_t rl = std::strlen(with);
  for (size_t pos = s.find(what); pos != std::string::npos; pos = s.find(what, pos + rl)) {
    s.replace(pos, wl, with);
  }
}

std::string str(JsonVariantConst v) {
  const char* p = v.as<const char*>();
  return p ? std::string(p) : std::string();
}

}  // namespace

uint32_t fnv1a(const uint8_t* data, size_t len, uint32_t seed) {
  uint32_t h = seed;
  for (size_t i = 0; i < len; ++i) h = fnvAddByte(h, data[i]);
  return h;
}

namespace {
std::string lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool hasAny(const std::string& haystack, std::initializer_list<const char*> needles) {
  for (const char* n : needles) {
    if (haystack.find(n) != std::string::npos) return true;
  }
  return false;
}

// True if word appears in s on its own, not inside a longer word ("Call", not "Callcenter").
bool hasWord(const std::string& s, const char* word) {
  const size_t n = std::strlen(word);
  const auto isAlnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
  for (size_t pos = s.find(word); pos != std::string::npos; pos = s.find(word, pos + 1)) {
    const bool startOk = pos == 0 || !isAlnum(s[pos - 1]);
    const bool endOk = pos + n == s.size() || !isAlnum(s[pos + n]);
    if (startOk && endOk) return true;
  }
  return false;
}

// "+49 40 1234567", "040/123 45 67", "tel:+49...": mostly digits, at least 6.
bool looksLikePhone(const std::string& s) {
  if (s.find("tel:") != std::string::npos) return true;
  int digits = 0;
  for (const char c : s) {
    if (c >= '0' && c <= '9') {
      ++digits;
    } else if (!(c == ' ' || c == '+' || c == '-' || c == '/' || c == '(' || c == ')' || c == '.')) {
      return false;
    }
  }
  return digits >= 6;
}
}  // namespace

MeetingPlatform CalEvent::platform() const {
  if (meetingPlatformRaw == "meet") return MeetingPlatform::Meet;
  if (meetingPlatformRaw == "teams") return MeetingPlatform::Teams;
  if (meetingPlatformRaw == "ne") return MeetingPlatform::Ne;
  if (meetingPlatformRaw == "linkedin") return MeetingPlatform::LinkedIn;
  const std::string loc = lower(location);
  const std::string text = loc + "\n" + lower(notes);
  if (hasAny(text, {"join.ecke.lt"})) return MeetingPlatform::Ne;
  // The bridge strips the Teams footer (with its link) before it looks for one;
  // the location usually still says "Microsoft Teams-Besprechung".
  if (hasAny(text, {"teams.microsoft.com", "teams.live.com", "microsoft teams", "teams-besprechung",
                    "teams meeting", "teams-meeting"})) {
    return MeetingPlatform::Teams;
  }
  if (hasAny(text, {"meet.google.com", "google meet"})) return MeetingPlatform::Meet;
  if (hasAny(text, {"linkedin.com"}) || subject.find("LinkedIn") != std::string::npos) return MeetingPlatform::LinkedIn;
  const std::string title = lower(subject);
  // A video platform was ruled out above, so a bare "Call" as the place is a phone call.
  if (looksLikePhone(location) || hasAny(loc, {"telefon", "anruf", "phone"}) || hasWord(loc, "call") ||
      hasAny(title, {"telefon", "anruf", "phone call"})) {
    return MeetingPlatform::Phone;
  }
  return MeetingPlatform::None;
}

bool CalEvent::isOnSite() const {
  if (platform() != MeetingPlatform::None) return false;
  const std::string loc = lower(displayLocation());
  if (loc.empty() || loc.find("://") != std::string::npos) return false;
  return !hasAny(loc, {"online", "zoom", "webex", "whereby", "jitsi", "facetime", "skype", "teams", "meet"});
}

std::string CalEvent::organizerName() const {
  std::string name = normalizeHashText(organizer);
  if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
  const size_t comma = name.find(',');
  if (name.find('@') != std::string::npos || comma == std::string::npos ||
      name.find(',', comma + 1) != std::string::npos) {
    return name;
  }
  const std::string last = normalizeHashText(name.substr(0, comma));
  const std::string first = normalizeHashText(name.substr(comma + 1));
  if (last.empty() || first.empty()) return name;
  return first + " " + last;
}

std::string CalEvent::displayLocation() const {
  std::string l = location;
  if (l.rfind("; ", 0) == 0) l.erase(0, 2);
  replaceAll(l, "DE-", "");
  replaceAll(l, "HB-", "");
  replaceAll(l, "COC-", "");
  if (l.find("obsidian://") != std::string::npos) return std::string();
  return l;
}

int CalEvent::startMinutes() const { return parseHhMm(start); }

int CalEvent::endMinutes() const {
  const int s = startMinutes();
  int e = parseHhMm(end);
  // An end on a later day (overnight event) runs to the end of this day.
  if (end.size() >= 10 && start.size() >= 10 && end.compare(0, 10, start, 0, 10) > 0) e = 24 * 60;
  if (e <= s) e = s + 60;
  return e;
}

namespace {
bool parseIsoDate(const std::string& iso, Date& out) {
  if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return false;
  out = {std::atoi(iso.substr(0, 4).c_str()), std::atoi(iso.substr(5, 2).c_str()), std::atoi(iso.substr(8, 2).c_str())};
  return out.month >= 1 && out.month <= 12 && out.day >= 1 && out.day <= 31;
}
}  // namespace

std::string CalEvent::lastDate() const {
  const std::string first = startDate();
  if (end.size() < 10) return first;
  const std::string last = end.substr(0, 10);
  if (last <= first) return first;
  const int endMin = parseHhMm(end);
  if (endMin <= 0) {  // 00:00 or no time: exclusive end
    Date d{};
    if (!parseIsoDate(last, d)) return first;
    const std::string previous = isoString(addDays(d, -1));
    return previous < first ? first : previous;
  }
  return last;
}

bool CalEvent::barOn(const std::string& isoDate) const {
  return isAllDay || (startDate() < isoDate && isoDate < lastDate());
}

int CalEvent::startMinutesOn(const std::string& isoDate) const {
  return isoDate == startDate() ? startMinutes() : 0;
}

int CalEvent::endMinutesOn(const std::string& isoDate) const {
  if (lastDate() == startDate()) return endMinutes();
  if (isoDate != lastDate()) return 24 * 60;
  const int e = parseHhMm(end);
  return e > 0 ? e : 24 * 60;
}

bool CalEvent::coversDate(const std::string& isoDate) const {
  const std::string first = startDate();
  return !first.empty() && first <= isoDate && isoDate <= lastDate();
}

bool parsePayload(const char* json, size_t len, std::vector<CalEvent>& out) {
  out.clear();
  JsonDocument filter;
  JsonObject f = filter.add<JsonObject>();
  for (const char* key : {"start", "end", "subject", "summary", "location", "organizer", "importance",
                          "hasAttachments", "meetingPlatform", "isRecurring", "isMoved", "isCancelled", "isAllDay",
                          "notes", "hasObsidianNote", "calendar", "numAttendees"}) {
    f[key] = true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  if (arr.isNull()) return false;
  out.reserve(arr.size());
  for (JsonObjectConst e : arr) {
    CalEvent ev;
    ev.start = str(e["start"]);
    ev.end = str(e["end"]);
    ev.subject = e["subject"].is<const char*>() ? str(e["subject"]) : str(e["summary"]);
    ev.location = str(e["location"]);
    ev.organizer = str(e["organizer"]);
    ev.notes = str(e["notes"]);
    ev.importance = str(e["importance"]);
    ev.meetingPlatformRaw = str(e["meetingPlatform"]);
    ev.calendar = str(e["calendar"]);
    ev.hasAttachments = e["hasAttachments"] | false;
    ev.isRecurring = e["isRecurring"] | false;
    ev.isMoved = e["isMoved"] | false;
    ev.isCancelled = e["isCancelled"] | false;
    ev.isAllDay = e["isAllDay"] | false;
    ev.hasObsidianNote = e["hasObsidianNote"] | false;
    ev.numAttendees = e["numAttendees"] | 0;
    if (ev.start.empty()) continue;
    out.push_back(std::move(ev));
  }
  return true;
}

uint32_t semanticPayloadHash(const std::vector<CalEvent>& events) {
  std::vector<std::string> lines;
  lines.reserve(events.size());
  for (const CalEvent& e : events) {
    std::string line;
    for (const std::string* field : {&e.start, &e.end, &e.subject, &e.location, &e.notes, &e.organizer, &e.importance,
                                     &e.meetingPlatformRaw, &e.calendar}) {
      line += normalizeHashText(*field);
      line += static_cast<char>(0x1F);
    }
    for (const bool b : {e.hasAttachments, e.isRecurring, e.isMoved, e.isCancelled, e.isAllDay, e.hasObsidianNote}) {
      line += b ? '1' : '0';
      line += static_cast<char>(0x1F);
    }
    line += std::to_string(e.numAttendees);
    lines.push_back(std::move(line));
  }
  std::sort(lines.begin(), lines.end());
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < lines.size(); ++i) {
    h = fnvAdd(h, lines[i]);
    if (i + 1 < lines.size()) h = fnvAddByte(h, '\n');
  }
  return h;
}

std::vector<const CalEvent*> eventsOnDate(const std::vector<CalEvent>& events, const std::string& isoDate) {
  std::vector<const CalEvent*> out;
  for (const CalEvent& e : events) {
    if (e.coversDate(isoDate)) out.push_back(&e);
  }
  std::stable_sort(out.begin(), out.end(), [&isoDate](const CalEvent* a, const CalEvent* b) {
    const bool barA = a->barOn(isoDate);
    const bool barB = b->barOn(isoDate);
    if (barA != barB) return barA;
    return a->startMinutesOn(isoDate) < b->startMinutesOn(isoDate);
  });
  return out;
}

uint32_t dayHash(const std::vector<const CalEvent*>& dayEvents, const std::string& isoDate) {
  uint32_t h = fnvAdd(2166136261u, isoDate);
  for (const CalEvent* e : dayEvents) {
    for (const std::string* s : {&e->subject, &e->start, &e->end, &e->location, &e->organizer, &e->notes, &e->calendar,
                                 &e->meetingPlatformRaw, &e->importance}) {
      h = fnvAdd(h, *s);
      h = fnvAddByte(h, 0x1F);
    }
    const uint8_t flags = (e->isRecurring << 0) | (e->isMoved << 1) | (e->hasAttachments << 2) |
                          (e->isCancelled << 3) | (e->isAllDay << 4) | (e->hasObsidianNote << 5);
    h = fnvAddByte(h, flags);
    h = fnvAddByte(h, static_cast<uint8_t>(e->numAttendees));
    h = fnvAddByte(h, '\n');
  }
  return h;
}

std::vector<LayoutBox> layoutDay(const std::vector<LayoutInput>& inputs) {
  struct Iv {
    size_t idx;
    int s;
    int e;
  };
  std::vector<Iv> iv;
  iv.reserve(inputs.size());
  for (size_t i = 0; i < inputs.size(); ++i) {
    int e = inputs[i].endMin;
    if (e <= inputs[i].startMin) e = inputs[i].startMin + 60;
    iv.push_back({i, inputs[i].startMin, e});
  }
  // Earlier start left; on equal start the longer event goes left.
  std::sort(iv.begin(), iv.end(), [](const Iv& a, const Iv& b) {
    if (a.s != b.s) return a.s < b.s;
    return (a.e - a.s) > (b.e - b.s);
  });

  std::vector<LayoutBox> result;
  result.reserve(iv.size());
  size_t groupBegin = 0;
  while (groupBegin < iv.size()) {
    // Overlap group: chain of events that overlap the group's running end.
    size_t groupEnd = groupBegin + 1;
    int maxEnd = iv[groupBegin].e;
    while (groupEnd < iv.size() && iv[groupEnd].s < maxEnd) {
      maxEnd = std::max(maxEnd, iv[groupEnd].e);
      ++groupEnd;
    }
    std::vector<int> col(groupEnd - groupBegin, 0);
    int totalCols = 0;
    for (size_t i = groupBegin; i < groupEnd; ++i) {
      int c = 0;
      for (;; ++c) {
        bool clash = false;
        for (size_t j = groupBegin; j < i; ++j) {
          if (col[j - groupBegin] == c && iv[i].s < iv[j].e && iv[j].s < iv[i].e) {
            clash = true;
            break;
          }
        }
        if (!clash) break;
      }
      col[i - groupBegin] = c;
      totalCols = std::max(totalCols, c + 1);
    }
    for (size_t i = groupBegin; i < groupEnd; ++i) {
      int span = 1;
      for (int next = col[i - groupBegin] + 1; next < totalCols; ++next) {
        bool free = true;
        for (size_t j = groupBegin; j < groupEnd; ++j) {
          if (col[j - groupBegin] == next && iv[i].s < iv[j].e && iv[j].s < iv[i].e) {
            free = false;
            break;
          }
        }
        if (!free) break;
        ++span;
      }
      result.push_back({iv[i].idx, col[i - groupBegin], totalCols, span, iv[i].s, iv[i].e});
    }
    groupBegin = groupEnd;
  }
  return result;
}

Date dateFromTm(const struct tm& t) { return {t.tm_year + 1900, t.tm_mon + 1, t.tm_mday}; }

int daysInMonth(int year, int month) {
  static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[(month - 1) % 12];
}

namespace {
// Days since 1970-01-01 (proleptic Gregorian), Howard Hinnant's algorithm.
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

Date civilFromDays(long z) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  return {y + (m <= 2), m, d};
}
}  // namespace

Date addDays(Date d, int days) { return civilFromDays(daysFromCivil(d.year, d.month, d.day) + days); }

int weekdayMon0(Date d) {
  const long z = daysFromCivil(d.year, d.month, d.day);
  // 1970-01-01 was a Thursday (Mon0 index 3).
  return static_cast<int>(((z % 7) + 7 + 3) % 7);
}

int isoWeek(Date d) {
  // The ISO week belongs to the year of its Thursday.
  const Date thursday = addDays(d, 3 - weekdayMon0(d));
  const long jan1 = daysFromCivil(thursday.year, 1, 1);
  return static_cast<int>((daysFromCivil(thursday.year, thursday.month, thursday.day) - jan1) / 7 + 1);
}

std::string isoString(Date d) {
  char buf[11];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
  return buf;
}

bool sameDate(Date a, Date b) { return a.year == b.year && a.month == b.month && a.day == b.day; }

const char* weekdayNameDe(int mon0) {
  static const char* const kNames[7] = {"Montag", "Dienstag", "Mittwoch", "Donnerstag",
                                        "Freitag", "Samstag", "Sonntag"};
  return kNames[((mon0 % 7) + 7) % 7];
}

const char* weekdayShortDe(int mon0) {
  static const char* const kNames[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  return kNames[((mon0 % 7) + 7) % 7];
}

const char* monthNameDe(int month) {
  static const char* const kNames[12] = {"Januar", "Februar", "März",      "April",   "Mai",      "Juni",
                                         "Juli",   "August",  "September", "Oktober", "November", "Dezember"};
  return kNames[((month - 1) % 12 + 12) % 12];
}

uint32_t nextWakeMinutes(const struct tm& now, uint32_t dayIntervalMin) {
  const int minuteOfDay = now.tm_hour * 60 + now.tm_min;
  const uint32_t toMidnight5 = static_cast<uint32_t>(24 * 60 - minuteOfDay + 5);
  const bool weekend = now.tm_wday == 0 || now.tm_wday == 6;
  if (weekend) {
    if (minuteOfDay < 12 * 60) return static_cast<uint32_t>(12 * 60 - minuteOfDay);
    return toMidnight5;
  }
  if (minuteOfDay < 7 * 60) return static_cast<uint32_t>(7 * 60 - minuteOfDay);
  if (minuteOfDay >= 19 * 60) return toMidnight5;
  return dayIntervalMin;
}

}  // namespace deskleaf
