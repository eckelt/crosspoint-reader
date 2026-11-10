#pragma once
// Presentation clicker: the X4 Pro pairs with a computer as a Bluetooth
// keyboard and mouse. Right/Down/page-forward/Confirm send Right Arrow,
// Left/Up/page-back send Left Arrow, Back leaves. The screen holds three tool
// buttons for Screenbrush (Spotlight = Shift held, Brush = left mouse button
// held, Clear = Cmd+Delete) and a touchpad that moves the pointer; a tap on
// the pad is a click. The page turner and the calendar sync are stopped while
// this screen owns the BLE stack.
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
  enum class Tool : uint8_t { Spotlight, Brush, Clear };

  bool connected() const;
  void sendKeys(uint8_t mods, uint8_t usage);
  void tapKey(uint8_t mods, uint8_t usage);
  void sendMouse(int dx, int dy);
  void useTool(Tool tool);
  void trackPad();
  void resetTools();

  // Layout, set by render: tool bar row and touchpad rectangle.
  int barTop = 0, barBottom = 0;
  int padTop = 0, padBottom = 0;

  NimBLEServer* server = nullptr;
  NimBLEHIDDevice* hid = nullptr;
  NimBLECharacteristic* keyboard = nullptr;
  NimBLECharacteristic* mouse = nullptr;
  bool started = false;
  bool shownConnected = false;

  bool spotlight = false;  // Shift held
  bool brush = false;      // left mouse button held
  bool dragging = false;
  int lastX = 0, lastY = 0;
  float carryX = 0, carryY = 0;
  unsigned long lastMouseMs = 0;
};
