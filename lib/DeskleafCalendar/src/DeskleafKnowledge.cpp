#include "DeskleafKnowledge.h"

#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>

namespace deskleaf::knowledge {
namespace {
uint32_t fnv(const std::string& s) {
  uint32_t h = 2166136261u;
  for (const unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

// Tabs and line breaks would break the file layout.
std::string oneLine(std::string s) {
  for (char& c : s) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return s;
}

// The worker starts the text with the note's "# " title, which the card shows apart.
std::string withoutTitle(const std::string& text) {
  if (text.rfind("# ", 0) != 0) return text;
  const size_t nl = text.find('\n');
  if (nl == std::string::npos) return {};
  size_t start = nl + 1;
  while (start < text.size() && text[start] == '\n') ++start;
  return text.substr(start);
}

bool parseFile(const std::string& content, std::string& first, std::string& second, std::string& rest) {
  const size_t a = content.find('\n');
  if (a == std::string::npos) return false;
  const size_t b = content.find('\n', a + 1);
  first = content.substr(0, a);
  second = content.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
  rest = b == std::string::npos ? std::string() : content.substr(b + 1);
  return !first.empty();
}
}  // namespace

std::string eventKey(const std::string& start, const std::string& subject) { return start.substr(0, 16) + "|" + subject; }

std::string fileName(const char prefix, const std::string& key) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%c-%08x.txt", prefix, static_cast<unsigned>(fnv(key)));
  return buf;
}

bool split(const char* json, const size_t len, std::string& hash, std::vector<File>& files) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
  if (!doc["events"].is<JsonArrayConst>() || !doc["hash"].is<const char*>()) return false;
  hash = doc["hash"].as<const char*>();
  files.clear();
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    const char* key = e["key"] | "";
    if (!*key) continue;
    std::string links;
    for (JsonVariantConst l : e["links"].as<JsonArrayConst>()) {
      if (!links.empty()) links += '\t';
      links += oneLine(l | "");
    }
    files.push_back({fileName('e', key), oneLine(e["title"] | "") + "\n" + links + "\n" + withoutTitle(e["text"] | "")});
  }
  for (JsonPairConst n : doc["notes"].as<JsonObjectConst>()) {
    JsonObjectConst note = n.value().as<JsonObjectConst>();
    const std::string name = n.key().c_str();
    std::string title = oneLine(note["title"] | "");
    if (title.empty()) title = oneLine(name);
    files.push_back({fileName('n', name), title + "\n" + oneLine(note["type"] | "") + "\n" + withoutTitle(note["text"] | "")});
  }
  return true;
}

bool parseEvent(const std::string& content, Entry& out) {
  out = Entry{};
  std::string links;
  if (!parseFile(content, out.title, links, out.text)) return false;
  size_t pos = 0;
  while (pos < links.size()) {
    const size_t tab = links.find('\t', pos);
    const std::string name = links.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos);
    if (!name.empty()) out.links.push_back(name);
    if (tab == std::string::npos) break;
    pos = tab + 1;
  }
  return true;
}

bool parseNote(const std::string& content, Entry& out) {
  out = Entry{};
  return parseFile(content, out.title, out.type, out.text);
}

std::string typeLabel(const std::string& type) {
  if (type == "person") return "Person";
  if (type == "kunde") return "Kunde";
  if (type == "projekt") return "Projekt";
  if (type == "termin") return "Termin";
  if (type == "thema") return "Thema";
  return type;
}

}  // namespace deskleaf::knowledge
