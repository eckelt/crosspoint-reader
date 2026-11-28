#pragma once
// The daily to-do list from the Deskleaf worker (GET /display/todos) and taps
// on it. The list and the unsent taps live on the SD card, so ticking works
// offline; the next Wi-Fi visit sends the taps in order, then fetches the list.
// Formats and the queue rules: lib/DeskleafCalendar/DeskleafFeed.h.
#include <string>

#include "DeskleafFeed.h"

namespace deskleaf::todos {

// The stored list with unsent taps shown over it. False when there is none yet.
bool load(feed::TodoList& list);

// A tap: flips the item in `list` and stores the change for sending.
bool toggle(feed::TodoList& list, const std::string& id);

// Taps not yet sent.
size_t pending();

// A Wi-Fi visit is worth it: taps are waiting, or the stored list is not
// today's. Rate-limited, so a server without the endpoint does not keep the
// radio busy.
bool wanted(const std::string& todayIso);

// With Wi-Fi up: sends the waiting taps in order, then fetches the list.
// True when a new list was stored.
bool syncOverWifi();

// Inbox and/or to-dos on a worker task (join Wi-Fi, fetch, leave), for views
// that stay usable meanwhile. Notes fetched from the inbox go to /Spick.
namespace background {
bool start(bool inbox, bool todos);
bool running();
// True once, when a started job has finished; `inboxFiles` = files it saved.
bool finished(size_t* inboxFiles = nullptr);
}  // namespace background

}  // namespace deskleaf::todos
