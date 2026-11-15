#include "ToolsActivity.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
struct Tool {
  const char* title;
  const char* hint;
};
constexpr Tool kTools[] = {
    {"Klicker", "Folien weiterschalten, Touchpad als Maus"},
    {"QR-Codes", "Links zeigen, bearbeiten, als Schlafbild"},
};
constexpr int kCount = sizeof(kTools) / sizeof(kTools[0]);
constexpr int kRowH = 110;
constexpr int kGap = 16;
constexpr int kMargin = 20;
}  // namespace

void ToolsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ToolsActivity::open(const int row) {
  if (row == 0) activityManager.goToClicker();
  if (row == 1) activityManager.goToQrCodes();
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
  const bool next = mappedInput.wasReleased(B::Down) || mappedInput.wasReleased(B::Right) ||
                    mappedInput.wasReleased(B::PageForward);
  const bool prev =
      mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::PageBack);
  if (next || prev) {
    selected = (selected + (next ? 1 : kCount - 1)) % kCount;
    requestUpdate();
    return;
  }
  int tx = 0, ty = 0;
  if (mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty)) {
    const int row = (ty - rowsTop) / (kRowH + kGap);
    if (ty >= rowsTop && row < kCount && (ty - rowsTop) % (kRowH + kGap) < kRowH) open(row);
  }
}

void ToolsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  renderer.drawText(UI_12_FONT_ID, kMargin, 30, "Werkzeuge", true, EpdFontFamily::BOLD);
  rowsTop = 30 + renderer.getLineHeight(UI_12_FONT_ID) + 24;
  for (int i = 0; i < kCount; ++i) {
    const int y = rowsTop + i * (kRowH + kGap);
    const bool on = i == selected;
    if (on) {
      renderer.fillRoundedRect(kMargin, y, w - 2 * kMargin, kRowH, 8, Color::Black);
    } else {
      renderer.drawRoundedRect(kMargin, y, w - 2 * kMargin, kRowH, 2, 8, true);
    }
    const int th = renderer.getLineHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, kMargin + 20, y + kRowH / 2 - th, kTools[i].title, !on, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, kMargin + 20, y + kRowH / 2 + 4, kTools[i].hint, !on);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Öffnen", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
