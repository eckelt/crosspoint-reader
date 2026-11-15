#include "QrCodesActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"
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
  entries = parse(std::string(raw.c_str()));
  index = 0;
  requestUpdate();
}

void QrCodesActivity::step(const int direction) {
  if (entries.size() < 2) return;
  const int n = static_cast<int>(entries.size());
  index = (index + direction + n) % n;
  requestUpdate();
}

void QrCodesActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    onGoHome(HomeMenuItem::QR_CODES);
    return;
  }
  if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) ||
      mappedInput.wasReleased(B::PageForward) || mappedInput.wasReleased(B::Confirm)) {
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
    if (mappedInput.wasScreenTapped(tx, ty)) step(tx < renderer.getScreenWidth() / 3 ? -1 : +1);
  }
}

void QrCodesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int margin = 20;

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, bottom / 2 - 20, "Keine QR-Codes", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, bottom / 2 + 10, "qr-codes.txt per File Transfer bearbeiten", true);
  } else {
    const Entry& e = entries[index];
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
    const int footer = static_cast<int>(linkLines.size()) * lh + 12 + (entries.size() > 1 ? small + 8 : 0);
    const int qrSize = std::min(w - 2 * margin, bottom - footer - y - 12);
    QrUtils::drawQrCode(renderer, Rect{(w - qrSize) / 2, y, qrSize, qrSize}, e.link);
    y += qrSize + 12;
    for (const std::string& line : linkLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true);
      y += lh;
    }
    if (entries.size() > 1) {
      char page[16];
      std::snprintf(page, sizeof(page), "%d / %d", index + 1, static_cast<int>(entries.size()));
      renderer.drawCenteredText(SMALL_FONT_ID, y + 8, page, true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
