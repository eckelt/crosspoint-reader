#include "DeskleafFeed.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace deskleaf::feed {
namespace {

std::vector<std::string> splitTabs(const std::string& line, size_t maxFields) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (fields.size() + 1 < maxFields) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) break;
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  fields.push_back(line.substr(start));  // the last field keeps any further tabs
  return fields;
}

template <typename Fn>
void forEachLine(const std::string& text, Fn fn) {
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    pos = nl == std::string::npos ? text.size() : nl + 1;
    fn(line);
  }
}

bool isIsoDate(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

bool isTodoId(const std::string& id) {
  if (id.size() != 6) return false;
  return std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'); });
}

}  // namespace

std::vector<InboxEntry> parseInboxListing(const std::string& text) {
  std::vector<InboxEntry> out;
  forEachLine(text, [&](const std::string& line) {
    const auto f = splitTabs(line, 4);
    if (f.size() != 4 || f[0].empty() || f[1].empty() || f[3].empty()) return;
    char* end = nullptr;
    const unsigned long bytes = std::strtoul(f[2].c_str(), &end, 10);
    if (end == f[2].c_str() || *end != '\0') return;
    out.push_back({f[0], f[1], static_cast<size_t>(bytes), f[3]});
  });
  return out;
}

std::string percentEncodeSegment(const std::string& segment) {
  std::string out;
  for (const unsigned char c : segment) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out += static_cast<char>(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

std::string sdSafeName(const std::string& name) {
  std::string out;
  for (const unsigned char c : name) {
    if (c < 0x20 || c == 0x7f) continue;
    out += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
               ? '-'
               : static_cast<char>(c);
  }
  while (!out.empty() && (out[0] == '.' || out[0] == ' ')) out.erase(0, 1);
  return out.empty() ? std::string("datei") : out;
}

std::string inboxDestination(const InboxEntry& e) {
  if (e.kind == "sleep") return "/sleep.bmp";
  if (e.kind == "note") return "/Spick/" + sdSafeName(e.id);
  if (e.kind == "book") return "/Books/" + sdSafeName(e.name);
  return "/Inbox/" + sdSafeName(e.name);
}

bool parseTodos(const std::string& text, TodoList& out) {
  out = TodoList{};
  bool header = false;
  bool ok = false;
  forEachLine(text, [&](const std::string& line) {
    if (!header) {
      header = true;
      if (line.size() < 11 || line[0] != '#') return;
      const auto f = splitTabs(line.substr(1), 2);
      if (!isIsoDate(f[0])) return;
      out.date = f[0];
      ok = true;
      return;
    }
    if (!ok) return;
    const auto f = splitTabs(line, 5);
    if (f.size() != 5 || !isTodoId(f[0]) || (f[1] != "open" && f[1] != "done")) return;
    if (!f[2].empty() && !isIsoDate(f[2])) return;
    TodoItem item;
    item.id = f[0];
    item.done = f[1] == "done";
    item.due = f[2];
    if (f[3] == "overdue") {
      item.bucket = TodoBucket::Overdue;
    } else if (f[3] == "today") {
      item.bucket = TodoBucket::Today;
    } else if (f[3] == "this_week") {
      item.bucket = TodoBucket::ThisWeek;
    } else {
      return;
    }
    item.text = f[4];
    out.items.push_back(std::move(item));
  });
  return ok;
}

std::vector<TodoChange> parseQueue(const std::string& text) {
  std::vector<TodoChange> out;
  forEachLine(text, [&](const std::string& line) {
    const auto f = splitTabs(line, 2);
    if (f.size() != 2 || !isTodoId(f[0]) || (f[1] != "done" && f[1] != "open")) return;
    out.push_back({f[0], f[1] == "done"});
  });
  return out;
}

std::string serializeQueue(const std::vector<TodoChange>& queue) {
  std::string out;
  for (const TodoChange& c : queue) out += c.id + (c.done ? "\tdone\n" : "\topen\n");
  return out;
}

bool toggleTodo(TodoList& list, std::vector<TodoChange>& queue, const std::string& id) {
  auto item = std::find_if(list.items.begin(), list.items.end(), [&](const TodoItem& i) { return i.id == id; });
  if (item == list.items.end()) return false;
  item->done = !item->done;
  auto pending = std::find_if(queue.begin(), queue.end(), [&](const TodoChange& c) { return c.id == id; });
  if (pending != queue.end()) {
    queue.erase(pending);  // back to what the server has: nothing to send
  } else {
    queue.push_back({id, item->done});
  }
  return true;
}

void applyQueue(TodoList& list, std::vector<TodoChange>& queue) {
  queue.erase(std::remove_if(queue.begin(), queue.end(),
                             [&](const TodoChange& c) {
                               auto item = std::find_if(list.items.begin(), list.items.end(),
                                                        [&](const TodoItem& i) { return i.id == c.id; });
                               if (item == list.items.end()) return true;
                               item->done = c.done;
                               return false;
                             }),
              queue.end());
}

SendOutcome classifySend(const int httpStatus, const std::string& body, const TodoChange& change) {
  if (httpStatus == 200) {
    const std::string expected = change.id + (change.done ? "\tdone" : "\topen");
    return body.rfind(expected, 0) == 0 ? SendOutcome::Sent : SendOutcome::Stale;
  }
  if (httpStatus == 404 && body.rfind("not_on_todays_list", 0) == 0) return SendOutcome::Stale;
  if (httpStatus == 409) return SendOutcome::Stale;
  return SendOutcome::Stop;
}

std::string todoActionPath(const TodoChange& change) {
  return "/display/todos/" + percentEncodeSegment(change.id) + (change.done ? "/done" : "/undo");
}

std::string shortGermanDate(const std::string& iso) {
  if (!isIsoDate(iso)) return {};
  return iso.substr(8, 2) + "." + iso.substr(5, 2) + ".";
}

}  // namespace deskleaf::feed
