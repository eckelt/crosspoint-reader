#pragma once
// The Brain's notes for the calendar's detail view, fetched over Wi-Fi from
// the Deskleaf worker (GET /display/knowledge) and kept on the SD card in
// /.crosspoint/deskleaf/hirn: one file per event and per linked note.
// Formats: lib/DeskleafCalendar/include/DeskleafKnowledge.h.
#include <string>

#include "DeskleafBle.h"
#include "DeskleafCalendar.h"
#include "DeskleafKnowledge.h"

namespace deskleaf::hirn {

// With Wi-Fi up: fetches the notes for `weeks` weeks from `monday` and
// replaces the stored ones when they changed. At most hourly unless `force`;
// nothing after an abort.
bool fetchOverWifi(const std::string& monday, int weeks, bool force, AbortCheck shouldAbort);

// The JSON of /display/knowledge as the bridge relays it (a K frame): stored
// like a Wi-Fi fetch. False when it does not parse or a write failed.
bool storeJson(const char* json, size_t len);

// The hash of the stored notes for the WREQ line, "-" when there are none.
std::string storedHash();

// Worth a Wi-Fi visit of its own while the user looks: the last fetch is
// older than a few minutes.
bool due();

// The termin note of an event; false when the Brain has none.
bool forEvent(const CalEvent& event, knowledge::Entry& out);

// A linked note by its name; false when it was not fetched.
bool note(const std::string& name, knowledge::Entry& out);

}  // namespace deskleaf::hirn
