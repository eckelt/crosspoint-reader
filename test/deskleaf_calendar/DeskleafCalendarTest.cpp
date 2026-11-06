// Host tests for lib/DeskleafCalendar. Hash vectors are the contract vectors
// of the Swift bridge (deskleaf-display: EinkalHashTests.swift).
//   ./test/deskleaf_calendar/run.sh
#include <DeskleafCalendar.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace deskleaf;

static int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                  \
    }                                                              \
  } while (0)

static const std::string kEv1 =
    R"({"date":"2026-09-11","start":"2026-09-11T10:00:00+02:00","end":"2026-09-11T11:00:00+02:00","subject":"Team Weekly","organizer":"Chefin","location":"Raum 3","importance":"normal","hasAttachments":false,"meetingPlatform":"teams","calendar":"Arbeit","isRecurring":true,"isMoved":false,"isCancelled":false,"isAllDay":false,"notes":"Agenda","numAttendees":4})";
static const std::string kEv2 =
    R"({"date":"2026-09-11","start":"2026-09-11T14:00:00+02:00","end":"2026-09-11T14:30:00+02:00","subject":"1:1","importance":"normal","hasAttachments":false,"calendar":"Arbeit","isRecurring":false,"isMoved":false,"isCancelled":false,"isAllDay":false,"notes":"","numAttendees":0})";

