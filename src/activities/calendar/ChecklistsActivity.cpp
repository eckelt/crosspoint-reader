#include "ChecklistsActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kDir = "/Checklisten";  // visible in the web file manager
constexpr int kMargin = 18;
constexpr int kBox = 24;

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::string pathOf(const std::string& name) { return std::string(kDir) + "/" + name + ".txt"; }

// A file name from what the user typed: no path separators.
std::string safeName(std::string s) {
  for (char& c : s) {
    if (c == '/' || c == '\\' || c == ':' || c == '\n') c = '-';
  }
  return trim(s);
}

void seedExamples() {
  Storage.ensureDirectoryExists(kDir);
  Storage.writeFile(pathOf("Einkauf").c_str(), String("- [ ] Milch\n- [ ] Brot\n- [ ] Äpfel\n"));
  Storage.writeFile(pathOf("Workshop").c_str(),
                    String("- [ ] Beamer-Adapter\n- [ ] Klicker (X4)\n- [ ] Moderationskarten\n- [ ] Rechnung vorbereiten\n"));
}
}  // namespace

std::vector<ChecklistsActivity::Item> ChecklistsActivity::parse(const std::string& text) {
  std::vector<Item> out;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t nl = text.find('\n', pos);
    std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? text.size() : nl + 1;
    if (line.empty() || line[0] == '#') continue;
    if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) line = trim(line.substr(2));
    Item item;
    if (line.size() >= 3 && line[0] == '[' && line[2] == ']') {
      item.done = line[1] == 'x' || line[1] == 'X';
      line = trim(line.substr(3));
    }
    if (line.empty()) continue;
    item.text = line;
    out.push_back(std::move(item));
  }
  return out;
}

std::string ChecklistsActivity::serialize(const std::vector<Item>& items) {
  std::string out;
  for (const Item& i : items) out += std::string(i.done ? "- [x] " : "- [ ] ") + i.text + "\n";
  return out;
}

void ChecklistsActivity::onEnter() {
  Activity::onEnter();
  if (!Storage.exists(kDir)) seedExamples();
  open = -1;
  loadLists();
  requestUpdate();
}

void ChecklistsActivity::loadLists() {
  lists.clear();
  counts.clear();
  for (const String& f : Storage.listFiles(kDir, 100)) {
    std::string name(f.c_str());
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".txt") != 0 || name[0] == '.') continue;
    lists.push_back(name.substr(0, name.size() - 4));
  }
  std::sort(lists.begin(), lists.end());
  for (const std::string& name : lists) {
    const auto list = parse(std::string(Storage.readFile(pathOf(name).c_str()).c_str()));
    const int done = static_cast<int>(std::count_if(list.begin(), list.end(), [](const Item& i) { return i.done; }));
    counts.emplace_back(done, static_cast<int>(list.size()));
  }
}

void ChecklistsActivity::openList(const int index) {
  open = index;
  items = parse(std::string(Storage.readFile(pathOf(lists[index]).c_str()).c_str()));
  page = 0;
  requestUpdate();
}

void ChecklistsActivity::saveList() const {
  if (open < 0) return;
  Storage.writeFile(pathOf(lists[open]).c_str(), String(serialize(items).c_str()));
}

void ChecklistsActivity::startKeyboard(const Edit what, const std::string& title, const std::string& initial) {
  edit = what;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, initial, 120),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             edit = Edit::None;
                             return;
                           }
                           answer = std::get<KeyboardResult>(result.data).text;
                           answerPending = true;
                         });
}

