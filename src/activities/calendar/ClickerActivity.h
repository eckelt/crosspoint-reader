#pragma once
// Presentation clicker: the X4 Pro pairs with a computer as a Bluetooth
// keyboard. Right/Down/page-forward (or a tap) send Right Arrow, Left/Up/
// page-back send Left Arrow, Back leaves. The page turner and the calendar sync
// are stopped while this screen owns the BLE stack.
#include <atomic>

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
  void sendKey(uint8_t usage);

  NimBLEServer* server = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  NimBLECharacteristic* input = nullptr;
  bool started = false;
  bool shownConnected = false;
  int presses = 0;
};
