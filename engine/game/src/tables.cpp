#include "engine/game/tables.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>
#include <utility>

namespace engine::game {

namespace {

using serialize::Json;

constexpr std::array<std::string_view, 5> kind_ids{"text", "whole", "number", "yes_no", "formula"};

/// How deep formulas may read other formula columns, so two that read each other stop.
constexpr int deepest = 16;

std::optional<double> read_number(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (text == "yes" || text == "true") {
        return 1.0;
    }
    if (text == "no" || text == "false") {
        return 0.0;
    }
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::string number_text(double value) {
    if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e15) {
        return std::to_string(static_cast<std::int64_t>(std::llround(value)));
    }
    std::ostringstream out;
    out << value;
    return out.str();
}

/// A small recursive-descent reader of formulas.
class Formula {
public:
    Formula(std::string_view text, const Table& table, const TableRow& row, int depth)
        : text_(text), table_(table), row_(row), depth_(depth) {}

    std::optional<double> run(std::string& error) {
        const std::optional<double> value = sum();
        skip();
        if (!value || at_ != text_.size()) {
            error = error_.empty() ? "The formula \"" + std::string(text_) + "\" cannot be read."
                                   : error_;
            return std::nullopt;
        }
        return value;
    }

private:
    void skip() {
        while (at_ < text_.size() && text_[at_] == ' ') {
            ++at_;
        }
    }
    bool take(char c) {
        skip();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    std::optional<double> sum() {
        std::optional<double> left = product();
        while (left) {
            if (take('+')) {
                const auto right = product();
                left = right ? std::optional<double>(*left + *right) : std::nullopt;
            } else if (take('-')) {
                const auto right = product();
                left = right ? std::optional<double>(*left - *right) : std::nullopt;
            } else {
                break;
            }
        }
        return left;
    }
    std::optional<double> product() {
        std::optional<double> left = power();
        while (left) {
            if (take('*')) {
                const auto right = power();
                left = right ? std::optional<double>(*left * *right) : std::nullopt;
            } else if (take('/')) {
                const auto right = power();
                if (right && *right == 0.0) {
                    error_ = "The formula divides by 0.";
                    return std::nullopt;
                }
                left = right ? std::optional<double>(*left / *right) : std::nullopt;
            } else {
                break;
            }
        }
        return left;
    }
    std::optional<double> power() {
        const std::optional<double> base = unary();
        if (base && take('^')) {
            const auto exponent = power();
            if (!exponent) {
                return std::nullopt;
            }
            const double v = std::pow(*base, *exponent);
            return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
        }
        return base;
    }
    std::optional<double> unary() {
        if (take('-')) {
            const auto v = unary();
            return v ? std::optional<double>(-*v) : std::nullopt;
        }
        return atom();
    }
    std::optional<double> atom() {
        skip();
        if (take('(')) {
            const auto v = sum();
            return v && take(')') ? v : std::nullopt;
        }
        if (at_ >= text_.size()) {
            return std::nullopt;
        }
        const char c = text_[at_];
        if ((c >= '0' && c <= '9') || c == '.') {
            const std::size_t start = at_;
            while (at_ < text_.size() &&
                   ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.')) {
                ++at_;
            }
            return read_number(text_.substr(start, at_ - start));
        }
        const std::size_t start = at_;
        while (at_ < text_.size() &&
               ((text_[at_] >= 'a' && text_[at_] <= 'z') ||
                (text_[at_] >= 'A' && text_[at_] <= 'Z') ||
                (text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '_')) {
            ++at_;
        }
        const std::string_view name = text_.substr(start, at_ - start);
        if (name.empty()) {
            return std::nullopt;
        }
        if (take('(')) {
            // A function of one or two numbers.
            const auto a = sum();
            std::optional<double> b;
            if (take(',')) {
                b = sum();
            }
            if (!a || !take(')')) {
                return std::nullopt;
            }
            if (name == "min" && b) {
                return std::min(*a, *b);
            }
            if (name == "max" && b) {
                return std::max(*a, *b);
            }
            if (name == "floor" && !b) {
                return std::floor(*a);
            }
            if (name == "round" && !b) {
                return std::round(*a);
            }
            error_ = "The formula uses " + std::string(name) +
                     "(), which is not one of min, max, floor and round.";
            return std::nullopt;
        }
        if (name == "row") {
            for (std::size_t i = 0; i < table_.rows.size(); ++i) {
                if (&table_.rows[i] == &row_) {
                    return static_cast<double>(i + 1);
                }
            }
            return std::nullopt;
        }
        const TableColumn* column = table_.column(name);
        if (column == nullptr) {
            error_ = "The formula reads \"" + std::string(name) + "\", which is not a column.";
            return std::nullopt;
        }
        if (column->kind == ColumnKind::Formula) {
            if (depth_ >= deepest) {
                error_ = "The formulas read each other round in a ring.";
                return std::nullopt;
            }
            Formula inner(column->formula, table_, row_, depth_ + 1);
            std::string ignored;
            const std::optional<double> v = inner.run(ignored);
            if (!v) {
                error_ = inner.error_.empty() ? ignored : inner.error_;
            }
            return v;
        }
        const auto cell = row_.cells.find(name);
        return cell == row_.cells.end() ? std::optional<double>(0.0) : read_number(cell->second);
    }

    std::string_view text_;
    const Table& table_;
    const TableRow& row_;
    int depth_ = 0;
    std::size_t at_ = 0;
    std::string error_;
};

std::string csv_cell(const std::string& text) {
    if (text.find_first_of(",\"\n\r") == std::string::npos) {
        return text;
    }
    std::string out = "\"";
    for (const char c : text) {
        out += c;
        if (c == '"') {
            out += '"';
        }
    }
    return out + "\"";
}

/// Rows of cells from CSV text, quotes understood. Nothing if a quote is never closed.
std::optional<std::vector<std::vector<std::string>>> read_csv(std::string_view csv) {
    std::vector<std::vector<std::string>> rows(1);
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < csv.size(); ++i) {
        const char c = csv[i];
        if (quoted) {
            if (c == '"' && i + 1 < csv.size() && csv[i + 1] == '"') {
                cell += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cell += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            rows.back().push_back(std::move(cell));
            cell.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < csv.size() && csv[i + 1] == '\n') {
                ++i;
            }
            rows.back().push_back(std::move(cell));
            cell.clear();
            rows.emplace_back();
        } else {
            cell += c;
        }
    }
    if (quoted) {
        return std::nullopt;
    }
    if (!cell.empty() || !rows.back().empty()) {
        rows.back().push_back(std::move(cell));
    }
    std::erase_if(rows, [](const std::vector<std::string>& r) {
        return r.empty() || (r.size() == 1 && r[0].empty());
    });
    return rows;
}

} // namespace

const TableColumn* Table::column(std::string_view id) const noexcept {
    for (const TableColumn& c : columns) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const TableRow* Table::row(std::string_view id) const noexcept {
    for (const TableRow& r : rows) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

std::optional<double> Table::number(std::string_view row_id, std::string_view column_id) const {
    const TableRow* r = row(row_id);
    const TableColumn* c = column(column_id);
    if (r == nullptr || c == nullptr) {
        return std::nullopt;
    }
    if (c->kind == ColumnKind::Formula) {
        std::string error;
        const std::optional<double> v = evaluate(c->formula, *this, *r, error);
        return v ? std::optional<double>(std::clamp(*v, c->min, c->max)) : std::nullopt;
    }
    const auto cell = r->cells.find(column_id);
    if (cell == r->cells.end()) {
        return c->kind == ColumnKind::Text ? std::nullopt : std::optional<double>(0.0);
    }
    const std::optional<double> v = read_number(cell->second);
    if (!v) {
        return std::nullopt;
    }
    return c->kind == ColumnKind::Text ? *v : std::clamp(*v, c->min, c->max);
}

std::optional<std::string> Table::text(std::string_view row_id, std::string_view column_id) const {
    const TableRow* r = row(row_id);
    const TableColumn* c = column(column_id);
    if (r == nullptr || c == nullptr) {
        return std::nullopt;
    }
    if (c->kind == ColumnKind::Formula) {
        const std::optional<double> v = number(row_id, column_id);
        return v ? std::optional<std::string>(number_text(*v)) : std::nullopt;
    }
    const auto cell = r->cells.find(column_id);
    return cell == r->cells.end() ? std::string() : cell->second;
}

std::optional<double> evaluate(std::string_view formula, const Table& table, const TableRow& row,
                               std::string& error) {
    Formula f(formula, table, row, 0);
    return f.run(error);
}

std::optional<Table> parse_table(std::string_view text, std::string& error) {
    std::vector<serialize::Issue> issues;
    const std::optional<Json> parsed = serialize::parse(text, issues);
    if (!parsed || !parsed->is_object() || parsed->value("format", std::string()) != table_format) {
        error = "This is not a table (\"sparxel.table\").";
        return std::nullopt;
    }
    const Json& json = *parsed;
    if (!json.contains("version") || !json["version"].is_number_integer() ||
        json["version"].get<int>() > table_version) {
        error = "This table was saved by a newer version of Sparxel.";
        return std::nullopt;
    }
    Table table;
    if (json.contains("columns") && json["columns"].is_array()) {
        for (const Json& c : json["columns"]) {
            if (!c.is_object() || table.columns.size() >= most_table_columns) {
                continue;
            }
            TableColumn column;
            column.id = c.value("id", std::string());
            const std::string kind = c.value("kind", std::string("number"));
            for (std::size_t k = 0; k < kind_ids.size(); ++k) {
                if (kind_ids.at(k) == kind) {
                    column.kind = static_cast<ColumnKind>(k);
                }
            }
            column.formula = c.value("formula", std::string());
            if (c.contains("min") && c["min"].is_number()) {
                column.min = c["min"].get<double>();
            }
            if (c.contains("max") && c["max"].is_number()) {
                column.max = c["max"].get<double>();
            }
            if (!column.id.empty() && column.id != "id") {
                table.columns.push_back(std::move(column));
            }
        }
    }
    if (json.contains("rows") && json["rows"].is_array()) {
        for (const Json& r : json["rows"]) {
            if (!r.is_object() || table.rows.size() >= most_table_rows) {
                continue;
            }
            TableRow row;
            row.id = r.value("id", std::string());
            for (const auto& [key, value] : r.items()) {
                if (key != "id" && value.is_string()) {
                    row.cells[key] = value.get<std::string>();
                }
            }
            if (!row.id.empty()) {
                table.rows.push_back(std::move(row));
            }
        }
    }
    return table;
}

std::string table_to_text(const Table& table) {
    Json json = Json::object();
    json["format"] = table_format;
    json["version"] = table_version;
    Json columns = Json::array();
    for (const TableColumn& c : table.columns) {
        Json column{{"id", c.id}, {"kind", kind_ids.at(static_cast<std::size_t>(c.kind))}};
        if (c.kind == ColumnKind::Formula) {
            column["formula"] = c.formula;
        }
        if (c.min > -1e9) {
            column["min"] = c.min;
        }
        if (c.max < 1e9) {
            column["max"] = c.max;
        }
        columns.push_back(std::move(column));
    }
    json["columns"] = std::move(columns);
    Json rows = Json::array();
    for (const TableRow& r : table.rows) {
        Json row{{"id", r.id}};
        for (const auto& [key, value] : r.cells) {
            if (table.column(key) != nullptr && table.column(key)->kind != ColumnKind::Formula) {
                row[key] = value;
            }
        }
        rows.push_back(std::move(row));
    }
    json["rows"] = std::move(rows);
    return serialize::to_text(json);
}

std::string table_to_csv(const Table& table) {
    std::string out = "id";
    for (const TableColumn& c : table.columns) {
        out += "," + csv_cell(c.id);
    }
    out += "\n";
    for (const TableRow& r : table.rows) {
        out += csv_cell(r.id);
        for (const TableColumn& c : table.columns) {
            out += "," + csv_cell(table.text(r.id, c.id).value_or(std::string()));
        }
        out += "\n";
    }
    return out;
}

bool table_from_csv(Table& table, std::string_view csv, std::string& error) {
    const std::optional<std::vector<std::vector<std::string>>> rows = read_csv(csv);
    if (!rows) {
        error = "The CSV has a quote that is never closed.";
        return false;
    }
    if (rows->empty() || rows->front().size() < 2) {
        error = "The CSV needs a first line naming the columns: the row's name, then the rest.";
        return false;
    }
    const std::vector<std::string>& header = rows->front();
    Table next = table;
    for (std::size_t c = 1; c < header.size(); ++c) {
        if (header[c].empty() || next.column(header[c]) != nullptr) {
            continue;
        }
        bool numbers = rows->size() > 1;
        for (std::size_t r = 1; r < rows->size(); ++r) {
            const auto& line = (*rows)[r];
            numbers = numbers && (c >= line.size() || line[c].empty() || read_number(line[c]));
        }
        if (next.columns.size() >= most_table_columns) {
            error = "A table has at most 64 columns.";
            return false;
        }
        next.columns.push_back(
            TableColumn{header[c], numbers ? ColumnKind::Number : ColumnKind::Text});
    }
    for (std::size_t r = 1; r < rows->size(); ++r) {
        const auto& line = (*rows)[r];
        if (line.empty() || line[0].empty()) {
            continue;
        }
        TableRow row;
        row.id = line[0];
        for (std::size_t c = 1; c < header.size() && c < line.size(); ++c) {
            const TableColumn* column = next.column(header[c]);
            if (column != nullptr && column->kind != ColumnKind::Formula && !line[c].empty()) {
                row.cells[header[c]] = line[c];
            }
        }
        const auto same = std::find_if(next.rows.begin(), next.rows.end(),
                                       [&](const TableRow& t) { return t.id == row.id; });
        if (same != next.rows.end()) {
            *same = std::move(row);
        } else if (next.rows.size() < most_table_rows) {
            next.rows.push_back(std::move(row));
        }
    }
    table = std::move(next);
    return true;
}

std::vector<std::string> check_table(const Table& table) {
    std::vector<std::string> problems;
    std::set<std::string, std::less<>> seen;
    for (const TableColumn& c : table.columns) {
        if (!seen.insert(c.id).second) {
            problems.push_back("Two columns are called \"" + c.id + "\".");
        }
    }
    seen.clear();
    for (const TableRow& r : table.rows) {
        if (!seen.insert(r.id).second) {
            problems.push_back("Two rows are called \"" + r.id + "\".");
        }
        for (const TableColumn& c : table.columns) {
            if (c.kind == ColumnKind::Formula) {
                std::string error;
                if (!evaluate(c.formula, table, r, error)) {
                    problems.push_back(r.id + ", " + c.id + ": " + error);
                }
                continue;
            }
            const auto cell = r.cells.find(c.id);
            if (cell == r.cells.end() || c.kind == ColumnKind::Text) {
                continue;
            }
            const std::optional<double> v = read_number(cell->second);
            const bool fits = v && (c.kind != ColumnKind::Whole || *v == std::floor(*v)) &&
                              (c.kind != ColumnKind::YesNo || *v == 0.0 || *v == 1.0);
            if (!fits) {
                problems.push_back(r.id + ", " + c.id + ": \"" + cell->second + "\" is not " +
                                   (c.kind == ColumnKind::Whole   ? "a whole number."
                                    : c.kind == ColumnKind::YesNo ? "yes or no."
                                                                  : "a number."));
            }
        }
    }
    return problems;
}

const Table* TablesObject::find(std::string_view path) const {
    std::string key(path);
    if (!key.ends_with(".dtable")) {
        key += ".dtable";
    }
    if (const auto known = tables_.find(key); known != tables_.end()) {
        if (const auto& found = known->second; found.has_value()) {
            return &*found;
        }
        return nullptr;
    }
    std::optional<Table> table;
    const assets::AssetRecord* record = assets_ != nullptr ? assets_->find(key) : nullptr;
    if (record != nullptr) {
        if (const auto bytes = assets_->read(record->guid)) {
            std::string error;
            table = parse_table(std::string_view(reinterpret_cast<const char*>(bytes->data()),
                                                 bytes->size()), // NOLINT
                                error);
        }
    }
    const auto [at, added] = tables_.emplace(key, std::move(table));
    static_cast<void>(added);
    if (const auto& made = at->second; made.has_value()) {
        return &*made;
    }
    return nullptr;
}

double TablesObject::number_of(const std::string& table, const std::string& row,
                               const std::string& column) const {
    const Table* t = find(table);
    return t != nullptr ? t->number(row, column).value_or(0.0) : 0.0;
}

std::string TablesObject::text_of(const std::string& table, const std::string& row,
                                  const std::string& column) const {
    const Table* t = find(table);
    return t != nullptr ? t->text(row, column).value_or(std::string()) : std::string();
}

bool TablesObject::has_row(const std::string& table, const std::string& row) const {
    const Table* t = find(table);
    return t != nullptr && t->row(row) != nullptr;
}

std::int64_t TablesObject::row_count(const std::string& table) const {
    const Table* t = find(table);
    return t != nullptr ? static_cast<std::int64_t>(t->rows.size()) : 0;
}

void register_table_types(reflect::Registry& registry) {
    auto tables =
        registry.add_type<TablesObject>("tables").display_name("Tables").category("Story").tooltip(
            "The game's data: items, enemies, skills, levels, as tables.");
    tables.getter<&TablesObject::number_of>("number_of", {"table", "row", "column"})
        .text("The {column} of {row} in {table}")
        .tooltip("A number from a table, such as the attack of slime in data/enemies; 0 if "
                 "there is none.");
    tables.getter<&TablesObject::text_of>("text_of", {"table", "row", "column"})
        .text("The text {column} of {row} in {table}")
        .tooltip("A cell of a table as text, such as an item's name.");
    tables.condition<&TablesObject::has_row>("has_row", {"table", "row"})
        .text("{table} has a row {row}")
        .tooltip("Whether a table has a row by that name.");
    tables.getter<&TablesObject::row_count>("row_count", {"table"})
        .text("The number of rows in {table}")
        .tooltip("How many rows a table has.");
}

} // namespace engine::game
