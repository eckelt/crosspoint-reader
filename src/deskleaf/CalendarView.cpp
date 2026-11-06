#include "CalendarView.h"

#if DESKLEAF_CALENDAR

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "fontIds.h"

namespace deskleaf::view {

namespace {

constexpr int kMargin = 8;
constexpr int kAxisW = 30;  // hour labels left of the timeline
constexpr int kColGap = 4;
constexpr int kMinBoxH = 26;
// The family calendar was red on the 4-color Deskleaf; here dark gray, white text.
constexpr const char* kFamilyCalendar = "Ohana";

const char* platformLabel(MeetingPlatform p) {
  switch (p) {
    case MeetingPlatform::Teams:
      return "Teams";
    case MeetingPlatform::Meet:
      return "Meet";
    case MeetingPlatform::Ne:
      return "NE";
    case MeetingPlatform::LinkedIn:
      return "LinkedIn";
    case MeetingPlatform::None:
      break;
  }
  return nullptr;
}

std::string hhmm(int minutes) {
  char buf[6];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return buf;
}

// Line of small markers under the title: platform, series, attachment, attendees.
std::string markerLine(const CalEvent& e) {
  std::string out;
  auto add = [&out](const std::string& s) {
    if (s.empty()) return;
    if (!out.empty()) out += " · ";
    out += s;
  };
  if (const char* p = platformLabel(e.platform())) add(p);
  if (e.isRecurring) add(e.isMoved ? "Serie (verschoben)" : "Serie");
  if (e.hasAttachments) add("Anhang");
  if (e.hasObsidianNote || e.location.find("obsidian://") != std::string::npos) add("Notiz");
  if (e.isCancelled) add("abgesagt");
  return out;
}

// Draws wrapped lines and returns the y after the last one; stops at maxY.
int drawLines(const GfxRenderer& r, int font, int x, int y, int w, const std::string& text, int maxLines, int maxY,
              bool black, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (text.empty() || maxLines <= 0) return y;
  const int lh = r.getLineHeight(font);
  const int fit = std::max(0, (maxY - y) / lh);
  const int lines = std::min(maxLines, fit);
  if (lines <= 0) return y;
  for (const std::string& line : r.wrappedText(font, text.c_str(), w, lines, style)) {
    r.drawText(font, x, y, line.c_str(), black, style);
    y += lh;
  }
  return y;
}

void dottedRect(const GfxRenderer& r, int x, int y, int w, int h) {
  for (int i = x; i < x + w; i += 3) {
    r.drawPixel(i, y);
    r.drawPixel(i, y + h - 1);
  }
  for (int j = y; j < y + h; j += 3) {
    r.drawPixel(x, j);
    r.drawPixel(x + w - 1, j);
  }
}

void drawEventBox(const GfxRenderer& r, const CalEvent& e, const std::string& dayIso, int x, int y, int w, int h) {
  const bool family = e.calendar == kFamilyCalendar && !e.isCancelled;
  const bool textBlack = !family;
  if (e.isCancelled) {
    r.fillRect(x, y, w, h, false);
    dottedRect(r, x, y, w, h);
  } else if (family) {
    r.fillRoundedRect(x, y, w, h, 4, Color::DarkGray);
  } else {
    // Border only slightly darker than the fill: a dithered dark-gray ring.
    r.fillRoundedRect(x, y, w, h, 4, Color::DarkGray);
    r.fillRoundedRect(x + 1, y + 1, w - 2, h - 2, 3, Color::LightGray);
  }
  if (e.isImportant()) r.fillRect(x, y, 4, h, textBlack);

  const int tx = x + 6;
  const int tw = w - 10;
  const int maxY = y + h - 2;
  int cy = y + 3;
  const int titleLines = h >= 70 ? 2 : 1;
  cy = drawLines(r, UI_10_FONT_ID, tx, cy, tw, e.subject, titleLines, maxY, textBlack, EpdFontFamily::BOLD);

  std::string meta;
  if (e.lastDate() == e.startDate()) {
    meta = hhmm(e.startMinutes()) + "–" + hhmm(e.endMinutes() % (24 * 60));
  } else if (dayIso == e.startDate()) {
    meta = "ab " + hhmm(e.startMinutes());
  } else {
    meta = "bis " + hhmm(e.endMinutesOn(dayIso) % (24 * 60));
  }
  const std::string loc = e.displayLocation();
  if (!loc.empty()) meta += "  " + loc;
  cy = drawLines(r, SMALL_FONT_ID, tx, cy, tw, meta, 1, maxY, textBlack);

  std::string who = e.organizer;
  if (e.numAttendees > 0) who += (who.empty() ? "" : " ") + std::string("(+") + std::to_string(e.numAttendees) + ")";
  cy = drawLines(r, SMALL_FONT_ID, tx, cy, tw, who, 1, maxY, textBlack);
  cy = drawLines(r, SMALL_FONT_ID, tx, cy, tw, markerLine(e), 1, maxY, textBlack);
  // Notes from the Obsidian brain: markdown checkboxes become ASCII markers.
  if (!e.notes.empty() && cy + r.getLineHeight(SMALL_FONT_ID) <= maxY) {
    std::string notes = e.notes;
    for (const char* box : {"- [ ] ", "- [x] ", "- [X] "}) {
      for (size_t p = notes.find(box); p != std::string::npos; p = notes.find(box, p)) {
        notes.replace(p, 6, box[3] == ' ' ? "[ ] " : "[x] ");
      }
    }
    drawLines(r, SMALL_FONT_ID, tx, cy + 2, tw, notes, 6, maxY, textBlack);
  }
}

}  // namespace

int drawHeader(const GfxRenderer& r, const char* title, const char* subtitle, const Status& status) {
  const int w = r.getScreenWidth();
  const int h = 8 + r.getLineHeight(UI_12_FONT_ID) + r.getLineHeight(UI_10_FONT_ID) + 8;
  r.fillRect(0, 0, w, h, true);
  r.drawText(UI_12_FONT_ID, kMargin + 4, 8, title, false, EpdFontFamily::BOLD);
  r.drawText(UI_10_FONT_ID, kMargin + 4, 8 + r.getLineHeight(UI_12_FONT_ID), subtitle, false);

  std::string right;
  if (status.statusText) {
    right = status.statusText;
  } else if (status.lastSyncEpoch > 0) {
    const time_t t = static_cast<time_t>(status.lastSyncEpoch);
    struct tm lt {};
    localtime_r(&t, &lt);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "Stand %02d:%02d", lt.tm_hour, lt.tm_min);
    right = buf;
  }
  if (!right.empty()) {
    const int tw = r.getTextWidth(SMALL_FONT_ID, right.c_str());
    r.drawText(SMALL_FONT_ID, w - kMargin - 4 - tw, 10, right.c_str(), false);
  }
  return h;
}

