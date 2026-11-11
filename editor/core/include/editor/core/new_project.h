#pragma once

// Making a new project: a folder with a project file and a small starter game to change,
// so the first thing a creator sees is something that plays.
//
// See docs/dev/editor.md.

#include <filesystem>
#include <string>
#include <string_view>

namespace editor {

/// Makes a new project named `name` in `folder`, which must not exist yet or be empty:
/// the project file; a tileset (grass, path, stone) and a hero, drawn here, in
/// assets/art; and a start scene, assets/maps/start.dscene, with a ground of grass
/// crossed by a path, stone walls around it that stop the hero, and the hero in the
/// middle, moved with the arrow keys. Every asset gets its ".meta". Returns false, with
/// `error` in plain language, if it cannot; nothing already there is overwritten.
bool create_project(const std::filesystem::path& folder, std::string_view name, std::string& error);

} // namespace editor
