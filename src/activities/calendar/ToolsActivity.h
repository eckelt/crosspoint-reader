#pragma once
// "Apps" on Home: the Deskleaf apps as a springboard of icon tiles (Kalender,
// Klicker, QR-Codes), since Home's own menu has no room to scroll. The arrow
// buttons move the selection, Confirm or a tap opens, Back goes home.
#include "activities/Activity.h"

class ToolsActivity final : public Activity {
 public:
  ToolsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Apps", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void open(int tile);
  int selected = 0;
  // Grid geometry, set by render, for tap tests.
  int gridTop = 0, tileW = 0, tileH = 0;
};
