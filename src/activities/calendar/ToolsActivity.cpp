#include "ToolsActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "deskleaf/AppIcons.h"
#include "fontIds.h"

namespace {
struct App {
  const char* name;
  const unsigned char* icon;
};
constexpr App kApps[] = {
    {"Kalender", deskleaf::icons::kAppCalendar},
    {"Klicker", deskleaf::icons::kAppClicker},
    {"QR-Codes", deskleaf::icons::kAppQr},
};
constexpr int kCount = sizeof(kApps) / sizeof(kApps[0]);
constexpr int kColumns = 3;
constexpr int kMargin = 16;
constexpr int kGap = 12;

void drawAppIcon(const GfxRenderer& r, const unsigned char* bits, const int x, const int y) {
  constexpr int kRowBytes = (deskleaf::icons::kAppSize + 7) / 8;
  for (int j = 0; j < deskleaf::icons::kAppSize; ++j) {
    for (int i = 0; i < deskleaf::icons::kAppSize; ++i) {
      if (bits[j * kRowBytes + i / 8] & (0x80 >> (i % 8))) r.drawPixel(x + i, y + j, true);
    }
  }
}
}  // namespace

void ToolsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ToolsActivity::open(const int tile) {
  if (tile == 0) activityManager.goToCalendar();
  if (tile == 1) activityManager.goToClicker();
  if (tile == 2) activityManager.goToQrCodes();
}

void ToolsActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasBackGesture()) {
    onGoHome(HomeMenuItem::TOOLS);
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    open(selected);
    return;
  }
  int move = 0;
  if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::PageForward)) move = 1;
  if (mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::PageBack)) move = -1;
  if (mappedInput.wasReleased(B::Down)) move = kColumns;
  if (mappedInput.wasReleased(B::Up)) move = -kColumns;
  if (move != 0) {
    selected = ((selected + move) % kCount + kCount) % kCount;
    requestUpdate();
    return;
  }
  int tx = 0, ty = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty) && tileW > 0 && ty >= gridTop) {
    const int col = (tx - kMargin) / (tileW + kGap);
    const int row = (ty - gridTop) / (tileH + kGap);
    const int tile = row * kColumns + col;
    if (tx >= kMargin && col < kColumns && tile < kCount) open(tile);
  }
}

void ToolsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  renderer.drawText(UI_12_FONT_ID, kMargin + 4, 30, "Apps", true, EpdFontFamily::BOLD);
  gridTop = 30 + renderer.getLineHeight(UI_12_FONT_ID) + 24;
  tileW = (w - 2 * kMargin - (kColumns - 1) * kGap) / kColumns;
  const int label = renderer.getLineHeight(UI_10_FONT_ID);
  tileH = deskleaf::icons::kAppSize + 28 + label;
  for (int i = 0; i < kCount; ++i) {
    const int x = kMargin + (i % kColumns) * (tileW + kGap);
    const int y = gridTop + (i / kColumns) * (tileH + kGap);
    // The selected tile gets a heavy frame; the others a light one.
    renderer.drawRoundedRect(x, y, tileW, tileH, i == selected ? 4 : 1, 12, true);
    drawAppIcon(renderer, kApps[i].icon, x + (tileW - deskleaf::icons::kAppSize) / 2, y + 12);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, kApps[i].name, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + (tileW - tw) / 2, y + 12 + deskleaf::icons::kAppSize + 8, kApps[i].name,
                      true, EpdFontFamily::BOLD);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Öffnen", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