void drawDay(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date day, const Date today,
             const Status& status, const int top, const int bottom, std::vector<HitBox>* hits) {
  if (hits) hits->clear();
  const int w = r.getScreenWidth();
  const auto dayEvents = eventsOnDate(events, isoString(day));
  std::vector<const CalEvent*> allDay;
  std::vector<const CalEvent*> timed;
  const std::string dayIso = isoString(day);
  for (const CalEvent* e : dayEvents) (e->barOn(dayIso) ? allDay : timed).push_back(e);

  int y = top + 6;
  const int barH = r.getLineHeight(UI_10_FONT_ID) + 6;
  for (const CalEvent* e : allDay) {
    if (y + barH > bottom) break;
    const bool family = e->calendar == kFamilyCalendar && !e->isCancelled;
    if (e->isCancelled) {
      dottedRect(r, kMargin, y, w - 2 * kMargin, barH);
    } else {
      r.fillRoundedRect(kMargin, y, w - 2 * kMargin, barH, 4, family ? Color::DarkGray : Color::LightGray);
    }
    const std::string t = r.truncatedText(UI_10_FONT_ID, e->subject.c_str(), w - 2 * kMargin - 12);
    r.drawText(UI_10_FONT_ID, kMargin + 6, y + 3, t.c_str(), !family);
    if (hits) hits->push_back({kMargin, y, w - 2 * kMargin, barH, e});
    y += barH + 3;
  }

  if (timed.empty()) {
    const char* msg = allDay.empty() ? (sameDate(day, today) ? "Heute bin ich frei!" : "Keine Termine") : "";
    r.drawCenteredText(UI_12_FONT_ID, (y + bottom) / 2 - r.getLineHeight(UI_12_FONT_ID) / 2, msg, true,
                       EpdFontFamily::BOLD);
    return;
  }

  // Visible hours: 07-17 by default, widened to cover every timed event.
  int firstHour = 7;
  int lastHour = 17;
  for (const CalEvent* e : timed) {
    // A segment that comes from or runs into another day does not stretch the axis.
    const int s0 = e->startMinutesOn(dayIso);
    const int e0 = e->endMinutesOn(dayIso);
    if (s0 > 0) firstHour = std::min(firstHour, s0 / 60);
    if (e0 < 24 * 60) lastHour = std::max(lastHour, (e0 + 59) / 60);
  }
  lastHour = std::min(lastHour, 24);
  const int axisTop = y + 8;
  const int axisBottom = bottom - 6;
  const float pxPerMin = static_cast<float>(axisBottom - axisTop) / ((lastHour - firstHour) * 60);
  const auto yOf = [&](int minutes) {
    return axisTop + static_cast<int>((minutes - firstHour * 60) * pxPerMin);
  };

  for (int h = firstHour; h <= lastHour; ++h) {
    const int ly = yOf(h * 60);
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02d", h % 24);
    // Digits span roughly the lower 70% of the ascender: center them on the line.
    r.drawText(SMALL_FONT_ID, kMargin - 4, ly - r.getFontAscenderSize(SMALL_FONT_ID) * 64 / 100, buf, true);
    for (int x = kMargin + kAxisW - 6; x < w - kMargin; x += 4) r.drawPixel(x, ly);
  }

  std::vector<LayoutInput> inputs;
  inputs.reserve(timed.size());
  for (const CalEvent* e : timed) inputs.push_back({e->startMinutesOn(dayIso), e->endMinutesOn(dayIso)});
  const int areaX = kMargin + kAxisW;
  const int areaW = w - areaX - kMargin;
  for (const LayoutBox& b : layoutDay(inputs)) {
    const int colW = (areaW - kColGap * (b.groupColumns - 1)) / b.groupColumns;
    const int bx = areaX + b.column * (colW + kColGap);
    const int bw = colW * b.colSpan + kColGap * (b.colSpan - 1);
    const int by = yOf(std::max(b.startMin, firstHour * 60)) + 1;
    const int bh = std::max(kMinBoxH, yOf(std::min(b.endMin, lastHour * 60)) - by - 1);
    const int boxH = std::min(bh, axisBottom - by);
    drawEventBox(r, *timed[b.eventIndex], dayIso, bx, by, bw, boxH);
    if (hits) hits->push_back({bx, by, bw, boxH, timed[b.eventIndex]});
  }

  if (status.showNowLine && sameDate(day, today)) {
    const time_t now = time(nullptr);
    struct tm lt {};
    localtime_r(&now, &lt);
    const int m = lt.tm_hour * 60 + lt.tm_min;
    if (m >= firstHour * 60 && m <= lastHour * 60) {
      const int ny = yOf(m);
      r.fillRect(kMargin + kAxisW - 10, ny - 1, w - kMargin - (kMargin + kAxisW - 10), 3, true);
      r.fillRoundedRect(kMargin + kAxisW - 14, ny - 5, 10, 10, 5, Color::Black);
    }
  }
}

