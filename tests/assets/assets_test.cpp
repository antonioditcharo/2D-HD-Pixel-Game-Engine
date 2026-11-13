#include "engine/assets/aseprite.h"
#include "engine/assets/assets.h"
#include "engine/assets/autotile.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
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

TEST_CASE("Auto-tiles: 47 shapes from the neighbours, each put together from quarter tiles") {
    using namespace engine::assets;
    CHECK(autotile_shape(0) == 0);
    CHECK(autotile_shape(255) == 46);
    // A corner counts only with both of its sides.
    CHECK(autotile_shape(north_east) == autotile_shape(0));
    CHECK(autotile_shape(north | east | north_east) != autotile_shape(north | east));
    std::array<bool, 47> used{};
    for (unsigned m = 0; m < 256; ++m) {
        const std::uint32_t s = autotile_shape(static_cast<std::uint8_t>(m));
        REQUIRE(s < 47);
        used.at(s) = true;
    }
    CHECK(std::all_of(used.begin(), used.end(), [](bool u) { return u; }));

    // A block of 4-pixel tiles whose every quarter (2 x 2 pixels) is coloured by where it
    // is: red = its column (0 to 3), green = its row (0 to 5).
    Image block;
    block.width = 8;
    block.height = 12;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            block.rgba.insert(block.rgba.end(), {static_cast<std::uint8_t>(x / 2),
                                                 static_cast<std::uint8_t>(y / 2), 0, 255});
        }
    }
    std::string error;
    const std::optional<Image> out = expand_autotiles(block, 4, error);
    REQUIRE(out.has_value());
    if (!out) {
        return;
    }
    CHECK(out->width == 32);  // 8 tiles
    CHECK(out->height == 24); // 6 rows
    // The quarter of shape s at quarter q (0 NW, 1 NE, 2 SW, 3 SE): the block's (column, row).
    const auto quarter = [&](std::uint32_t s, std::uint32_t q) {
        const std::uint32_t x = ((s % 8) * 4) + ((q % 2) * 2);
        const std::uint32_t y = ((s / 8) * 4) + ((q / 2) * 2);
        const std::size_t at = ((std::size_t{y} * out->width) + x) * 4;
        return std::pair{out->rgba[at], out->rgba[at + 1]};
    };
    using Q = std::pair<std::uint8_t, std::uint8_t>;
    // Alone: the four outer corners of the patch.
    CHECK(quarter(autotile_shape(0), 0) == Q{0, 2});
    CHECK(quarter(autotile_shape(0), 1) == Q{3, 2});
    CHECK(quarter(autotile_shape(0), 2) == Q{0, 5});
    CHECK(quarter(autotile_shape(0), 3) == Q{3, 5});
    // All round: the middle of the patch.
    CHECK(quarter(46, 0) == Q{2, 4});
    CHECK(quarter(46, 3) == Q{1, 3});
    // Terrain to the north and west but not north-west: a corner cut in, top left.
    CHECK(quarter(autotile_shape(north | west), 0) == Q{2, 0});
    // Terrain to the west only: the top-left quarter is the top edge.
    CHECK(quarter(autotile_shape(west), 0) == Q{2, 2});
    // Terrain to the north only: the top-left quarter is the left edge.
    CHECK(quarter(autotile_shape(north), 0) == Q{0, 4});
    // The preview, last, as drawn.
    CHECK(quarter(47, 0) == Q{0, 0});
    CHECK(quarter(47, 3) == Q{1, 1});
    // Odd tiles, or a picture smaller than a block, are refused.
    CHECK_FALSE(expand_autotiles(block, 3, error).has_value());
    CHECK_FALSE(expand_autotiles(block, 8, error).has_value());
}

namespace {

/// Builds Aseprite files for tests, chunk by chunk, as Aseprite lays them out.
struct AseWriter {
    std::vector<std::uint8_t> out;
    void u8(std::uint32_t v) { out.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint32_t v) {
        u8(v & 0xFFU);
        u8((v >> 8U) & 0xFFU);
    }
    void u32(std::uint32_t v) {
        u16(v & 0xFFFFU);
        u16(v >> 16U);
    }
    void text(const std::string& s) {
        u16(static_cast<std::uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void put_u32(std::size_t at, std::uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) {
            out[at + i] = static_cast<std::uint8_t>(v >> (8U * i));
        }
    }
};

/// zlib with one stored (uncompressed) block, as any inflater reads it.
std::vector<std::uint8_t> zlib_stored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> z{0x78, 0x01, 0x01};
    const auto n = static_cast<std::uint16_t>(data.size());
    const auto inverse = static_cast<std::uint16_t>(~n);
    z.push_back(static_cast<std::uint8_t>(n & 0xFFU));
    z.push_back(static_cast<std::uint8_t>(n >> 8U));
    z.push_back(static_cast<std::uint8_t>(inverse & 0xFFU));
    z.push_back(static_cast<std::uint8_t>(inverse >> 8U));
    z.insert(z.end(), data.begin(), data.end());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t c : data) {
        a = (a + c) % 65521U;
        b = (b + a) % 65521U;
    }
    const std::uint32_t adler = (b << 16U) | a;
    for (unsigned shift = 24;; shift -= 8) {
        z.push_back(static_cast<std::uint8_t>(adler >> shift));
        if (shift == 0) {
            break;
        }
    }
    return z;
}

std::vector<std::uint8_t> solid(std::uint32_t count, std::array<std::uint8_t, 4> colour) {
    std::vector<std::uint8_t> p;
    for (std::uint32_t i = 0; i < count; ++i) {
        p.insert(p.end(), colour.begin(), colour.end());
    }
    return p;
}

} // namespace

