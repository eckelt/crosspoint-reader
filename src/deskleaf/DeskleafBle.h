#pragma once
// BLE central that fetches the calendar from the Deskleaf bridge on the Mac.
// Protocol (deskleaf-display bridge):
//   device -> bridge  REQ:<semanticHash hex8>:<battery mV>\n            (one payload)
//                     WREQ:<set hash hex8>:<mV>:<Monday>:<weeks>\n     (week frames,
//                     see DeskleafCalendar.h)
//   bridge -> device  TIME:<epoch>\n  TZ:<posix>\n  then SAME\n  or  LEN[F]:<n>\n<body...>
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
  char bridgeTz[64] = "";    // POSIX TZ the bridge sent, empty if none
  char* payload = nullptr;   // NUL-terminated body, owned; release with freePayload()
  size_t payloadLen = 0;
};

// Scans up to scanSeconds, connects, writes `requestLine` (REQ or WREQ, with
// its newline) and waits for the answer: up to timeoutMs until a transfer
// starts, then as long as chunks keep arriving. Leaves the BLE stack as it
// found it.
SyncResult syncWithBridge(const char* requestLine, AbortCheck shouldAbort = nullptr, uint8_t scanSeconds = 5,
                          uint32_t timeoutMs = 45000);

void freePayload(SyncResult& result);

const char* syncStatusText(SyncStatus status);

}  // namespace deskleaf
