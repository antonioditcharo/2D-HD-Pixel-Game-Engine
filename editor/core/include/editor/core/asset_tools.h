#pragma once

// The asset browser's tools (plan/02 A4): what uses an asset, and renaming one safely.
//
// Assets are referred to two ways: pictures and sounds by the GUID in their ".meta" file
// (which stays the same when they move), and scenes, conversations, tables and logic by
// their path ("maps/road.dscene", "data/party"). Renaming moves an asset with its ".meta"
// and its companions (a picture's sprite sheet and Sprite Studio source), and rewrites every
// reference by path, so nothing stops working.
//
// See docs/dev/editor.md (The Assets panel).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

/// One file that uses an asset.
struct AssetUse {
    std::string file;               ///< Relative to assets/, or "project.sparxel.json".
    std::vector<std::string> where; ///< In a scene or prefab: the entities that use it.
    std::size_t count = 0;          ///< How many times it is named there.
};

/// The files that use the asset at `asset` (relative to the project's assets/ folder), by
/// its GUID or its path, sorted by file. Its own files are left out.
[[nodiscard]] std::vector<AssetUse> find_asset_usages(const std::filesystem::path& project,
                                                      std::string_view asset);

/// The files a rename of `asset` moves together: itself, and for a picture or sprite, the
/// files beside it with the same name (its .png, .sheet, .dsprite, .aseprite), each with its
/// .meta. Relative to assets/.
[[nodiscard]] std::vector<std::string> asset_group(const std::filesystem::path& project,
                                                   std::string_view asset);

/// Renames `asset` (relative to assets/) to `name` (no folder, no extension) in the same
/// folder, with its group, and rewrites every reference to the moved files by path in the
/// project's scenes, logic, conversations, prefabs, tables and settings. `changed` gets the
/// files rewritten. False, with `error` and nothing changed, if the name is not usable, is
/// taken, or is one of the tables the game itself reads (data/party, ...).
bool rename_asset(const std::filesystem::path& project, std::string_view asset,
                  std::string_view name, std::vector<std::string>& changed, std::string& error);

} // namespace editor