static uint32_t hashOf(const std::string& json) {
  std::vector<CalEvent> ev;
  if (!parsePayload(json.data(), json.size(), ev)) {
    return fnv1a(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  }
  return semanticPayloadHash(ev);
}

static std::string replace(std::string s, const std::string& a, const std::string& b) {
  const size_t p = s.find(a);
  if (p != std::string::npos) s.replace(p, a.size(), b);
  return s;
}

static void testHashContract() {
  CHECK(hashOf("[]") == 0x811C9DC5u);
  CHECK(hashOf("[" + kEv1 + "]") == 0x272257CDu);
  CHECK(hashOf("[" + kEv1 + "," + kEv2 + "]") == 0xF26E1C55u);
  CHECK(hashOf("[" + kEv2 + "," + kEv1 + "]") == 0xF26E1C55u);
  std::string noisy = replace(kEv1, "Team Weekly", "  Team Weekly \\r\\n");
  noisy = replace(noisy, R"("notes":"Agenda")", R"("notes":"Agenda  ")");
  CHECK(hashOf("[" + noisy + "]") == 0x272257CDu);
  CHECK(hashOf("[" + replace(kEv1, "Team Weekly", "Team Weekly 2") + "]") == 0x49F1CD2Fu);
  CHECK(hashOf("[" + replace(kEv1, R"("notes":"Agenda")", R"("notes":"Agenda","hasObsidianNote":false)") + "]") ==
        0x272257CDu);
  CHECK(hashOf("[" + replace(kEv1, R"("notes":"Agenda")", R"("notes":"Agenda","hasObsidianNote":true)") + "]") ==
        0x8249DE82u);
  CHECK(hashOf("not json") == 0x87817168u);
}

static void testDaySelection() {
  const std::string json = "[" + kEv2 + "," + kEv1 +
                           R"(,{"start":"2026-09-11T00:00:00","end":"2026-09-12T00:00:00","subject":"Urlaub","isAllDay":true},)"
                           R"({"start":"2026-09-12T09:00:00","end":"2026-09-12T10:00:00","subject":"Morgen"}])";
  std::vector<CalEvent> ev;
  CHECK(parsePayload(json.data(), json.size(), ev));
  CHECK(ev.size() == 4);
  const auto day = eventsOnDate(ev, "2026-09-11");
  CHECK(day.size() == 3);
  CHECK(day[0]->subject == "Urlaub");
  CHECK(day[1]->subject == "Team Weekly");
  CHECK(day[2]->subject == "1:1");
  CHECK(day[1]->platform() == MeetingPlatform::Teams);
  CHECK(day[1]->startMinutes() == 600);
  CHECK(day[1]->endMinutes() == 660);
  CHECK(day[0]->endMinutes() == 24 * 60);  // ends next day
  CHECK(dayHash(day, "2026-09-11") != dayHash(day, "2026-09-12"));
  const auto next = eventsOnDate(ev, "2026-09-12");
  CHECK(next.size() == 1);
  CHECK(dayHash(day, "2026-09-11") == dayHash(eventsOnDate(ev, "2026-09-11"), "2026-09-11"));
}

static void testMultiDay() {
  const std::string json =
      R"([{"start":"2026-11-05T00:00:00","end":"2026-11-07T00:00:00","subject":"Vorwerk","isAllDay":true},)"
      R"({"start":"2026-11-05T09:00:00+01:00","end":"2026-11-06T17:00:00+01:00","subject":"Messe"},)"
      R"({"start":"2026-11-05T22:00:00","end":"2026-11-06T00:00:00","subject":"Spaet"},)"
      R"({"start":"2026-11-06T10:00:00","end":"2026-11-06T11:00:00","subject":"Call"}])";
  std::vector<CalEvent> ev;
  CHECK(parsePayload(json.data(), json.size(), ev));
  CHECK(ev[0].lastDate() == "2026-11-06");  // all-day end is exclusive
  CHECK(ev[1].lastDate() == "2026-11-06");
  CHECK(ev[2].lastDate() == "2026-11-05");  // ends at midnight
  const auto day2 = eventsOnDate(ev, "2026-11-06");
  CHECK(day2.size() == 3);
  // All-day stays a bar; the timed two-day event is a 00:00-17:00 segment.
  CHECK(day2[0]->subject == "Vorwerk" && day2[0]->barOn("2026-11-06"));
  CHECK(day2[1]->subject == "Messe" && !day2[1]->barOn("2026-11-06"));
  CHECK(day2[1]->startMinutesOn("2026-11-06") == 0 && day2[1]->endMinutesOn("2026-11-06") == 17 * 60);
  CHECK(day2[1]->startMinutesOn("2026-11-05") == 9 * 60 && day2[1]->endMinutesOn("2026-11-05") == 24 * 60);
  CHECK(day2[2]->subject == "Call" && !day2[2]->barOn("2026-11-06"));
  CHECK(ev[3].endMinutesOn("2026-11-06") == 11 * 60);  // single-day unchanged
  CHECK(eventsOnDate(ev, "2026-11-05").size() == 3);
  CHECK(eventsOnDate(ev, "2026-11-07").empty());
  // A three-day timed event: the middle day is fully covered, so a bar.
  const std::string three = R"([{"start":"2026-11-05T09:00:00","end":"2026-11-07T17:00:00","subject":"Klausur"}])";
  std::vector<CalEvent> ev3;
  CHECK(parsePayload(three.data(), three.size(), ev3));
  CHECK(!ev3[0].barOn("2026-11-05") && ev3[0].barOn("2026-11-06") && !ev3[0].barOn("2026-11-07"));
}

static void testPlatforms() {
  CalEvent e;
  e.location = "Microsoft Teams-Besprechung";
  CHECK(e.platform() == MeetingPlatform::Teams && !e.isOnSite());
  e.location = "+49 40 123 456 78";
  CHECK(e.platform() == MeetingPlatform::Phone && !e.isOnSite());
  e.location = "";
  e.subject = "Telefontermin Mobilitätsberatung";
  CHECK(e.platform() == MeetingPlatform::Phone);
  e.subject = "Mobilitätsberatung mit SIXT";
  e.location = "Call";
  CHECK(e.platform() == MeetingPlatform::Phone && !e.isOnSite());
  e.location = "Callcenter Altona";
  CHECK(e.platform() == MeetingPlatform::None);
  e.subject = "Workshop";
  e.location = "Hobenköök, Stockmeyerstraße 43, Hamburg";
  CHECK(e.platform() == MeetingPlatform::None && e.isOnSite());
  e.location = "Online";
  CHECK(!e.isOnSite());
  e.location = "https://join.ecke.lt/abc";
  CHECK(e.platform() == MeetingPlatform::Ne);
  e.location = "";
  e.meetingPlatformRaw = "teams";
  CHECK(e.platform() == MeetingPlatform::Teams);
}

static void testOrganizer() {
  CalEvent e;
  e.organizer = "Koschmieder, Thomas";
  CHECK(e.organizerName() == "Thomas Koschmieder");
  e.organizer = "\"Koschmieder, Thomas\"";
  CHECK(e.organizerName() == "Thomas Koschmieder");
  e.organizer = "Alexander Rommel";
  CHECK(e.organizerName() == "Alexander Rommel");
  e.organizer = "chefin@example.com";
  CHECK(e.organizerName() == "chefin@example.com");
  e.organizer = "A, B, C";
  CHECK(e.organizerName() == "A, B, C");
}

static void testLocation() {
  CalEvent e;
  e.location = "; DE-HB-Raum 4";
  CHECK(e.displayLocation() == "Raum 4");
  e.location = "obsidian://open?vault=Brain";
  CHECK(e.displayLocation().empty());
  e.location = "https://meet.google.com/abc";
  CHECK(e.platform() == MeetingPlatform::Meet);
}

static void testLayout() {
  // A 9-11, B 9-10, C 10-11, D 12-13
  const auto boxes = layoutDay({{540, 660}, {540, 600}, {600, 660}, {720, 780}});
  CHECK(boxes.size() == 4);
  auto find = [&](size_t idx) -> const LayoutBox& {
    for (const auto& b : boxes)
      if (b.eventIndex == idx) return b;
    return boxes[0];
  };
  CHECK(find(0).column == 0 && find(0).groupColumns == 2 && find(0).colSpan == 1);
  CHECK(find(1).column == 1 && find(1).groupColumns == 2);
  CHECK(find(2).column == 1 && find(2).groupColumns == 2);
  CHECK(find(3).column == 0 && find(3).groupColumns == 1 && find(3).colSpan == 1);
  // Three columns where the short left event lets the middle one span.
  const auto b2 = layoutDay({{540, 720}, {540, 600}, {560, 620}, {620, 700}});
  CHECK(b2.size() == 4);
  for (const auto& b : b2) {
    if (b.eventIndex == 3) CHECK(b.column == 1 && b.colSpan == 2 && b.groupColumns == 3);
  }
}

static void testDates() {
  CHECK(weekdayMon0({2026, 11, 3}) == 1);  // Tuesday
  CHECK(weekdayMon0({1970, 1, 1}) == 3);
  CHECK(isoWeek({2026, 11, 3}) == 45);
  CHECK(isoWeek({2027, 1, 1}) == 53);  // Fri 1 Jan 2027 belongs to 2026-W53
  CHECK(isoWeek({2026, 1, 1}) == 1);
  CHECK(isoString(addDays({2026, 12, 31}, 1)) == "2027-01-01");
  CHECK(isoString(addDays({2028, 3, 1}, -1)) == "2028-02-29");
  CHECK(daysInMonth(2026, 2) == 28);
  CHECK(daysInMonth(2000, 2) == 29);
  CHECK(std::strcmp(monthNameDe(3), "März") == 0);
}

static void testWake() {
  struct tm t {};
  t.tm_wday = 2;  // Tuesday
  t.tm_hour = 10;
  t.tm_min = 3;
  CHECK(nextWakeMinutes(t) == 15);
  t.tm_hour = 5;
  t.tm_min = 30;
  CHECK(nextWakeMinutes(t) == 90);
  t.tm_hour = 20;
  t.tm_min = 0;
  CHECK(nextWakeMinutes(t) == 245);
  t.tm_wday = 6;  // Saturday
  t.tm_hour = 9;
  CHECK(nextWakeMinutes(t) == 180);
  t.tm_hour = 13;
  CHECK(nextWakeMinutes(t) == 665);
}

int main() {
  testHashContract();
  testDaySelection();
  testMultiDay();
  testPlatforms();
  testOrganizer();
  testLocation();
  testLayout();
  testDates();
  testWake();
  if (failures == 0) std::printf("deskleaf_calendar: all tests passed\n");
  return failures == 0 ? 0 : 1;
}
