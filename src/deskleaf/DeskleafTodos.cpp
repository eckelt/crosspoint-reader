#include "DeskleafTodos.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <ctime>
#include <mutex>
#include <vector>

#include "DeskleafWifiSync.h"

namespace deskleaf::todos {
namespace {
constexpr const char* kDir = "/.crosspoint/deskleaf";
constexpr const char* kListPath = "/.crosspoint/deskleaf/todos.txt";
constexpr const char* kQueuePath = "/.crosspoint/deskleaf/todo-queue.txt";
constexpr const char* kAttemptPath = "/.crosspoint/deskleaf/todo-attempt.txt";
constexpr size_t kMaxList = 8 * 1024;  // 15 items of short text
constexpr size_t kMaxSends = 32;       // bounds the loop when taps keep coming

// The queue file is read, changed and written by the loop task (taps) and the
// network worker (sends); this keeps each read-modify-write whole.
std::mutex queueMutex;

std::string readText(const char* path) {
  if (!Storage.exists(path)) return {};
  return std::string(Storage.readFile(path).c_str());
}

void writeText(const char* path, const std::string& text) {
  Storage.ensureDirectoryExists(kDir);
  Storage.writeFile(path, String(text.c_str()));
}

std::vector<feed::TodoChange> loadQueue() { return feed::parseQueue(readText(kQueuePath)); }

void saveQueue(const std::vector<feed::TodoChange>& queue) {
  if (queue.empty()) {
    Storage.remove(kQueuePath);
  } else {
    writeText(kQueuePath, feed::serializeQueue(queue));
  }
}

int64_t now() { return static_cast<int64_t>(time(nullptr)); }
}  // namespace

bool load(feed::TodoList& list) {
  if (!feed::parseTodos(readText(kListPath), list)) return false;
  std::lock_guard<std::mutex> guard(queueMutex);
  std::vector<feed::TodoChange> queue = loadQueue();
  const size_t before = queue.size();
  feed::applyQueue(list, queue);
  if (queue.size() != before) saveQueue(queue);
  return true;
}

bool toggle(feed::TodoList& list, const std::string& id) {
  std::lock_guard<std::mutex> guard(queueMutex);
  std::vector<feed::TodoChange> queue = loadQueue();
  if (!feed::toggleTodo(list, queue, id)) return false;
  saveQueue(queue);
  return true;
}

size_t pending() {
  std::lock_guard<std::mutex> guard(queueMutex);
  return loadQueue().size();
}

bool wanted(const std::string& todayIso, const bool userIsLooking) {
  feed::TodoList stored;
  const bool today = feed::parseTodos(readText(kListPath), stored) && stored.date >= todayIso;
  const bool taps = pending() > 0;
  if (userIsLooking) return taps || !today;
  return feed::todoVisitDue(taps, today, std::atoll(readText(kAttemptPath).c_str()), now());
}

void noteAttempt() { writeText(kAttemptPath, std::to_string(now())); }

bool syncOverWifi(const AbortCheck shouldAbort) {
  // One change at a time against the queue as it is now: taps keep working meanwhile.
  for (size_t sends = 0; sends < kMaxSends; ++sends) {
    if (shouldAbort && shouldAbort()) return false;
    feed::TodoChange change;
    {
      std::lock_guard<std::mutex> guard(queueMutex);
      const std::vector<feed::TodoChange> queue = loadQueue();
      if (queue.empty()) break;
      change = queue.front();
    }
    std::string body;
    const int status = wifisync::getText(feed::todoActionPath(change), body, 256);
    const feed::SendOutcome outcome = feed::classifySend(status, body, change);
    if (outcome == feed::SendOutcome::Stop) {
      LOG_ERR("DSK", "To-do %s: HTTP %d, kept for later", change.id.c_str(), status);
      break;  // in order: nothing after a change that did not go out
    }
    if (outcome == feed::SendOutcome::Stale) LOG_INF("DSK", "To-do %s: stale (HTTP %d), dropped", change.id.c_str(), status);
    std::lock_guard<std::mutex> guard(queueMutex);
    std::vector<feed::TodoChange> queue = loadQueue();
    feed::settleChange(queue, change, outcome == feed::SendOutcome::Sent);
    saveQueue(queue);
  }
  if (shouldAbort && shouldAbort()) return false;
  std::string body;
  const int status = wifisync::getText("/display/todos", body, kMaxList);
  feed::TodoList list;
  if (status != 200 || !feed::parseTodos(body, list)) {
    LOG_ERR("DSK", "To-dos: fetch failed (HTTP %d)", status);
    return false;
  }
  writeText(kListPath, body);
  {
    // The device never sees a 404's body, so a tap on an item gone from today's
    // list stops the queue; the fresh list drops such taps here.
    std::lock_guard<std::mutex> guard(queueMutex);
    std::vector<feed::TodoChange> queue = loadQueue();
    const size_t before = queue.size();
    feed::applyQueue(list, queue);
    if (queue.size() != before) saveQueue(queue);
  }
  LOG_INF("DSK", "To-dos: %u for %s", static_cast<unsigned>(list.items.size()), list.date.c_str());
  return true;
}

}  // namespace deskleaf::todos

#endif  // DESKLEAF_CALENDAR
