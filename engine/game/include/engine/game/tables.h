#pragma once

// Data tables (plan/02 T1-T5): the game's numbers and names kept as tables, as RPG
// databases and spreadsheets keep them: items, enemies, skills, levels. Each column has a
// kind (text, whole number, number, yes/no) or a formula worked out from the row's other
// cells ("level * level * 10"); each row has a name the game finds it by. Logic reads any
// cell ("The attack of slime in enemies"). Tables go to and from CSV for spreadsheets.
//
// See docs/dev/tables.md.

#include "engine/assets/assets.h"
#include "engine/foundation/reflect.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

enum class ColumnKind : std::uint8_t { Text, Whole, Number, YesNo, Formula };

struct TableColumn {
    std::string id; ///< Unique in its table: "attack", "max_hp".
    ColumnKind kind = ColumnKind::Number;
    std::string formula; ///< For a formula: "level * level * 10", "attack + defense / 2".
    double min = -1e9;   ///< Numbers are kept within these.
    double max = 1e9;

    friend bool operator==(const TableColumn&, const TableColumn&) = default;
};

struct TableRow {
    std::string id; ///< Unique in its table: "slime", "potion", "3".
    /// By column id, as text ("12", "yes", "Potion"); formula columns are not stored.
    std::map<std::string, std::string, std::less<>> cells;

    friend bool operator==(const TableRow&, const TableRow&) = default;
};

struct Table {
    std::vector<TableColumn> columns;
    std::vector<TableRow> rows;

    [[nodiscard]] const TableColumn* column(std::string_view id) const noexcept;
    [[nodiscard]] const TableRow* row(std::string_view id) const noexcept;
    /// A cell's value as a number: a whole number, a number, yes (1) and no (0), or a
    /// formula worked out. Nothing for text that is not a number, an unknown row or column,
    /// or a formula that does not work out.
    [[nodiscard]] std::optional<double> number(std::string_view row, std::string_view column) const;
    /// A cell as text, formulas worked out ("12", "Potion", "yes").
    [[nodiscard]] std::optional<std::string> text(std::string_view row,
                                                  std::string_view column) const;

    friend bool operator==(const Table&, const Table&) = default;
};

/// The ".dtable" format name and version.
inline constexpr std::string_view table_format = "sparxel.table";
inline constexpr int table_version = 1;
/// The most rows and columns a table has.
inline constexpr std::size_t most_table_rows = 10000;
inline constexpr std::size_t most_table_columns = 64;

[[nodiscard]] std::optional<Table> parse_table(std::string_view text, std::string& error);
[[nodiscard]] std::string table_to_text(const Table& table);

/// The table as CSV: a header of "id" and the column ids, then a line per row (formulas
/// worked out). Commas, quotes and line breaks in cells are quoted.
[[nodiscard]] std::string table_to_csv(const Table& table);
/// Reads CSV made by a spreadsheet into `table`: the header names columns (the first is the
/// row's name); columns the table lacks are added as text, or as numbers when every cell is
/// one; rows are added or replaced by name. False, with `error`, for CSV that cannot be
/// read, or no header.
bool table_from_csv(Table& table, std::string_view csv, std::string& error);

/// Works out `formula` for `row` of `table`: numbers, + - * / ^ and brackets, min(a, b),
/// max(a, b), floor(a), round(a), and the row's other columns by id (and "row", the row's
/// place counting from 1). Nothing, with `error`, for one that does not work out.
[[nodiscard]] std::optional<double> evaluate(std::string_view formula, const Table& table,
                                             const TableRow& row, std::string& error);

/// What is wrong with a table, in plain language: two rows or columns with one id, a cell
/// that is not of its column's kind, a formula that does not work out. Empty if nothing.
[[nodiscard]] std::vector<std::string> check_table(const Table& table);

/// The "tables" object logic reads the project's tables through, by path in assets/
/// ("data/enemies.dtable"), each read on first use.
class TablesObject {
public:
    TablesObject() = default;
    explicit TablesObject(const assets::AssetDatabase* assets) : assets_(assets) {}

    [[nodiscard]] double number_of(const std::string& table, const std::string& row,
                                   const std::string& column) const;
    [[nodiscard]] std::string text_of(const std::string& table, const std::string& row,
                                      const std::string& column) const;
    [[nodiscard]] bool has_row(const std::string& table, const std::string& row) const;
    [[nodiscard]] std::int64_t row_count(const std::string& table) const;

    /// The table at `path` (".dtable" may be left off), or null.
    const Table* find(std::string_view path) const;
    /// Forgets the tables read, so changed files are read again.
    void forget() noexcept { tables_.clear(); }

private:
    const assets::AssetDatabase* assets_ = nullptr;
    /// Read on first use: reading changes nothing the game sees.
    mutable std::map<std::string, std::optional<Table>, std::less<>> tables_;
};

/// Registers TablesObject as "tables".
void register_table_types(reflect::Registry& registry);

} // namespace engine::game
