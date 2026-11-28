#pragma once
// Pure parts of the display feed beyond the calendar: the worker's inbox and
// the daily to-do list. No IO, host-tested in test/deskleaf_calendar.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deskleaf::feed {

// --- Inbox (GET /display/inbox) -------------------------------------------------

struct InboxEntry {
  std::string id;    // base36 arrival time, "-", file name (may hold spaces and UTF-8)
  std::string kind;  // sleep | book | note | file
  size_t bytes = 0;
  std::string name;
};

// One entry per "<id>\t<kind>\t<bytes>\t<name>\n" line; malformed lines are skipped.
std::vector<InboxEntry> parseInboxListing(const std::string& text);

// A URL path segment: everything but A-Z a-z 0-9 - . _ ~ as %XX of its UTF-8 bytes.
std::string percentEncodeSegment(const std::string& segment);

// A name usable on the SD card: path separators and control characters replaced.
std::string sdSafeName(const std::string& name);

// Where a fetched file goes: a BMP becomes /sleep.bmp, notes /Spick/<id> (the id
// keeps the arrival order), books /Books/<name>, anything else /Inbox/<name>.
std::string inboxDestination(const InboxEntry& entry);

// --- Daily to-do list (GET /display/todos) --------------------------------------

enum class TodoBucket : uint8_t { Overdue, Today, ThisWeek };

struct TodoItem {
  std::string id;   // 6 characters [0-9a-z]
  bool done = false;
  std::string due;  // YYYY-MM-DD or ""
  TodoBucket bucket = TodoBucket::Today;
  std::string text;
};

struct TodoList {
  std::string date;  // the day the server built the list for, YYYY-MM-DD
  std::vector<TodoItem> items;
};

// "#<date>\t<count>" then one "<id>\t<open|done>\t<due>\t<bucket>\t<text>" per line.
// False when the header is missing or malformed; bad item lines are skipped.
bool parseTodos(const std::string& text, TodoList& out);

// A tap the server has not seen yet: the state the item should end in.
struct TodoChange {
  std::string id;
  bool done = false;
};

// Stored queue: one "<id>\t<done|open>" per line, oldest first.
std::vector<TodoChange> parseQueue(const std::string& text);
std::string serializeQueue(const std::vector<TodoChange>& queue);

// A tap on `id`: flips it in `list` and records the new state. Tapping again
// before the first change went out cancels it instead of queueing a second one.
// False when the id is not on the list.
bool toggleTodo(TodoList& list, std::vector<TodoChange>& queue, const std::string& id);

// Shows unsent changes over a freshly fetched list; changes for ids no longer on
// it are dropped from the queue (the server would refuse them anyway).
void applyQueue(TodoList& list, std::vector<TodoChange>& queue);

// After `change` went out (sent) or was refused as stale, against the queue as
// it is now: taps made during the send may have changed it. A matching entry
// is dropped. A missing one means the item was tapped back meanwhile, so a sent
// change needs its reverse queued.
void settleChange(std::vector<TodoChange>& queue, const TodoChange& change, bool sent);

// A Wi-Fi visit for the to-dos is due: taps wait (retry after 15 min) or the
// stored list is not today's (retry after 3 h). Times in epoch seconds.
bool todoVisitDue(bool tapsWaiting, bool listIsToday, int64_t lastAttempt, int64_t now);

// What one GET .../done or .../undo answer means for the queue.
enum class SendOutcome : uint8_t {
  Sent,   // 200 with the expected "<id>\t<state>": drop the change
  Stale,  // 404 not_on_todays_list or 409 changed: drop it and fetch the list again
  Stop,   // anything else (no network, 401, server error): keep it, try later
};
SendOutcome classifySend(int httpStatus, const std::string& body, const TodoChange& change);

// The request path for a change, relative to the base URL.
std::string todoActionPath(const TodoChange& change);

// "2026-11-28" -> "28.11.", "" for anything else.
std::string shortGermanDate(const std::string& iso);

}  // namespace deskleaf::feed
