#pragma once

// Localization (plan/02 G7): the words a game shows, in other languages. Translations are a
// data table, data/translations (tables.h): a row per text, named by a key made from it
// ("t_1a2b3c4d"), its words as written in the `source` column, and a column per language
// ("fr", "de", "ja") with its translation. Translators fill it in the editor's Table window,
// or in a spreadsheet through CSV. The game shows each text in the language chosen, and the
// text as written where a translation is missing.
//
// See docs/dev/localization.md.

#include "engine/game/tables.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// The table translations are kept in.
inline constexpr std::string_view translations_table = "data/translations";
/// The language that shows every text with its vowels doubled, in brackets ("[Heelloo!]"),
/// to find words that are not translated and room that is too tight.
inline constexpr std::string_view pseudo_language = "pseudo";

/// The key a text's row is named by: "t_" and 8 hex digits made from the words.
[[nodiscard]] std::string translation_key(std::string_view text);
/// `text` in the pseudo language: "[Heelloo!]", keeping [tags] and {names} as they are.
[[nodiscard]] std::string pseudo_localize(std::string_view text);

class Translations {
public:
    Translations() = default;
    /// From a translations table: its `source` column, and each other column but `notes`
    /// as a language.
    explicit Translations(const Table* table);

    /// The languages the table has columns for, in its order.
    [[nodiscard]] const std::vector<std::string>& languages() const noexcept { return languages_; }
    /// The language texts are shown in: one of languages(), pseudo_language, or empty for
    /// the words as written.
    void set_language(std::string language) { language_ = std::move(language); }
    [[nodiscard]] const std::string& language() const noexcept { return language_; }

    /// `text` in the language chosen; as written if there is no translation for it.
    [[nodiscard]] std::string translate(std::string_view text) const;
    /// The same for answers separated by "|", each translated on its own.
    [[nodiscard]] std::string translate_answers(std::string_view answers) const;

private:
    std::vector<std::string> languages_;
    std::string language_;
    /// By source text, then by language.
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> by_text_;
};

/// Adds a row for each of `texts` that the translations table lacks (by its key), with its
/// words in `source`, and a column for each of `languages` it lacks. Rows already there keep
/// their translations. Returns how many rows were added.
std::size_t add_translation_rows(Table& table, const std::vector<std::string>& texts,
                                 const std::vector<std::string>& languages);

} // namespace engine::game
