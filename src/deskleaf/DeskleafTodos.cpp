#include "DeskleafTodos.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdlib>
#include <ctime>

#include "DeskleafWifiSync.h"

namespace deskleaf::todos {
namespace {
constexpr const char* kDir = "/.crosspoint/deskleaf";
constexpr const char* kListPath = "/.crosspoint/deskleaf/todos.txt";
constexpr const char* kQueuePath = "/.crosspoint/deskleaf/todo-queue.txt";
constexpr const char* kAttemptPath = "/.crosspoint/deskleaf/todo-attempt.txt";
constexpr size_t kMaxList = 8 * 1024;           // 15 items of short text
constexpr int64_t kRetryWithTaps = 15 * 60;     // taps waiting: try again soon
constexpr int64_t kRetryStaleList = 3 * 3600;   // only the list is old: no hurry

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
  std::vector<feed::TodoChange> queue = loadQueue();
  const size_t before = queue.size();
  feed::applyQueue(list, queue);
  if (queue.size() != before) saveQueue(queue);
  return true;
}

bool toggle(feed::TodoList& list, const std::string& id) {
  std::vector<feed::TodoChange> queue = loadQueue();
  if (!feed::toggleTodo(list, queue, id)) return false;
  saveQueue(queue);
  return true;
}

size_t pending() { return loadQueue().size(); }

bool wanted(const std::string& todayIso) {
  const bool taps = pending() > 0;
  feed::TodoList stored;
  const bool fresh = feed::parseTodos(readText(kListPath), stored) && stored.date >= todayIso;
  if (!taps && fresh) return false;
  const int64_t last = std::atoll(readText(kAttemptPath).c_str());
  return now() - last >= (taps ? kRetryWithTaps : kRetryStaleList);
}

bool syncOverWifi() {
  writeText(kAttemptPath, std::to_string(now()));
  std::vector<feed::TodoChange> queue = loadQueue();
  size_t handled = 0;
  for (const feed::TodoChange& change : queue) {
    std::string body;
    const int status = wifisync::getText(feed::todoActionPath(change), body, 256);
    const feed::SendOutcome outcome = feed::classifySend(status, body, change);
    if (outcome == feed::SendOutcome::Stop) {
      LOG_ERR("DSK", "To-do %s: HTTP %d, kept for later", change.id.c_str(), status);
      break;  // in order: nothing after a change that did not go out
    }
    if (outcome == feed::SendOutcome::Stale) LOG_INF("DSK", "To-do %s: stale (HTTP %d), dropped", change.id.c_str(), status);
    ++handled;
  }
  if (handled > 0) {
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(handled));
    saveQueue(queue);
  }
  std::string body;
  const int status = wifisync::getText("/display/todos", body, kMaxList);
  feed::TodoList list;
  if (status != 200 || !feed::parseTodos(body, list)) {
    LOG_ERR("DSK", "To-dos: fetch failed (HTTP %d)", status);
    return false;
  }
  writeText(kListPath, body);
  LOG_INF("DSK", "To-dos: %u for %s, %u taps waiting", static_cast<unsigned>(list.items.size()), list.date.c_str(),
          static_cast<unsigned>(queue.size()));
  return true;
}

namespace background {
namespace {
// TLS on this stack, as in the calendar's sync task.
constexpr uint32_t kStackBytes = 16384;
std::atomic<bool> busy{false};
std::atomic<bool> done{false};
std::atomic<size_t> savedFiles{0};
bool wantInbox = false;
bool wantTodos = false;

void worker(void*) {
  size_t saved = 0;
  if (wifisync::join(nullptr)) {
    if (wantInbox) saved = wifisync::fetchInbox().size();
    if (wantTodos) syncOverWifi();
  }
  wifisync::leave();
  savedFiles = saved;
  done = true;
  busy = false;  // the task touches nothing after this
  vTaskDelete(nullptr);
}
}  // namespace

bool start(const bool inbox, const bool todos) {
  if (busy || !wifisync::configured()) return false;
  wantInbox = inbox;
  wantTodos = todos;
  done = false;
  busy = true;
  if (xTaskCreate(&worker, "DeskleafNet", kStackBytes, nullptr, 1, nullptr) != pdPASS) {
    LOG_ERR("DSK", "OOM: network task");
    busy = false;
    return false;
  }
  return true;
}

bool running() { return busy; }

bool finished(size_t* inboxFiles) {
  if (!done) return false;
  done = false;
  if (inboxFiles) *inboxFiles = savedFiles;
  return true;
}
}  // namespace background

}  // namespace deskleaf::todos

#endif  // DESKLEAF_CALENDAR
