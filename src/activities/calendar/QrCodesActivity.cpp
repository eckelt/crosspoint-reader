#include "QrCodesActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdio>

#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "deskleaf/EventIcons.h"
#include "deskleaf/QrSleep.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {
constexpr const char* kPath = "/qr-codes.txt";  // SD root: the web file manager shows it
constexpr const char* kTemplate =
    "# QR-Codes für das X4. Eine Zeile pro Code:\n"
    "#   Titel | Link\n"
    "#   Titel | Beschreibung | Link\n"
    "# Leere Zeilen und Zeilen mit # werden übersprungen.\n"
    "ecke.lt | Blog | https://ecke.lt\n"
    "Nils Eckelt | Profil | https://nils.ecke.lt\n"
    "Termin buchen | https://book.ecke.lt\n"
    "# LinkedIn | https://www.linkedin.com/in/DEIN-PROFIL\n";

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// A field of the file: '|' separates fields, so it may not appear in one.
std::string field(std::string s) {
  for (char& c : s) {
    if (c == '|' || c == '\n' || c == '\r') c = '/';
  }
  return s;
}

constexpr int kEditTap = 64;

constexpr int kIconSize = 20;
void drawPencil(const GfxRenderer& r, const int x, const int y) {
  const unsigned char* bits = deskleaf::icons::kEdit;
  for (int j = 0; j < kIconSize; ++j) {
    for (int i = 0; i < kIconSize; ++i) {
      if (bits[j * 3 + i / 8] & (0x80 >> (i % 8))) r.drawPixel(x + i, y + j, true);
    }
  }
}  // the pencil's tap square in the top right corner

// What the code leads to, readable: without scheme and trailing slash.
std::string shortLink(std::string link) {
  for (const char* scheme : {"https://", "http://"}) {
    if (link.rfind(scheme, 0) == 0) link.erase(0, std::char_traits<char>::length(scheme));
  }
  if (link.size() > 1 && link.back() == '/') link.pop_back();
  return link;
}
}  // namespace

std::vector<QrCodesActivity::Entry> QrCodesActivity::parse(const std::string& text) {
  std::vector<Entry> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t nl = text.find('\n', pos);
    const std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> parts;
    size_t from = 0;
    while (true) {
      const size_t bar = line.find('|', from);
      parts.push_back(trim(line.substr(from, bar == std::string::npos ? std::string::npos : bar - from)));
      if (bar == std::string::npos) break;
      from = bar + 1;
    }
    Entry e;
    e.link = parts.back();
    if (e.link.empty()) continue;
    if (parts.size() >= 2) e.title = parts[0];
    if (parts.size() >= 3) e.description = parts[1];
    if (e.title.empty()) e.title = shortLink(e.link);
    out.push_back(std::move(e));
  }
  return out;
}

void QrCodesActivity::onEnter() {
  Activity::onEnter();
  if (!Storage.exists(kPath)) Storage.writeFile(kPath, String(kTemplate));
  const String raw = Storage.readFile(kPath);
  ask = Ask::None;
  askPending = false;
  entries = parse(std::string(raw.c_str()));
  index = 0;
  requestUpdate();
}

void QrCodesActivity::step(const int direction) {
  const int pages = static_cast<int>(entries.size()) + 1;  // the last one adds a code
  index = (index + direction + pages) % pages;
  requestUpdate();
}

void QrCodesActivity::startEdit() {
  draft = onAddPage() ? Entry{} : entries[index];
  ask = Ask::Title;
  askPending = true;
}

// Opens the keyboard for the current question; its answer moves to the next.
void QrCodesActivity::askNext() {
  askPending = false;
  const char* title = ask == Ask::Title ? "Titel" : ask == Ask::Description ? "Beschreibung (optional)" : "Link";
  std::string& value = ask == Ask::Title ? draft.title : ask == Ask::Description ? draft.description : draft.link;
  const InputType type = ask == Ask::Link ? InputType::Url : InputType::Text;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, value, 300, type),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             ask = Ask::None;  // the whole edit is dropped
                             return;
                           }
                           const std::string text = std::get<KeyboardResult>(result.data).text;
                           if (ask == Ask::Title) {
                             draft.title = text;
                             ask = Ask::Description;
                           } else if (ask == Ask::Description) {
                             draft.description = text;
                             ask = Ask::Link;
                           } else {
                             draft.link = text;
                             ask = Ask::Done;
                           }
                           askPending = true;
                         });
}

void QrCodesActivity::finishEdit() {
  askPending = false;
  ask = Ask::None;
  draft.title = field(draft.title);
  draft.description = field(draft.description);
  draft.link = field(draft.link);
  const bool adding = onAddPage();
  const bool wasPinned = !adding && deskleaf::qrsleep::pinnedLink() == entries[index].link;
  if (wasPinned) deskleaf::qrsleep::unpin();
  if (draft.link.empty()) {
    if (!adding) entries.erase(entries.begin() + index);  // no link: the code goes
  } else {
    if (draft.title.empty()) draft.title = shortLink(draft.link);
    if (adding) {
      entries.push_back(draft);
    } else {
      entries[index] = draft;
    }
  }
  if (wasPinned && !draft.link.empty()) deskleaf::qrsleep::pin(draft.title, draft.description, draft.link);
  if (index > static_cast<int>(entries.size())) index = static_cast<int>(entries.size());
  save();
  requestUpdate();
}

