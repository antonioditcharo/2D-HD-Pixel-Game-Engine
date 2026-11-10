#include "engine/assets/assets.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace assets = engine::assets;
namespace fs = std::filesystem;
using engine::Guid;

namespace {

/// A fresh folder that is deleted when the test ends.
struct TempFolder {
    TempFolder() {
        path = fs::temp_directory_path() / ("sparxel-assets-" + Guid::generate().to_string());
        fs::create_directories(path);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    fs::path path;
};

void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Guid guid_of(const assets::AssetDatabase& db, std::string_view path) {
    const assets::AssetRecord* record = db.find(path);
    REQUIRE(record != nullptr);
    return record == nullptr ? Guid{} : record->guid;
}

/// Sets a file's time well past its current one, so a change is seen even on file
/// systems with coarse timestamps.
void touch_later(const fs::path& file) {
    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
}

} // namespace

TEST_CASE("New assets get a .meta file with a GUID") {
    const TempFolder folder;
    write(folder.path / "sprites/hero.png", "not really a png");
    write(folder.path / "logic/intro.json", "{}");
    assets::AssetDatabase db(folder.path);

    const assets::AssetChanges first = db.refresh();
    CHECK(first.added.size() == 2);
    CHECK(db.issues().empty());
    const Guid hero = guid_of(db, "sprites/hero.png");
    CHECK(db.find(hero)->importer == "image");
    CHECK(db.find("logic/intro.json")->importer == "file");
    CHECK(read_text(folder.path / "sprites/hero.png.meta") ==
          "{\n  \"format\": \"sparxel.meta\",\n  \"guid\": \"" + hero.to_string() +
              "\",\n  \"importer\": \"image\",\n  \"version\": 1\n}\n");

    // Nothing changed: nothing is reported, and .meta files are not assets.
    CHECK(db.refresh().empty());
    CHECK(db.assets().size() == 2);
    CHECK(db.assets()[0].path == "logic/intro.json");

    // Another database on the same folder reads the same GUIDs.
    assets::AssetDatabase again(folder.path);
    CHECK(again.refresh().added.size() == 2);
    CHECK(guid_of(again, "sprites/hero.png") == hero);
}

TEST_CASE("Changed content is reported as modified") {
    const TempFolder folder;
    write(folder.path / "a.txt", "one");
    assets::AssetDatabase db(folder.path);
    db.refresh();
    const Guid a = guid_of(db, "a.txt");

    write(folder.path / "a.txt", "two"); // same size, different bytes
    touch_later(folder.path / "a.txt");
    const assets::AssetChanges changes = db.refresh();
    CHECK(changes.modified == std::vector<Guid>{a});
    CHECK(changes.added.empty());
    CHECK(changes.moved.empty());

    // Only the time changed: the content is hashed again and found unchanged.
    touch_later(folder.path / "a.txt");
    CHECK(db.refresh().empty());
}

TEST_CASE("Renames keep the GUID, with or without the .meta file") {
    const TempFolder folder;
    write(folder.path / "old/door.png", "door pixels");
    write(folder.path / "old/tree.png", "tree pixels");
    assets::AssetDatabase db(folder.path);
    db.refresh();
    const Guid door = guid_of(db, "old/door.png");
    const Guid tree = guid_of(db, "old/tree.png");

    // Moved together with its .meta, as the editor does.
    fs::create_directories(folder.path / "new");
    fs::rename(folder.path / "old/door.png", folder.path / "new/door.png");
    fs::rename(folder.path / "old/door.png.meta", folder.path / "new/door.png.meta");
    // Moved without its .meta, as a file manager might.
    fs::rename(folder.path / "old/tree.png", folder.path / "new/tree.png");

    const assets::AssetChanges changes = db.refresh();
    std::vector<Guid> expected{door, tree};
    std::sort(expected.begin(), expected.end());
    CHECK(changes.moved == expected);
    CHECK(changes.added.empty());
    CHECK(changes.removed.empty());
    CHECK(guid_of(db, "new/door.png") == door);
    CHECK(guid_of(db, "new/tree.png") == tree);
    // The stale .meta left behind was removed; the new one holds the old GUID.
    CHECK_FALSE(fs::exists(folder.path / "old/tree.png.meta"));
    CHECK(read_text(folder.path / "new/tree.png.meta").find(tree.to_string()) != std::string::npos);
    CHECK(db.issues().empty());
}

TEST_CASE("A copy made with its .meta gets a new GUID") {
    const TempFolder folder;
    write(folder.path / "hero.png", "hero");
    assets::AssetDatabase db(folder.path);
    db.refresh();
    const Guid hero = guid_of(db, "hero.png");

    fs::copy_file(folder.path / "hero.png", folder.path / "hero_copy.png");
    fs::copy_file(folder.path / "hero.png.meta", folder.path / "hero_copy.png.meta");
    const assets::AssetChanges changes = db.refresh();
    CHECK(guid_of(db, "hero.png") == hero);
    const Guid copy = guid_of(db, "hero_copy.png");
    CHECK(copy != hero);
    CHECK(changes.added == std::vector<Guid>{copy});
    REQUIRE(db.issues().size() == 1);
    CHECK(db.issues()[0].path == "hero_copy.png.meta");
    CHECK(db.issues()[0].message == "This .meta file had the same GUID as hero.png's, probably "
                                    "from a copy. It was given a new GUID.");
    CHECK(read_text(folder.path / "hero_copy.png.meta").find(copy.to_string()) !=
          std::string::npos);
}

TEST_CASE("A broken .meta file is reported and never overwritten") {
    const TempFolder folder;
    write(folder.path / "map.json", "{}");
    write(folder.path / "map.json.meta", "{ this is not json");
    assets::AssetDatabase db(folder.path);
    db.refresh();
    CHECK(db.find("map.json") == nullptr);
    REQUIRE(db.issues().size() == 1);
    CHECK(db.issues()[0].path == "map.json.meta");
    CHECK(
        db.issues()[0].message ==
        "The .meta file is not valid JSON, so the asset was skipped. Fix or delete the .meta file "
        "(deleting it gives the asset a new GUID).");
    CHECK(read_text(folder.path / "map.json.meta") == "{ this is not json");

    write(folder.path / "map.json.meta", R"({"format": "something.else", "guid": "x"})");
    db.refresh();
    REQUIRE(db.issues().size() == 1);
    CHECK(db.issues()[0].message ==
          "The .meta file does not hold a Sparxel GUID, so the asset was skipped.");
}

TEST_CASE("Removed assets are reported, and a .meta left alone is flagged") {
    const TempFolder folder;
    write(folder.path / "a.txt", "a");
    write(folder.path / "b.txt", "b");
    assets::AssetDatabase db(folder.path);
    db.refresh();
    const Guid a = guid_of(db, "a.txt");
    const Guid b = guid_of(db, "b.txt");

    fs::remove(folder.path / "a.txt");
    fs::remove(folder.path / "a.txt.meta");
    fs::remove(folder.path / "b.txt"); // its .meta stays behind
    const assets::AssetChanges changes = db.refresh();
    std::vector<Guid> expected{a, b};
    std::sort(expected.begin(), expected.end());
    CHECK(changes.removed == expected);
    REQUIRE(db.issues().size() == 1);
    CHECK(db.issues()[0].path == "b.txt.meta");
    CHECK(fs::exists(folder.path / "b.txt.meta"));
}

TEST_CASE("Hidden files and folders are ignored") {
    const TempFolder folder;
    write(folder.path / ".sparxel/cache.bin", "cache");
    write(folder.path / ".hidden.txt", "x");
    write(folder.path / "visible.txt", "y");
    assets::AssetDatabase db(folder.path);
    CHECK(db.refresh().added.size() == 1);
    CHECK_FALSE(fs::exists(folder.path / ".sparxel/cache.bin.meta"));
}

TEST_CASE("A missing assets folder is reported") {
    const TempFolder folder;
    assets::AssetDatabase db(folder.path / "missing");
    CHECK(db.refresh().empty());
    REQUIRE(db.issues().size() == 1);
    CHECK(db.issues()[0].message.find("does not exist") != std::string::npos);
}

TEST_CASE("PNG images round-trip exactly, and broken ones are refused") {
    assets::Image image;
    image.width = 3;
    image.height = 2;
    image.rgba = {255, 0,  0,  255, 0,   255, 0,   128, 0, 0, 255, 0, //
                  10,  20, 30, 40,  250, 240, 230, 255, 1, 2, 3,   4};
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    REQUIRE_FALSE(png.empty());
    CHECK(png[1] == 'P');
    CHECK(png[2] == 'N');
    CHECK(png[3] == 'G');
    std::string error;
    const std::optional<assets::Image> decoded = assets::decode_png(png, error);
    REQUIRE(decoded.has_value());
    const assets::Image result = decoded.value_or(assets::Image{});
    CHECK(result.width == 3);
    CHECK(result.height == 2);
    CHECK(result.rgba == image.rgba);

    const std::vector<std::uint8_t> junk{1, 2, 3, 4, 5};
    CHECK_FALSE(assets::decode_png(junk, error).has_value());
    CHECK(error.rfind("The image is not a valid PNG", 0) == 0);
    CHECK_FALSE(assets::decode_png({}, error).has_value());
    CHECK(error == "The image file is empty.");
}

TEST_CASE("An asset's bytes are read by GUID") {
    const TempFolder folder;
    assets::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {9, 8, 7, 255};
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    {
        std::ofstream out(folder.path / "dot.png", std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()),
                  static_cast<std::streamsize>(png.size()));
    }
    assets::AssetDatabase db(folder.path);
    db.refresh();
    const Guid dot = guid_of(db, "dot.png");
    const std::optional<std::vector<std::uint8_t>> bytes = db.read(dot);
    REQUIRE(bytes.has_value());
    const std::vector<std::uint8_t> content = bytes.value_or(std::vector<std::uint8_t>{});
    CHECK(db.find(dot)->content_hash == assets::content_hash(content));
    std::string error;
    const std::optional<assets::Image> decoded = assets::decode_png(content, error);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value_or(assets::Image{}).rgba == image.rgba);
    CHECK_FALSE(db.read(Guid::generate()).has_value());
}
