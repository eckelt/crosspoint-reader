#include "ClickerActivity.h"

#if DESKLEAF_CALENDAR

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <string>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* kName = "X4 Klicker";
constexpr uint8_t kReportId = 1;
constexpr uint8_t kKeyRight = 0x4F;  // HID usage: Right Arrow
constexpr uint8_t kKeyLeft = 0x50;   // HID usage: Left Arrow
constexpr uint16_t kAppearanceKeyboard = 0x03C1;

// Boot-compatible keyboard: modifiers, reserved, six key slots.
const uint8_t kReportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, kReportId,  //   Report ID
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Minimum (224)
    0x29, 0xE7,       //   Usage Maximum (231)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute): modifiers
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant): reserved
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array): keys
    0xC0,             // End Collection
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
  hid->setPnp(0x02, 0x05AC, 0x820A, 0x0001);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
  hid->setBatteryLevel(100);
  input = hid->getInputReport(kReportId);
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
  if (NimBLEDevice::isInitialized()) {
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);  // also frees the server and its services
  }
  delete hid;
  hid = nullptr;
  server = nullptr;
  input = nullptr;
  started = false;
  Activity::onExit();
}

void ClickerActivity::sendKey(const uint8_t usage) {
  if (!input || !server || server->getConnectedCount() == 0) return;
  uint8_t report[8] = {0, 0, usage, 0, 0, 0, 0, 0};
  input->setValue(report, sizeof(report));
  input->notify();
  delay(15);
  report[2] = 0;
  input->setValue(report, sizeof(report));
  input->notify();
  ++presses;
}

void ClickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture()) {
    onGoHome(HomeMenuItem::CLICKER);
    return;
  }
  using B = MappedInputManager::Button;
  int tx = 0, ty = 0;
  const bool forward = mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) ||
                       mappedInput.wasReleased(B::PageForward) || mappedInput.wasReleased(B::Confirm) ||
                       (mappedInput.hasTouch() && mappedInput.wasScreenTapped(tx, ty));
  const bool backward =
      mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::PageBack);
  if (forward) sendKey(kKeyRight);
  if (backward) sendKey(kKeyLeft);

  const bool connected = server && server->getConnectedCount() > 0;
  if (connected != shownConnected) {
    shownConnected = connected;
    requestUpdate();
  }
}

void ClickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int h = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_12_FONT_ID, h / 4, "Klicker", true, EpdFontFamily::BOLD);
  if (!started) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2, "Bluetooth konnte nicht starten", true);
  } else if (shownConnected) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 20, "Verbunden", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 20, "Rechts: nächste Folie · Links: vorherige", true);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 30, "Warte auf Verbindung …", true);
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 10, "Am Mac: Systemeinstellungen → Bluetooth", true);
    const std::string line = std::string("→ „") + kName + "“ verbinden";
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 40, line.c_str(), true);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), ">", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // DESKLEAF_CALENDAR
