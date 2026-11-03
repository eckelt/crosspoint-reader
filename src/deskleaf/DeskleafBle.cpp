#include "DeskleafBle.h"

#if DESKLEAF_CALENDAR

#include <Arduino.h>
#include <Logging.h>
#include <NimBLEDevice.h>
#include <TrustedTime.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sys/time.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "util/TaskWatchdog.h"

namespace deskleaf {

namespace {

// Must match the BLE peripheral of the Deskleaf bridge.
constexpr const char* kServiceUuid = "7e20c560-55dd-4c7a-9c61-8f6ea7d7c301";
constexpr const char* kCharUuid = "9c5a5dd9-3c40-4e58-9d0a-95bf7cb9d302";
constexpr const char* kPeripheralName = "Deskleaf";
constexpr size_t kMaxPayload = 200000;
constexpr uint32_t kChunkTimeoutMs = 20000;

// Receive state, shared between the NimBLE host task (notify) and the caller.
struct Rx {
  SemaphoreHandle_t mutex = nullptr;
  char* buffer = nullptr;
  size_t expected = 0;
  size_t written = 0;
  bool active = false;
  bool force = false;
  int64_t bridgeEpoch = 0;  // applied on the caller task, not in the NimBLE host task
  std::string header;       // control lines may be split across notifications
  uint32_t lastChunkMs = 0;
  std::atomic<bool> done{false};
  std::atomic<bool> unchanged{false};
  std::atomic<bool> oom{false};
  std::atomic<bool> connected{false};
};
Rx rx;

char* allocPayload(size_t n) {
  // The X4 Pro has 8 MB PSRAM; a full 10-day payload is tens of KB.
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(n);
  return static_cast<char*>(p);
}

// Consumes one "KEY:value\n" line at the front of `v` if it starts with `key`.
bool takeLine(std::string& v, const char* key, std::string& value) {
  const size_t kl = strlen(key);
  if (v.compare(0, kl, key) != 0) return false;
  const size_t nl = v.find('\n');
  if (nl == std::string::npos) return false;
  value = v.substr(kl, nl - kl);
  v.erase(0, nl + 1);
  return true;
}

void processChunk(const uint8_t* data, size_t len) {
  if (len == 0 || !rx.mutex) return;
  if (xSemaphoreTake(rx.mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

  if (rx.active) {
    const size_t add = std::min(len, rx.expected - rx.written);
    memcpy(rx.buffer + rx.written, data, add);
    rx.written += add;
  } else {
    rx.header.append(reinterpret_cast<const char*>(data), len);
    std::string value;
    // Handle complete control lines; keep a partial one for the next notify.
    for (bool progressed = true; progressed && !rx.active && !rx.done;) {
      progressed = false;
      if (takeLine(rx.header, "TIME:", value)) {
        rx.bridgeEpoch = atoll(value.c_str());
        progressed = true;
      } else if (takeLine(rx.header, "TZ:", value)) {
        progressed = true;  // the device keeps its own timezone setting
      } else if (rx.header.compare(0, 5, "SAME\n") == 0) {
        rx.unchanged = true;
        rx.done = true;
      } else if (rx.header.compare(0, 4, "LEN:") == 0 || rx.header.compare(0, 5, "LENF:") == 0) {
        const size_t nl = rx.header.find('\n');
        if (nl == std::string::npos) break;  // wait for the rest of the header
        const bool force = rx.header[3] == 'F';
        const long declared = strtol(rx.header.c_str() + (force ? 5 : 4), nullptr, 10);
        if (declared <= 0 || static_cast<size_t>(declared) > kMaxPayload) {
          rx.done = true;  // reported as timeout
          break;
        }
        rx.buffer = allocPayload(static_cast<size_t>(declared) + 1);
        if (!rx.buffer) {
          rx.oom = true;
          rx.done = true;
          break;
        }
        rx.expected = static_cast<size_t>(declared);
        rx.written = std::min(rx.header.size() - (nl + 1), rx.expected);
        memcpy(rx.buffer, rx.header.data() + nl + 1, rx.written);
        rx.force = force;
        rx.active = true;
        rx.header.clear();
      } else if (rx.header.find('\n') != std::string::npos) {
        takeLine(rx.header, "", value);  // unknown line: drop it
        progressed = true;
      }
    }
    if (rx.header.size() > 256) rx.header.clear();  // garbage without newline
  }
  rx.lastChunkMs = millis();
  if (rx.active && rx.written >= rx.expected) {
    rx.buffer[rx.expected] = '\0';
    rx.active = false;
    rx.done = true;
  }
  xSemaphoreGive(rx.mutex);
}

void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) { processChunk(data, len); }

class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient*) override { rx.connected = true; }
  void onDisconnect(NimBLEClient*, int reason) override {
    rx.connected = false;
    LOG_DBG("DSK", "Disconnected (%d)", reason);
  }
};
ClientCallbacks clientCallbacks;

void resetRx() {
  if (rx.buffer) free(rx.buffer);
  rx.buffer = nullptr;
  rx.expected = rx.written = 0;
  rx.active = rx.force = false;
  rx.bridgeEpoch = 0;
  rx.header.clear();
  rx.done = rx.unchanged = rx.oom = rx.connected = false;
  rx.lastChunkMs = millis();
}

SyncStatus connectAndRequest(NimBLEClient*& client, uint32_t knownHash, uint16_t batteryMv, AbortCheck shouldAbort,
                             uint8_t scanSeconds) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(30);
  // Non-blocking scan so the caller's task watchdog keeps being fed.
  scan->clearResults();
  if (!scan->start(static_cast<uint32_t>(scanSeconds) * 1000, false)) return SyncStatus::BridgeNotFound;
  while (scan->isScanning()) {
    resetTaskWatchdogIfSubscribed();
    if (shouldAbort && shouldAbort()) {
      scan->stop();
      return SyncStatus::Aborted;
    }
    delay(50);
  }
  const NimBLEScanResults results = scan->getResults();
  const NimBLEAdvertisedDevice* target = nullptr;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* dev = results.getDevice(i);
    if (dev->isAdvertisingService(NimBLEUUID(kServiceUuid)) || dev->getName() == kPeripheralName) {
      target = dev;
      break;
    }
  }
  if (!target) return SyncStatus::BridgeNotFound;

  client = NimBLEDevice::createClient();
  if (!client) return SyncStatus::ConnectFailed;
  client->setClientCallbacks(&clientCallbacks, false);
  client->setConnectTimeout(3000);
  if (!client->connect(target->getAddress())) return SyncStatus::ConnectFailed;

  NimBLERemoteService* svc = client->getService(kServiceUuid);
  NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(kCharUuid) : nullptr;
  if (!chr || !chr->canNotify() || !chr->subscribe(true, onNotify)) return SyncStatus::ConnectFailed;

  char request[32];
  snprintf(request, sizeof(request), "REQ:%08lX:%u\n", static_cast<unsigned long>(knownHash),
           static_cast<unsigned>(batteryMv));
  if (!chr->writeValue(std::string(request), true)) return SyncStatus::ConnectFailed;
  LOG_INF("DSK", "REQ sent (hash %08lX, %u mV)", static_cast<unsigned long>(knownHash), batteryMv);
  return SyncStatus::NewPayload;  // "request sent"; the caller waits for the answer
}

}  // namespace

