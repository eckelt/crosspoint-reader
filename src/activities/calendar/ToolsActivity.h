#pragma once
// "Werkzeuge" on Home: the Deskleaf extras that do not fit Home's fixed menu
// (Klicker, QR-Codes) as large rows. Up/Down or Left/Right select, Confirm or a
// tap opens, Back goes home.
#include "activities/Activity.h"

class ToolsActivity final : public Activity {
 public:
  ToolsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Tools", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void open(int row);
  int selected = 0;
  int rowsTop = 0;  // set by render, for tap tests
};
