// Snapshots: a whole project kept as a restore point, listed, put back and deleted.

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "editor/core/snapshots.h"

namespace fs = std::filesystem;
using engine::Guid;

namespace {

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_all(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("Snapshots: taken with a picture, listed newest first, restored and deleted") {
    const fs::path project =
        fs::temp_directory_path() / ("sparxel-snapshots-" + Guid::generate().to_string());
    write_all(project / "project.sparxel", "{}");
    write_all(project / "maps" / "village.dscene", "first");
    write_all(project / ".sparxel" / "recovery.json", "kept");

    std::string error;
    const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G'};
    const editor::Snapshot first = editor::take_snapshot(project, "before the boss", png, error);
    REQUIRE_FALSE(first.id.empty());
    CHECK(first.name == "before the boss");
    CHECK(first.id.ends_with("before the boss"));
    CHECK(read_all(first.folder / "project" / "maps" / "village.dscene") == "first");
    // Its own folder is not copied into itself.
    CHECK_FALSE(fs::exists(first.folder / "project" / ".sparxel"));
    CHECK(fs::file_size(first.picture) == png.size());

    // A name with slashes is kept as given, but made safe for its folder.
    const editor::Snapshot second = editor::take_snapshot(project, "a/b:c", {}, error);
    REQUIRE_FALSE(second.id.empty());
    CHECK(second.id.find('/') == std::string::npos);
    CHECK(second.id.find(':') == std::string::npos);
    CHECK(second.id != first.id);
    CHECK(second.picture.empty());

    std::vector<editor::Snapshot> all = editor::list_snapshots(project);
    REQUIRE(all.size() == 2);
    CHECK(all[0].id >= all[1].id); // the newest first
    CHECK((all[0].name == "a/b:c" || all[1].name == "a/b:c"));

    // Work goes on; a new file and a changed one.
    write_all(project / "maps" / "village.dscene", "second");
    write_all(project / "maps" / "cave.dscene", "new");
    REQUIRE(editor::restore_snapshot(project, first.id, error));
    CHECK(read_all(project / "maps" / "village.dscene") == "first");
    CHECK_FALSE(fs::exists(project / "maps" / "cave.dscene"));
    CHECK(read_all(project / ".sparxel" / "recovery.json") == "kept");
    // The project as it was is a snapshot too, so the restore can be undone.
    all = editor::list_snapshots(project);
    REQUIRE(all.size() == 3);
    const auto before = std::find_if(all.begin(), all.end(), [](const editor::Snapshot& s) {
        return s.name == "Before restoring";
    });
    REQUIRE(before != all.end());
    CHECK(read_all(before->folder / "project" / "maps" / "cave.dscene") == "new");

    // Nothing outside the snapshots folder can be named.
    CHECK_FALSE(editor::restore_snapshot(project, "../maps", error));
    CHECK_FALSE(editor::restore_snapshot(project, "missing", error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(editor::delete_snapshot(project, "", error));
    REQUIRE(editor::delete_snapshot(project, second.id, error));
    CHECK(editor::list_snapshots(project).size() == 2);

    std::error_code ignored;
    fs::remove_all(project, ignored);
}