SyncResult syncWithBridge(const uint32_t knownPayloadHash, const uint16_t batteryMv, const AbortCheck shouldAbort,
                          const uint8_t scanSeconds, const uint32_t timeoutMs) {
  SyncResult result;
  if (!rx.mutex) rx.mutex = xSemaphoreCreateMutex();
  if (!rx.mutex) {
    result.status = SyncStatus::OutOfMemory;
    return result;
  }
  resetRx();

  // Reuse a stack another module started; otherwise own it for this call only.
  const bool ownsStack = !NimBLEDevice::isInitialized();
  if (ownsStack) {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(9);  // dBm
    NimBLEDevice::setMTU(247);
  }

  NimBLEClient* client = nullptr;
  SyncStatus status = connectAndRequest(client, knownPayloadHash, batteryMv, shouldAbort, scanSeconds);
  if (status == SyncStatus::NewPayload) {
    const uint32_t start = millis();
    status = SyncStatus::Timeout;
    while (millis() - start < timeoutMs) {
      if (rx.done) break;
      if (!rx.connected) break;
      if (millis() - rx.lastChunkMs > kChunkTimeoutMs) break;
      if (shouldAbort && shouldAbort()) {
        status = SyncStatus::Aborted;
        break;
      }
      resetTaskWatchdogIfSubscribed();
      delay(50);
    }
    xSemaphoreTake(rx.mutex, portMAX_DELAY);
    if (status == SyncStatus::Aborted) {
      // keep it
    } else if (rx.oom) {
      status = SyncStatus::OutOfMemory;
    } else if (rx.unchanged) {
      status = SyncStatus::Unchanged;
    } else if (rx.done && rx.buffer) {
      status = SyncStatus::NewPayload;
      result.payload = rx.buffer;
      result.payloadLen = rx.expected;
      result.forceRedraw = rx.force;
      rx.buffer = nullptr;
    }
    const int64_t epoch = rx.bridgeEpoch;
    xSemaphoreGive(rx.mutex);
    // settimeofday + NVS write stay off the NimBLE host task's small stack.
    if (epoch > 1700000000LL) {
      const timeval tv{static_cast<time_t>(epoch), 0};
      if (settimeofday(&tv, nullptr) == 0) trustedtime::note();
    }
  }

  if (client) {
    // deleteClient() only defers while DISCONNECTING and deinit() would then
    // strand the slot, so wait for the disconnect event first.
    if (client->isConnected() && client->disconnect()) {
      const uint32_t start = millis();
      while (rx.connected && millis() - start < 2000) delay(20);
    }
    NimBLEDevice::deleteClient(client);
  }
  if (ownsStack) NimBLEDevice::deinit(true);
  resetRx();
  result.status = status;
  LOG_INF("DSK", "Sync: %s", syncStatusText(status));
  return result;
}

void freePayload(SyncResult& result) {
  if (result.payload) free(result.payload);
  result.payload = nullptr;
  result.payloadLen = 0;
}

const char* syncStatusText(const SyncStatus status) {
  switch (status) {
    case SyncStatus::BridgeNotFound:
      return "Bridge nicht gefunden";
    case SyncStatus::ConnectFailed:
      return "Verbindung fehlgeschlagen";
    case SyncStatus::Timeout:
      return "Zeitüberschreitung";
    case SyncStatus::Unchanged:
      return "Aktuell";
    case SyncStatus::NewPayload:
      return "Aktualisiert";
    case SyncStatus::OutOfMemory:
      return "Kein Speicher";
    case SyncStatus::Aborted:
      return "Abgebrochen";
    case SyncStatus::StoreFailed:
      return "Speichern fehlgeschlagen";
  }
  return "";
}

}  // namespace deskleaf

#endif  // DESKLEAF_CALENDAR
