#include "DeskleafAutoUpdate.h"

#if DESKLEAF_CALENDAR

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "DeskleafWifiSync.h"
#include "fontIds.h"
#include "network/OtaBootSwitch.h"
#include "network/OtaUpdater.h"

namespace deskleaf::autoupdate {
namespace {
constexpr const char* kPrefs = "dlfw";
constexpr uint32_t kGuardMagic = 0xD1F00D42;
constexpr int kMaxCrashes = 3;
constexpr int kMinBatteryPercent = 30;
constexpr int64_t kRetrySeconds = 3600;

// Survive panic and watchdog resets (not power loss): crashes of a fresh install.
RTC_NOINIT_ATTR uint32_t g_guardMagic;
RTC_NOINIT_ATTR uint32_t g_crashes;
bool g_guarding = false;

bool parse(const char* v, int& a, int& b, int& c) { return std::sscanf(v, "%d.%d.%d", &a, &b, &c) == 3; }

void notice(GfxRenderer* r, const std::string& version) {
  if (!r) return;
  r->setOrientation(GfxRenderer::Orientation::Portrait);
  r->clearScreen();
  const int mid = r->getScreenHeight() / 2;
  const std::string line = "Firmware " + version + " wird installiert";
  r->drawCenteredText(UI_12_FONT_ID, mid - 20, line.c_str(), true, EpdFontFamily::BOLD);
  r->drawCenteredText(UI_10_FONT_ID, mid + 16, "Das dauert etwa eine Minute.", true);
  r->displayBuffer(HalDisplay::HALF_REFRESH);
}
}  // namespace

bool isNewer(const std::string& candidate) {
  int a, b, c, x, y, z;
  if (!parse(candidate.c_str(), a, b, c) || !parse(CROSSPOINT_VERSION, x, y, z)) return false;
  if (a != x) return a > x;
  if (b != y) return b > y;
  return c > z;
}

std::string newestRelease() {
  OtaUpdater ota;
  if (ota.checkForUpdate() != OtaUpdater::OK) return {};
  return ota.getLatestVersion();
}

bool due(const std::string& announced, const int64_t lastAttempt) {
  if (announced.empty() || !isNewer(announced)) return false;
  if (static_cast<int64_t>(time(nullptr)) - lastAttempt < kRetrySeconds) return false;
  if (!gpio.isUsbConnected() && powerManager.getBatteryPercentage() < kMinBatteryPercent) {
    LOG_INF("UPD", "Firmware %s waits: battery low", announced.c_str());
    return false;
  }
  return true;
}

bool install(GfxRenderer* renderer, const std::string& version, const AbortCheck shouldAbort) {
  LOG_INF("UPD", "Installing firmware %s (running %s)", version.c_str(), CROSSPOINT_VERSION);
  if (!wifisync::join(shouldAbort)) {
    LOG_ERR("UPD", "No Wi-Fi for the update");
    wifisync::leave();
    return false;
  }
  OtaUpdater ota;
  bool ok = ota.checkForUpdate() == OtaUpdater::OK && ota.isUpdateNewer();
  if (ok) {
    notice(renderer, ota.getLatestVersion());
    // Remember where we came from before the new image becomes the boot partition.
    const esp_partition_t* running = esp_ota_get_running_partition();
    Preferences prefs;
    prefs.begin(kPrefs, false);
    prefs.putString("prev", running ? running->label : "");
    prefs.end();
    ok = ota.installUpdate() == OtaUpdater::OK;
    if (!ok) {
      LOG_ERR("UPD", "Install failed: %s", ota.getErrorDetail().c_str());
      prefs.begin(kPrefs, false);
      prefs.remove("prev");
      prefs.end();
    }
  }
  wifisync::leave();
  if (ok) {
    g_guardMagic = kGuardMagic;
    g_crashes = 0;
  }
  return ok;
}

void bootGuard() {
  Preferences prefs;
  if (!prefs.begin(kPrefs, false)) return;
  const String prev = prefs.getString("prev", "");
  if (prev.isEmpty()) {
    prefs.end();
    return;
  }
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running || prev == running->label) {
    prefs.remove("prev");  // still (or again) on the old firmware: nothing to guard
    prefs.end();
    return;
  }
  g_guarding = true;
  if (g_guardMagic != kGuardMagic) {
    g_guardMagic = kGuardMagic;
    g_crashes = 0;
  }
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
    ++g_crashes;
  }
  if (g_crashes >= kMaxCrashes) {
    const esp_partition_t* old =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, prev.c_str());
    prefs.remove("prev");
    prefs.end();
    g_guardMagic = 0;
    if (old && ota_boot::switchTo(old)) {
      esp_restart();  // back to the firmware that worked
    }
    return;
  }
  prefs.end();
}

void confirmHealthy() {
  if (!g_guarding) return;
  g_guarding = false;
  g_guardMagic = 0;
  Preferences prefs;
  if (!prefs.begin(kPrefs, false)) return;
  prefs.remove("prev");
  prefs.end();
  LOG_INF("UPD", "Firmware %s confirmed", CROSSPOINT_VERSION);
}

}  // namespace deskleaf::autoupdate

#endif  // DESKLEAF_CALENDAR
