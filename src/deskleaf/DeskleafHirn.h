#pragma once
// The Brain's notes for the calendar's detail view, fetched over Wi-Fi from
// the Deskleaf worker (GET /display/knowledge) and kept on the SD card in
// /.crosspoint/deskleaf/hirn: one file per event and per linked note.
// Formats: lib/DeskleafCalendar/DeskleafKnowledge.h.
#include <string>

#include "DeskleafCalendar.h"
#include "DeskleafKnowledge.h"

namespace deskleaf::hirn {

// With Wi-Fi up: fetches the notes for `weeks` weeks from `monday` and
// replaces the stored ones when they changed. At most hourly unless `force`.
bool fetchOverWifi(const std::string& monday, int weeks, bool force);

// The termin note of an event; false when the Brain has none.
bool forEvent(const CalEvent& event, knowledge::Entry& out);

// A linked note by its name; false when it was not fetched.
bool note(const std::string& name, knowledge::Entry& out);

}  // namespace deskleaf::hirn