void drawDetail(const GfxRenderer& r, const CalEvent& e, const int top, const int bottom) {
  const int w = r.getScreenWidth();
  const int x = kMargin + 8;
  const int tw = w - 2 * x;
  int y = top + 12;
  y = drawLines(r, UI_12_FONT_ID, x, y, tw, e.subject, 3, bottom, true, EpdFontFamily::BOLD) + 6;

  std::string when = e.isAllDay ? std::string("ganztägig")
                                : hhmm(e.startMinutes()) + " – " + hhmm(e.endMinutes() % (24 * 60)) + " Uhr";
  // Multi-day: name the span; times then refer to the first and last day.
  if (e.lastDate() != e.startDate()) {
    Date a{};
    Date b{};
    const auto parse = [](const std::string& s, Date& d) {
      d = {std::atoi(s.substr(0, 4).c_str()), std::atoi(s.substr(5, 2).c_str()), std::atoi(s.substr(8, 2).c_str())};
    };
    parse(e.startDate(), a);
    parse(e.lastDate(), b);
    char span[64];
    std::snprintf(span, sizeof(span), "%d.%d. – %d.%d.", a.day, a.month, b.day, b.month);
    when = span;
    if (!e.isAllDay && e.end.size() >= 16) {
      when += "  ·  " + hhmm(e.startMinutes()) + " – " + e.end.substr(11, 5) + " Uhr";
    }
  }
  if (e.isCancelled) when += "  ·  abgesagt";
  y = drawLines(r, UI_10_FONT_ID, x, y, tw, when, 1, bottom, true, EpdFontFamily::BOLD);

  const auto field = [&](const char* label, const std::string& value) {
    if (value.empty() || y + 2 * r.getLineHeight(SMALL_FONT_ID) > bottom) return;
    y += 8;
    r.drawText(SMALL_FONT_ID, x, y, label, true, EpdFontFamily::BOLD);
    y += r.getLineHeight(SMALL_FONT_ID);
    y = drawLines(r, UI_10_FONT_ID, x, y, tw, value, 3, bottom, true);
  };
  field("Ort", e.displayLocation());
  std::string who = e.organizer;
  if (e.numAttendees > 0) who += (who.empty() ? "" : "  ·  ") + std::to_string(e.numAttendees) + " Teilnehmer";
  field("Organisation", who);
  field("Kalender", e.calendar);
  field("Hinweise", markerLine(e));

  if (!e.notes.empty() && y + 2 * r.getLineHeight(SMALL_FONT_ID) < bottom) {
    y += 10;
    r.drawLine(x, y, w - x, y, true);
    y += 8;
    std::string notes = e.notes;
    for (const char* box : {"- [ ] ", "- [x] ", "- [X] "}) {
      for (size_t p = notes.find(box); p != std::string::npos; p = notes.find(box, p)) {
        notes.replace(p, 6, box[3] == ' ' ? "[ ] " : "[x] ");
      }
    }
    drawLines(r, UI_10_FONT_ID, x, y, tw, notes, 40, bottom, true);
  }
}

