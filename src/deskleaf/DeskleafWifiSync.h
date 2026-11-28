#pragma once
// Wi-Fi fallback for the calendar sync: when the Mac bridge is out of reach,
// join a saved network and fetch the same week frames from the Deskleaf worker
// (GET /display/weeks). Needs deskleaf-feed.txt in the SD root (or in /.crosspoint):
// the feed token on the first line, optionally the worker's base URL on the
// second (default https://mcp.deskleaf.one).
#include <string>
#include <vector>

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

// GET <base><path> with the feed token; the body (at most maxBytes) into `body`.
// Returns the HTTP status, 0 when there was no answer or the body was too large.
int getText(const std::string& path, std::string& body, size_t maxBytes);

// Files waiting in the worker's display inbox (GET /display/inbox), saved to the
// SD card and checked off: a .bmp becomes /sleep.bmp, books go to /Books, notes
// to /Spick (named by their inbox id, so they sort by arrival), the rest to /Inbox. Returns the names saved, in arrival order. Needs Wi-Fi up.
std::vector<std::string> fetchInbox();

// Disconnects and switches the radio off.
void leave();

}  // namespace deskleaf::wifisync