void QrCodesActivity::save() const {
  std::string text =
      "# QR-Codes für das X4. Eine Zeile pro Code:\n"
      "#   Titel | Link\n"
      "#   Titel | Beschreibung | Link\n";
  for (const Entry& e : entries) {
    text += e.title + " | ";
    if (!e.description.empty()) text += e.description + " | ";
    text += e.link + "\n";
  }
  Storage.writeFile(kPath, String(text.c_str()));
}

void QrCodesActivity::loop() {
  if (askPending) {
    if (ask == Ask::Done) {
      finishEdit();
    } else if (ask != Ask::None) {
      askNext();
    }
    return;
  }
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    activityManager.goToTools();  // back to the Werkzeuge list
    return;
  }
  // Long press: this code becomes the sleep screen, or stops being it.
  int lx = 0, ly = 0;
  if (!onAddPage() && (mappedInput.wasLongPressed(B::Confirm, 800) ||
                       (mappedInput.hasTouch() && mappedInput.wasScreenLongPress(lx, ly)))) {
    const Entry& e = entries[index];
    if (deskleaf::qrsleep::pinnedLink() == e.link) {
      deskleaf::qrsleep::unpin();
    } else {
      deskleaf::qrsleep::pin(e.title, e.description, e.link);
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    startEdit();
    return;
  }
  if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) || mappedInput.wasReleased(B::PageForward)) {
    step(+1);
  }
  if (mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::PageBack)) {
    step(-1);
  }
  if (mappedInput.hasTouch()) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) step(+1);
    if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) step(-1);
    int tx = 0, ty = 0;
    if (mappedInput.wasScreenTapped(tx, ty)) {
      const int w = renderer.getScreenWidth();
      if (onAddPage() || (tx >= w - kEditTap && ty < kEditTap)) {
        startEdit();
      } else {
        step(tx < w / 3 ? -1 : +1);
      }
    }
  }
}

void QrCodesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int margin = 20;

  if (onAddPage()) {
    // A big plus: tap or Confirm asks for title, description and link.
    const int cx = w / 2, cy = bottom / 2 - 30, arm = 50, t = 10;
    renderer.fillRect(cx - arm, cy - t / 2, 2 * arm, t, true);
    renderer.fillRect(cx - t / 2, cy - arm, t, 2 * arm, true);
    renderer.drawCenteredText(UI_12_FONT_ID, cy + arm + 24, "Neuer QR-Code", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, cy + arm + 60, "Tippen: Titel, Beschreibung, Link", true);
  } else {
    const Entry& e = entries[index];
    // Pencil in the top right corner: edit this code.
    drawPencil(renderer, w - 12 - kIconSize, 14);
    // Pinned as the sleep screen: a black badge top left.
    if (deskleaf::qrsleep::pinnedLink() == e.link) {
      const char* badge = "Schlafbild";
      const int bw = renderer.getTextWidth(SMALL_FONT_ID, badge, EpdFontFamily::BOLD) + 12;
      renderer.fillRoundedRect(10, 12, bw, renderer.getLineHeight(SMALL_FONT_ID) + 6, 4, Color::Black);
      renderer.drawText(SMALL_FONT_ID, 16, 15, badge, false, EpdFontFamily::BOLD);
    }
    int y = 24;
    // Title in its own case, up to two lines.
    for (const std::string& line : renderer.wrappedText(UI_12_FONT_ID, e.title.c_str(), w - 2 * margin, 2,
                                                        EpdFontFamily::BOLD)) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    if (!e.description.empty()) {
      y += 4;
      renderer.drawCenteredText(UI_10_FONT_ID, y, e.description.c_str(), true);
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
    y += 16;

    // Below the code: where it leads, then which page this is.
    const int lh = renderer.getLineHeight(UI_10_FONT_ID);
    const int small = renderer.getLineHeight(SMALL_FONT_ID);
    const std::vector<std::string> linkLines =
        renderer.wrappedText(UI_10_FONT_ID, shortLink(e.link).c_str(), w - 2 * margin, 2);
    const int footer = static_cast<int>(linkLines.size()) * lh + 12 + small + 8;
    const int qrSize = std::min(w - 2 * margin, bottom - footer - y - 12);
    QrUtils::drawQrCode(renderer, Rect{(w - qrSize) / 2, y, qrSize, qrSize}, e.link);
    y += qrSize + 12;
    for (const std::string& line : linkLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true);
      y += lh;
    }
    {
      char page[16];
      std::snprintf(page, sizeof(page), "%d / %d", index + 1, static_cast<int>(entries.size()));
      renderer.drawCenteredText(SMALL_FONT_ID, y + 8, page, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), onAddPage() ? "Neu" : "Bearbeiten", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
