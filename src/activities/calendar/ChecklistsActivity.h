#pragma once
// Checklists for shopping, workshops, packing: one text file per list in
// /Checklisten on the SD card ("- [ ] item" / "- [x] item" per line, plain
// lines count as open), so the web file manager can edit them too.
// Overview: tap a list to open it, long press to rename (an empty name deletes
// it), the last row adds a list. In a list: tap an item to tick it, long press
// to edit (empty deletes), Confirm or "+ Eintrag" adds one; "Zurücksetzen"
// unticks all (a workshop list for the next run), "Erledigte löschen" drops the
// ticked ones (a shopping list after the shop). Left/Right page, Back goes up.
#include <string>
#include <vector>

#include "activities/Activity.h"

class ChecklistsActivity final : public Activity {
 public:
  ChecklistsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Checklists", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  struct Item {
    std::string text;
    bool done = false;
  };
  static std::vector<Item> parse(const std::string& text);
  static std::string serialize(const std::vector<Item>& items);

 private:
  enum class Edit : uint8_t { None, NewList, RenameList, NewItem, EditItem };

  void loadLists();
  void openList(int index);
  void saveList() const;
  void startKeyboard(Edit what, const std::string& title, const std::string& initial);
  void applyEdit(const std::string& text);
  int rowsPerPage() const;
  void handleOverviewTap(int x, int y, bool longPress);
  void handleListTap(int x, int y, bool longPress);

  // Overview
  std::vector<std::string> lists;  // names without ".txt"
  std::vector<std::pair<int, int>> counts;  // done, total per list
  // Open list (-1 = overview)
  int open = -1;
  std::vector<Item> items;
  int page = 0;

  // Keyboard round trip: what is being edited, and the answer once it is back.
  Edit edit = Edit::None;
  int editIndex = -1;
  bool answerPending = false;
  std::string answer;

  // Geometry set by render, for taps.
  int rowsTop = 0;
  int rowH = 0;
  int actionsTop = 0;
};
