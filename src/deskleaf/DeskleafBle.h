#pragma once
// BLE central that fetches the calendar from the Deskleaf bridge on the Mac.
// Protocol (unchanged from deskleaf-display, see its README):
//   device -> bridge  REQ:<semanticHash hex8>:<battery mV>\n
//   bridge -> device  TIME:<epoch>\n  TZ:<posix>\n  then SAME\n  or  LEN[F]:<n>\n<json...>
// One blocking call per sync; the page turner owns the radio only while a book
// is open, so callers run this from the calendar activity or the timer wake.
#include <cstddef>
#include <cstdint>

namespace deskleaf {

enum class SyncStatus : uint8_t {
  BridgeNotFound,
  ConnectFailed,
  Timeout,
  Unchanged,
  NewPayload,
  OutOfMemory,
  Aborted,
  StoreFailed,
};

// Polled while scanning and waiting; returning true cancels the sync.
using AbortCheck = bool (*)();

struct SyncResult {
  SyncStatus status = SyncStatus::BridgeNotFound;
  bool forceRedraw = false;  // LENF header
  char* payload = nullptr;   // NUL-terminated JSON, owned; release with freePayload()
  size_t payloadLen = 0;
};

// Scans up to scanSeconds, connects, sends REQ and waits until the transfer
// completes or timeoutMs passes. Leaves the BLE stack as it found it.
SyncResult syncWithBridge(uint32_t knownPayloadHash, uint16_t batteryMv, AbortCheck shouldAbort = nullptr,
                          uint8_t scanSeconds = 5, uint32_t timeoutMs = 45000);

void freePayload(SyncResult& result);

const char* syncStatusText(SyncStatus status);

}  // namespace deskleaf
