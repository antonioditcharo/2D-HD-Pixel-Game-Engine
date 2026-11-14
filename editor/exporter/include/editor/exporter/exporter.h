#pragma once

// Exporting a game (plan/03 §9): checks the project, then packages it with the player into
// a folder that plays the game when its executable is opened. No compiler is involved:
// the player is prebuilt, so exporting is copying.
//
//   <output>/<Game Name>.exe        the player, named after the game
//   <output>/*.dll                  the libraries beside the player (SDL3 on Windows)
//   <output>/LICENSE.txt            the engine's MIT licence and the notices of the
//   <output>/THIRD_PARTY_NOTICES.md libraries in the player, which every copy must carry
//   <output>/game/                  project.sparxel.json and assets/
//
// See docs/dev/export.md.

#include "engine/serialize/json.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

struct ExportOptions {
    std::filesystem::path project; ///< The project folder.
    /// The player executable to ship. The libraries beside it, and its licence files
    /// (LICENSE.txt and THIRD_PARTY_NOTICES.md, which the build puts there), go with it.
    std::filesystem::path player;
    /// A folder that does not exist yet, or is empty; not inside the project.
    std::filesystem::path output;
};

struct ExportReport {
    std::filesystem::path executable;         ///< The game's executable, in `output`.
    std::vector<std::filesystem::path> files; ///< Everything written, relative to `output`.
};

/// The executable's name for a game called `game_name`, without an extension: the name
/// with characters Windows does not allow in file names removed, and "game" if nothing
/// usable is left or the name is reserved (CON, NUL, COM1, ...).
[[nodiscard]] std::string executable_name(std::string_view game_name);

/// Checks the project by opening it as the player would (any issue, even an unknown
/// setting, stops the export), then writes the game to `options.output`. Returns nothing,
/// with issues in plain language, if the project has problems or anything cannot be
/// written; a failed export removes what it wrote.
[[nodiscard]] std::optional<ExportReport>
export_game(const ExportOptions& options, std::vector<engine::serialize::Issue>& issues);

} // namespace editor
