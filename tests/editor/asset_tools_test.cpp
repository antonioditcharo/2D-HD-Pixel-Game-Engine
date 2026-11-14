// The asset browser's tools: what uses an asset, and renaming one without breaking it.

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "editor/core/asset_tools.h"

namespace fs = std::filesystem;
using engine::Guid;

namespace {

struct Sample {
    Sample() {
        path = fs::temp_directory_path() / ("sparxel-assets-" + Guid::generate().to_string());
        fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
    }
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;
    Sample(Sample&&) = delete;
    Sample& operator=(Sample&&) = delete;
    ~Sample() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Find usages: a picture by its GUID, a scene by its path, with the entities") {
    const Sample s;
    const std::vector<editor::AssetUse> hero = editor::find_asset_usages(s.path, "art/hero.png");
    // Both scenes have the hero; sorted by file.
    REQUIRE(hero.size() == 2);
    CHECK(hero[0].file == "maps/road.dscene");
    CHECK(hero[1].file == "maps/village.dscene");
    CHECK(hero[1].where == std::vector<std::string>{"hero"});

    // The road scene: named in the village's logic, by its path.
    const std::vector<editor::AssetUse> road =
        editor::find_asset_usages(s.path, "maps/road.dscene");
    REQUIRE_FALSE(road.empty());
    CHECK(std::ranges::any_of(road, [](const editor::AssetUse& u) {
        return u.file.ends_with(".drules") && u.count >= 1;
    }));
    // The start scene is named in the project's settings.
    const std::vector<editor::AssetUse> village =
        editor::find_asset_usages(s.path, "maps/village.dscene");
    CHECK(std::ranges::any_of(
        village, [](const editor::AssetUse& u) { return u.file == "project.sparxel.json"; }));
    CHECK(editor::find_asset_usages(s.path, "sfx/missing.wav").empty());
}

TEST_CASE("Renaming: a picture with its companions, and a scene with every reference") {
    const Sample s;
    const fs::path assets = s.path / "assets";
    // A sprite sheet beside the hero's picture moves with it.
    std::ofstream(assets / "art" / "hero.sheet") << R"({"frame_width": 16})";
    CHECK(editor::asset_group(s.path, "art/hero.png") ==
          std::vector<std::string>{"art/hero.png", "art/hero.sheet"});
    const std::string guid = read_all(assets / "art" / "hero.png.meta");

    std::vector<std::string> changed;
    std::string error;
    REQUIRE(editor::rename_asset(s.path, "art/hero.png", "knight", changed, error));
    CHECK(fs::exists(assets / "art" / "knight.png"));
    CHECK(fs::exists(assets / "art" / "knight.sheet"));
    CHECK_FALSE(fs::exists(assets / "art" / "hero.png"));
    // The GUID went with it, so the scene, which names it by GUID, did not change.
    CHECK(read_all(assets / "art" / "knight.png.meta") == guid);
    CHECK(changed.empty());
    CHECK(editor::find_asset_usages(s.path, "art/knight.png").size() == 2);

    // A scene, named by its path in logic and the settings: every reference follows.
    changed.clear();
    REQUIRE(editor::rename_asset(s.path, "maps/village.dscene", "town", changed, error));
    CHECK(fs::exists(assets / "maps" / "town.dscene"));
    CHECK(fs::exists(assets / "maps" / "town.dscene.meta"));
    CHECK(std::ranges::find(changed, "project.sparxel.json") != changed.end());
    CHECK(read_all(s.path / "project.sparxel.json").find("\"maps/town.dscene\"") !=
          std::string::npos);
    CHECK(editor::find_asset_usages(s.path, "maps/village.dscene").empty());
    CHECK_FALSE(editor::find_asset_usages(s.path, "maps/town.dscene").empty());

    // Refused: a taken name, an unusable one, the same one, a missing asset, a table the
    // game reads by name.
    CHECK_FALSE(editor::rename_asset(s.path, "maps/town.dscene", "road", changed, error));
    CHECK(error.find("already") != std::string::npos);
    CHECK_FALSE(editor::rename_asset(s.path, "maps/town.dscene", "a/b", changed, error));
    CHECK_FALSE(editor::rename_asset(s.path, "maps/town.dscene", "town", changed, error));
    CHECK_FALSE(editor::rename_asset(s.path, "maps/nowhere.dscene", "x", changed, error));
    fs::create_directories(assets / "data");
    std::ofstream(assets / "data" / "party.dtable") << "{}";
    CHECK_FALSE(editor::rename_asset(s.path, "data/party.dtable", "heroes", changed, error));
    CHECK(fs::exists(assets / "maps" / "town.dscene"));
}

TEST_CASE("Renaming a table rewrites references with and without its extension") {
    const Sample s;
    const fs::path assets = s.path / "assets";
    fs::create_directories(assets / "data");
    std::ofstream(assets / "data" / "loot.dtable") << "{}";
    std::ofstream(assets / "logic" / "loot.drules")
        << R"({"table": "data/loot", "file": "data/loot.dtable", "other": "data/looted"})";
    std::vector<std::string> changed;
    std::string error;
    REQUIRE(editor::rename_asset(s.path, "data/loot.dtable", "treasure", changed, error));
    CHECK(changed == std::vector<std::string>{"logic/loot.drules"});
    CHECK(read_all(assets / "logic" / "loot.drules") ==
          R"({"table": "data/treasure", "file": "data/treasure.dtable", "other": "data/looted"})");
}