void ChecklistsActivity::applyEdit(const std::string& raw) {
  const std::string text = trim(raw);
  switch (edit) {
    case Edit::NewList: {
      const std::string name = safeName(text);
      if (!name.empty() && !Storage.exists(pathOf(name).c_str())) {
        Storage.ensureDirectoryExists(kDir);
        Storage.writeFile(pathOf(name).c_str(), String(""));
        loadLists();
        const auto it = std::find(lists.begin(), lists.end(), name);
        if (it != lists.end()) openList(static_cast<int>(it - lists.begin()));
      }
      break;
    }
    case Edit::RenameList: {
      const std::string name = safeName(text);
      if (editIndex >= 0 && editIndex < static_cast<int>(lists.size())) {
        if (name.empty()) {
          Storage.remove(pathOf(lists[editIndex]).c_str());  // no name: the list goes
        } else if (name != lists[editIndex] && !Storage.exists(pathOf(name).c_str())) {
          Storage.rename(pathOf(lists[editIndex]).c_str(), pathOf(name).c_str());
        }
      }
      loadLists();
      break;
    }
    case Edit::NewItem:
      if (!text.empty()) {
        items.push_back({text, false});
        saveList();
        page = std::max(0, (static_cast<int>(items.size()) - 1) / std::max(1, rowsPerPage()));
      }
      break;
    case Edit::EditItem:
      if (editIndex >= 0 && editIndex < static_cast<int>(items.size())) {
        if (text.empty()) {
          items.erase(items.begin() + editIndex);  // no text: the item goes
        } else {
          items[editIndex].text = text;
        }
        saveList();
      }
      break;
    case Edit::None:
      break;
  }
  edit = Edit::None;
  requestUpdate();
}

int ChecklistsActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = 24 + renderer.getLineHeight(UI_12_FONT_ID) + 20;
  const int actions = renderer.getLineHeight(UI_10_FONT_ID) + 24;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - actions - 8;
  const int h = std::max(kBox + 16, renderer.getLineHeight(UI_10_FONT_ID) + 18);
  return std::max(1, (bottom - listTop) / h);
}

void ChecklistsActivity::handleOverviewTap(const int x, const int y, const bool longPress) {
  (void)x;
  if (rowH <= 0 || y < rowsTop) return;
  const int row = (y - rowsTop) / rowH;
  const int n = static_cast<int>(lists.size());
  if (row < n) {
    if (longPress) {
      editIndex = row;
      startKeyboard(Edit::RenameList, "Liste umbenennen (leer = löschen)", lists[row]);
    } else {
      openList(row);
    }
  } else if (row == n && !longPress) {
    startKeyboard(Edit::NewList, "Neue Liste", "");
  }
}

void ChecklistsActivity::handleListTap(const int x, const int y, const bool longPress) {
  const int w = renderer.getScreenWidth();
  if (y >= actionsTop) {
    if (longPress) return;
    const int third = w / 3;
    if (x < third) {
      startKeyboard(Edit::NewItem, "Neuer Eintrag", "");
    } else if (x < 2 * third) {
      for (Item& i : items) i.done = false;  // ready for the next workshop
      saveList();
      requestUpdate();
    } else {
      items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) { return i.done; }), items.end());
      page = 0;
      saveList();
      requestUpdate();
    }
    return;
  }
  if (rowH <= 0 || y < rowsTop) return;
  const int index = page * rowsPerPage() + (y - rowsTop) / rowH;
  if (index >= static_cast<int>(items.size())) return;
  if (longPress) {
    editIndex = index;
    startKeyboard(Edit::EditItem, "Eintrag (leer = löschen)", items[index].text);
  } else {
    items[index].done = !items[index].done;
    saveList();
    requestUpdate();
  }
}

void ChecklistsActivity::loop() {
  if (answerPending) {
    answerPending = false;
    applyEdit(answer);
    return;
  }
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    if (open >= 0) {
      open = -1;
      loadLists();
      requestUpdate();
    } else {
      onGoHome(HomeMenuItem::CHECKLISTS);
    }
    return;
  }
  if (open >= 0) {
    if (mappedInput.wasReleased(B::Confirm)) {
      startKeyboard(Edit::NewItem, "Neuer Eintrag", "");
      return;
    }
    const int pages = std::max(1, (static_cast<int>(items.size()) + rowsPerPage() - 1) / rowsPerPage());
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
    }
    if (step != 0 && pages > 1) {
      page = (page + step + pages) % pages;
      requestUpdate();
      return;
    }
  } else if (mappedInput.wasReleased(B::Confirm) && !lists.empty()) {
    openList(0);
    return;
  }
  if (!mappedInput.hasTouch()) return;
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenLongPress(tx, ty)) {
    open >= 0 ? handleListTap(tx, ty, true) : handleOverviewTap(tx, ty, true);
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) open >= 0 ? handleListTap(tx, ty, false) : handleOverviewTap(tx, ty, false);
}

void ChecklistsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  char buf[32];

  // Title row: the list's name (or "Checklisten") and its progress.
  const std::string title = open >= 0 ? lists[open] : std::string("Checklisten");
  renderer.drawText(UI_12_FONT_ID, kMargin, 24,
                    renderer.truncatedText(UI_12_FONT_ID, title.c_str(), w - 2 * kMargin - 70, EpdFontFamily::BOLD)
                        .c_str(),
                    true, EpdFontFamily::BOLD);
  if (open >= 0) {
    const int done = static_cast<int>(std::count_if(items.begin(), items.end(), [](const Item& i) { return i.done; }));
    std::snprintf(buf, sizeof(buf), "%d/%d", done, static_cast<int>(items.size()));
    renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), 30, buf, true);
  }
  rowsTop = 24 + renderer.getLineHeight(UI_12_FONT_ID) + 20;
  renderer.drawLine(kMargin, rowsTop - 8, w - kMargin, rowsTop - 8, true);

  if (open < 0) {
    rowH = lh + 30;
    int y = rowsTop;
    for (size_t i = 0; i < lists.size(); ++i, y += rowH) {
      renderer.drawText(UI_10_FONT_ID, kMargin, y + 12, lists[i].c_str(), true, EpdFontFamily::BOLD);
      std::snprintf(buf, sizeof(buf), "%d/%d", counts[i].first, counts[i].second);
      renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), y + 12, buf, true);
      for (int x = kMargin; x < w - kMargin; x += 3) renderer.drawPixel(x, y + rowH - 1);
    }
    renderer.drawText(UI_10_FONT_ID, kMargin, y + 12, "+ Neue Liste", true);
    renderer.drawText(SMALL_FONT_ID, kMargin, y + rowH + 16, "Lang drücken: umbenennen oder löschen", true);
  } else {
    rowH = std::max(kBox + 16, lh + 18);
    const int per = rowsPerPage();
    const int first = page * per;
    int y = rowsTop;
    for (int i = first; i < std::min(first + per, static_cast<int>(items.size())); ++i, y += rowH) {
      const Item& it = items[i];
      const int by = y + (rowH - kBox) / 2;
      renderer.drawRect(kMargin, by, kBox, kBox, 2, true);
      if (it.done) {
        // A tick inside the box.
        for (int t = 0; t < 3; ++t) {
          renderer.drawLine(kMargin + 5, by + 12 + t, kMargin + 10, by + 17 + t, true);
          renderer.drawLine(kMargin + 10, by + 17 + t, kMargin + 19, by + 6 + t, true);
        }
      }
      const int tx = kMargin + kBox + 12;
      const std::string text = renderer.truncatedText(UI_10_FONT_ID, it.text.c_str(), w - tx - kMargin);
      const int ty = y + (rowH - lh) / 2;
      renderer.drawText(UI_10_FONT_ID, tx, ty, text.c_str(), true);
      if (it.done) {
        // Struck through: done, but still on the list until it is cleared.
        const int sw = renderer.getTextWidth(UI_10_FONT_ID, text.c_str());
        renderer.fillRect(tx, ty + lh / 2, sw, 2, true);
      }
    }
    if (items.empty()) renderer.drawText(UI_10_FONT_ID, kMargin, rowsTop + 12, "Noch leer: + Eintrag tippen", true);
    const int pages = std::max(1, (static_cast<int>(items.size()) + per - 1) / per);

    // Actions along the bottom: add, untick all, drop ticked.
    const auto& metrics = UITheme::getInstance().getMetrics();
    actionsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - (lh + 24);
    renderer.drawLine(kMargin, actionsTop, w - kMargin, actionsTop, true);
    const char* actions[3] = {"+ Eintrag", "Zurücksetzen", "Erledigte löschen"};
    for (int a = 0; a < 3; ++a) {
      const int cw = w / 3;
      const int font = renderer.getTextWidth(UI_10_FONT_ID, actions[a]) <= cw - 6 ? UI_10_FONT_ID : SMALL_FONT_ID;
      const int aw = renderer.getTextWidth(font, actions[a]);
      renderer.drawText(font, a * cw + (cw - aw) / 2, actionsTop + 12 + (lh - renderer.getLineHeight(font)) / 2,
                        actions[a], true, a == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    if (pages > 1) {
      std::snprintf(buf, sizeof(buf), "Seite %d/%d", page + 1, pages);
      renderer.drawCenteredText(SMALL_FONT_ID, actionsTop - small - 4, buf, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), open >= 0 ? "Neu" : "Öffnen", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
