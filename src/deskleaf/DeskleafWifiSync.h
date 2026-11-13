#pragma once
// Wi-Fi fallback for the calendar sync: when the Mac bridge is out of reach,
// join a saved network and fetch the same week frames from the Deskleaf worker
// (GET /einkal/weeks). Needs /.crosspoint/deskleaf-feed.txt on the SD card:
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

// Disconnects and switches the radio off.
void leave();

}  // namespace deskleaf::wifisync
