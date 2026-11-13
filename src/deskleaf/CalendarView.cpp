#include "CalendarView.h"

#if DESKLEAF_CALENDAR

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "EventIcons.h"
#include "FreeDayArt.h"
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
    case MeetingPlatform::Phone:
      return "Telefon";
    case MeetingPlatform::Zoom:
      return "Zoom";
    case MeetingPlatform::Webex:
      return "Webex";
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

// Draws wrapped lines and returns the y after the last one; stops at maxY.
int drawLines(const GfxRenderer& r, int font, int x, int y, int w, const std::string& text, int maxLines, int maxY,
              bool black, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (text.empty() || maxLines <= 0) return y;
  const int lh = r.getLineHeight(font);
  const int fit = std::max(0, (maxY - y) / lh);
  const int lines = std::min(maxLines, fit);
  if (lines <= 0) return y;
  // Line breaks in notes start a new paragraph; the font has no glyph for them.
  int left = lines;
  bool gap = false;
  bool drewAny = false;
  size_t start = 0;
  while (start <= text.size() && left > 0) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string para = text.substr(start, end - start);
    para.erase(std::remove(para.begin(), para.end(), '\r'), para.end());
    start = end + 1;
    if (para.find_first_not_of(" \t") == std::string::npos) {
      gap = drewAny;  // blank line: half a line of space before the next paragraph
      continue;
    }
    if (gap) {
      y += lh / 2;
      gap = false;
      if (y + lh > maxY) break;
    }
    for (const std::string& line : r.wrappedText(font, para.c_str(), w, left, style)) {
      r.drawText(font, x, y, line.c_str(), black, style);
      y += lh;
      --left;
      drewAny = true;
    }
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

// 1-bit icon (rows MSB first, padded to bytes), scaled by an integer factor.
void drawIcon(const GfxRenderer& r, const unsigned char* bitmap, int w, int h, int x, int y, int scale, bool black) {
  const int rowBytes = (w + 7) / 8;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      if (!(bitmap[j * rowBytes + i / 8] & (0x80 >> (i % 8)))) continue;
      for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) r.drawPixel(x + i * scale + dx, y + j * scale + dy, black);
      }
    }
  }
}

// Where the event happens: a call service, or a pin for a physical place.
const unsigned char* placeIcon(const CalEvent& e) {
  switch (e.platform()) {
    case MeetingPlatform::Teams:
      return icons::kTeams;
    case MeetingPlatform::Meet:
      return icons::kMeet;
    case MeetingPlatform::Ne:
      return icons::kNE;
    case MeetingPlatform::LinkedIn:
      return icons::kLinkedIn;
    case MeetingPlatform::Phone:
      return icons::kPhone;
    case MeetingPlatform::Zoom:
      return icons::kZoom;
    case MeetingPlatform::Webex:
      return icons::kWebex;
    case MeetingPlatform::None:
      break;
  }
  return e.isOnSite() ? icons::kOnSite : nullptr;
}

