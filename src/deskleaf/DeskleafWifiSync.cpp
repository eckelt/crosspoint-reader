#include "DeskleafWifiSync.h"

#if DESKLEAF_CALENDAR

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first (see OtaUpdater.cpp).
#include "network/HttpDownloader.h"
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
// clang-format on

#include <HalStorage.h>

#include "DeskleafFeed.h"
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
int g_lastHttp = 0;

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
  g_lastHttp = HttpDownloader::lastStatus();
  if (tooLarge) return SyncStatus::OutOfMemory;
  if (!ok) {
    LOG_ERR("DSK", "Wi-Fi fallback: fetch failed (HTTP %d)", HttpDownloader::lastStatus());
    return HttpDownloader::lastStatus() == 0 ? SyncStatus::ConnectFailed : SyncStatus::Timeout;
  }
  LOG_INF("DSK", "Wi-Fi fallback: %u bytes of week frames", static_cast<unsigned>(body.size()));
  return SyncStatus::NewPayload;
}

int lastHttpStatus() { return g_lastHttp; }

int getText(const std::string& path, std::string& body, const size_t maxBytes) {
  body.clear();
  std::string token, base;
  if (!readConfig(token, base)) return 0;
  bool tooLarge = false;
  HttpDownloader::fetchUrl(
      base + path,
      [&](const uint8_t* data, size_t len) {
        if (body.size() + len > maxBytes) {
          tooLarge = true;
          return false;
        }
        body.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      "", "", {{"Authorization", "Bearer " + token}});
  if (tooLarge) return 0;
  return HttpDownloader::lastStatus();
}

std::vector<std::string> fetchInbox(const AbortCheck shouldAbort) {
  std::vector<std::string> saved;
  std::string token, base;
  if (!readConfig(token, base)) return saved;
  const std::vector<HttpDownloader::Header> auth = {{"Authorization", "Bearer " + token}};
  std::string listing;
  const int status = getText("/display/inbox", listing, 16 * 1024);
  if (status != 200) {
    LOG_ERR("DSK", "Inbox: list failed (HTTP %d)", status);
    return saved;
  }
  const auto entries = feed::parseInboxListing(listing);
  LOG_INF("DSK", "Inbox: %u waiting", static_cast<unsigned>(entries.size()));
  for (const feed::InboxEntry& entry : entries) {
    if (shouldAbort && shouldAbort()) break;
    const std::string dest = feed::inboxDestination(entry);
    const size_t slash = dest.rfind('/');
    if (slash > 0) Storage.ensureDirectoryExists(dest.substr(0, slash).c_str());
    // The id holds spaces and UTF-8 ("Heute · Sa 28 11 .md"): one encoded path segment.
    const std::string url = base + "/display/inbox/" + feed::percentEncodeSegment(entry.id);
    if (HttpDownloader::downloadToFile(url, dest, nullptr, nullptr, "", "", auth) != HttpDownloader::OK) {
      LOG_ERR("DSK", "Inbox: download of %s failed (HTTP %d)", entry.name.c_str(), HttpDownloader::lastStatus());
      continue;  // not checked off: stays on the server for the next time
    }
    // Checked off only once the file is on the card.
    std::string ack;
    const int done = getText("/display/inbox/" + feed::percentEncodeSegment(entry.id) + "/done", ack, 64);
    if (done != 200) LOG_ERR("DSK", "Inbox: check-off of %s failed (HTTP %d)", entry.name.c_str(), done);
    LOG_INF("DSK", "Inbox: %s -> %s", entry.name.c_str(), dest.c_str());
    saved.push_back(entry.name);
  }
  return saved;
}

void leave() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

}  // namespace deskleaf::wifisync

#endif  // DESKLEAF_CALENDAR
