#pragma once

// Snapshots (plan/02 A7): named restore points of a whole project, each a copy kept inside
// the project in .sparxel/snapshots, with a picture of the scene when it was taken. Taking
// one is quick, and restoring one first takes another, so nothing is ever lost.
//
// See docs/dev/editor.md (Snapshots).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

struct Snapshot {
    std::string id;   ///< Its folder's name: "2026-11-14 05-12-03 before the boss".
    std::string name; ///< As given: "before the boss".
    std::string when; ///< "2026-11-14 05:12".
    std::filesystem::path folder;
    std::filesystem::path picture; ///< Its PNG, or empty if it has none.
};

/// Where a project's snapshots are kept.
[[nodiscard]] std::filesystem::path snapshots_folder(const std::filesystem::path& project);

/// Copies the project (all but its snapshots) as a snapshot named `name`, with `picture`
/// (PNG bytes; may be empty). Returns it; its id is empty, with `error`, if it failed.
Snapshot take_snapshot(const std::filesystem::path& project, std::string_view name,
                       const std::vector<std::uint8_t>& picture, std::string& error);
/// The project's snapshots, the newest first.
[[nodiscard]] std::vector<Snapshot> list_snapshots(const std::filesystem::path& project);
/// Puts the project back as snapshot `id` was, after taking a snapshot of it as it is now
/// ("Before restoring …"). False, with `error`, changing nothing, if it cannot.
bool restore_snapshot(const std::filesystem::path& project, std::string_view id,
                      std::string& error);
/// Deletes a snapshot. False, with `error`, if it cannot.
bool delete_snapshot(const std::filesystem::path& project, std::string_view id, std::string& error);

} // namespace editor
