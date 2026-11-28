// Host tests for lib/DeskleafCalendar/DeskleafFeed: the display inbox and the
// daily to-do list. Formats as the Deskleaf worker sends them
// (deskleaf-for-ai: display-inbox.ts, display-todos.ts).
#include <DeskleafFeed.h>

#include <cstdio>
#include <string>

using namespace deskleaf::feed;

static int failures = 0;
#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                  \
    }                                                              \
  } while (0)

static void testInboxListing() {
  // The note that stayed on the server: spaces and a UTF-8 middle dot in the id.
  const std::string listing =
      "0mwzyipol-Heute \xC2\xB7 Sa 28 11 .md\tnote\t563\tHeute \xC2\xB7 Sa 28 11 .md\n"
      "0mwy2ps0g-Vorwerk Tag 2.epub\tbook\t8042\tVorwerk Tag 2.epub\n"
      "kaputt ohne tabs\n"
      "0mwz-x.bmp\tsleep\tzwölf\tx.bmp\n"
      "0mwz-a.bin\tfile\t3\ta\tb.bin\n";
  const auto entries = parseInboxListing(listing);
  CHECK(entries.size() == 3);
  CHECK(entries[0].id == "0mwzyipol-Heute \xC2\xB7 Sa 28 11 .md");
  CHECK(entries[0].kind == "note");
  CHECK(entries[0].bytes == 563);
  CHECK(entries[1].kind == "book");
  CHECK(entries[2].name == "a\tb.bin");  // a tab in the name stays in the last field

  CHECK(percentEncodeSegment(entries[0].id) == "0mwzyipol-Heute%20%C2%B7%20Sa%2028%2011%20.md");
  CHECK(percentEncodeSegment("a/b?c#d%e") == "a%2Fb%3Fc%23d%25e");
  CHECK(percentEncodeSegment("abc-._~XYZ019") == "abc-._~XYZ019");

  CHECK(inboxDestination(entries[0]) == "/Spick/0mwzyipol-Heute \xC2\xB7 Sa 28 11 .md");
  CHECK(inboxDestination(entries[1]) == "/Books/Vorwerk Tag 2.epub");
  CHECK(inboxDestination({"1-s.bmp", "sleep", 1, "s.bmp"}) == "/sleep.bmp");
  CHECK(inboxDestination({"1-x", "file", 1, "../x:y"}) == "/Inbox/-x-y");
  CHECK(sdSafeName("") == "datei");
  CHECK(sdSafeName("a\nb") == "ab");
}

static const char* kTodos =
    "#2026-11-28\t4\n"
    "a1b2c3\topen\t2026-11-25\toverdue\tSanit\xC3\xA4r anrufen\n"
    "d4e5f6\tdone\t\ttoday\tPaket abholen\n"
    "g7h8i9\topen\t2026-11-30\tthis_week\tRechnung schreiben\n"
    "BAD123\topen\t\ttoday\tkeine gültige id\n"
    "j0k1l2\topen\t\tsomeday\tunbekannte Gruppe\n";

static void testTodoParsing() {
  TodoList list;
  CHECK(parseTodos(kTodos, list));
  CHECK(list.date == "2026-11-28");
  CHECK(list.items.size() == 3);
  CHECK(list.items[0].bucket == TodoBucket::Overdue && !list.items[0].done && list.items[0].due == "2026-11-25");
  CHECK(list.items[1].done && list.items[1].due.empty() && list.items[1].bucket == TodoBucket::Today);
  CHECK(list.items[2].bucket == TodoBucket::ThisWeek && list.items[2].text == "Rechnung schreiben");

  CHECK(!parseTodos("", list));
  CHECK(!parseTodos("a1b2c3\topen\t\ttoday\tx\n", list));  // no header
  CHECK(!parseTodos("#28.11.2026\t1\n", list));
  CHECK(parseTodos("#2026-11-28\t0\n", list) && list.items.empty());

  CHECK(shortGermanDate("2026-11-28") == "28.11.");
  CHECK(shortGermanDate("") == "");
}

static void testTodoQueue() {
  TodoList list;
  parseTodos(kTodos, list);
  std::vector<TodoChange> queue;
  CHECK(toggleTodo(list, queue, "a1b2c3"));
  CHECK(list.items[0].done && queue.size() == 1 && queue[0].done);
  CHECK(toggleTodo(list, queue, "d4e5f6"));  // a done one reopened
  CHECK(!list.items[1].done && queue.size() == 2 && !queue[1].done);
  CHECK(toggleTodo(list, queue, "a1b2c3"));  // tapped back before sending: nothing to send
  CHECK(!list.items[0].done && queue.size() == 1 && queue[0].id == "d4e5f6");
  CHECK(!toggleTodo(list, queue, "zzzzzz"));

  // Stored and read back in order.
  queue.push_back({"g7h8i9", true});
  const std::string stored = serializeQueue(queue);
  CHECK(stored == "d4e5f6\topen\ng7h8i9\tdone\n");
  const auto back = parseQueue(stored + "garbage\nBAD\tdone\n");
  CHECK(back.size() == 2 && back[0].id == "d4e5f6" && !back[0].done && back[1].done);

  // A fresh list from the server shows the unsent taps; gone ids drop out.
  TodoList fresh;
  parseTodos("#2026-11-29\t2\nd4e5f6\tdone\t\ttoday\tPaket abholen\nm3n4o5\topen\t\ttoday\tNeu\n", fresh);
  auto pending = back;
  applyQueue(fresh, pending);
  CHECK(pending.size() == 1 && pending[0].id == "d4e5f6");
  CHECK(!fresh.items[0].done);
}

static void testSendOutcome() {
  const TodoChange done{"a1b2c3", true};
  const TodoChange undo{"a1b2c3", false};
  CHECK(classifySend(200, "a1b2c3\tdone\n", done) == SendOutcome::Sent);
  CHECK(classifySend(200, "a1b2c3\topen\n", undo) == SendOutcome::Sent);
  CHECK(classifySend(200, "a1b2c3\topen\n", done) == SendOutcome::Stale);
  CHECK(classifySend(404, "not_on_todays_list\n", done) == SendOutcome::Stale);
  CHECK(classifySend(409, "changed\n", done) == SendOutcome::Stale);
  CHECK(classifySend(404, "not_found\n", done) == SendOutcome::Stop);  // feature off on the server
  CHECK(classifySend(401, "unauthorized\n", done) == SendOutcome::Stop);
  CHECK(classifySend(0, "", done) == SendOutcome::Stop);
  CHECK(classifySend(500, "", done) == SendOutcome::Stop);
  CHECK(todoActionPath(done) == "/display/todos/a1b2c3/done");
  CHECK(todoActionPath(undo) == "/display/todos/a1b2c3/undo");
}

int runFeedTests() {
  testInboxListing();
  testTodoParsing();
  testTodoQueue();
  testSendOutcome();
  return failures;
}
