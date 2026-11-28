#include "SpickActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "deskleaf/DeskleafTodos.h"
#include "deskleaf/DeskleafWifiSync.h"
#include "fontIds.h"

namespace {
constexpr const char* kDir = "/Spick";
constexpr int kMargin = 20;

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// Inline Markdown the screen cannot show: **bold**, `code`, [text](link).
std::string plain(std::string s) {
  for (const char* mark : {"**", "__", "`"}) {
    for (size_t p; (p = s.find(mark)) != std::string::npos;) s.erase(p, std::string(mark).size());
  }
  for (size_t open; (open = s.find('[')) != std::string::npos;) {
    const size_t mid = s.find("](", open);
    const size_t close = mid == std::string::npos ? mid : s.find(')', mid);
    if (close == std::string::npos) break;
    s = s.substr(0, open) + s.substr(open + 1, mid - open - 1) + s.substr(close + 1);
  }
  return s;
}

std::string titleOf(const std::string& path, const std::string& file) {
  const String raw = Storage.readFile(path.c_str());
  const std::string text(raw.c_str());
  const size_t nl = text.find('\n');
  const std::string first = trim(text.substr(0, nl));
  if (first.rfind("# ", 0) == 0) return plain(first.substr(2));
  // Fall back to the file name without the arrival prefix and extension.
  std::string name = file.substr(file.find('-') + 1);
  if (name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0) name.resize(name.size() - 3);
  return name;
}
}  // namespace

void SpickActivity::onEnter() {
  Activity::onEnter();
  rescueInboxNotes();
  loadNotes();
  if (!files.empty()) openNote(0);
  fetchNow();  // in the background: the stored notes show meanwhile
  requestUpdate();
}

// Firmware before 2.0.69 saved notes to /Inbox under their plain name.
void SpickActivity::rescueInboxNotes() {
  for (const String& f : Storage.listFiles("/Inbox", 200)) {
    const std::string name(f.c_str());
    if (name.size() <= 3 || name.compare(name.size() - 3, 3, ".md") != 0 || name[0] == '.') continue;
    Storage.ensureDirectoryExists(kDir);
    // "0-" sorts before every arrival id: rescued notes count as the oldest.
    Storage.rename(("/Inbox/" + name).c_str(), (std::string(kDir) + "/0-" + name).c_str());
  }
}

void SpickActivity::loadNotes() {
  files.clear();
  titles.clear();
  for (const String& f : Storage.listFiles(kDir, 200)) {
    const std::string name(f.c_str());
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0 && name[0] != '.') files.push_back(name);
  }
  std::sort(files.begin(), files.end(), std::greater<std::string>());  // arrival prefix: newest first
  for (const std::string& f : files) titles.push_back(titleOf(std::string(kDir) + "/" + f, f));
}

void SpickActivity::openNote(const int index) {
  open = index;
  page = 0;
  armedDelete = -1;
  layout();
}

void SpickActivity::layout() {
  lines.clear();
  pageStarts.clear();
  if (open < 0 || open >= static_cast<int>(files.size())) return;
  const String raw = Storage.readFile((std::string(kDir) + "/" + files[open]).c_str());
  const std::string text(raw.c_str());
  const int width = renderer.getScreenWidth() - 2 * kMargin;
  size_t pos = 0;
  bool first = true;
  int pendingGap = 0;  // a blank line: a little space before the next paragraph
  while (pos <= text.size()) {
    const size_t nl = text.find('\n', pos);
    std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
    if (line.empty()) {
      pendingGap = 8;
      continue;
    }
    uint8_t style = 0;
    int indent = 0;
    std::string bullet;
    if (line.rfind("# ", 0) == 0 && first) {
      style = 2;
      line = line.substr(2);
    } else if (line[0] == '#') {
      style = 1;
      line = trim(line.substr(line.find_first_not_of('#')));
    } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
      bullet = "\xE2\x80\xA2 ";
      indent = 18;
      line = line.substr(2);
    }
    uint8_t box = 0;
    if (!bullet.empty() && line.size() >= 3 && line[0] == '[' && line[2] == ']' &&
        (line[1] == ' ' || line[1] == 'x' || line[1] == 'X')) {
      box = line[1] == ' ' ? 1 : 2;
      bullet.clear();
      indent = 28;  // room for the box
      line = trim(line.substr(3));
    }
    first = false;
    line = plain(line);
    const int font = style ? UI_12_FONT_ID : UI_12_FONT_ID;
    const auto ef = style ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto wrapped = renderer.wrappedText(font, line.c_str(), width - indent, 50, ef);
    for (size_t i = 0; i < wrapped.size(); ++i) {
      Line l{wrapped[i], style, indent, 0, 0};
      if (i == 0) {
        l.box = box;
        l.gapBefore = (style == 1 ? 14 : style == 2 ? 0 : 4) + pendingGap;
        pendingGap = 0;
        if (!bullet.empty()) l.text = bullet + l.text, l.indent = 0;
      }
      lines.push_back(l);
    }
  }
  // Pages: as many lines as fit between the top and the button hints.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = 24;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 28;
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  int y = top;
  pageStarts.push_back(0);
  for (size_t i = 0; i < lines.size(); ++i) {
    const int need = lines[i].gapBefore + lh + (lines[i].style == 2 ? 10 : 0);
    if (y + need > bottom && i > static_cast<size_t>(pageStarts.back())) {
      pageStarts.push_back(static_cast<int>(i));
      y = top;
    }
    y += need;
  }
}

void SpickActivity::fetchNow() {
  if (!deskleaf::wifisync::configured()) {
    message = "Kein Feed-Token auf der SD-Karte";
    return;
  }
  if (fetching) return;
  fetching = deskleaf::todos::background::start(/*inbox=*/true, /*todos=*/false);
}

