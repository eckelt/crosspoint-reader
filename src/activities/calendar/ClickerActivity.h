#pragma once
// Presentation clicker: the X4 Pro pairs with a computer as a Bluetooth
// keyboard and mouse. Right/Down/page-forward/Confirm send Right Arrow,
// Left/Up/page-back send Left Arrow, Back leaves. The screen is a touchpad:
// swiping moves the pointer, a tap clicks, and tap-then-swipe drags with the
// button held (enough to draw with Screenbrush). The page turner and the
// calendar sync are stopped while this screen owns the BLE stack.
#include <cstdint>

#include "activities/Activity.h"

class NimBLEServer;
class NimBLEHIDDevice;
class NimBLECharacteristic;

class ClickerActivity final : public Activity {
 public:
  ClickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Clicker", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  bool connected() const;
  void sendKeys(uint8_t mods, uint8_t usage);
  void tapKey(uint8_t mods, uint8_t usage);
  void sendMouse(int dx, int dy);
  void trackPad();

  // Touchpad rectangle, set by render.
  int padTop = 0, padBottom = 0;

  NimBLEServer* server = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  NimBLECharacteristic* keyboard = nullptr;
  NimBLECharacteristic* mouse = nullptr;
  bool started = false;
  bool shownConnected = false;

  bool held = false;       // left mouse button held (tap-then-swipe drag)
  bool holdEnded = false;  // the last lift ended a held contact (its tap is no click)
  unsigned long lastTapMs = 0;
  bool dragging = false;
  int lastX = 0, lastY = 0;
  float carryX = 0, carryY = 0;
  unsigned long lastMouseMs = 0;
};
