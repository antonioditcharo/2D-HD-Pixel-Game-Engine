// The table editor's model: cells, rows and columns changed with undo, formulas checked.

#include "engine/game/tables.h"

#include <doctest/doctest.h>

#include <string>

#include "editor/core/table_edit.h"

namespace game = engine::game;

TEST_CASE("Tables: cells typed (merged), rows and columns added, renamed and undone") {
    editor::TableEditor t(editor::TableEditor::blank());
    std::string error;
    CHECK(t.set_cell("first", "name", "S", error));
    CHECK(t.set_cell("first", "name", "Slime", error));
    CHECK(t.history().undo_text() == "Change name of first");
    t.history().end_merge();
    CHECK(t.rename_row("first", "slime", error));
    CHECK_FALSE(t.rename_row("slime", "", error));
    const std::string bat = t.add_row("slime");
    CHECK(bat == "row_2");
    CHECK_FALSE(t.rename_row(bat, "slime", error));
    CHECK(error == "There is a row called \"slime\" already.");
    CHECK(t.rename_row(bat, "bat", error));
    CHECK(t.move_row("bat", true));
    CHECK(t.table().rows.front().id == "bat");
    CHECK_FALSE(t.move_row("bat", true));

    CHECK(t.add_column({"attack", game::ColumnKind::Whole}, error));
    CHECK_FALSE(t.add_column({"attack", game::ColumnKind::Whole}, error));
    CHECK_FALSE(t.add_column({"has space", game::ColumnKind::Whole}, error));
    CHECK_FALSE(t.add_column({"id", game::ColumnKind::Text}, error));
    CHECK(t.set_cell("slime", "attack", "4", error));
    CHECK(t.add_column({"double", game::ColumnKind::Formula, "attack * 2"}, error));
    CHECK(t.table().number("slime", "double") == 8.0);
    CHECK_FALSE(t.set_cell("slime", "double", "3", error));
    // A formula that does not work out is refused; renaming a column carries its cells.
    CHECK_FALSE(t.set_column("double", {"double", game::ColumnKind::Formula, "speed * 2"}, error));
    CHECK(error == "The formula reads \"speed\", which is not a column.");
    CHECK(t.set_column("attack", {"power", game::ColumnKind::Whole}, error));
    CHECK(t.table().number("slime", "power") == 4.0);
    CHECK(t.remove_column("double"));
    CHECK(t.import_csv("id,power\nghost,9\n", error));
    CHECK(t.table().number("ghost", "power") == 9.0);
    while (t.history().can_undo()) {
        t.history().undo();
    }
    CHECK(t.table() == editor::TableEditor::blank());
    CHECK_FALSE(t.history().modified());
}
