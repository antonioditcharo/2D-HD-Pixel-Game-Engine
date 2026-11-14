#include "editor/core/table_edit.h"

#include "engine/game/save.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;

namespace {

bool valid_name(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}

game::TableRow* row_of(game::Table& t, std::string_view id) {
    const auto found = std::find_if(t.rows.begin(), t.rows.end(),
                                    [&](const game::TableRow& r) { return r.id == id; });
    return found == t.rows.end() ? nullptr : &*found;
}

} // namespace

game::Table TableEditor::blank() {
    game::Table t;
    t.columns.push_back({"name", game::ColumnKind::Text});
    t.rows.push_back({"first", {}});
    return t;
}

std::optional<game::Table> TableEditor::load(const fs::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "The table cannot be opened.";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return game::parse_table(text, error);
}

bool TableEditor::save(const game::Table& table, const fs::path& file, std::string& error) {
    return game::write_file_atomically(file, game::table_to_text(table), error);
}

bool TableEditor::set_cell(std::string_view row, std::string_view column, std::string_view text,
                           std::string& error) {
    const game::TableColumn* c = table().column(column);
    if (table().row(row) == nullptr || c == nullptr) {
        error = "There is no such cell.";
        return false;
    }
    if (c->kind == game::ColumnKind::Formula) {
        error = "\"" + c->id + "\" is worked out by its formula: change the formula instead.";
        return false;
    }
    return history_.apply(
        "Change " + std::string(column) + " of " + std::string(row),
        [&](game::Table& t) {
            game::TableRow* r = row_of(t, row);
            if (text.empty()) {
                r->cells.erase(std::string(column));
            } else {
                r->cells[std::string(column)] = std::string(text);
            }
            return true;
        },
        "cell:" + std::string(row) + "/" + std::string(column));
}

std::string TableEditor::add_row(std::string_view after) {
    std::string id;
    for (int n = static_cast<int>(table().rows.size()) + 1;; ++n) {
        id = "row_" + std::to_string(n);
        if (table().row(id) == nullptr) {
            break;
        }
    }
    if (table().rows.size() >= game::most_table_rows) {
        return {};
    }
    history_.apply("Add " + id, [&](game::Table& t) {
        const auto at = std::find_if(t.rows.begin(), t.rows.end(),
                                     [&](const game::TableRow& r) { return r.id == after; });
        t.rows.insert(at == t.rows.end() ? t.rows.end() : at + 1, game::TableRow{id, {}});
        return true;
    });
    return id;
}

bool TableEditor::remove_row(std::string_view row) {
    return history_.apply("Delete " + std::string(row), [&](game::Table& t) {
        return std::erase_if(t.rows, [&](const game::TableRow& r) { return r.id == row; }) > 0;
    });
}

bool TableEditor::rename_row(std::string_view row, std::string_view name, std::string& error) {
    if (name.empty()) {
        error = "A row needs a name.";
        return false;
    }
    if (row != name && table().row(name) != nullptr) {
        error = "There is a row called \"" + std::string(name) + "\" already.";
        return false;
    }
    return history_.apply("Rename " + std::string(row), [&](game::Table& t) {
        game::TableRow* r = row_of(t, row);
        if (r == nullptr) {
            return false;
        }
        r->id = std::string(name);
        return true;
    });
}

bool TableEditor::move_row(std::string_view row, bool up) {
    return history_.apply("Move " + std::string(row), [&](game::Table& t) {
        const auto at = std::find_if(t.rows.begin(), t.rows.end(),
                                     [&](const game::TableRow& r) { return r.id == row; });
        if (at == t.rows.end() || (up ? at == t.rows.begin() : at + 1 == t.rows.end())) {
            return false;
        }
        std::iter_swap(at, up ? at - 1 : at + 1);
        return true;
    });
}

bool TableEditor::add_column(const game::TableColumn& column, std::string& error) {
    if (!valid_name(column.id) || column.id == "id") {
        error = "A column's name is letters, digits and _ only (not \"id\"), such as max_hp.";
        return false;
    }
    if (table().column(column.id) != nullptr) {
        error = "There is a column called \"" + column.id + "\" already.";
        return false;
    }
    if (table().columns.size() >= game::most_table_columns) {
        error = "A table has at most 64 columns.";
        return false;
    }
    return history_.apply("Add column " + column.id, [&](game::Table& t) {
        t.columns.push_back(column);
        return true;
    });
}

bool TableEditor::set_column(std::string_view column, const game::TableColumn& value,
                             std::string& error) {
    if (table().column(column) == nullptr) {
        error = "There is no such column.";
        return false;
    }
    if (value.id != column &&
        (!valid_name(value.id) || value.id == "id" || table().column(value.id) != nullptr)) {
        error = "A column's name is letters, digits and _ only, and not one taken.";
        return false;
    }
    game::Table next = table();
    for (game::TableColumn& c : next.columns) {
        if (c.id == column) {
            c = value;
        }
    }
    for (game::TableRow& r : next.rows) {
        if (value.id != column) {
            if (auto node = r.cells.extract(std::string(column)); !node.empty()) {
                node.key() = value.id;
                r.cells.insert(std::move(node));
            }
        }
        if (value.kind == game::ColumnKind::Formula) {
            std::string formula_error;
            if (!game::evaluate(value.formula, next, r, formula_error)) {
                error = formula_error;
                return false;
            }
        }
    }
    return history_.apply(
        "Change column " + std::string(column),
        [&](game::Table& t) {
            t = next;
            return true;
        },
        "column:" + value.id);
}

bool TableEditor::remove_column(std::string_view column) {
    return history_.apply("Delete column " + std::string(column), [&](game::Table& t) {
        const auto removed =
            std::erase_if(t.columns, [&](const game::TableColumn& c) { return c.id == column; });
        for (game::TableRow& r : t.rows) {
            r.cells.erase(std::string(column));
        }
        return removed > 0;
    });
}

bool TableEditor::import_csv(std::string_view csv, std::string& error) {
    return history_.apply("Import from CSV",
                          [&](game::Table& t) { return game::table_from_csv(t, csv, error); });
}

} // namespace editor
