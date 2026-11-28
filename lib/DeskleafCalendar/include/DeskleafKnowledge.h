#pragma once
// What the Brain knows about the coming events (GET /display/knowledge on the
// Deskleaf worker): the termin note of an event and short cards for the notes
// it links, one level deep. Stored on the SD card as one small file per event
// and per note, so the detail view reads only what it shows. Pure: no IO.
#include <ArduinoJson.h>

#include <cstddef>
#include <functional>
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

// The worker's answer, parsed once; the caller can free the body afterwards
// and then write the files one at a time.
// Event file: title, then the linked names tab-separated, then the text.
// Note file: title, then its type, then the text.
class Answer {
 public:
  bool parse(const char* json, size_t len);  // false when it does not parse
  const std::string& hash() const { return hash_; }
  // Calls `fn` for each file; stops early when it returns false.
  bool forEachFile(const std::function<bool(const File&)>& fn) const;

 private:
  JsonDocument doc_;
  std::string hash_;
};

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
