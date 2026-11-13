#include "DeskleafWifiSync.h"

#if DESKLEAF_CALENDAR

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first (see OtaUpdater.cpp).
#include "network/HttpDownloader.h"
#include <Logging.h>
#include <WiFi.h>
// clang-format on

#include <HalStorage.h>

#include "WifiCredentialStore.h"
#include "util/TaskWatchdog.h"

namespace deskleaf::wifisync {
namespace {
// The web file manager hides dot folders, so the file may also sit in the SD root.
constexpr const char* kConfigPaths[] = {"/deskleaf-feed.txt", "/.crosspoint/deskleaf-feed.txt"};
constexpr const char* kDefaultBase = "https://mcp.deskleaf.one";
constexpr unsigned long kJoinTimeoutMs = 8000;
constexpr int kMaxNetworks = 3;      // bounds the time spent when none is around
constexpr size_t kMaxBody = 512 * 1024;  // a few weeks are tens of KB

std::string trimmed(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool readConfig(std::string& token, std::string& base) {
  const char* path = nullptr;
  for (const char* candidate : kConfigPaths) {
    if (Storage.exists(candidate)) {
      path = candidate;
      break;
    }
  }
  if (!path) return false;
  const String raw = Storage.readFile(path);
  const std::string text(raw.c_str());
  const size_t nl = text.find('\n');
  token = trimmed(text.substr(0, nl));
  base = nl == std::string::npos ? std::string() : trimmed(text.substr(nl + 1));
  if (base.empty()) base = kDefaultBase;
  while (!base.empty() && base.back() == '/') base.pop_back();
  return !token.empty();
}

bool tryNetwork(const WifiCredential& cred, const AbortCheck shouldAbort) {
  LOG_INF("DSK", "Wi-Fi fallback: joining %s", cred.ssid.c_str());
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kJoinTimeoutMs) {
    if (shouldAbort && shouldAbort()) return false;
    resetTaskWatchdogIfSubscribed();
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect();
  return false;
}
}  // namespace

bool configured() {
  std::string token, base;
  return readConfig(token, base);
}

bool join(const AbortCheck shouldAbort) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WIFI_STORE.loadFromFile();
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  int tried = 0;
  if (const auto cred = WIFI_STORE.findCredential(last)) {
    ++tried;
    if (tryNetwork(*cred, shouldAbort)) return true;
  }
  for (size_t i = 0; i < WIFI_STORE.getCredentialCount() && tried < kMaxNetworks; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == last) continue;
    if (shouldAbort && shouldAbort()) return false;
    ++tried;
    if (tryNetwork(*cred, shouldAbort)) return true;
  }
  return false;
}

SyncStatus fetchWeeks(const std::string& monday, const int weeks, std::string& body) {
  std::string token, base;
  if (!readConfig(token, base)) return SyncStatus::BridgeNotFound;
  std::string url = base + "/display/weeks?weeks=" + std::to_string(weeks);
  if (!monday.empty()) url += "&monday=" + monday;
  body.clear();
  bool tooLarge = false;
  const bool ok = HttpDownloader::fetchUrl(
      url,
      [&](const uint8_t* data, size_t len) {
        if (body.size() + len > kMaxBody) {
          tooLarge = true;
          return false;
        }
        body.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      "", "", {{"Authorization", "Bearer " + token}});
  if (tooLarge) return SyncStatus::OutOfMemory;
  if (!ok) {
    LOG_ERR("DSK", "Wi-Fi fallback: fetch failed (HTTP %d)", HttpDownloader::lastStatus());
    return HttpDownloader::lastStatus() == 0 ? SyncStatus::ConnectFailed : SyncStatus::Timeout;
  }
  LOG_INF("DSK", "Wi-Fi fallback: %u bytes of week frames", static_cast<unsigned>(body.size()));
  return SyncStatus::NewPayload;
}

void leave() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace deskleaf::wifisync

#endif  // DESKLEAF_CALENDAR
