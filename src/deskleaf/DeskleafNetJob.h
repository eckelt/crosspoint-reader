#pragma once
// A Wi-Fi visit on a worker task for the Spick and Heute views, which stay
// usable meanwhile: join, fetch the inbox and/or sync the to-dos, leave.
// Only the loop task calls these. Like bgsync it owns the radio while it runs,
// so screen changes and deep sleep call stop() first; nothing else on the
// device joins Wi-Fi while one of these views is open.
#include <cstddef>

namespace deskleaf::netjob {

// False when a job runs already or the feed is not configured.
bool start(bool inbox, bool todos);
bool running();
// True once, when a started job has finished; `inboxFiles` = files it saved,
// `online` = Wi-Fi came up.
bool finished(size_t* inboxFiles = nullptr, bool* online = nullptr);
// Aborts a running job, waits for the worker and drops its result.
void stop();

}  // namespace deskleaf::netjob
