#include "engine/game/localization.h"

#include <array>
#include <cstdint>
#include <utility>

namespace engine::game {

std::string translation_key(std::string_view text) {
    // FNV-1a: short, and the same everywhere and every time.
    std::uint32_t hash = 2166136261U;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619U;
    }
    constexpr std::array<char, 16> hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string key = "t_";
    for (int shift = 28; shift >= 0; shift -= 4) {
        key += hex.at((hash >> static_cast<unsigned>(shift)) & 0xFU);
    }
    return key;
}

std::string pseudo_localize(std::string_view text) {
    // In the letters every font has: each vowel twice, so texts grow as other languages
    // make them, inside brackets that show where each text starts and ends.
    std::string out = "[";
    char inside = 0; // ']' inside a [tag], '}' inside a {name}
    for (const char c : text) {
        if (inside != 0) {
            out += c;
            if (c == inside) {
                inside = 0;
            }
            continue;
        }
        if (c == '[' || c == '{') {
            inside = c == '[' ? ']' : '}';
        }
        out += c;
        if (inside == 0 && std::string_view("aeiouAEIOU").find(c) != std::string_view::npos) {
            out += c;
        }
    }
    out += "]";
    return out;
}

Translations::Translations(const Table* table) {
    if (table == nullptr || table->column("source") == nullptr) {
        return;
    }
    for (const TableColumn& column : table->columns) {
        if (column.id != "source" && column.id != "notes") {
            languages_.push_back(column.id);
        }
    }
    for (const TableRow& row : table->rows) {
        const auto source = row.cells.find("source");
        if (source == row.cells.end() || source->second.empty()) {
            continue;
        }
        auto& translated = by_text_[source->second];
        for (const std::string& language : languages_) {
            if (const auto cell = row.cells.find(language);
                cell != row.cells.end() && !cell->second.empty()) {
                translated[language] = cell->second;
            }
        }
    }
}

std::string Translations::translate(std::string_view text) const {
    if (language_.empty() || text.empty()) {
        return std::string(text);
    }
    if (language_ == pseudo_language) {
        return pseudo_localize(text);
    }
    if (const auto found = by_text_.find(text); found != by_text_.end()) {
        if (const auto words = found->second.find(language_); words != found->second.end()) {
            return words->second;
        }
    }
    return std::string(text);
}

std::string Translations::translate_answers(std::string_view answers) const {
    std::string out;
    std::size_t start = 0;
    while (start <= answers.size()) {
        const std::size_t bar = answers.find('|', start);
        const std::string_view one = answers.substr(
            start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        // Spaces around each answer are dropped, as the dialogue does.
        const std::size_t a = one.find_first_not_of(' ');
        const std::string_view trimmed = a == std::string_view::npos
                                             ? std::string_view{}
                                             : one.substr(a, one.find_last_not_of(' ') - a + 1);
        out += (start == 0 ? "" : "|") + translate(trimmed);
        if (bar == std::string_view::npos) {
            break;
        }
        start = bar + 1;
    }
    return out;
}

std::size_t add_translation_rows(Table& table, const std::vector<std::string>& texts,
                                 const std::vector<std::string>& languages) {
    if (table.column("source") == nullptr) {
        table.columns.insert(table.columns.begin(), TableColumn{"source", ColumnKind::Text});
    }
    for (const std::string& language : languages) {
        if (!language.empty() && table.column(language) == nullptr &&
            table.columns.size() < most_table_columns) {
            table.columns.push_back(TableColumn{language, ColumnKind::Text});
        }
    }
    std::size_t added = 0;
    for (const std::string& text : texts) {
        if (text.empty() || table.rows.size() >= most_table_rows) {
            continue;
        }
        const std::string key = translation_key(text);
        if (table.row(key) != nullptr) {
            continue;
        }
        table.rows.push_back(TableRow{key, {{"source", text}}});
        ++added;
    }
    return added;
}

} // namespace engine::game
