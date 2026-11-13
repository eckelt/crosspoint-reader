#pragma once
// Wi-Fi fallback for the calendar sync: when the Mac bridge is out of reach,
// join a saved network and fetch the same week frames from the Deskleaf worker
// (GET /display/weeks). Needs deskleaf-feed.txt in the SD root (or in /.crosspoint):
// the feed token on the first line, optionally the worker's base URL on the
// second (default https://mcp.deskleaf.one).
#include <string>

#include "DeskleafBle.h"

namespace deskleaf::wifisync {

// The config file exists and holds a token.
bool configured();

// Joins the last network first, then the other saved ones, each for a few
// seconds. True once connected; the caller ends with leave().
bool join(AbortCheck shouldAbort);

// Week frames for `weeks` weeks from `monday` ("YYYY-MM-DD"; "" = this week),
// into `body`. NewPayload on success, otherwise why not.
SyncStatus fetchWeeks(const std::string& monday, int weeks, std::string& body);

// HTTP status of the last fetchWeeks(), 0 when the worker never answered.
int lastHttpStatus();

// Disconnects and switches the radio off.
void leave();

}  // namespace deskleaf::wifisync