TEST_CASE("Aseprite files: layers, opacity, compressed and linked cels, durations and tags") {
    AseWriter w;
    // Header: 2 frames of 4 x 2 RGBA; layer opacity valid.
    w.u32(0);
    w.u16(0xA5E0);
    w.u16(2);
    w.u16(4);
    w.u16(2);
    w.u16(32);
    w.u32(1);
    while (w.out.size() < 128) {
        w.u8(0);
    }
    const auto chunk = [&w](std::uint32_t type, const std::function<void()>& body) {
        const std::size_t start = w.out.size();
        w.u32(0);
        w.u16(type);
        body();
        w.put_u32(start, static_cast<std::uint32_t>(w.out.size() - start));
    };
    const auto frame = [&w](std::uint32_t duration, std::uint32_t chunks,
                            const std::function<void()>& body) {
        const std::size_t start = w.out.size();
        w.u32(0);
        w.u16(0xF1FA);
        w.u16(chunks);
        w.u16(duration);
        w.u16(0);
        w.u32(chunks);
        body();
        w.put_u32(start, static_cast<std::uint32_t>(w.out.size() - start));
    };
    const auto layer = [&](bool visible, std::uint32_t opacity, const char* name) {
        chunk(0x2004, [&] {
            w.u16(visible ? 1 : 0);
            for (int i = 0; i < 5; ++i) {
                w.u16(0); // a normal layer, child level, default size, normal blending
            }
            w.u8(opacity);
            w.u8(0);
            w.u8(0);
            w.u8(0);
            w.text(name);
        });
    };
    const auto cel_head = [&w](std::uint32_t layer_index, std::uint32_t x, std::uint32_t kind) {
        w.u16(layer_index);
        w.u16(x);
        w.u16(0);
        w.u8(255);
        w.u16(kind);
        w.u16(0);
        for (int i = 0; i < 5; ++i) {
            w.u8(0);
        }
    };
    const auto raw = [&](std::uint32_t layer_index, std::array<std::uint8_t, 4> colour) {
        chunk(0x2005, [&] {
            cel_head(layer_index, 0, 0);
            w.u16(4);
            w.u16(2);
            const std::vector<std::uint8_t> p = solid(8, colour);
            w.out.insert(w.out.end(), p.begin(), p.end());
        });
    };
    frame(100, 7, [&] {
        layer(true, 255, "background");
        layer(false, 255, "hidden");
        layer(true, 128, "top");
        raw(0, {255, 0, 0, 255}); // red everywhere
        raw(1, {0, 0, 255, 255}); // blue, on the hidden layer: never seen
        chunk(0x2005, [&] {       // green, 2 x 1 at (1, 0), compressed, at half opacity
            cel_head(2, 1, 2);
            w.u16(2);
            w.u16(1);
            const std::vector<std::uint8_t> z = zlib_stored(solid(2, {0, 255, 0, 255}));
            w.out.insert(w.out.end(), z.begin(), z.end());
        });
        chunk(0x2018, [&] {
            w.u16(2);
            for (int i = 0; i < 8; ++i) {
                w.u8(0);
            }
            const auto tag = [&w](std::uint32_t from, std::uint32_t to, std::uint32_t direction,
                                  const char* name) {
                w.u16(from);
                w.u16(to);
                w.u8(direction);
                for (int i = 0; i < 12; ++i) {
                    w.u8(0);
                }
                w.text(name);
            };
            tag(0, 1, 0, "walk");
            tag(1, 1, 2, "rest");
        });
    });
    frame(150, 1, [&] {
        chunk(0x2005, [&] { // the background, linked to the first frame's
            cel_head(0, 0, 1);
            w.u16(0);
        });
    });
    w.put_u32(0, static_cast<std::uint32_t>(w.out.size()));

    std::string error;
    const std::optional<engine::assets::AsepriteSheet> sheet =
        engine::assets::read_aseprite(w.out, error);
    INFO(error);
    REQUIRE(sheet.has_value());
    if (!sheet) {
        return;
    }
    CHECK(sheet->frame_width == 4);
    CHECK(sheet->frame_height == 2);
    CHECK(sheet->sheet.width == 8);
    CHECK(sheet->durations_ms == std::vector<std::uint32_t>{100, 150});
    REQUIRE(sheet->tags.size() == 2);
    CHECK(sheet->tags[0].name == "walk");
    CHECK(sheet->tags[1].direction == 2);
    const auto px = [&sheet](std::uint32_t x, std::uint32_t y) {
        const std::size_t at = ((std::size_t{y} * sheet->sheet.width) + x) * 4;
        return std::array<int, 4>{sheet->sheet.rgba[at], sheet->sheet.rgba[at + 1],
                                  sheet->sheet.rgba[at + 2], sheet->sheet.rgba[at + 3]};
    };
    CHECK(px(0, 0) == std::array<int, 4>{255, 0, 0, 255}); // the hidden blue is not drawn
    const std::array<int, 4> mixed = px(1, 0);             // green at half over red
    CHECK(mixed[0] == 127);
    CHECK(mixed[1] == 128);
    CHECK(mixed[3] == 255);
    CHECK(px(1, 1) == std::array<int, 4>{255, 0, 0, 255});
    CHECK(px(5, 0) == std::array<int, 4>{255, 0, 0, 255}); // second frame: linked, no top
    const std::vector<std::uint8_t> junk(200, 7);
    CHECK_FALSE(engine::assets::read_aseprite(junk, error).has_value());
    CHECK(error == "This is not an Aseprite file.");
}
