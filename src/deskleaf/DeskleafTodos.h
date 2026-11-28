#pragma once
// The daily to-do list from the Deskleaf worker (GET /display/todos) and taps
// on it. The list and the unsent taps live on the SD card, so ticking works
// offline; the next Wi-Fi visit sends the taps in order, then fetches the list.
// Formats and the queue rules: lib/DeskleafCalendar/DeskleafFeed.h.
#include <cstdint>
#include <string>

#include "DeskleafBle.h"
#include "DeskleafFeed.h"

namespace deskleaf::todos {

// The stored list with unsent taps shown over it. False when there is none yet.
bool load(feed::TodoList& list);

// A tap: flips the item in `list` and stores the change for sending. Safe
// while syncOverWifi() runs on another task.
bool toggle(feed::TodoList& list, const std::string& id);

// Taps not yet sent.
size_t pending();

// A Wi-Fi visit is worth it: taps are waiting, or the stored list is not
// today's. Rate-limited by noteAttempt(), so a server without the endpoint or
// a missing network does not keep the radio busy. A view the user is looking
// at passes userIsLooking and skips the rate limit.
bool wanted(const std::string& todayIso, bool userIsLooking = false);

// Records a visit; call it before joining Wi-Fi for the to-dos.
void noteAttempt();

// Today's list as the bridge relays it (a T frame): stored like a fetched one,
// taps on items no longer on it dropped. False when it does not parse.
bool storeList(const std::string& text);

// FNV-1a of the stored list for the WREQ line (0 without one), so the bridge
// sends the list only when it changed.
uint32_t listHash();

// With Wi-Fi up: sends the waiting taps in order, then fetches the list.
// True when a new list was stored.
bool syncOverWifi(AbortCheck shouldAbort);

}  // namespace deskleaf::todos