// Notes in the detail view: the Markdown subset of parseNotes() with headings,
// drawn bullets and task boxes, and rules. Draws blocks from `firstBlock` on as
// long as they fit whole; returns the first block left for the next page, or
// -1 when everything was drawn. A block taller than an empty page is cut.
int drawNotes(const GfxRenderer& r, const std::vector<NoteBlock>& blocks, const int firstBlock, const int x, int y,
              const int w, const int bottom) {
  const int lh = r.getLineHeight(UI_10_FONT_ID);
  const int pageTop = y;
  for (int i = std::max(0, firstBlock); i < static_cast<int>(blocks.size()); ++i) {
    const NoteBlock& b = blocks[i];
    const bool first = y == pageTop;
    int font = UI_10_FONT_ID;
    auto style = EpdFontFamily::REGULAR;
    int gapBefore = 0;
    int textX = x;
    switch (b.kind) {
      case NoteBlock::Kind::Blank:
        if (!first) y += lh / 2;
        continue;
      case NoteBlock::Kind::Rule:
        if (y + lh > bottom) return i;
        y += lh / 3;
        for (int px = x; px < x + w; px += 2) r.drawPixel(px, y);
        y += lh / 3;
        continue;
      case NoteBlock::Kind::Heading1:
        font = UI_12_FONT_ID;
        style = EpdFontFamily::BOLD;
        gapBefore = first ? 0 : lh / 3;
        break;
      case NoteBlock::Kind::Heading2:
        style = EpdFontFamily::BOLD;
        gapBefore = first ? 0 : lh / 4;
        break;
      case NoteBlock::Kind::Paragraph:
        break;
      case NoteBlock::Kind::Bullet:
      case NoteBlock::Kind::Task:
      case NoteBlock::Kind::TaskDone:
        textX = x + 18 * std::min(b.indent, 4) + 18;
        break;
    }
    const int width = w - (textX - x);
    const int lines = static_cast<int>(r.wrappedText(font, b.text.c_str(), width, 200, style).size());
    const int height = gapBefore + std::max(1, lines) * r.getLineHeight(font);
    // Keep a block for the next page unless it alone does not fit a page.
    if (y + height > bottom && !first) return i;
    y += gapBefore;
    if (textX != x) {
      const int ix = textX - 18;
      const int mid = y + lh / 2;
      if (b.kind == NoteBlock::Kind::Bullet) {
        r.fillRect(ix + 3, mid - 2, 5, 5, true);
      } else {
        constexpr int kBox = 12;
        r.drawRect(ix, mid - kBox / 2, kBox, kBox, true);
        if (b.kind == NoteBlock::Kind::TaskDone) {
          r.drawLine(ix + 2, mid, ix + 5, mid + 3, true);
          r.drawLine(ix + 5, mid + 3, ix + 10, mid - 3, true);
        }
      }
    }
    y = drawLines(r, font, textX, y, width, b.text, 200, bottom, true, style);
    if (y + lh > bottom && i + 1 < static_cast<int>(blocks.size())) return i + 1;
  }
  return -1;
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

  // Title-row icons are 20 px; the Obsidian mark is 12 px, 2x when there is room.
  const int scale = h >= 26 ? 2 : 1;
  const int iconPx = 12 * scale;
  const int maxY = y + h - 2;
  const int titleH = r.getLineHeight(UI_10_FONT_ID);
  const int iconY = y + 3 + std::max(-1, (titleH - icons::kSize) / 2);

  // Right side, filled right to left: where it happens, then series, attachment.
  int right = x + w - 4;
  const auto pushIcon = [&](const unsigned char* bitmap) {
    right -= icons::kSize;
    drawIcon(r, bitmap, icons::kSize, icons::kSize, right, iconY, 1, textBlack);
    right -= 3;
  };
  if (const unsigned char* place = placeIcon(e)) pushIcon(place);
  if (e.isRecurring) pushIcon(e.isMoved ? icons::kSeriesMoved : icons::kSeries);
  if (e.hasAttachments) pushIcon(icons::kAttachment);
  const bool hasNote = e.hasObsidianNote || e.location.find("obsidian://") != std::string::npos;
  if (hasNote && h >= iconPx + 8 + titleH) {
    drawIcon(r, icons::kObsidian, 12, 12, x + w - 4 - iconPx, y + h - 3 - iconPx, scale, textBlack);
  }

  const int tx = x + 6;
  const int titleW = right - tx - 2;
  const int smallH = r.getLineHeight(SMALL_FONT_ID);
  const bool oneLine = h - 6 < titleH + smallH;
  std::string who = e.organizerName();
  if (e.numAttendees > 0) who += (who.empty() ? "+" : " +") + std::to_string(e.numAttendees);

  int cy = y + 3;
  if (oneLine) {
    // No room for a second line: the organizer follows the title, smaller.
    std::string org = who;
    int orgW = org.empty() ? 0 : r.getTextWidth(SMALL_FONT_ID, org.c_str());
    if (orgW > titleW * 2 / 5) {
      org = r.truncatedText(SMALL_FONT_ID, org.c_str(), titleW * 2 / 5);
      orgW = r.getTextWidth(SMALL_FONT_ID, org.c_str());
    }
    const int gap = org.empty() ? 0 : 8;
    const std::string title =
        r.truncatedText(UI_10_FONT_ID, e.subject.c_str(), titleW - orgW - gap, EpdFontFamily::BOLD);
    r.drawText(UI_10_FONT_ID, tx, cy, title.c_str(), textBlack, EpdFontFamily::BOLD);
    if (!org.empty()) {
      const int titleEnd = tx + r.getTextWidth(UI_10_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
      r.drawText(SMALL_FONT_ID, titleEnd + gap, cy + (titleH - smallH) / 2 + 1, org.c_str(), textBlack);
    }
    return;
  }
  const int titleLines = h >= 90 ? 2 : 1;
  cy = drawLines(r, UI_10_FONT_ID, tx, cy, titleW, e.subject, titleLines, maxY, textBlack, EpdFontFamily::BOLD);

  const int bx = x + 6;
  const int bw = w - 10;
  // Time, then who: the organizer and how many others are invited.
  std::string meta;
  if (e.lastDate() == e.startDate()) {
    meta = hhmm(e.startMinutes()) + "–" + hhmm(e.endMinutes() % (24 * 60));
  } else if (dayIso == e.startDate()) {
    meta = "ab " + hhmm(e.startMinutes());
  } else {
    meta = "bis " + hhmm(e.endMinutesOn(dayIso) % (24 * 60));
  }
  if (e.isCancelled) meta += "  abgesagt";
  if (!who.empty()) meta += "  ·  " + who;
  cy = drawLines(r, SMALL_FONT_ID, bx, cy, bw, meta, 1, maxY, textBlack);

  const std::string loc = e.displayLocation();
  if (!loc.empty() && loc.find("://") == std::string::npos) {
    cy = drawLines(r, SMALL_FONT_ID, bx, cy, bw, loc, 1, maxY, textBlack);
  }

  // Notes (calendar text and the Obsidian note) as plain lines, markup removed.
  if (!e.notes.empty() && cy + r.getLineHeight(SMALL_FONT_ID) <= maxY) {
    drawLines(r, SMALL_FONT_ID, bx, cy + 2, bw - (hasNote ? iconPx + 4 : 0), plainNotes(e.notes), 6, maxY,
              textBlack);
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
  // Right edge: a running sync as a white chip with its radio (Bluetooth or
  // Wi-Fi); otherwise the radio the shown data came over, plain on black.
  int textRight = w - kMargin - 4;
  const unsigned char* radioIcon = nullptr;
  const uint8_t radio = status.active ? status.active : status.via;
  if (radio == 1) radioIcon = icons::kBluetooth;
  if (radio == 2) radioIcon = icons::kWifi;
  if (radioIcon) {
    const int box = icons::kSize + 6;
    const int bx = textRight - box;
    if (status.active) {
      r.fillRoundedRect(bx, 6, box, box, 5, Color::White);
      drawIcon(r, radioIcon, icons::kSize, icons::kSize, bx + 3, 9, 1, true);
    } else {
      drawIcon(r, radioIcon, icons::kSize, icons::kSize, bx + 3, 9, 1, false);
    }
    textRight = bx - 4;
  }
  if (!right.empty()) {
    const int tw = r.getTextWidth(SMALL_FONT_ID, right.c_str());
    r.drawText(SMALL_FONT_ID, textRight - tw, 10, right.c_str(), false);
  }
  // A later attempt that brought nothing: when the device last tried.
  if (!status.statusText && !status.active && status.lastAttemptEpoch > status.lastSyncEpoch + 60) {
    const time_t t = static_cast<time_t>(status.lastAttemptEpoch);
    struct tm lt {};
    localtime_r(&t, &lt);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "kein Kontakt %02d:%02d", lt.tm_hour, lt.tm_min);
    const int tw = r.getTextWidth(SMALL_FONT_ID, buf);
    r.drawText(SMALL_FONT_ID, w - kMargin - 4 - tw, 10 + r.getLineHeight(SMALL_FONT_ID), buf, false);
  }
  return h;
}

namespace {

struct DaySplit {
  std::string iso;
  std::vector<const CalEvent*> bars;
  std::vector<const CalEvent*> timed;
};

DaySplit splitDay(const std::vector<CalEvent>& events, const Date day) {
  DaySplit d;
  d.iso = isoString(day);
  for (const CalEvent* e : eventsOnDate(events, d.iso)) (e->barOn(d.iso) ? d.bars : d.timed).push_back(e);
  return d;
}

int barHeight(const GfxRenderer& r) { return r.getLineHeight(UI_10_FONT_ID) + 6; }

// Visible hours: 07-17 by default, widened to cover the day's timed events plus
// half an hour of free space below the last one, so the end is visibly the end.
// A segment coming from or running into another day does not stretch the axis.
constexpr int kTailMinutes = 30;
void widenHours(const DaySplit& d, int& firstHour, int& lastHour) {
  for (const CalEvent* e : d.timed) {
    const int s0 = e->startMinutesOn(d.iso);
    const int e0 = e->endMinutesOn(d.iso);
    if (s0 > 0) firstHour = std::min(firstHour, s0 / 60);
    if (e0 < 24 * 60) lastHour = std::max(lastHour, (e0 + kTailMinutes + 59) / 60);
  }
  lastHour = std::min(lastHour, 24);
}

// Day bars from y down; returns the y below them.
int drawBars(const GfxRenderer& r, const DaySplit& d, int x, int w, int y, int bottom, std::vector<HitBox>* hits) {
  const int barH = barHeight(r);
  for (const CalEvent* e : d.bars) {
    if (y + barH > bottom) break;
    const bool family = e->calendar == kFamilyCalendar && !e->isCancelled;
    if (e->isCancelled) {
      dottedRect(r, x, y, w, barH);
    } else {
      r.fillRoundedRect(x, y, w, barH, 4, family ? Color::DarkGray : Color::LightGray);
    }
    const std::string t = r.truncatedText(UI_10_FONT_ID, e->subject.c_str(), w - 12);
    r.drawText(UI_10_FONT_ID, x + 6, y + 3, t.c_str(), !family);
    if (hits) hits->push_back({x, y, w, barH, e});
    y += barH + 3;
  }
  return y;
}

struct Axis {
  int top, bottom, firstHour, lastHour;
  int yOf(int minutes) const {
    const float pxPerMin = static_cast<float>(bottom - top) / ((lastHour - firstHour) * 60);
    return top + static_cast<int>((minutes - firstHour * 60) * pxPerMin);
  }
};

void drawAxis(const GfxRenderer& r, const Axis& a, int lineX0, int lineX1) {
  for (int h = a.firstHour; h <= a.lastHour; ++h) {
    const int ly = a.yOf(h * 60);
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02d", h % 24);
    // Digits span roughly the lower 70% of the ascender: center them on the line.
    r.drawText(SMALL_FONT_ID, kMargin - 4, ly - r.getFontAscenderSize(SMALL_FONT_ID) * 64 / 100, buf, true);
    for (int x = lineX0; x < lineX1; x += 4) r.drawPixel(x, ly);
  }
}

std::string shortDate(const std::string& iso);  // defined with the detail page

// A box cut off by the bottom of the axis gets a torn lower edge and says where
// the event really ends, so it does not read as ending at the edge.
void drawContinues(const GfxRenderer& r, const CalEvent& e, const std::string& dayIso, int x, int y, int w, int h) {
  const int lh = r.getLineHeight(SMALL_FONT_ID);
  constexpr int kTooth = 10;  // width of one tooth of the torn edge
  constexpr int kDepth = 5;
  // Torn edge: white teeth cut upwards into the bottom of the fill.
  const int base = y + h - 1;
  for (int tx = x; tx < x + w; tx += kTooth) {
    for (int i = 0; i < kDepth; ++i) {
      const int half = (kTooth / 2) * (kDepth - i) / kDepth;
      const int cx = tx + kTooth / 2;
      r.drawLine(std::max(x, cx - half), base - i, std::min(x + w - 1, cx + half), base - i, false);
    }
  }
  if (h < 3 * lh + kDepth) return;

  std::string text = "bis ";
  if (e.lastDate() != dayIso) {
    text += shortDate(e.lastDate());
    if (!e.isAllDay && e.end.size() >= 16) text += " " + e.end.substr(11, 5);
  } else {
    text += hhmm(e.endMinutes() % (24 * 60));
  }
  constexpr int kArrow = 10;
  const int ax = x + 7;
  const int ty = base - kDepth - lh - 2;
  const std::string t = r.truncatedText(SMALL_FONT_ID, text.c_str(), w - (ax - x) - kArrow - 12, EpdFontFamily::BOLD);
  // Fresh fill behind the line, in case notes run this far down.
  r.fillRectDither(x + 1, ty - 2, w - 2, lh + 3, Color::LightGray);
  // Filled down chevron, centred on the text line.
  const int ay = ty + (lh - kArrow / 2) / 2;
  for (int i = 0; i <= kArrow / 2; ++i) r.drawLine(ax + i, ay + i, ax + kArrow - i, ay + i, true);
  r.drawText(SMALL_FONT_ID, ax + kArrow + 5, ty, t.c_str(), true, EpdFontFamily::BOLD);
}

void drawTimed(const GfxRenderer& r, const DaySplit& d, const Axis& a, int areaX, int areaW,
               std::vector<HitBox>* hits) {
  std::vector<LayoutInput> inputs;
  inputs.reserve(d.timed.size());
  for (const CalEvent* e : d.timed) inputs.push_back({e->startMinutesOn(d.iso), e->endMinutesOn(d.iso)});
  for (const LayoutBox& b : layoutDay(inputs)) {
    const int colW = (areaW - kColGap * (b.groupColumns - 1)) / b.groupColumns;
    const int bx = areaX + b.column * (colW + kColGap);
    const int bw = colW * b.colSpan + kColGap * (b.colSpan - 1);
    const int by = a.yOf(std::max(b.startMin, a.firstHour * 60)) + 1;
    const int bh = std::max(kMinBoxH, a.yOf(std::min(b.endMin, a.lastHour * 60)) - by - 1);
    const int boxH = std::min(bh, a.bottom - by);
    drawEventBox(r, *d.timed[b.eventIndex], d.iso, bx, by, bw, boxH);
    if (b.endMin > a.lastHour * 60) drawContinues(r, *d.timed[b.eventIndex], d.iso, bx, by, bw, boxH);
    if (hits) hits->push_back({bx, by, bw, boxH, d.timed[b.eventIndex]});
  }
}

constexpr int kPeekW = 90;  // the next day's column, to see how full it is more than to read it

// The next working day on the same hour axis: blocks with a short title, so gaps
// and busy stretches show at a glance. Bars (all-day events) are left out.
void drawPeek(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date next, const Axis& a,
              const int labelY, const int x, const int w) {
  char label[16];
  std::snprintf(label, sizeof(label), "%s %d.", weekdayShortDe(weekdayMon0(next)), next.day);
  const int tw = r.getTextWidth(SMALL_FONT_ID, label, EpdFontFamily::BOLD);
  r.drawText(SMALL_FONT_ID, x + (w - tw) / 2, labelY, label, true, EpdFontFamily::BOLD);
  for (int y = a.top; y < a.bottom; y += 3) r.drawPixel(x - 3, y);  // dotted divider

  const DaySplit nd = splitDay(events, next);
  const int lh = r.getLineHeight(SMALL_FONT_ID);
  std::vector<LayoutInput> inputs;
  inputs.reserve(nd.timed.size());
  for (const CalEvent* e : nd.timed) inputs.push_back({e->startMinutesOn(nd.iso), e->endMinutesOn(nd.iso)});
  for (const LayoutBox& b : layoutDay(inputs)) {
    if (b.endMin <= a.firstHour * 60 || b.startMin >= a.lastHour * 60) continue;
    const int colW = (w - 2 * (b.groupColumns - 1)) / b.groupColumns;
    const int bx = x + b.column * (colW + 2);
    const int bw = colW * b.colSpan + 2 * (b.colSpan - 1);
    const int by = a.yOf(std::max(b.startMin, a.firstHour * 60)) + 1;
    const int bh = std::max(6, a.yOf(std::min(b.endMin, a.lastHour * 60)) - by - 1);
    const CalEvent& e = *nd.timed[b.eventIndex];
    if (e.isCancelled) {
      r.drawRoundedRect(bx, by, bw, bh, 1, 3, true);
    } else {
      r.fillRoundedRect(bx, by, bw, bh, 3, Color::DarkGray);
      r.fillRoundedRect(bx + 1, by + 1, bw - 2, bh - 2, 2, Color::LightGray);
    }
    // As much of the title as fits, then the start time when the block is tall enough.
    if (bh >= lh + 2 && bw >= 24) {
      const std::string title = r.truncatedText(SMALL_FONT_ID, e.subject.c_str(), bw - 6);
      r.drawText(SMALL_FONT_ID, bx + 3, by + 1, title.c_str(), true);
      if (bh >= 2 * lh + 2 && e.startDate() == nd.iso) {
        r.drawText(SMALL_FONT_ID, bx + 3, by + 1 + lh, hhmm(e.startMinutes()).c_str(), true);
      }
    }
  }
}

void drawNowLine(const GfxRenderer& r, const Axis& a, int x0, int x1) {
  const time_t now = time(nullptr);
  struct tm lt {};
  localtime_r(&now, &lt);
  const int m = lt.tm_hour * 60 + lt.tm_min;
  if (m < a.firstHour * 60 || m > a.lastHour * 60) return;
  const int ny = a.yOf(m);
  r.fillRect(x0, ny - 1, x1 - x0, 3, true);
  r.fillRoundedRect(x0 - 4, ny - 5, 10, 10, 5, Color::Black);
}

}  // namespace

bool isWeekend(const Date d) { return weekdayMon0(d) >= 5; }

Date peekDay(const Date day) {
  Date next = addDays(day, 1);
  while (isWeekend(next)) next = addDays(next, 1);
  return next;
}

// The Deskleaf free-day motif with a caption below it.
void drawFreeDay(const GfxRenderer& r, const char* caption, const int top, const int bottom) {
  const int captionH = r.getLineHeight(UI_12_FONT_ID) + 10;
  const int artH = art::drawFreeDay(r, 0, top + 4, r.getScreenWidth(), bottom - top - 4 - captionH);
  const int textY = artH > 0 ? top + 4 + artH + 6 : (top + bottom) / 2 - captionH / 2;
  r.drawCenteredText(UI_12_FONT_ID, textY, caption, true, EpdFontFamily::BOLD);
}

void drawDay(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date day, const Date today,
             const Status& status, const int top, const int bottom, std::vector<HitBox>* hits) {
  if (hits) hits->clear();
  if (isWeekend(day)) return drawWeekend(r, events, day, today, status, top, bottom, hits);
  const int w = r.getScreenWidth();
  const DaySplit d = splitDay(events, day);
  const int y = drawBars(r, d, kMargin, w - 2 * kMargin, top + 6, bottom, hits);
  const Date next = peekDay(day);
  const DaySplit nd = splitDay(events, next);
  const bool peek = status.peekNextDay && (!d.timed.empty() || !nd.timed.empty());

  if (d.timed.empty() && !peek) {
    if (d.bars.empty()) {
      drawFreeDay(r, sameDate(day, today) ? "Heute bin ich frei!" : "Frei!", y, bottom);
    }
    return;
  }

  const int lh = r.getLineHeight(SMALL_FONT_ID);
  Axis a{y + 8 + (peek ? lh : 0), bottom - 6, 7, 17};
  widenHours(d, a.firstHour, a.lastHour);
  if (peek) widenHours(nd, a.firstHour, a.lastHour);
  const int right = w - kMargin - (peek ? kPeekW + 8 : 0);
  drawAxis(r, a, kMargin + kAxisW - 6, w - kMargin);
  drawTimed(r, d, a, kMargin + kAxisW, right - (kMargin + kAxisW), hits);
  if (peek) {
    const int px = w - kMargin - kPeekW;
    drawPeek(r, events, next, a, y + 4, px, kPeekW);
    if (hits) hits->push_back({px - 4, y, kPeekW + 4 + kMargin, a.bottom - y, nullptr});
  }
  if (status.showNowLine && sameDate(day, today)) drawNowLine(r, a, kMargin + kAxisW - 10, right);
}

void drawWeekend(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date anyWeekendDay,
                 const Date today, const Status& status, const int top, const int bottom, std::vector<HitBox>* hits) {
  if (hits) hits->clear();
  const int w = r.getScreenWidth();
  const Date sat = addDays(anyWeekendDay, 5 - weekdayMon0(anyWeekendDay));
  const Date days[2] = {sat, addDays(sat, 1)};
  const DaySplit split[2] = {splitDay(events, days[0]), splitDay(events, days[1])};

  constexpr int kGap = 8;
  const int areaX = kMargin + kAxisW;
  const int colW = (w - kMargin - areaX - kGap) / 2;
  const int colX[2] = {areaX, areaX + colW + kGap};

  // Column heads: weekday and date; today inverted.
  const int headH = r.getLineHeight(UI_10_FONT_ID) + 8;
  for (int i = 0; i < 2; ++i) {
    const bool isToday = sameDate(days[i], today);
    if (isToday) r.fillRoundedRect(colX[i], top + 4, colW, headH, 4, Color::Black);
    char label[24];
    std::snprintf(label, sizeof(label), "%s %d.%d.", weekdayShortDe(5 + i), days[i].day, days[i].month);
    const int tw = r.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    r.drawText(UI_10_FONT_ID, colX[i] + (colW - tw) / 2, top + 8, label, !isToday, EpdFontFamily::BOLD);
  }

  // Bars per column; the shared axis starts below the taller stack.
  const int barsTop = top + 4 + headH + 6;
  int axisTop = barsTop;
  for (int i = 0; i < 2; ++i) {
    axisTop = std::max(axisTop, drawBars(r, split[i], colX[i], colW, barsTop, bottom, hits));
  }

  if (split[0].bars.empty() && split[0].timed.empty() && split[1].bars.empty() && split[1].timed.empty()) {
    drawFreeDay(r, "Freies Wochenende!", axisTop, bottom);
    return;
  }

  Axis a{axisTop + 8, bottom - 6, 7, 17};
  bool anyTimed = false;
  for (const auto& d : split) {
    widenHours(d, a.firstHour, a.lastHour);
    anyTimed = anyTimed || !d.timed.empty();
  }
  if (anyTimed) drawAxis(r, a, areaX - 6, w - kMargin);
  for (int i = 0; i < 2; ++i) {
    if (split[i].timed.empty() && split[i].bars.empty()) {
      const char* msg = "frei";
      const int tw = r.getTextWidth(UI_12_FONT_ID, msg, EpdFontFamily::BOLD);
      r.drawText(UI_12_FONT_ID, colX[i] + (colW - tw) / 2, (axisTop + bottom) / 2, msg, true, EpdFontFamily::BOLD);
      continue;
    }
    if (anyTimed) drawTimed(r, split[i], a, colX[i], colW, hits);
    if (anyTimed && status.showNowLine && sameDate(days[i], today)) drawNowLine(r, a, colX[i], colX[i] + colW);
  }
}

namespace {

// What kind of event it is, as icon + short word: the call service or the
// place, series, attachment, Obsidian note, cancelled.
struct Chip {
  const unsigned char* icon;  // 20x20, or nullptr for text only
  std::string label;
};

std::vector<Chip> chipsFor(const CalEvent& e) {
  std::vector<Chip> chips;
  if (const unsigned char* place = placeIcon(e)) {
    const char* label = platformLabel(e.platform());
    chips.push_back({place, label ? label : "Vor Ort"});
  }
  if (e.isRecurring) chips.push_back({e.isMoved ? icons::kSeriesMoved : icons::kSeries, e.isMoved ? "verschoben" : "Serie"});
  if (e.hasAttachments) chips.push_back({icons::kAttachment, "Anhang"});
  if (e.hasObsidianNote || e.location.find("obsidian://") != std::string::npos) chips.push_back({nullptr, "Notiz"});
  if (e.isImportant()) chips.push_back({nullptr, "wichtig"});
  if (e.isCancelled) chips.push_back({nullptr, "abgesagt"});
  return chips;
}

// One row of chips, wrapping; returns the y below. draw=false only measures.
int drawChips(const GfxRenderer& r, const std::vector<Chip>& chips, const int x, int y, const int w, const bool draw,
              const bool black = true) {
  if (chips.empty()) return y;
  const int lh = std::max(icons::kSize, r.getLineHeight(UI_10_FONT_ID));
  int cx = x;
  for (const Chip& c : chips) {
    const int textW = r.getTextWidth(UI_10_FONT_ID, c.label.c_str());
    const int chipW = (c.icon ? icons::kSize + 5 : 0) + textW;
    if (cx > x && cx + chipW > x + w) {
      cx = x;
      y += lh + 4;
    }
    if (draw) {
      if (c.icon) drawIcon(r, c.icon, icons::kSize, icons::kSize, cx, y + (lh - icons::kSize) / 2, 1, black);
      r.drawText(UI_10_FONT_ID, cx + (c.icon ? icons::kSize + 5 : 0), y + (lh - r.getLineHeight(UI_10_FONT_ID)) / 2,
                 c.label.c_str(), black);
    }
    cx += chipW + 16;
  }
  return y + lh;
}

std::string shortDate(const std::string& iso) {
  Date d{};
  if (!parseIsoDate(iso, d)) return iso;
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%s %d.%d.", weekdayShortDe(weekdayMon0(d)), d.day, d.month);
  return buf;
}

// "Fr 6.11. · 11:30–12:10 · 40 min", "Fr 6.11. · ganztägig", "Di 10.11. 09:00 – Mi 11.11. 17:00",
// "Fr 6.11. – So 8.11.".
std::string whenLine(const CalEvent& e) {
  std::string out = shortDate(e.startDate());
  if (e.lastDate() != e.startDate()) {
    // Both ends with their date and no "  ·  ", so the detail header keeps the whole line.
    if (!e.isAllDay && e.end.size() >= 16) {
      return out + " " + hhmm(e.startMinutes()) + " – " + shortDate(e.lastDate()) + " " + e.end.substr(11, 5);
    }
    return out + " – " + shortDate(e.lastDate());
  }
  if (e.isAllDay) return out + "  ·  ganztägig";
  const int minutes = e.endMinutes() - e.startMinutes();
  out += "  ·  " + hhmm(e.startMinutes()) + "–" + hhmm(e.endMinutes() % (24 * 60));
  if (minutes > 0) {
    char dur[24];
    if (minutes % 60 == 0) {
      std::snprintf(dur, sizeof(dur), "%d h", minutes / 60);
    } else if (minutes > 60) {
      std::snprintf(dur, sizeof(dur), "%d h %d min", minutes / 60, minutes % 60);
    } else {
      std::snprintf(dur, sizeof(dur), "%d min", minutes);
    }
    out += "  ·  " + std::string(dur);
  }
  return out;
}

std::string whoLine(const CalEvent& e) {
  std::string who = e.organizerName();
  if (e.numAttendees > 0) who += (who.empty() ? "" : "  ·  ") + ("+" + std::to_string(e.numAttendees));
  return who;
}

// A labelled field; empty values are skipped. Returns the y below.
int drawField(const GfxRenderer& r, const char* label, const std::string& value, const int x, int y, const int w,
              const int bottom, const bool draw) {
  if (value.empty()) return y;
  y += 6;
  if (draw) r.drawText(SMALL_FONT_ID, x, y, label, true, EpdFontFamily::BOLD);
  y += r.getLineHeight(SMALL_FONT_ID);
  if (!draw) {
    const int lines = static_cast<int>(r.wrappedText(UI_10_FONT_ID, value.c_str(), w, 3).size());
    return y + std::max(1, lines) * r.getLineHeight(UI_10_FONT_ID);
  }
  return drawLines(r, UI_10_FONT_ID, x, y, w, value, 3, bottom, true);
}

// Popup card body; draw=false measures. Returns the y below the content.
int popupBody(const GfxRenderer& r, const CalEvent& e, const int x, int y, const int w, const int bottom,
              const bool draw) {
  // When, and the calendar as a small tag on the right.
  const int tagW = r.getTextWidth(SMALL_FONT_ID, e.calendar.c_str(), EpdFontFamily::BOLD) + 14;
  const int whenW = e.calendar.empty() ? w : w - tagW - 8;
  const std::string when = r.truncatedText(SMALL_FONT_ID, whenLine(e).c_str(), whenW);
  const int smallH = r.getLineHeight(SMALL_FONT_ID);
  if (draw) {
    r.drawText(SMALL_FONT_ID, x, y + 2, when.c_str(), true);
    if (!e.calendar.empty()) {
      r.drawRoundedRect(x + w - tagW, y, tagW, smallH + 4, 1, 8, true);
      r.drawText(SMALL_FONT_ID, x + w - tagW + 7, y + 2, e.calendar.c_str(), true, EpdFontFamily::BOLD);
    }
  }
  y += smallH + 10;
  const int titleLines =
      std::max(1, static_cast<int>(r.wrappedText(UI_12_FONT_ID, e.subject.c_str(), w, 3, EpdFontFamily::BOLD).size()));
  if (draw) drawLines(r, UI_12_FONT_ID, x, y, w, e.subject, 3, bottom, true, EpdFontFamily::BOLD);
  y += titleLines * r.getLineHeight(UI_12_FONT_ID) + 6;
  y = drawChips(r, chipsFor(e), x, y, w, draw);

  const std::string place = e.isOnSite() ? e.displayLocation() : std::string();
  const std::string who = whoLine(e);
  const std::string notes = plainNotes(e.notes);
  if (!place.empty() || !who.empty() || !notes.empty()) {
    y += 8;
    if (draw) r.drawLine(x, y, x + w, y, true);
    y += 2;
  }
  y = drawField(r, "Ort", place, x, y, w, bottom, draw);
  y = drawField(r, "Organisation", who, x, y, w, bottom, draw);
  if (!notes.empty()) {
    // A preview; the full notes are on the detail page.
    constexpr int kPreviewLines = 4;
    const auto lines = r.wrappedText(SMALL_FONT_ID, notes.c_str(), w, kPreviewLines + 1);
    const int shown = std::min(kPreviewLines, static_cast<int>(lines.size()));
    y += 8;
    if (draw) drawLines(r, SMALL_FONT_ID, x, y, w, notes, kPreviewLines, bottom, true);
    y += shown * smallH;
  }
  return y;
}

}  // namespace

void fadeScreen(const GfxRenderer& r) {
  // Every other dot white: black becomes mid gray, gray becomes lighter.
  const int w = r.getScreenWidth();
  const int h = r.getScreenHeight();
  for (int y = 0; y < h; ++y) {
    for (int x = y & 1; x < w; x += 2) r.drawPixel(x, y, false);
  }
}

HitBox drawPopup(const GfxRenderer& r, const CalEvent& e, const int top, const int bottom) {
  fadeScreen(r);
  const int w = r.getScreenWidth();
  constexpr int kOuter = 20;
  constexpr int kPad = 16;
  const int cardW = w - 2 * kOuter;
  const int innerW = cardW - 2 * kPad;
  const int hintH = r.getLineHeight(SMALL_FONT_ID) + 8;
  const int maxH = bottom - top - 2 * kOuter;
  const int contentH = popupBody(r, e, 0, 0, innerW, maxH, false);
  const int cardH = std::min(maxH, contentH + 2 * kPad + hintH);
  const int cardX = kOuter;
  const int cardY = top + (bottom - top - cardH) / 2;
  // Shadow, white card, frame.
  r.fillRect(cardX + 4, cardY + 4, cardW, cardH, true);
  r.fillRect(cardX, cardY, cardW, cardH, false);
  r.drawRect(cardX, cardY, cardW, cardH, 2, true);
  popupBody(r, e, cardX + kPad, cardY + kPad, innerW, cardY + cardH - hintH, true);
  const char* hint = "Tippen für alle Details";
  r.drawText(SMALL_FONT_ID, cardX + kPad, cardY + cardH - hintH, hint, true);
  return HitBox{cardX, cardY, cardW, cardH, &e};
}

int drawDetail(const GfxRenderer& r, const CalEvent& e, const int bottom, const int firstNoteBlock) {
  const int w = r.getScreenWidth();
  const int x = kMargin + 8;
  const int tw = w - 2 * x;
  // Header: calendar and date small, the title, then the time.
  const int smallH = r.getLineHeight(SMALL_FONT_ID);
  const int titleH = r.getLineHeight(UI_12_FONT_ID);
  const int titleLines =
      std::max(1, static_cast<int>(r.wrappedText(UI_12_FONT_ID, e.subject.c_str(), tw, 3, EpdFontFamily::BOLD).size()));
  const int headerH = 10 + smallH + 4 + titleLines * titleH + 4 + r.getLineHeight(UI_10_FONT_ID) + 10;
  r.fillRect(0, 0, w, headerH, true);
  std::string top = e.calendar.empty() ? shortDate(e.startDate()) : e.calendar + "  ·  " + shortDate(e.startDate());
  if (firstNoteBlock >= 0) top += "  ·  Fortsetzung";
  r.drawText(SMALL_FONT_ID, x, 10, top.c_str(), false);
  int y = drawLines(r, UI_12_FONT_ID, x, 10 + smallH + 4, tw, e.subject, 3, headerH, false, EpdFontFamily::BOLD);
  const std::string when = whenLine(e);
  const size_t dot = when.find("  ·  ");
  // The date is already above; the line below the title is the time.
  r.drawText(UI_10_FONT_ID, x, y + 4, (dot == std::string::npos ? when : when.substr(dot + 5)).c_str(), false);
  y = headerH + 12;

  const std::vector<NoteBlock> blocks = parseNotes(e.notes);
  const int notesBottom = bottom - smallH - 4;
  const auto more = [&](int next) {
    if (next >= 0) {
      const char* label = "weiter »";
      r.drawText(SMALL_FONT_ID, w - x - r.getTextWidth(SMALL_FONT_ID, label), notesBottom + 2, label, true);
    }
    return next;
  };
  // Later pages carry only notes.
  if (firstNoteBlock >= 0) return more(drawNotes(r, blocks, firstNoteBlock, x, y, tw, notesBottom));

  y = drawChips(r, chipsFor(e), x, y, tw, true);
  y = drawField(r, "Ort", e.isOnSite() ? e.displayLocation() : std::string(), x, y, tw, bottom, true);
  y = drawField(r, "Organisation", e.organizerName(), x, y, tw, bottom, true);
  y = drawField(r, "Teilnehmer", e.numAttendees > 0 ? std::to_string(e.numAttendees) + " weitere" : std::string(), x,
                y, tw, bottom, true);

  if (blocks.empty()) return -1;
  if (y + 2 * smallH >= notesBottom) return more(0);  // notes start on page 2
  y += 12;
  r.drawLine(x, y, w - x, y, true);
  y += 10;
  return more(drawNotes(r, blocks, 0, x, y, tw, notesBottom));
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
      std::string text = e.barOn(dIso)                ? std::string("ganztags  ")
                         : dIso != e.startDate()      ? "bis " + hhmm(e.endMinutesOn(dIso) % (24 * 60)) + "  "
                         : e.lastDate() != dIso       ? "ab " + hhmm(e.startMinutes()) + "  "
                                                      : hhmm(e.startMinutes()) + "–" + hhmm(e.endMinutes() % (24 * 60)) + "  ";
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
  const int x0 = kMonthGridX;
  const int colW = monthColumnWidth(w);
  const int gridR = x0 + 7 * colW;
  const int lh = r.getLineHeight(SMALL_FONT_ID);
  const int headH = lh + 6;
  const Date first{year, month, 1};
  const int lead = weekdayMon0(first);
  const int days = daysInMonth(year, month);
  const int rows = (lead + days + 6) / 7;
  const int cellH = (bottom - top - headH) / rows;

  for (int c = 0; c < 7; ++c) {
    r.drawText(SMALL_FONT_ID, x0 + c * colW + 4, top + 2, weekdayShortDe(c), true, EpdFontFamily::BOLD);
  }
  for (int row = 0; row < rows; ++row) {
    const int y = top + headH + row * cellH;
    const Date rowMonday = addDays(first, row * 7 - lead);
    r.drawLine(x0, y, gridR, y, true);
    for (int c = 0; c < 7; ++c) {
      const Date d = addDays(rowMonday, c);
      const int x = x0 + c * colW;
      const bool inMonth = d.month == month;
      const bool isToday = inMonth && sameDate(d, today);
      // The calendar week sits at the bottom of the Monday cell, also when
      // that Monday belongs to the previous month.
      const bool showsWeek = c == 0;
      if (inMonth) {
        if (isToday) {
          r.fillRect(x + 1, y + 1, colW - 2, cellH - 2, true);
        } else if (c >= 5) {
          r.fillRectDither(x + 1, y + 1, colW - 2, cellH - 2, Color::LightGray);
        }
        const std::string num = std::to_string(d.day);
        r.drawText(UI_10_FONT_ID, x + 4, y + 3, num.c_str(), !isToday, EpdFontFamily::BOLD);

        // One line per event while it fits, then a count.
        const auto dayEvents = eventsOnDate(events, isoString(d));
        int ty = y + 5 + r.getLineHeight(UI_10_FONT_ID);
        const int textBottom = y + cellH - 2 - (showsWeek ? lh : 0);
        const int maxLines = std::max(0, (textBottom - ty) / lh);
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
      if (showsWeek) {
        const std::string kw = "KW " + std::to_string(isoWeek(rowMonday));
        r.drawText(SMALL_FONT_ID, x + 3, y + cellH - 1 - lh, kw.c_str(), !isToday);
      }
    }
  }
  r.drawLine(x0, top + headH + rows * cellH, gridR, top + headH + rows * cellH, true);
}

void dayTitle(const Date day, char* title, size_t titleSize, char* subtitle, size_t subtitleSize, bool withYear) {
  if (isWeekend(day)) {
    const Date sat = addDays(day, 5 - weekdayMon0(day));
    const Date sun = addDays(sat, 1);
    std::snprintf(title, titleSize, "Wochenende");
    if (sat.month == sun.month) {
      std::snprintf(subtitle, subtitleSize, "%d.–%d. %s · KW %d", sat.day, sun.day, monthNameDe(sun.month), isoWeek(sat));
    } else {
      std::snprintf(subtitle, subtitleSize, "%d.%d. – %d.%d. · KW %d", sat.day, sat.month, sun.day, sun.month,
                    isoWeek(sat));
    }
    return;
  }
  std::snprintf(title, titleSize, "%s", weekdayNameDe(weekdayMon0(day)));
  if (withYear) {
    std::snprintf(subtitle, subtitleSize, "%d. %s %d · KW %d", day.day, monthNameDe(day.month), day.year, isoWeek(day));
  } else {
    std::snprintf(subtitle, subtitleSize, "%d. %s · KW %d", day.day, monthNameDe(day.month), isoWeek(day));
  }
}

void drawSleepScreen(const GfxRenderer& r, const std::vector<CalEvent>& events, const Date today,
                     const Status& status) {
  r.clearScreen();
  char title[32];
  char subtitle[48];
  dayTitle(today, title, sizeof(title), subtitle, sizeof(subtitle), false);
  const int top = drawHeader(r, title, subtitle, status);
  drawDay(r, events, today, today, status, top, r.getScreenHeight() - 4);
}

}  // namespace deskleaf::view

#endif  // DESKLEAF_CALENDAR