void drawWeek(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date day, const Date today,
              const int top, const int bottom) {
  const int w = r.getScreenWidth();
  const Date monday = addDays(day, -weekdayMon0(day));
  const int rowH = (bottom - top) / 7;
  const int labelW = 78;
  const int lh = r.getLineHeight(SMALL_FONT_ID);
  for (int i = 0; i < 7; ++i) {
    const Date d = addDays(monday, i);
    const int y = top + i * rowH;
    const bool isToday = sameDate(d, today);
    if (isToday) {
      r.fillRect(0, y, labelW, rowH, true);
    } else if (i >= 5) {
      r.fillRectDither(0, y, labelW, rowH, Color::LightGray);
    }
    char label[8];
    std::snprintf(label, sizeof(label), "%d.%d.", d.day, d.month);
    r.drawText(UI_12_FONT_ID, kMargin, y + 6, weekdayShortDe(i), !isToday, EpdFontFamily::BOLD);
    r.drawText(SMALL_FONT_ID, kMargin, y + 8 + r.getLineHeight(UI_12_FONT_ID), label, !isToday);
    r.drawLine(0, y + rowH - 1, w, y + rowH - 1, true);

    const auto dayEvents = eventsOnDate(events, isoString(d));
    const int maxLines = std::max(1, (rowH - 8) / lh);
    int line = 0;
    for (size_t k = 0; k < dayEvents.size() && line < maxLines; ++k, ++line) {
      const CalEvent& e = *dayEvents[k];
      if (line == maxLines - 1 && k + 1 < dayEvents.size()) {
        const std::string more = "+" + std::to_string(dayEvents.size() - k) + " weitere";
        r.drawText(SMALL_FONT_ID, labelW + 8, y + 4 + line * lh, more.c_str(), true, EpdFontFamily::BOLD);
        break;
      }
      const std::string dIso = isoString(d);
      std::string text = e.barOn(dIso) ? std::string("ganztags  ")
                         : dIso != e.startDate() ? "bis " + hhmm(e.endMinutesOn(dIso) % (24 * 60)) + "  "
                                                 : hhmm(e.startMinutes()) + "  ";
      text += e.subject;
      if (e.isCancelled) text += " (abgesagt)";
      const std::string t = r.truncatedText(SMALL_FONT_ID, text.c_str(), w - labelW - 16);
      r.drawText(SMALL_FONT_ID, labelW + 8, y + 4 + line * lh, t.c_str(), true,
                 e.isImportant() ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }
}

void drawMonth(const GfxRenderer& r, const std::vector<CalEvent>& events, const int year, const int month,
               const Date today, const int top, const int bottom) {
  const int w = r.getScreenWidth();
  const int kwW = 34;
  const int colW = (w - kwW - kMargin) / 7;
  const int headH = r.getLineHeight(SMALL_FONT_ID) + 6;
  const Date first{year, month, 1};
  const int lead = weekdayMon0(first);
  const int days = daysInMonth(year, month);
  const int rows = (lead + days + 6) / 7;
  const int cellH = (bottom - top - headH) / rows;

  r.drawText(SMALL_FONT_ID, 4, top + 2, "KW", true);
  for (int c = 0; c < 7; ++c) {
    r.drawText(SMALL_FONT_ID, kwW + c * colW + 4, top + 2, weekdayShortDe(c), true, EpdFontFamily::BOLD);
  }
  for (int row = 0; row < rows; ++row) {
    const int y = top + headH + row * cellH;
    const Date rowMonday = addDays(first, row * 7 - lead);
    r.drawText(SMALL_FONT_ID, 4, y + 4, std::to_string(isoWeek(rowMonday)).c_str(), true);
    r.drawLine(kwW, y, w - kMargin, y, true);
    for (int c = 0; c < 7; ++c) {
      const Date d = addDays(rowMonday, c);
      if (d.month != month) continue;
      const int x = kwW + c * colW;
      const bool isToday = sameDate(d, today);
      if (isToday) {
        r.fillRect(x + 1, y + 1, colW - 2, cellH - 2, true);
      } else if (c >= 5) {
        r.fillRectDither(x + 1, y + 1, colW - 2, cellH - 2, Color::LightGray);
      }
      const std::string num = std::to_string(d.day);
      r.drawText(UI_10_FONT_ID, x + 4, y + 3, num.c_str(), !isToday, EpdFontFamily::BOLD);

      // One line per event while it fits, then a count.
      const auto dayEvents = eventsOnDate(events, isoString(d));
      const int lh = r.getLineHeight(SMALL_FONT_ID);
      int ty = y + 5 + r.getLineHeight(UI_10_FONT_ID);
      const int maxLines = std::max(0, (y + cellH - 2 - ty) / lh);
      for (int k = 0; k < static_cast<int>(dayEvents.size()) && k < maxLines; ++k) {
        std::string t;
        if (k == maxLines - 1 && k + 1 < static_cast<int>(dayEvents.size())) {
          t = "+" + std::to_string(dayEvents.size() - k);
        } else {
          t = r.truncatedText(SMALL_FONT_ID, dayEvents[k]->subject.c_str(), colW - 6);
        }
        r.drawText(SMALL_FONT_ID, x + 3, ty, t.c_str(), !isToday);
        ty += lh;
      }
    }
  }
  r.drawLine(kwW, top + headH + rows * cellH, w - kMargin, top + headH + rows * cellH, true);
}

void drawSleepScreen(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date today,
                     const Status& status) {
  r.clearScreen();
  char subtitle[48];
  std::snprintf(subtitle, sizeof(subtitle), "%d. %s · KW %d", today.day, monthNameDe(today.month), isoWeek(today));
  const int top = drawHeader(r, weekdayNameDe(weekdayMon0(today)), subtitle, status);
  drawDay(r, events, today, today, status, top, r.getScreenHeight() - 4);
}

}  // namespace deskleaf::view

#endif  // DESKLEAF_CALENDAR
