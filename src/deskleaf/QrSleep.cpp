#include "QrSleep.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <algorithm>

#include "fontIds.h"
#include "util/QrUtils.h"

namespace deskleaf::qrsleep {
namespace {
constexpr const char* kDir = "/.crosspoint/deskleaf";
constexpr const char* kPath = "/.crosspoint/deskleaf/qr-sleep.txt";  // title, description, link: one per line

struct Pinned {
  std::string title, description, link;
};

bool load(Pinned& p) {
  if (!Storage.exists(kPath)) return false;
  const std::string text(Storage.readFile(kPath).c_str());
  std::string* fields[] = {&p.title, &p.description, &p.link};
  size_t pos = 0;
  for (std::string* f : fields) {
    const size_t nl = text.find('\n', pos);
    *f = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() : nl + 1;
  }
  return !p.link.empty();
}

std::string shortLink(std::string link) {
  for (const char* scheme : {"https://", "http://"}) {
    if (link.rfind(scheme, 0) == 0) link.erase(0, std::char_traits<char>::length(scheme));
  }
  if (link.size() > 1 && link.back() == '/') link.pop_back();
  return link;
}
}  // namespace

bool active() {
  Pinned p;
  return load(p);
}

std::string pinnedLink() {
  Pinned p;
  return load(p) ? p.link : std::string();
}

bool pin(const std::string& title, const std::string& description, const std::string& link) {
  Storage.ensureDirectoryExists(kDir);
  return Storage.writeFile(kPath, String((title + "\n" + description + "\n" + link + "\n").c_str()));
}

void unpin() { Storage.remove(kPath); }

void render(GfxRenderer& renderer) {
  Pinned p;
  load(p);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int margin = 24;
  int y = 40;
  for (const std::string& line : renderer.wrappedText(UI_12_FONT_ID, p.title.c_str(), w - 2 * margin, 2,
                                                      EpdFontFamily::BOLD)) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  if (!p.description.empty()) {
    y += 4;
    renderer.drawCenteredText(UI_10_FONT_ID, y, p.description.c_str(), true);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  y += 24;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);
  const int qrSize = std::min(w - 2 * margin, h - y - 2 * lh - 60);
  QrUtils::drawQrCode(renderer, Rect{(w - qrSize) / 2, y, qrSize, qrSize}, p.link);
  y += qrSize + 20;
  for (const std::string& line : renderer.wrappedText(UI_10_FONT_ID, shortLink(p.link).c_str(), w - 2 * margin, 2)) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true);
    y += lh;
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

}  // namespace deskleaf::qrsleep

#endif  // DESKLEAF_CALENDAR
