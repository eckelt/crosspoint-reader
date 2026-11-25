#pragma once
// Automatic firmware updates for the calendar device. The bridge announces the
// newest release in its BLE answer (FW:<version>); without the bridge the
// hourly Wi-Fi fallback asks GitHub itself. A newer version is installed over
// Wi-Fi on a timer wake or while the calendar is open, never while reading or
// with less than 30 % battery off the charger.
//
// Rollback: before the reboot the running app partition is remembered. If the
// new firmware crashes three times (panic or watchdog reset) before it ran long
// enough to call confirmHealthy(), bootGuard() switches back to the old one.
#include <string>

#include "DeskleafBle.h"

class GfxRenderer;

namespace deskleaf::autoupdate {

// a.b.c of `candidate` is newer than the running firmware.
bool isNewer(const std::string& candidate);

// Newest release on GitHub, "" when unknown. Needs Wi-Fi to be up.
std::string newestRelease();

// The announced release is newer, the battery allows it and the last attempt is
// more than an hour ago.
bool due(const std::string& announced, int64_t lastAttempt);

// Joins Wi-Fi, installs the newest release and returns true when the new image
// is ready to boot (the caller restarts or sleeps). `renderer` shows a notice.
bool install(GfxRenderer* renderer, const std::string& version, AbortCheck shouldAbort);

// First thing in setup(): counts crash resets of a fresh install and rolls back.
void bootGuard();
// The new firmware works (a sync went through, or it ran a minute): stop guarding.
void confirmHealthy();

}  // namespace deskleaf::autoupdate
