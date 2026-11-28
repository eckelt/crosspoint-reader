#pragma once
// What the Brain knows about the coming events (GET /display/knowledge on the
// Deskleaf worker): the termin note of an event and short cards for the notes
// it links, one level deep. Stored on the SD card as one small file per event
// and per note, so the detail view reads only what it shows. Pure: no IO.
#include <cstddef>
#include <string>
#include <vector>

namespace deskleaf::knowledge {

// "2026-11-12T10:00:00" + "Austausch" -> "2026-11-12T10:00|Austausch", the
// worker's key for an event.
std::string eventKey(const std::string& start, const std::string& subject);

// The file for an event ("e") or a note ("n"): "<prefix>-<FNV-1a hex>.txt".
std::string fileName(char prefix, const std::string& key);

struct File {
  std::string name;
  std::string content;
};

// The worker's JSON as files plus its hash. False when it does not parse.
// Event file: title, then the linked names tab-separated, then the text.
// Note file: title, then its type, then the text.
bool split(const char* json, size_t len, std::string& hash, std::vector<File>& files);

struct Entry {
  std::string title;
  std::string type;                // notes: "person", "kunde", …
  std::vector<std::string> links;  // events: names of the linked notes
  std::string text;                // Markdown, without its "# " title line
};
bool parseEvent(const std::string& content, Entry& out);
bool parseNote(const std::string& content, Entry& out);

// "kunde" -> "Kunde"; unknown types as they are.
std::string typeLabel(const std::string& type);

}  // namespace deskleaf::knowledge
