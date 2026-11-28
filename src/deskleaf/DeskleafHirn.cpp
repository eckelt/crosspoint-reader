#include "DeskleafHirn.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <ctime>
#include <vector>

#include "DeskleafWifiSync.h"

namespace deskleaf::hirn {
namespace {
constexpr const char* kDir = "/.crosspoint/deskleaf/hirn";
constexpr const char* kStampPath = "/.crosspoint/deskleaf/hirn-stamp.txt";  // "<hash>\n<epoch>"
constexpr size_t kMaxBody = 160 * 1024;
constexpr int64_t kMinInterval = 3600;

std::string readText(const std::string& path) {
  if (!Storage.exists(path.c_str())) return {};
  return std::string(Storage.readFile(path.c_str()).c_str());
}

bool readEntry(const char prefix, const std::string& key, const bool isNote, knowledge::Entry& out) {
  const std::string content = readText(std::string(kDir) + "/" + knowledge::fileName(prefix, key));
  if (content.empty()) return false;
  return isNote ? knowledge::parseNote(content, out) : knowledge::parseEvent(content, out);
}
}  // namespace

bool fetchOverWifi(const std::string& monday, const int weeks, const bool force) {
  const std::string stamp = readText(kStampPath);
  const size_t nl = stamp.find('\n');
  const std::string oldHash = stamp.substr(0, nl);
  const int64_t last = nl == std::string::npos ? 0 : std::atoll(stamp.c_str() + nl + 1);
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (!force && now - last < kMinInterval) return false;

  std::string body;
  const int status = wifisync::getText("/display/knowledge?monday=" + monday + "&weeks=" + std::to_string(weeks), body,
                                       kMaxBody);
  if (status != 200) {
    LOG_ERR("DSK", "Hirn: fetch failed (HTTP %d)", status);
    return false;
  }
  std::string hash;
  std::vector<knowledge::File> files;
  if (!knowledge::split(body.data(), body.size(), hash, files)) {
    LOG_ERR("DSK", "Hirn: answer does not parse (%u bytes)", static_cast<unsigned>(body.size()));
    return false;
  }
  body.clear();
  body.shrink_to_fit();
  if (hash != oldHash) {
    // Replace all: notes that left the window go with the rest.
    for (const String& f : Storage.listFiles(kDir, 500)) Storage.remove((std::string(kDir) + "/" + f.c_str()).c_str());
    Storage.ensureDirectoryExists(kDir);
    for (const knowledge::File& f : files) {
      Storage.writeFile((std::string(kDir) + "/" + f.name).c_str(), String(f.content.c_str()));
    }
    LOG_INF("DSK", "Hirn: %u files stored", static_cast<unsigned>(files.size()));
  }
  Storage.writeFile(kStampPath, String((hash + "\n" + std::to_string(now)).c_str()));
  return true;
}

bool forEvent(const CalEvent& event, knowledge::Entry& out) {
  return readEntry('e', knowledge::eventKey(event.start, event.subject), false, out);
}

bool note(const std::string& name, knowledge::Entry& out) { return readEntry('n', name, true, out); }

}  // namespace deskleaf::hirn

#endif  // DESKLEAF_CALENDAR
