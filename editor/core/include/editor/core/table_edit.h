#pragma once

// The table editor's model (plan/02 T1-T5): a data table (engine/game/tables.h) being
// edited, with undo. No user interface: tested on its own.
//
// See docs/dev/tables.md.

#include "engine/game/tables.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "editor/core/history.h"

namespace editor {

class TableEditor {
public:
    explicit TableEditor(engine::game::Table table) : history_(std::move(table)) {}

    /// A new table: a "name" column of text and one row, "first".
    [[nodiscard]] static engine::game::Table blank();
    [[nodiscard]] static std::optional<engine::game::Table> load(const std::filesystem::path& file,
                                                                 std::string& error);
    static bool save(const engine::game::Table& table, const std::filesystem::path& file,
                     std::string& error);

    [[nodiscard]] const engine::game::Table& table() const noexcept { return history_.value(); }
    [[nodiscard]] History<engine::game::Table>& history() noexcept { return history_; }

    /// Writes `text` into a cell; typing into one cell merges. Refused, with `error`, for a
    /// formula column (it is worked out) or a row or column that is not there.
    bool set_cell(std::string_view row, std::string_view column, std::string_view text,
                  std::string& error);
    /// Adds a row after `after` (or at the end) named "row_N"; returns its name.
    std::string add_row(std::string_view after = {});
    bool remove_row(std::string_view row);
    /// Renames a row. Refused for an empty name or one taken.
    bool rename_row(std::string_view row, std::string_view name, std::string& error);
    /// Moves a row one place up or down.
    bool move_row(std::string_view row, bool up);
    /// Adds a column; refused for a name taken, "id", a name with characters other than
    /// letters, digits and _, or past 64 columns.
    bool add_column(const engine::game::TableColumn& column, std::string& error);
    /// Changes a column: its kind, formula, range, and name (cells follow a new name). A
    /// formula is checked on every row first.
    bool set_column(std::string_view column, const engine::game::TableColumn& value,
                    std::string& error);
    bool remove_column(std::string_view column);
    /// Reads CSV into the table (engine::game::table_from_csv()), as one step.
    bool import_csv(std::string_view csv, std::string& error);

private:
    History<engine::game::Table> history_;
};

} // namespace editor
