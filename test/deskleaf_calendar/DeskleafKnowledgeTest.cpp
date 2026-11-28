// Host tests for lib/DeskleafCalendar/DeskleafKnowledge: the worker's JSON
// split into per-event and per-note files, and read back.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DeskleafKnowledge.h"

using namespace deskleaf::knowledge;

namespace {
int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

const char* kJson = R"({"from":"2026-11-09","to":"2026-11-22","hash":"1a2b3c4d",
 "events":[{"key":"2026-11-12T10:00|Austausch","title":"Austausch Workshop","text":"# Austausch Workshop\n\n## Notizen\n- Atlas",
            "links":["Christoph Schiessl","DYNACISION"]},
           {"key":"","title":"ohne Schl\u00fcssel","text":"x","links":[]}],
 "notes":{"Christoph Schiessl":{"type":"person","title":"Christoph Schiessl","text":"# Christoph Schiessl\n\nCTO bei DYNACISION"},
          "DYNACISION":{"type":"kunde","title":"","text":"## Laufende Themen\n- Atlas"}}})";

const File* find(const std::vector<File>& files, const std::string& name) {
  for (const File& f : files) {
    if (f.name == name) return &f;
  }
  return nullptr;
}
}  // namespace

int runKnowledgeTests() {
  CHECK(eventKey("2026-11-12T10:00:00", "Austausch") == "2026-11-12T10:00|Austausch");
  CHECK(fileName('e', "a") == fileName('e', "a"));
  CHECK(fileName('e', "a") != fileName('n', "a"));
  CHECK(fileName('e', "a").size() == 14);  // "e-xxxxxxxx.txt"

  std::string hash;
  std::vector<File> files;
  CHECK(split(kJson, std::strlen(kJson), hash, files));
  CHECK(hash == "1a2b3c4d");
  CHECK(files.size() == 3);  // the event without a key is skipped

  Entry e;
  const File* ef = find(files, fileName('e', eventKey("2026-11-12T10:00:00", "Austausch")));
  CHECK(ef != nullptr);
  if (ef) {
    CHECK(parseEvent(ef->content, e));
    CHECK(e.title == "Austausch Workshop");
    CHECK(e.links.size() == 2 && e.links[0] == "Christoph Schiessl" && e.links[1] == "DYNACISION");
    CHECK(e.text == "## Notizen\n- Atlas");  // the "# " title is shown apart
  }
  Entry n;
  const File* nf = find(files, fileName('n', "DYNACISION"));
  CHECK(nf != nullptr);
  if (nf) {
    CHECK(parseNote(nf->content, n));
    CHECK(n.title == "DYNACISION");  // empty title: the name
    CHECK(n.type == "kunde");
    CHECK(typeLabel(n.type) == "Kunde");
    CHECK(n.text == "## Laufende Themen\n- Atlas");
  }
  // An event without links still has its (empty) link line.
  CHECK(parseEvent("Titel\n\nText", e) && e.links.empty() && e.text == "Text");
  CHECK(!parseEvent("", e));
  CHECK(!split("{", 1, hash, files));
  CHECK(!split("{\"events\":[]}", 13, hash, files));  // no hash
  return failures;
}