void SpickActivity::loop() {
  using B = MappedInputManager::Button;
  size_t saved = 0;
  if (fetching && deskleaf::todos::background::finished(&saved)) {
    fetching = false;
    message = saved == 0 ? std::string("Keine neuen Zettel") : std::string();
    if (saved > 0) {
      loadNotes();
      if (!files.empty()) openNote(0);  // the newest is what was just sent
    }
    requestUpdate();
  }
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    if (open >= 0) {
      open = -1;
      message.clear();
      requestUpdate();
    } else {
      onGoHome(HomeMenuItem::SPICK);
    }
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    fetchNow();
    requestUpdate();
    return;
  }

  if (open >= 0) {
    const int pages = static_cast<int>(pageStarts.size());
    int step = 0;
    if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) ||
        mappedInput.wasReleased(B::PageForward)) {
      step = 1;
    }
    if (mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::PageBack)) {
      step = -1;
    }
    if (mappedInput.hasTouch()) {
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) step = 1;
      if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) step = -1;
      int tx = 0, ty = 0;
      if (mappedInput.wasScreenTapped(tx, ty)) step = tx < renderer.getScreenWidth() / 3 ? -1 : 1;
    }
    if (step != 0) {
      const int next = page + step;
      if (next >= 0 && next < pages) {
        page = next;
        requestUpdate();
      }
    }
    return;
  }
  if (!mappedInput.hasTouch() || rowH <= 0) return;
  int tx = 0, ty = 0;
  const bool longPress = mappedInput.wasScreenLongPress(tx, ty);
  if (!longPress && !mappedInput.wasScreenTapped(tx, ty)) return;
  if (ty < rowsTop) {
    // The title row: fetch as Confirm does (the button hints may be hidden).
    if (!longPress) {
      fetchNow();
      requestUpdate();
    }
    return;
  }
  const int row = (ty - rowsTop) / rowH;
  if (row >= static_cast<int>(files.size())) return;
  if (longPress) {
    if (armedDelete == row) {
      Storage.remove((std::string(kDir) + "/" + files[row]).c_str());
      armedDelete = -1;
      loadNotes();
    } else {
      armedDelete = row;  // a second long press deletes
    }
    requestUpdate();
    return;
  }
  armedDelete = -1;
  openNote(row);
  requestUpdate();
}

void SpickActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  if (open >= 0) {
    const int first = pageStarts.empty() ? 0 : pageStarts[page];
    const int last = page + 1 < static_cast<int>(pageStarts.size()) ? pageStarts[page + 1] : static_cast<int>(lines.size());
    int y = 24;
    for (int i = first; i < last; ++i) {
      const Line& l = lines[i];
      if (i != first) y += l.gapBefore;
      const auto ef = l.style ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      if (l.box) {
        constexpr int kBox = 18;
        const int by = y + (lh - kBox) / 2;
        renderer.drawRect(kMargin, by, kBox, kBox, 2, true);
        if (l.box == 2) {
          for (int t = 0; t < 2; ++t) {
            renderer.drawLine(kMargin + 4, by + 9 + t, kMargin + 7, by + 12 + t, true);
            renderer.drawLine(kMargin + 7, by + 12 + t, kMargin + 14, by + 4 + t, true);
          }
        }
      }
      renderer.drawText(UI_12_FONT_ID, kMargin + l.indent, y, l.text.c_str(), true, ef);
      y += lh;
      if (l.style == 2 && (i + 1 >= last || lines[i + 1].style != 2)) {
        renderer.drawLine(kMargin, y + 4, w - kMargin, y + 4, true);
        y += 10;
      }
    }
    if (pageStarts.size() > 1) {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d/%d", page + 1, static_cast<int>(pageStarts.size()));
      const auto& metrics = UITheme::getInstance().getMetrics();
      renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - metrics.buttonHintsHeight - 22, buf, true);
    }
  } else {
    renderer.drawText(UI_12_FONT_ID, kMargin, 24, "Spickzettel", true, EpdFontFamily::BOLD);
    const char* action = fetching ? "Hole\xE2\x80\xA6" : "Abholen";
    renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, action), 28, action, true);
    rowsTop = 24 + lh + 20;
    renderer.drawLine(kMargin, rowsTop - 8, w - kMargin, rowsTop - 8, true);
    rowH = renderer.getLineHeight(UI_10_FONT_ID) + 28;
    int y = rowsTop;
    for (size_t i = 0; i < files.size(); ++i, y += rowH) {
      const bool armed = armedDelete == static_cast<int>(i);
      const std::string label = armed ? "Nochmal lang dr\xC3\xBC" "cken: l\xC3\xB6schen" : titles[i];
      const std::string t = renderer.truncatedText(UI_10_FONT_ID, label.c_str(), w - 2 * kMargin);
      renderer.drawText(UI_10_FONT_ID, kMargin, y + 12, t.c_str(), true, i == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      for (int x = kMargin; x < w - kMargin; x += 3) renderer.drawPixel(x, y + rowH - 1);
    }
    if (files.empty()) {
      renderer.drawText(UI_10_FONT_ID, kMargin, rowsTop + 12, "Noch keine Zettel.", true);
      renderer.drawText(SMALL_FONT_ID, kMargin, rowsTop + 12 + lh + 8, "Im Chat: \xE2\x80\x9Ezeig das auf dem X4\xE2\x80\x9C.", true);
    }
  }
  if (!message.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - 40,
                              message.c_str(), true);
    message.clear();
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Abholen", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
