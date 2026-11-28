#include "DeskleafHirn.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>

#include "DeskleafWifiSync.h"

namespace deskleaf::hirn {
namespace {
constexpr const char* kDir = "/.crosspoint/deskleaf/hirn";
constexpr const char* kStampPath = "/.crosspoint/deskleaf/hirn-stamp.txt";  // "<hash>\n<epoch>"
constexpr size_t kMaxBody = 96 * 1024;  // a month of termin notes; larger answers are refused
constexpr int64_t kMinInterval = 3600;  // background visits
constexpr int64_t kForcedGap = 5 * 60;   // the user is looking: still not on every open

struct Stamp {
  std::string hash;
  int64_t at = 0;
};

Stamp readStamp() {
  Stamp s;
  if (!Storage.exists(kStampPath)) return s;
  const std::string text = Storage.readFile(kStampPath).c_str();
  const size_t nl = text.find('\n');
  s.hash = text.substr(0, nl);
  if (nl != std::string::npos) s.at = std::atoll(text.c_str() + nl + 1);
  return s;
}

// Also after a failure, so a broken answer is not fetched again on every visit.
void writeStamp(const std::string& hash, const int64_t at) {
  Storage.ensureDirectoryExists("/.crosspoint/deskleaf");
  Storage.writeFile(kStampPath, String((hash + "\n" + std::to_string(at)).c_str()));
}

std::string readText(const std::string& path) {
  if (!Storage.exists(path.c_str())) return {};
  return std::string(Storage.readFile(path.c_str()).c_str());
}

bool readEntry(const char prefix, const std::string& key, const bool isNote, knowledge::Entry& out) {
  const std::string content = readText(std::string(kDir) + "/" + knowledge::fileName(prefix, key));
  if (content.empty()) return false;
  return isNote ? knowledge::parseNote(content, out) : knowledge::parseEvent(content, out);
}
bool store(const knowledge::Answer& answer, const Stamp& stamp, int64_t now, AbortCheck shouldAbort);
}  // namespace

bool fetchOverWifi(const std::string& monday, const int weeks, const bool force, const AbortCheck shouldAbort) {
  if (shouldAbort && shouldAbort()) return false;
  const Stamp stamp = readStamp();
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (now - stamp.at < (force ? kForcedGap : kMinInterval)) return false;

  std::string body;
  const int status = wifisync::getText("/display/knowledge?monday=" + monday + "&weeks=" + std::to_string(weeks), body,
                                       kMaxBody, shouldAbort);
  if (status != 200) {
    LOG_ERR("DSK", "Hirn: fetch failed (HTTP %d)", status);
    if (!(shouldAbort && shouldAbort())) writeStamp(stamp.hash, now);
    return false;
  }
  // Parsed once, then the body goes: the files are written one at a time.
  auto answer = std::unique_ptr<knowledge::Answer>(new (std::nothrow) knowledge::Answer());
  if (!answer || !answer->parse(body.data(), body.size())) {
    LOG_ERR("DSK", "Hirn: answer does not parse (%u bytes)", static_cast<unsigned>(body.size()));
    writeStamp(stamp.hash, now);
    return false;
  }
  std::string().swap(body);
  return store(*answer, stamp, now, shouldAbort);
}

bool storeJson(const char* json, const size_t len) {
  auto answer = std::unique_ptr<knowledge::Answer>(new (std::nothrow) knowledge::Answer());
  if (!answer || !answer->parse(json, len)) {
    LOG_ERR("DSK", "Hirn: BLE frame does not parse (%u bytes)", static_cast<unsigned>(len));
    return false;
  }
  return store(*answer, readStamp(), static_cast<int64_t>(time(nullptr)), nullptr);
}

std::string storedHash() {
  const std::string hash = readStamp().hash;
  // A field of the WREQ line: no separators, bounded.
  if (hash.empty() || hash.size() > 16 || hash.find_first_of(":\n") != std::string::npos) return "-";
  return hash;
}

namespace {
bool store(const knowledge::Answer& answerRef, const Stamp& stamp, const int64_t now, const AbortCheck shouldAbort) {
  const knowledge::Answer* answer = &answerRef;
  const std::string hash = answer->hash();
  if (hash != stamp.hash) {
    // Replace all: notes that left the window go with the rest. An aborted
    // write leaves the old stamp, so the next visit writes again.
    for (const String& f : Storage.listFiles(kDir, 500)) Storage.remove((std::string(kDir) + "/" + f.c_str()).c_str());
    Storage.ensureDirectoryExists(kDir);
    size_t written = 0;
    bool allWritten = true;
    const bool complete = answer->forEachFile([&](const knowledge::File& f) {
      if (shouldAbort && shouldAbort()) return false;
      if (Storage.writeFile((std::string(kDir) + "/" + f.name).c_str(), String(f.content.c_str()))) {
        ++written;
      } else {
        allWritten = false;
      }
      return true;
    });
    LOG_INF("DSK", "Hirn: %u files stored", static_cast<unsigned>(written));
    if (!complete) return false;  // the old stamp stays: the next visit writes again
    // A failed write (full card): an empty hash makes the next visit write again.
    writeStamp(allWritten ? hash : std::string(), now);
    return allWritten;
  }
  writeStamp(hash, now);
  return true;
}
}  // namespace

bool due() { return static_cast<int64_t>(time(nullptr)) - readStamp().at >= kForcedGap; }

bool forEvent(const CalEvent& event, knowledge::Entry& out) {
  return readEntry('e', knowledge::eventKey(event.start, event.subject), false, out);
}

bool note(const std::string& name, knowledge::Entry& out) { return readEntry('n', name, true, out); }

}  // namespace deskleaf::hirn

#endif  // DESKLEAF_CALENDAR
