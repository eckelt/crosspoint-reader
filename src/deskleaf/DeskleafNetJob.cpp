#include "DeskleafNetJob.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "DeskleafCalendarService.h"
#include "DeskleafTodos.h"
#include "DeskleafWifiSync.h"
#include "util/TaskWatchdog.h"

namespace deskleaf::netjob {
namespace {
// TLS on this stack, as in bgsync (unmeasured: the worker logs its high-water mark).
constexpr uint32_t kStackBytes = 16384;
std::atomic<bool> busy{false};  // from start() until the worker is done
std::atomic<bool> done{false};  // the worker finished and nobody took the result yet
std::atomic<bool> abortFlag{false};
std::atomic<size_t> savedFiles{0};
std::atomic<bool> joined{false};
bool wantInbox = false;  // written before the task starts, read only by it
bool wantTodos = false;

bool aborted() { return abortFlag.load(); }

void worker(void*) {
  size_t saved = 0;
  if (wantTodos) todos::noteAttempt();  // before joining: no network in range counts too
  const bool up = wifisync::join(&aborted);
  if (up) {
    if (wantInbox && !aborted()) saved = wifisync::fetchInbox(&aborted).size();
    if (wantTodos && !aborted()) todos::syncOverWifi(&aborted);
  }
  wifisync::leave();
  LOG_DBG("DSK", "Net task stack free: %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  savedFiles = saved;
  joined = up;
  done = true;
  busy = false;  // the worker touches nothing after this
  vTaskDelete(nullptr);
}
}  // namespace

bool start(const bool inbox, const bool todos) {
  if (busy || bgsync::running() || !wifisync::configured()) return false;
  wantInbox = inbox;
  wantTodos = todos;
  done = false;
  abortFlag = false;
  busy = true;
  if (xTaskCreate(&worker, "DeskleafNet", kStackBytes, nullptr, 1, nullptr) != pdPASS) {
    LOG_ERR("DSK", "OOM: network task");
    busy = false;
    return false;
  }
  return true;
}

bool running() { return busy; }

bool finished(size_t* inboxFiles, bool* online) {
  if (!done) return false;
  done = false;
  if (inboxFiles) *inboxFiles = savedFiles;
  if (online) *online = joined;
  return true;
}

void stop() {
  if (!busy) {
    done = false;
    return;
  }
  abortFlag = true;
  while (busy) {
    resetTaskWatchdogIfSubscribed();
    delay(10);
  }
  done = false;
}

}  // namespace deskleaf::netjob

#endif  // DESKLEAF_CALENDAR
