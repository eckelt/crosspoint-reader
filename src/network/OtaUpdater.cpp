#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <WiFi.h>
// clang-format on

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
#if DESKLEAF_CALENDAR
// The Deskleaf fork updates from its own releases (published by its CI).
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/eckelt/crosspoint-reader/releases/latest";
#else
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest";
#endif
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  // Heap-allocated: the parser embeds a 2 KB JSON token buffer.
  auto releaseParserPtr = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!releaseParserPtr) {
    LOG_ERR("OTA", "OOM: release parser");
    return OOM_ERROR;
  }
  ReleaseJsonParser& releaseParser = *releaseParserPtr;
  releaseParser.setFirmwareAssetName("");
  // Each board updates from crosspoint-<version>-<device>.bin. The combined
  // C3 image uses x3-x4; other asset suffixes match their firmware board tag.
  const bool isX4 = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char assetSuffix[20] = "-x3-x4";
  if (!isX4) {
    snprintf(assetSuffix, sizeof(assetSuffix), "-%.*s", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  }
  char assetName[48] = {};
  bool assetNameSet = false;
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&](const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (!assetNameSet && offset < len) {
      releaseParser.feed(reinterpret_cast<const char*>(data + offset), 1);
      offset++;
      if (releaseParser.foundTag()) {
        snprintf(assetName, sizeof(assetName), "crosspoint-%s%s.bin", releaseParser.getTagName(), assetSuffix);
        releaseParser.setFirmwareAssetName(assetName);
        assetNameSet = true;
      }
    }
    if (offset < len) releaseParser.feed(reinterpret_cast<const char*>(data + offset), len - offset);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaApiUrl = releaseParser.getFirmwareApiUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;

  const auto currentVersion = CROSSPOINT_VERSION;

  // semantic version check (only match on 3 segments)
  sscanf(latestVersion.c_str(), "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

namespace {
// GitHub answers the asset URL with a redirect to this download host.
constexpr const char* kAssetCdnHost = "release-assets.githubusercontent.com";
constexpr const char* kErrorLogPath = "/.crosspoint/ota-last-error.txt";
}  // namespace

void OtaUpdater::describeFailure(const bool flashOk) {
  char buf[200];
  if (!flashOk) {
    errorDetail = "Schreiben in den Flash fehlgeschlagen";
  } else {
    const HttpDownloader::Trace t = HttpDownloader::lastTrace();
    const int status = HttpDownloader::lastStatus();
    const char* where = lastHost;
    std::string reason;
    if (status > 0) {
      snprintf(buf, sizeof(buf), "HTTP %d", status);
      reason = buf;
    } else {
      // No answer at all: tell a name the network does not resolve from a refused or broken connection.
      IPAddress ip;
      const bool cdn = strcmp(lastHost, "Download-Server") == 0;
      const bool resolves = WiFi.hostByName(cdn ? kAssetCdnHost : lastHost, ip) == 1;
      reason = resolves ? "keine Verbindung (DNS ok)" : "Name nicht auflösbar (DNS)";
    }
    snprintf(buf, sizeof(buf),
             "%s: %s\n%u / %u Bytes · Verbindungen %d (Server %d)\nHeap %u KB frei, Block %u KB · WLAN %d dBm", where,
             reason.c_str(), static_cast<unsigned>(processedSize), static_cast<unsigned>(totalSize), t.connections,
             t.toRedirect, static_cast<unsigned>(t.freeHeap / 1024), static_cast<unsigned>(t.maxBlock / 1024),
             static_cast<int>(WiFi.RSSI()));
    errorDetail = buf;
    if (!attempts.empty()) errorDetail += "\nVersuche: " + attempts;  // route:HTTP status, 0 = no answer
  }
  LOG_ERR("OTA", "%s", errorDetail.c_str());
  HalFile file;
  if (Storage.openFileForWrite("OTA", kErrorLogPath, file)) {
    const std::string line = latestVersion + "\n" + errorDetail + "\n";
    file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    file.close();
  }
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  errorDetail.clear();
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  // All S3 boards share a chip_id, so also scan the stream for the embedded
  // board tag (FirmwareBoardTag.h). An untagged image passes; a tag naming a
  // different board aborts the download. The wrong image may partially land in
  // the inactive OTA slot, but esp_ota_abort() below means it never becomes
  // the boot target.
  board_tag::Scanner tagScanner;
  const auto onData = [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;  // abort the transfer
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  };

  // Two routes to the same file. The API asset URL first: its host just answered the
  // release check. Then the browser URL on github.com. A transfer that never
  // started is retried on the other route; one that did start is not, since the
  // OTA partition was already written to.
  struct Route {
    const char* label;
    const char* host;
    const std::string* url;
    std::vector<HttpDownloader::Header> headers;
  };
  std::vector<Route> routes;
  if (!otaApiUrl.empty()) {
    routes.push_back({"api", "api.github.com", &otaApiUrl, {{"Accept", "application/octet-stream"}}});
  }
  routes.push_back({"web", "github.com", &otaUrl, {}});
  constexpr int kAttempts = 3;
  bool fetchOk = false;
  attempts.clear();
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    const Route& route = routes[attempt % routes.size()];
    if (attempt > 0) delay(1500);
    lastHost = route.host;
    fetchOk = HttpDownloader::fetchUrl(*route.url, onData, "", "", route.headers);
    const HttpDownloader::Trace t = HttpDownloader::lastTrace();
    if (t.lastToRedirect) lastHost = "Download-Server";
    char entry[32];
    snprintf(entry, sizeof(entry), "%s%s:%d", attempts.empty() ? "" : " ", route.label, HttpDownloader::lastStatus());
    attempts += entry;
    if (fetchOk || processedSize > 0 || wrongChip || tagScanner.mismatch() || !flashOk) break;
    LOG_ERR("OTA", "Download attempt %d via %s failed before the first byte", attempt + 1, route.label);
  }

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    describeFailure(flashOk);
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
