#include "ClickerActivity.h"

#if DESKLEAF_CALENDAR

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <algorithm>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kName = "X4 Klicker";
constexpr uint8_t kKeyboardId = 1;
constexpr uint8_t kMouseId = 2;
constexpr uint8_t kKeyRight = 0x4F;      // HID usage: Right Arrow
constexpr uint8_t kKeyLeft = 0x50;       // HID usage: Left Arrow
constexpr uint8_t kButtonLeft = 0x01;
constexpr uint16_t kAppearanceKeyboard = 0x03C1;
// Pointer speed: Mac pixels per X4 pixel, and the shortest gap between two mouse reports.
constexpr float kPadScale = 2.5f;
constexpr unsigned long kMouseIntervalMs = 12;
// A touch this soon after a tap holds the button until the finger lifts.
constexpr unsigned long kDragAfterTapMs = 350;

// Keyboard (boot layout: modifiers, reserved, six keys) and a relative mouse
// (three buttons, X, Y, wheel).
const uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,  // Generic Desktop, Keyboard, Application
    0x85, kKeyboardId,                   //   Report ID
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,  //   Key Codes 224..231 (modifiers)
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,  //   reserved byte
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  //   six key slots
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01,  // Generic Desktop, Mouse, Application
    0x85, kMouseId,                      //   Report ID
    0x09, 0x01, 0xA1, 0x00,              //   Pointer, Physical
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03,  //     Buttons 1..3
    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,  //     padding
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,  // X, Y, Wheel
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0,
    0xC0,
};
}  // namespace

void ClickerActivity::onEnter() {
  Activity::onEnter();
  // The screen change already stopped the page turner; make sure, then take the stack.
  bleturner::stopNow(2000);
  if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);
  if (!NimBLEDevice::init(kName)) {
    LOG_ERR("CLK", "BLE init failed");
    requestUpdate();
    return;
  }
  NimBLEDevice::setPower(9);
  // Just Works pairing with bonding, so the computer reconnects on its own.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true);
  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Deskleaf");
  hid->setPnp(0x02, 0x05AC, 0x820A, 0x0002);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
  hid->setBatteryLevel(100);
  keyboard = hid->getInputReport(kKeyboardId);
  mouse = hid->getInputReport(kMouseId);
  server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(kAppearanceKeyboard);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName(kName);
  adv->enableScanResponse(true);
  started = adv->start();
  LOG_INF("CLK", "Advertising as %s: %d", kName, started);
  requestUpdate();
}

void ClickerActivity::onExit() {
  if (held) sendMouse(0, 0);  // never leave the button down on the computer
  if (NimBLEDevice::isInitialized()) {
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);  // also frees the server and its services
  }
  delete hid;
  hid = nullptr;
  server = nullptr;
  keyboard = mouse = nullptr;
  started = false;
  Activity::onExit();
}

bool ClickerActivity::connected() const { return server && server->getConnectedCount() > 0; }

void ClickerActivity::sendKeys(const uint8_t mods, const uint8_t usage) {
  if (!keyboard || !connected()) return;
  const uint8_t report[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  keyboard->setValue(report, sizeof(report));
  keyboard->notify();
}

void ClickerActivity::tapKey(const uint8_t mods, const uint8_t usage) {
  sendKeys(mods, usage);
  delay(15);
  sendKeys(0, 0);
}

void ClickerActivity::sendMouse(const int dx, const int dy) {
  if (!mouse || !connected()) return;
  const uint8_t report[4] = {static_cast<uint8_t>(held ? kButtonLeft : 0), static_cast<uint8_t>(static_cast<int8_t>(dx)),
                             static_cast<uint8_t>(static_cast<int8_t>(dy)), 0};
  mouse->setValue(report, sizeof(report));
  mouse->notify();
}

// The finger's movement on the pad moves the pointer; the first contact only anchors.
void ClickerActivity::trackPad() {
  int x = 0, y = 0;
  if (!mappedInput.isScreenTouchHeld(x, y)) {
    if (held) {
      held = false;
      holdEnded = true;
      sendMouse(0, 0);  // finger lifted: release the button
    }
    dragging = false;
    return;
  }
  if (!dragging) {
    if (y < padTop || y > padBottom) return;
    dragging = true;
    holdEnded = false;
    if (lastTapMs != 0 && millis() - lastTapMs < kDragAfterTapMs) {
      held = true;  // second touch of tap-then-swipe: press and keep the button down
      lastTapMs = 0;
      sendMouse(0, 0);
    }
    lastX = x;
    lastY = y;
    carryX = carryY = 0;
    return;
  }
  const unsigned long now = millis();
  if (now - lastMouseMs < kMouseIntervalMs) return;
  carryX += (x - lastX) * kPadScale;
  carryY += (y - lastY) * kPadScale;
  lastX = x;
  lastY = y;
  while (carryX != 0 || carryY != 0) {
    const int dx = static_cast<int>(std::max(-127.0f, std::min(127.0f, carryX)));
    const int dy = static_cast<int>(std::max(-127.0f, std::min(127.0f, carryY)));
    if (dx == 0 && dy == 0) break;  // keep the fraction for the next report
    sendMouse(dx, dy);
    carryX -= dx;
    carryY -= dy;
  }
  lastMouseMs = now;
}

void ClickerActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back)) {
    onGoHome(HomeMenuItem::CLICKER);
    return;
  }
  if (mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) ||
      mappedInput.wasReleased(B::PageForward) || mappedInput.wasReleased(B::Confirm)) {
    tapKey(0, kKeyRight);
  }
  if (mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::PageBack)) {
    tapKey(0, kKeyLeft);
  }

  if (mappedInput.hasTouch()) {
    int tx = 0, ty = 0;
    // Release edges first, so a lifted drag ends before a new touch starts.
    trackPad();
    if (mappedInput.wasScreenTapped(tx, ty) && ty >= padTop && ty <= padBottom && connected()) {
      if (holdEnded) {
        // Second tap of a double tap: its touch pressed the button and its lift released it.
        holdEnded = false;
      } else {
        held = true;  // a tap on the pad is a click
        sendMouse(0, 0);
        delay(15);
        held = false;
        sendMouse(0, 0);
        lastTapMs = millis();
      }
    }
  }

  const bool isConnected = connected();
  if (isConnected != shownConnected) {
    shownConnected = isConnected;
    if (!isConnected) held = false;  // the computer dropped the held button
    requestUpdate();
  }
}

void ClickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  const int gap = 8;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int lh = renderer.getLineHeight(UI_10_FONT_ID);

  int y = 20;
  renderer.drawCenteredText(UI_12_FONT_ID, y, "Klicker", true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 8;
  if (!started) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Bluetooth konnte nicht starten", true);
  } else if (shownConnected) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Verbunden · Tasten: Folie vor / zurück", true);
  } else {
    const std::string line = std::string("Am Mac unter Bluetooth „") + kName + "“ verbinden";
    renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str(), true);
  }
  y += lh + 16;

  // Touchpad below.
  padTop = y;
  padBottom = bottom - 12;
  renderer.drawRect(gap, padTop, w - 2 * gap, padBottom - padTop, 1, true);
  renderer.drawCenteredText(UI_10_FONT_ID, (padTop + padBottom) / 2 - lh, "Touchpad", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, (padTop + padBottom) / 2 + 4, "wischen = Zeiger · tippen = Klick", true);
  renderer.drawCenteredText(UI_10_FONT_ID, (padTop + padBottom) / 2 + 8 + lh, "tippen, dann wischen = ziehen / malen",
                            true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ">", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
