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
constexpr uint8_t kKeyBackspace = 0x2A;  // the Mac's Delete key
constexpr uint8_t kModShift = 0x02;
constexpr uint8_t kModGui = 0x08;  // Command
constexpr uint8_t kButtonLeft = 0x01;
constexpr uint16_t kAppearanceKeyboard = 0x03C1;
// Pointer speed: Mac pixels per X4 pixel, and the shortest gap between two mouse reports.
constexpr float kPadScale = 2.5f;
constexpr unsigned long kMouseIntervalMs = 12;

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

const char* toolLabel(const int i) {
  static const char* const kLabels[3] = {"Spotlight", "Brush", "Clear"};
  return kLabels[i];
}
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
  if (connected()) resetTools();
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

// Press and release; a held Spotlight Shift comes back afterwards, so arrows
// and Clear are never shifted.
void ClickerActivity::tapKey(const uint8_t mods, const uint8_t usage) {
  sendKeys(mods, usage);
  delay(15);
  sendKeys(mods, 0);
  delay(5);
  sendKeys(spotlight ? kModShift : 0, 0);
}

void ClickerActivity::sendMouse(const int dx, const int dy) {
  if (!mouse || !connected()) return;
  const uint8_t report[4] = {static_cast<uint8_t>(brush ? kButtonLeft : 0), static_cast<uint8_t>(static_cast<int8_t>(dx)),
                             static_cast<uint8_t>(static_cast<int8_t>(dy)), 0};
  mouse->setValue(report, sizeof(report));
  mouse->notify();
}

void ClickerActivity::resetTools() {
  spotlight = brush = false;
  sendKeys(0, 0);
  sendMouse(0, 0);
}

void ClickerActivity::useTool(const Tool tool) {
  switch (tool) {
    case Tool::Spotlight:
      spotlight = !spotlight;
      sendKeys(spotlight ? kModShift : 0, 0);
      break;
    case Tool::Brush:
      brush = !brush;
      sendMouse(0, 0);
      break;
    case Tool::Clear:
      tapKey(kModGui, kKeyBackspace);
      break;
  }
  requestUpdate();
}

// The finger's movement on the pad moves the pointer; the first contact only anchors.
void ClickerActivity::trackPad() {
  int x = 0, y = 0;
  if (!mappedInput.isScreenTouchHeld(x, y)) {
    dragging = false;
    return;
  }
  if (!dragging) {
    if (y < padTop || y > padBottom) return;
    dragging = true;
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
    if (mappedInput.wasScreenTapped(tx, ty)) {
      if (ty >= barTop && ty < barBottom) {
        const int col = std::min(2, std::max(0, tx * 3 / renderer.getScreenWidth()));
        useTool(static_cast<Tool>(col));
      } else if (ty >= padTop && ty <= padBottom && !brush && connected()) {
        // A tap on the pad is a click.
        const uint8_t press[4] = {kButtonLeft, 0, 0, 0};
        mouse->setValue(press, sizeof(press));
        mouse->notify();
        delay(15);
        sendMouse(0, 0);
      }
    }
    trackPad();
  }

  const bool isConnected = connected();
  if (isConnected != shownConnected) {
    shownConnected = isConnected;
    if (!isConnected) spotlight = brush = false;  // the computer dropped held keys and buttons
    requestUpdate();
  }
}

void ClickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
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

  // Tool bar: three tiles; an active toggle is drawn inverted.
  const int barH = 64;
  const int gap = 8;
  const int tileW = (w - 4 * gap) / 3;
  barTop = y;
  barBottom = y + barH;
  for (int i = 0; i < 3; ++i) {
    const int x = gap + i * (tileW + gap);
    const bool on = (i == 0 && spotlight) || (i == 1 && brush);
    if (on) {
      renderer.fillRect(x, y, tileW, barH, true);
    } else {
      renderer.drawRect(x, y, tileW, barH, 2, true);
    }
    const char* label = toolLabel(i);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + (tileW - tw) / 2, y + (barH - lh) / 2, label, !on, EpdFontFamily::BOLD);
  }

  // Touchpad below.
  padTop = barBottom + 16;
  padBottom = bottom - 12;
  renderer.drawRect(gap, padTop, w - 2 * gap, padBottom - padTop, 1, true);
  renderer.drawCenteredText(UI_10_FONT_ID, (padTop + padBottom) / 2 - lh, "Touchpad", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, (padTop + padBottom) / 2 + 4, "wischen = Zeiger · tippen = Klick", true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ">", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
