// End-to-end: PNG files in an assets folder, a scene of entities that refer to them by
// GUID, saved to JSON and loaded again, rendered to pixels. Needs a GPU (lavapipe is
// enough); see tests/render/render_test.cpp for how a missing GPU is handled.

#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/gpu.h"

namespace engine::render {
std::ostream& operator<<(std::ostream& out, Color c) {
    return out << "(" << int{c.r} << ", " << int{c.g} << ", " << int{c.b} << ", " << int{c.a}
               << ")";
}
} // namespace engine::render

namespace assets = engine::assets;
namespace render = engine::render;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace world = engine::world;
namespace fs = std::filesystem;
using engine::Guid;
using render::Color;

namespace {

using test_support::gpu;

struct TempFolder {
    TempFolder() {
        path = fs::temp_directory_path() / ("sparxel-scene2d-" + Guid::generate().to_string());
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

/// Writes a PNG of `width` x `height` pixels, given row by row.
void write_image(const fs::path& file, std::uint32_t width, std::uint32_t height,
                 const std::vector<Color>& colours) {
    REQUIRE(colours.size() == std::size_t{width} * height);
    assets::Image image;
    image.width = width;
    image.height = height;
    for (const Color c : colours) {
        image.rgba.insert(image.rgba.end(), {c.r, c.g, c.b, c.a});
    }
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    REQUIRE_FALSE(png.empty());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

/// Writes a solid-colour 2 x 2 PNG.
void write_png(const fs::path& file, Color c) {
    assets::Image image;
    image.width = 2;
    image.height = 2;
    for (int i = 0; i < 4; ++i) {
        image.rgba.insert(image.rgba.end(), {c.r, c.g, c.b, c.a});
    }
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    REQUIRE_FALSE(png.empty());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

Color pixel(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t x,
            std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * width + x) * 4;
    return Color{pixels.at(i), pixels.at(i + 1), pixels.at(i + 2), pixels.at(i + 3)};
}

struct Fixture {
    Fixture() {
        scene2d::register_types(registry);
        REQUIRE(registry.errors().empty());
        write_png(folder.path / "red.png", red);
        write_png(folder.path / "blue.png", blue);
        db.refresh();
        REQUIRE(db.issues().empty());
        red_image = db.find("red.png")->guid;
        blue_image = db.find("blue.png")->guid;
    }

    [[nodiscard]] std::unique_ptr<world::World> make_world() const {
        auto w = std::make_unique<world::World>(registry);
        REQUIRE(scene2d::register_components(*w));
        return w;
    }

    static world::Entity place(world::World& w, std::string_view name, double x, double y,
                               Guid image) {
        const world::Entity e = w.create(name);
        scene2d::Transform2D* t = w.add<scene2d::Transform2D>(e);
        t->x = x;
        t->y = y;
        w.add<scene2d::Sprite>(e)->image = image.to_string();
        return e;
    }

    const Color red{255, 0, 0, 255};
    const Color blue{0, 0, 255, 255};
    const Color background{10, 10, 10, 255};
    reflect::Registry registry;
    TempFolder folder;
    assets::AssetDatabase db{folder.path};
    Guid red_image;
    Guid blue_image;
};

} // namespace

TEST_CASE("A scene of sprites renders with pixel snapping, y-sorting and layers") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto w = f.make_world();
    // Camera: 2 pixels per unit, upscaled 2x, so one source pixel is 2 x 2 on screen. The
    // camera's position (0, 0) is the target's centre pixel (8, 8).
    const scene2d::Camera2D camera{0.0, 0.0, 2, 2};
    // Red at (0, 0), pivot bottom centre: its 2 x 2 image spans source pixels x -1..0 and
    // y -2..-1, so screen pixels x 6..9 and y 4..7.
    const world::Entity red = Fixture::place(*w, "Red", 0.0, 0.0, f.red_image);
    // Blue at (0.5, 0.5): screen x 8..11 and y 6..9. Lower on screen, so drawn on top.
    Fixture::place(*w, "Blue", 0.5, 0.5, f.blue_image);
    // A sprite whose image is not an asset is counted, not drawn.
    Fixture::place(*w, "Broken", 0.0, 0.0, Guid::generate());

    const auto target = device->create_render_target(16, 16);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);

    const scene2d::DrawStats stats =
        scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
    CHECK(stats.drawn == 2);
    CHECK(stats.missing == 1);
    CHECK(textures.size() == 2);
    std::vector<std::uint8_t> pixels = target->read_pixels();
    REQUIRE(pixels.size() == 16 * 16 * 4);
    CHECK(pixel(pixels, 16, 6, 4) == f.red);
    CHECK(pixel(pixels, 16, 7, 7) == f.red);
    CHECK(pixel(pixels, 16, 8, 6) == f.blue); // the overlap: blue is in front
    CHECK(pixel(pixels, 16, 11, 9) == f.blue);
    CHECK(pixel(pixels, 16, 5, 4) == f.background);
    CHECK(pixel(pixels, 16, 12, 9) == f.background);
    CHECK(pixel(pixels, 16, 6, 3) == f.background);

    // A higher layer wins over y.
    w->get<scene2d::Transform2D>(red)->layer = 1;
    scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
    pixels = target->read_pixels();
    CHECK(pixel(pixels, 16, 8, 6) == f.red);

    // Invisible sprites are not drawn.
    w->get<scene2d::Sprite>(red)->visible = false;
    CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background).drawn == 1);
}

TEST_CASE("A scene saved to JSON and loaded again renders the same pixels") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto original = f.make_world();
    Fixture::place(*original, "Red", -1.0, 1.0, f.red_image);
    const world::Entity blue = Fixture::place(*original, "Blue", 1.0, 2.0, f.blue_image);
    original->get<scene2d::Sprite>(blue)->tint_a = 128;

    const std::string text = serialize::to_text(serialize::scene_to_json(*original));
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = serialize::parse(text, issues);
    REQUIRE(json.has_value());
    const auto copy = f.make_world();
    const serialize::SceneLoad loaded =
        serialize::scene_from_json(json.value_or(serialize::Json{}), *copy);
    CHECK(loaded.issues.empty());
    CHECK(loaded.entities.size() == 2);

    const auto target = device->create_render_target(24, 24);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    const scene2d::Camera2D camera{0.0, 0.0, 4, 1};
    scene2d::draw_world(*original, textures, *sprites, *target, camera, f.background);
    const std::vector<std::uint8_t> expected = target->read_pixels();
    scene2d::draw_world(*copy, textures, *sprites, *target, camera, f.background);
    const std::vector<std::uint8_t> actual = target->read_pixels();
    REQUIRE_FALSE(expected.empty());
    CHECK(actual == expected);
    // And the scene is not just blank: both sprites are visible somewhere.
    int red_pixels = 0;
    int blended_pixels = 0;
    for (std::uint32_t y = 0; y < 24; ++y) {
        for (std::uint32_t x = 0; x < 24; ++x) {
            const Color c = pixel(actual, 24, x, y);
            red_pixels += c == f.red ? 1 : 0;
            blended_pixels += (c.b > 100 && c.b < 160) ? 1 : 0;
        }
    }
    CHECK(red_pixels == 4);
    CHECK(blended_pixels == 4);
}

TEST_CASE("Changing a PNG on disk hot-reloads it") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Fixture f;
    const auto w = f.make_world();
    Fixture::place(*w, "Blue", 0.0, 0.0, f.blue_image);
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    const scene2d::Camera2D camera{0.0, 0.0, 2, 1};
    scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
    CHECK(pixel(target->read_pixels(), 8, 3, 2) == f.blue);

    // The artist repaints the sprite green.
    const Color green{0, 255, 0, 255};
    write_png(f.folder.path / "blue.png", green);
    fs::last_write_time(f.folder.path / "blue.png",
                        fs::last_write_time(f.folder.path / "blue.png") + std::chrono::seconds(5));
    const assets::AssetChanges changes = f.db.refresh();
    CHECK(changes.modified == std::vector<Guid>{f.blue_image});
    textures.apply(changes);
    scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
    CHECK(pixel(target->read_pixels(), 8, 3, 2) == green);
}

TEST_CASE("The components are described for the editor") {
    reflect::Registry registry;
    scene2d::register_types(registry);
    CHECK(registry.errors().empty());
    const reflect::TypeInfo* sprite = registry.find_type<scene2d::Sprite>();
    REQUIRE(sprite != nullptr);
    CHECK(sprite->meta.display_name == "Sprite");
    CHECK(sprite->find_property("tint_a")->meta.display_name == "Opacity");
    const reflect::TypeInfo* transform = registry.find_type<scene2d::Transform2D>();
    REQUIRE(transform != nullptr);
    CHECK(transform->meta.display_name == "Position");
    CHECK(transform->find_property("x")->unit == reflect::Unit::Tiles);
    CHECK(sprite->find_property("frame_width")->unit == reflect::Unit::Pixels);
    const reflect::TypeInfo* animation = registry.find_type<scene2d::SpriteAnimation>();
    REQUIRE(animation != nullptr);
    CHECK(animation->meta.display_name == "Animation");
    CHECK(animation->find_function("play") != nullptr);
    CHECK(animation->find_event("finished") != nullptr);
    CHECK(reflect::has_flag(animation->find_property("elapsed")->flags,
                            reflect::property_flag::hidden));
    const reflect::TypeInfo* tilemap = registry.find_type<scene2d::Tilemap>();
    REQUIRE(tilemap != nullptr);
    CHECK(tilemap->meta.display_name == "Tilemap");
}

TEST_CASE("Tilemap text is parsed strictly and written back unchanged") {
    std::string error;
    const std::optional<scene2d::TileGrid> grid = scene2d::parse_tiles("0,1\n,2, 3\r\n-1", error);
    INFO(error);
    REQUIRE(grid.has_value());
    const scene2d::TileGrid g = grid.value_or(scene2d::TileGrid{});
    CHECK(g.width == 3);
    CHECK(g.height == 3);
    CHECK(g.cells == std::vector<std::int32_t>{0, 1, -1, -1, 2, 3, -1, -1, -1});
    CHECK(g.at(2, 1) == 3);
    CHECK(g.at(3, 0) == -1); // outside
    CHECK(scene2d::format_tiles(g) == "0,1,\n,2,3\n,,");
    CHECK(scene2d::parse_tiles(scene2d::format_tiles(g), error) == grid);

    // Empty text is an empty map; an all-empty map survives a round trip.
    CHECK(scene2d::parse_tiles("", error) == scene2d::TileGrid{});
    const scene2d::TileGrid blank{1, 2, {-1, -1}};
    CHECK(scene2d::parse_tiles(scene2d::format_tiles(blank), error) == blank);

    // Anything else is refused, with where it is.
    CHECK_FALSE(scene2d::parse_tiles("0,1\n2,x", error).has_value());
    CHECK(error ==
          "Row 2, column 2 is 'x', which is not a tile number (a whole number from -1 up).");
    CHECK_FALSE(scene2d::parse_tiles("-2", error).has_value());
    CHECK_FALSE(scene2d::parse_tiles("1.5", error).has_value());
    CHECK_FALSE(scene2d::parse_tiles("99999999999", error).has_value());
    CHECK_FALSE(
        scene2d::parse_tiles(std::string(scene2d::max_tilemap_side, ','), error).has_value());
    CHECK(error == "A tilemap can have at most 4096 columns.");
    CHECK_FALSE(
        scene2d::parse_tiles(std::string(scene2d::max_tilemap_side, '\n'), error).has_value());
    CHECK(error == "A tilemap can have at most 4096 rows.");
    // The largest allowed side is fine.
    CHECK(scene2d::parse_tiles(std::string(scene2d::max_tilemap_side - 1, ','), error)
              .value_or(scene2d::TileGrid{})
              .width == scene2d::max_tilemap_side);

    // The component keeps the parsed grid until its text changes.
    scene2d::Tilemap tilemap;
    tilemap.tiles = "4";
    const scene2d::TileGrid* first = tilemap.grid(error);
    REQUIRE(first != nullptr);
    CHECK(first->at(0, 0) == 4);
    CHECK(tilemap.grid(error) == first);
    tilemap.tiles = "5,6";
    REQUIRE(tilemap.grid(error) != nullptr);
    CHECK(tilemap.grid(error)->at(1, 0) == 6);
    tilemap.tiles = "oops";
    CHECK(tilemap.grid(error) == nullptr);
    CHECK_FALSE(error.empty());
}

TEST_CASE("Solid tilemaps stop boxes exactly at their edges") {
    reflect::Registry registry;
    scene2d::register_types(registry);
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    // A 3 x 3 ring of wall around an empty middle tile, at the origin, 1 unit per tile.
    const world::Entity walls = w.create("walls");
    w.add<scene2d::Transform2D>(walls);
    scene2d::Tilemap* ring = w.add<scene2d::Tilemap>(walls);
    ring->tiles = "1,1,1\n1,,1\n1,1,1";
    ring->solid = true;
    constexpr std::uint32_t ppu = 16;
    using scene2d::Axis;
    const auto sweep = [&](const scene2d::Box& box, Axis axis, double distance) {
        return scene2d::sweep(w, box, axis, distance, ppu);
    };

    // Feet in the middle tile; the bottom edge lies on the ring's bottom row.
    const scene2d::Box feet{1.25, 1.5, 1.75, 2.0};
    CHECK(sweep(feet, Axis::X, 1.0) == doctest::Approx(0.25));
    CHECK(sweep(feet, Axis::X, -1.0) == doctest::Approx(-0.25));
    CHECK(sweep(feet, Axis::Y, 0.5) == 0.0); // touching
    CHECK(sweep(feet, Axis::Y, -1.0) == doctest::Approx(-0.5));
    CHECK(sweep(feet, Axis::X, 0.1) == 0.1); // nothing in the way
    CHECK(sweep(feet, Axis::X, 0.0) == 0.0);
    CHECK(sweep(feet, Axis::X, std::numeric_limits<double>::quiet_NaN()) == 0.0);

    // A rounding error either side of an edge never lets a box through.
    CHECK(sweep(scene2d::Box{1.25, 1.5, 2.0 - 1e-12, 2.0}, Axis::X, 0.5) == doctest::Approx(0.0));
    CHECK(sweep(scene2d::Box{1.25, 1.5, 2.0 + 1e-12, 2.0}, Axis::X, 0.5) == 0.0);

    // A box already inside a wall can walk out of it, but not through another.
    const scene2d::Box stuck{0.25, 1.5, 0.75, 2.0};
    CHECK(sweep(stuck, Axis::X, -1.0) == -1.0);
    CHECK(sweep(stuck, Axis::X, 1.0) == 1.0);
    CHECK(sweep(stuck, Axis::X, 2.0) == doctest::Approx(1.25)); // stops at the right wall

    // Beside the map, or with the map not solid, nothing blocks.
    CHECK(sweep(scene2d::Box{1.25, 5.0, 1.75, 5.5}, Axis::X, 3.0) == 3.0);
    ring->solid = false;
    CHECK(sweep(feet, Axis::X, 1.0) == 1.0);
    ring->solid = true;

    // A second map elsewhere, with 32-pixel tiles (2 units), and a broken one ignored.
    const world::Entity post = w.create("post");
    w.add<scene2d::Transform2D>(post)->x = 10.0;
    scene2d::Tilemap* big = w.add<scene2d::Tilemap>(post);
    big->tiles = "1";
    big->tile_size = 32;
    big->solid = true;
    const world::Entity broken = w.create("broken");
    w.add<scene2d::Transform2D>(broken);
    scene2d::Tilemap* bad = w.add<scene2d::Tilemap>(broken);
    bad->tiles = "nonsense";
    bad->solid = true;
    CHECK(sweep(scene2d::Box{7.0, 0.5, 8.0, 1.0}, Axis::X, 5.0) == doctest::Approx(2.0));
    CHECK(sweep(scene2d::Box{12.5, 0.5, 13.0, 1.0}, Axis::X, -5.0) == doctest::Approx(-0.5));
    CHECK(sweep(scene2d::Box{10.5, 2.5, 11.0, 3.0}, Axis::Y, -5.0) == doctest::Approx(-0.5));
}

TEST_CASE("A sheet's description is read strictly and written the same") {
    std::string error;
    const std::optional<scene2d::SheetInfo> sheet = scene2d::parse_sheet(
        R"({"format": "sparxel.sheet", "version": 1, "frame_width": 16, "frame_height": 24,
            "durations_ms": [100, 50, 0, 70000],
            "tags": [{"name": "walk", "from": 0, "to": 3, "direction": "ping_pong"},
                     {"name": "out", "from": 2, "to": 9}, {"name": "back", "from": 3, "to": 1}]})",
        error);
    REQUIRE(sheet.has_value());
    const scene2d::SheetInfo info = sheet.value_or(scene2d::SheetInfo{});
    CHECK(info.frame_width == 16);
    CHECK(info.durations_ms == std::vector<std::uint32_t>{100, 50, 1, 65535}); // kept in range
    REQUIRE(info.tags.size() == 1); // tags outside the frames, or backwards, are dropped
    CHECK(info.find("walk") != nullptr);
    CHECK(info.find("run") == nullptr);
    std::string again_error;
    const std::optional<scene2d::SheetInfo> again =
        scene2d::parse_sheet(scene2d::sheet_to_text(info), again_error);
    REQUIRE(again.has_value());
    CHECK(scene2d::sheet_to_text(again.value_or(scene2d::SheetInfo{})) ==
          scene2d::sheet_to_text(info));
    // Forward and back, without repeating the ends.
    std::vector<std::uint32_t> order;
    for (const auto& [frame, ms] : info.sequence(info.tags[0])) {
        order.push_back(frame);
    }
    CHECK(order == std::vector<std::uint32_t>{0, 1, 2, 3, 2, 1});
    CHECK(scene2d::sheet_path("art/hero.png") == "art/hero.sheet");
    CHECK(scene2d::sheet_path("a.b/hero") == "a.b/hero.sheet");
    CHECK_FALSE(scene2d::parse_sheet("{\"format\": \"sparxel.scene\"}", error).has_value());
    CHECK(error == "This is not a sprite sheet's description (\"sparxel.sheet\").");
    CHECK_FALSE(scene2d::parse_sheet(R"({"format": "sparxel.sheet", "version": 1,
        "frame_width": 0, "frame_height": 8, "durations_ms": [100]})",
                                     error)
                    .has_value());
}

TEST_CASE("Animations play a tag of the sheet, each frame for its own time") {
    const TempFolder folder;
    write_image(folder.path / "hero.png", 4, 1, std::vector<Color>(4, Color{255, 0, 0, 255}));
    {
        std::ofstream sheet(folder.path / "hero.sheet", std::ios::binary);
        sheet << R"({"format": "sparxel.sheet", "version": 1, "frame_width": 1, "frame_height": 1,
                    "durations_ms": [100, 200, 100, 100],
                    "tags": [{"name": "walk", "from": 1, "to": 3},
                             {"name": "back", "from": 0, "to": 2, "direction": "reverse"}]})";
    }
    assets::AssetDatabase db(folder.path);
    db.refresh();
    scene2d::SheetLibrary sheets(db);

    reflect::Registry registry;
    scene2d::register_types(registry);
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    const world::Entity hero = w.create("hero");
    scene2d::Sprite* sprite = w.add<scene2d::Sprite>(hero);
    REQUIRE(sprite != nullptr);
    const assets::AssetRecord* picture = db.find("hero.png");
    REQUIRE(picture != nullptr);
    sprite->image = picture->guid.to_string();
    scene2d::SpriteAnimation* animation = w.add<scene2d::SpriteAnimation>(hero);
    REQUIRE(animation != nullptr);
    animation->play_tag("walk");

    // 50 ms steps: frame 1 for 200 ms, then 2 and 3 for 100 ms each, then round again.
    std::vector<std::uint32_t> frames;
    for (int step = 0; step < 10; ++step) {
        CHECK(scene2d::animate(w, 0.05, &sheets).empty());
        frames.push_back(w.get<scene2d::Sprite>(hero)->frame);
    }
    CHECK(frames == std::vector<std::uint32_t>{1, 1, 1, 2, 2, 3, 3, 1, 1, 1});
    CHECK(w.get<scene2d::Sprite>(hero)->frame_width == 1); // the sheet's frame size

    // Backwards, once: it says it finished, on the tag's last frame shown.
    w.get<scene2d::SpriteAnimation>(hero)->loop = false;
    w.get<scene2d::SpriteAnimation>(hero)->play_tag("back");
    std::size_t finished_at = 0;
    frames.clear();
    for (std::size_t step = 1; step <= 10; ++step) {
        if (!scene2d::animate(w, 0.05, &sheets).empty()) {
            CHECK(finished_at == 0);
            finished_at = step;
        }
        frames.push_back(w.get<scene2d::Sprite>(hero)->frame);
    }
    CHECK(frames == std::vector<std::uint32_t>{2, 1, 1, 1, 1, 0, 0, 0, 0, 0});
    CHECK(finished_at == 8); // 400 ms

    // A tag the sheet does not have plays the frames by number, as before.
    w.get<scene2d::SpriteAnimation>(hero)->loop = true;
    w.get<scene2d::SpriteAnimation>(hero)->play_tag("run");
    w.get<scene2d::SpriteAnimation>(hero)->first_frame = 3;
    static_cast<void>(scene2d::animate(w, 0.05, &sheets));
    CHECK(w.get<scene2d::Sprite>(hero)->frame == 3);
    // Without a library, tags are not read at all.
    w.get<scene2d::SpriteAnimation>(hero)->play_tag("walk");
    static_cast<void>(scene2d::animate(w, 0.05));
    CHECK(w.get<scene2d::Sprite>(hero)->frame == 3);
}

TEST_CASE("Animations step through frames, loop or finish once") {
    reflect::Registry registry;
    scene2d::register_types(registry);
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    const world::Entity torch = w.create("torch");
    w.add<scene2d::Sprite>(torch);
    scene2d::SpriteAnimation* animation = w.add<scene2d::SpriteAnimation>(torch);
    REQUIRE(animation != nullptr);
    animation->first_frame = 4;
    animation->frame_count = 3;
    animation->frames_per_second = 10.0;
    const world::Entity no_sprite = w.create("no_sprite");
    w.add<scene2d::SpriteAnimation>(no_sprite); // ignored: nothing to show the frames on

    // 1/60 s steps at 10 frames per second: a new frame every 6 steps, in exact steps.
    std::vector<std::uint32_t> frames;
    for (int step = 0; step < 24; ++step) {
        CHECK(scene2d::animate(w, 1.0 / 60.0).empty());
        frames.push_back(w.get<scene2d::Sprite>(torch)->frame);
    }
    CHECK(frames == std::vector<std::uint32_t>{4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6,
                                               6, 6, 6, 6, 6, 4, 4, 4, 4, 4, 4, 5});

    // Without looping it stops on the last frame and reports that once.
    w.get<scene2d::SpriteAnimation>(torch)->loop = false;
    w.get<scene2d::SpriteAnimation>(torch)->play();
    std::size_t finished_at = 0;
    for (std::size_t step = 1; step <= 40; ++step) {
        const std::vector<world::Entity> finished = scene2d::animate(w, 1.0 / 60.0);
        if (!finished.empty()) {
            CHECK(finished == std::vector<world::Entity>{torch});
            CHECK(finished_at == 0);
            finished_at = step;
        }
    }
    CHECK(finished_at == 18); // 0.3 s: the end of the third frame
    CHECK(w.get<scene2d::Sprite>(torch)->frame == 6);
    CHECK_FALSE(w.get<scene2d::SpriteAnimation>(torch)->playing);

    // Stopped, it stays on its frame; bad times are ignored.
    w.get<scene2d::SpriteAnimation>(torch)->loop = true;
    w.get<scene2d::SpriteAnimation>(torch)->play();
    scene2d::animate(w, 0.15);
    w.get<scene2d::SpriteAnimation>(torch)->stop();
    scene2d::animate(w, 1.0);
    scene2d::animate(w, -1.0);
    scene2d::animate(w, std::numeric_limits<double>::quiet_NaN());
    CHECK(w.get<scene2d::Sprite>(torch)->frame == 5);
}

TEST_CASE("Sprite sheet frames and tilemaps draw pixel-exact") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Fixture f;
    const Color green{0, 255, 0, 255};
    // A 4 x 2 sheet: a red 2 x 2 frame, then a blue one. Also the tileset for the map.
    write_image(f.folder.path / "sheet.png", 4, 2,
                {f.red, f.red, f.blue, f.blue, f.red, f.red, f.blue, f.blue});
    f.db.refresh();
    REQUIRE(f.db.issues().empty());
    const Guid sheet = f.db.find("sheet.png")->guid;
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    // 2 pixels per tile, no upscaling: the camera's (0, 0) is pixel (4, 4).
    scene2d::Camera2D camera{0.0, 0.0, 2, 1};

    SUBCASE("Frames") {
        const auto w = f.make_world();
        const world::Entity hero = Fixture::place(*w, "Hero", 0.0, 0.0, sheet);
        scene2d::Sprite* sprite = w->get<scene2d::Sprite>(hero);
        sprite->frame_width = 2;
        sprite->frame_height = 2;
        // Pivot bottom centre: pixels x 3..4, y 2..3.
        for (const auto& [frame, colour] :
             std::vector<std::pair<std::uint32_t, Color>>{{0, f.red}, {1, f.blue}, {2, f.red}}) {
            sprite->frame = frame; // 2 wraps around to 0
            CHECK(
                scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background).drawn ==
                1);
            const std::vector<std::uint8_t> pixels = target->read_pixels();
            CHECK(pixel(pixels, 8, 3, 2) == colour);
            CHECK(pixel(pixels, 8, 4, 3) == colour);
            CHECK(pixel(pixels, 8, 5, 3) == f.background); // only one frame wide
            CHECK(pixel(pixels, 8, 2, 3) == f.background);
        }
    }

    SUBCASE("Tilemaps") {
        const auto w = f.make_world();
        const world::Entity map = w->create("Ground");
        w->add<scene2d::Transform2D>(map);
        scene2d::Tilemap* tilemap = w->add<scene2d::Tilemap>(map);
        REQUIRE(tilemap != nullptr);
        tilemap->tileset = sheet.to_string(); // tile 0 red, tile 1 blue; 2 pixels each
        tilemap->tiles = "0,1\n1,-1\n7";      // 7 is past the end of the tileset: skipped
        scene2d::DrawStats stats =
            scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
        CHECK(stats.drawn == 3);
        CHECK(stats.missing == 0);
        std::vector<std::uint8_t> pixels = target->read_pixels();
        CHECK(pixel(pixels, 8, 4, 4) == f.red);
        CHECK(pixel(pixels, 8, 5, 5) == f.red);
        CHECK(pixel(pixels, 8, 6, 4) == f.blue);
        CHECK(pixel(pixels, 8, 7, 5) == f.blue);
        CHECK(pixel(pixels, 8, 4, 6) == f.blue);
        CHECK(pixel(pixels, 8, 6, 6) == f.background); // tile -1
        CHECK(pixel(pixels, 8, 3, 4) == f.background);

        // A sprite on the same entity is drawn over its tilemap.
        scene2d::Sprite* sprite = w->add<scene2d::Sprite>(map);
        sprite->image = f.blue_image.to_string();
        sprite->pivot_x = 0.0;
        sprite->pivot_y = 0.0;
        scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
        CHECK(pixel(target->read_pixels(), 8, 4, 4) == f.blue);
        w->remove(map, *f.registry.find_type<scene2d::Sprite>());

        // Only the tiles in view are drawn: in a 100 x 100 map, 4 x 4 tiles fill the view.
        const scene2d::TileGrid big{100, 100, std::vector<std::int32_t>(std::size_t{100} * 100, 1)};
        tilemap->tiles = scene2d::format_tiles(big);
        camera.x = 50.0;
        camera.y = 50.0;
        stats = scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
        CHECK(stats.drawn == 16);
        pixels = target->read_pixels();
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                CHECK(pixel(pixels, 8, x, y) == f.blue);
            }
        }
        // Half a tile off the grid: 5 x 5 tiles overlap the view.
        camera.x = 50.25;
        camera.y = 50.25;
        CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background).drawn ==
              25);
        // Far away: nothing.
        camera.x = -50.0;
        CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background).drawn ==
              0);

        // Bad tiles or a missing tileset are counted, not drawn.
        tilemap->tiles = "x";
        CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, green).missing == 1);
        tilemap->tiles = "0";
        tilemap->tileset = Guid::generate().to_string();
        CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, green).missing == 1);
    }
}

TEST_CASE("A sprite's corners, scaled and turned about its pivot") {
    scene2d::Sprite sprite; // pivot bottom centre
    using Corners = std::array<std::array<double, 2>, 4>;
    CHECK(scene2d::sprite_corners(sprite, 4.0, 2.0) ==
          Corners{{{-2.0, -2.0}, {2.0, -2.0}, {2.0, 0.0}, {-2.0, 0.0}}});
    // A quarter turn clockwise: the top goes to the right, exactly.
    sprite.rotation = 90.0;
    CHECK(scene2d::sprite_corners(sprite, 4.0, 2.0) ==
          Corners{{{2.0, -2.0}, {2.0, 2.0}, {0.0, 2.0}, {0.0, -2.0}}});
    sprite.rotation = -270.0; // the same
    CHECK(scene2d::sprite_corners(sprite, 4.0, 2.0) ==
          Corners{{{2.0, -2.0}, {2.0, 2.0}, {0.0, 2.0}, {0.0, -2.0}}});
    sprite.rotation = 45.0;
    const Corners slanted = scene2d::sprite_corners(sprite, 4.0, 2.0);
    CHECK(slanted[1][0] == doctest::Approx(std::sqrt(8.0))); // (2, -2) turned onto the x axis
    CHECK(slanted[1][1] == doctest::Approx(0.0));
    // Flipped across, and twice as tall; a rotation that is not a number is none.
    sprite.rotation = std::numeric_limits<double>::quiet_NaN();
    sprite.scale_x = -1.0;
    sprite.scale_y = 2.0;
    CHECK(scene2d::sprite_corners(sprite, 4.0, 2.0) ==
          Corners{{{2.0, -4.0}, {-2.0, -4.0}, {-2.0, 0.0}, {2.0, 0.0}}});
    sprite.scale_y = std::numeric_limits<double>::infinity();
    CHECK(scene2d::sprite_corners(sprite, 4.0, 2.0)[0][1] == -2.0);
}

TEST_CASE("Turned, scaled and flipped sprites draw pixel-exact") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Fixture f;
    // A 4 x 2 picture: red on the left half, blue on the right.
    write_image(f.folder.path / "sheet.png", 4, 2,
                {f.red, f.red, f.blue, f.blue, f.red, f.red, f.blue, f.blue});
    f.db.refresh();
    REQUIRE(f.db.issues().empty());
    const Guid sheet = f.db.find("sheet.png")->guid;
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    // 2 pixels per tile, no upscaling: the camera's (0, 0) is pixel (4, 4).
    const scene2d::Camera2D camera{0.0, 0.0, 2, 1};
    const auto w = f.make_world();
    scene2d::Sprite* sprite = w->get<scene2d::Sprite>(Fixture::place(*w, "Hero", 0.0, 0.0, sheet));
    const auto draw = [&] {
        CHECK(scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background).drawn ==
              1);
        return target->read_pixels();
    };

    // Upright, its feet on (4, 4): x 2 to 5, y 2 to 3.
    std::vector<std::uint8_t> pixels = draw();
    CHECK(pixel(pixels, 8, 2, 2) == f.red);
    CHECK(pixel(pixels, 8, 5, 3) == f.blue);
    CHECK(pixel(pixels, 8, 1, 3) == f.background);
    CHECK(pixel(pixels, 8, 6, 3) == f.background);
    CHECK(pixel(pixels, 8, 3, 4) == f.background);

    // Flipped: blue on the left.
    sprite->scale_x = -1.0;
    pixels = draw();
    CHECK(pixel(pixels, 8, 2, 2) == f.blue);
    CHECK(pixel(pixels, 8, 5, 3) == f.red);

    // A quarter turn clockwise about its feet: x 4 to 5, y 2 to 5, red (its left) on top.
    sprite->scale_x = 1.0;
    sprite->rotation = 90.0;
    pixels = draw();
    CHECK(pixel(pixels, 8, 4, 2) == f.red);
    CHECK(pixel(pixels, 8, 5, 3) == f.red);
    CHECK(pixel(pixels, 8, 4, 4) == f.blue);
    CHECK(pixel(pixels, 8, 5, 5) == f.blue);
    CHECK(pixel(pixels, 8, 3, 3) == f.background);
    CHECK(pixel(pixels, 8, 6, 3) == f.background);
    CHECK(pixel(pixels, 8, 4, 6) == f.background);
    sprite->rotation = -270.0;
    CHECK(draw() == pixels);

    // Twice as wide: the whole row, x 0 to 7.
    sprite->rotation = 0.0;
    sprite->scale_x = 2.0;
    pixels = draw();
    CHECK(pixel(pixels, 8, 0, 2) == f.red);
    CHECK(pixel(pixels, 8, 3, 3) == f.red);
    CHECK(pixel(pixels, 8, 4, 2) == f.blue);
    CHECK(pixel(pixels, 8, 7, 3) == f.blue);
    // Half as tall: one row, the one above its feet.
    sprite->scale_x = 1.0;
    sprite->scale_y = 0.5;
    pixels = draw();
    CHECK(pixel(pixels, 8, 2, 3) == f.red);
    CHECK(pixel(pixels, 8, 2, 2) == f.background);
    // A rotation that is not a number draws it upright.
    sprite->scale_y = 1.0;
    const std::vector<std::uint8_t> upright = draw();
    sprite->rotation = std::numeric_limits<double>::quiet_NaN();
    CHECK(draw() == upright);
}

TEST_CASE("The Diorama style: lit ground, standing sprites, blocks, turning and fog") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto w = f.make_world();
    // A 2 x 2 red ground around the origin. 2 pixels per tile, so 2 x 2 images are 1 tile.
    const world::Entity ground = w->create("ground");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(ground);
    at->x = -1.0;
    at->y = -1.0;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(ground);
    map->tileset = f.red_image.to_string();
    map->tiles = "0,0\n0,0";

    // An odd size puts the point looked at on the centre pixel.
    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    scene2d::DioramaCamera camera;
    camera.pitch = 45.0;
    camera.distance = 4.0;
    camera.fov = 30.0;
    camera.ppu = 2;
    const scene2d::DioramaLight light;
    const auto shade = [](float level, std::uint8_t channel) {
        return static_cast<int>(std::lround(level * static_cast<float>(channel)));
    };
    const auto close_to = [](Color c, int r, int g, int b) {
        return std::abs(c.r - r) <= 2 && std::abs(c.g - g) <= 2 && std::abs(c.b - b) <= 2;
    };

    scene2d::DrawStats stats =
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    CHECK(stats.drawn == 4);
    CHECK(stats.missing == 0);
    std::vector<std::uint8_t> pixels = target->read_pixels();
    const float top = scene2d::brightness(render::Vec3{0, 1, 0}, light);
    CHECK(close_to(pixel(pixels, 17, 8, 8), shade(top, 255), 0, 0)); // lit from above
    // The top row looks about 2 tiles north of the point: beyond the map, at the sky.
    CHECK(pixel(pixels, 17, 8, 0) == f.background);

    // A blue sprite standing on the point: above it on screen, with the ground in front.
    Fixture::place(*w, "Hero", 0.0, 0.0, f.blue_image);
    stats = scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    CHECK(stats.drawn == 5);
    pixels = target->read_pixels();
    const float facing = scene2d::brightness(render::Vec3{0, 0, 1}, light); // towards the eye
    CHECK(close_to(pixel(pixels, 17, 8, 6), 0, 0, shade(facing, 255)));
    CHECK(close_to(pixel(pixels, 17, 8, 10), shade(top, 255), 0, 0));

    // Scaled and turned in its own plane: the pixels showing points a quarter and three
    // quarters of a tile above its feet both show the sprite, 1 tile tall; at half the
    // height only the lower one does; turned upside down about its feet, it is under the
    // ground, out of sight.
    const auto pixel_at = [&](double up) {
        const auto clip = scene2d::diorama_view_projection(camera, 1.0F)
                              .transform(render::Vec3{0.0F, static_cast<float>(up), 0.0F});
        const double x = ((clip[0] / clip[3]) + 1.0) / 2.0 * 17.0;
        const double y = (1.0 - (clip[1] / clip[3])) / 2.0 * 17.0;
        REQUIRE(x >= 0.0);
        REQUIRE(x < 17.0);
        REQUIRE(y >= 0.0);
        REQUIRE(y < 17.0);
        return pixel(pixels, 17, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    };
    const auto is_sprite = [&](Color c) { return close_to(c, 0, 0, shade(facing, 255)); };
    scene2d::Sprite* standing = w->get<scene2d::Sprite>(w->find_named("Hero").at(0));
    CHECK(is_sprite(pixel_at(0.25)));
    CHECK(is_sprite(pixel_at(0.75)));
    standing->scale_y = 0.5;
    scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    pixels = target->read_pixels();
    CHECK(is_sprite(pixel_at(0.25)));
    CHECK_FALSE(is_sprite(pixel_at(0.75)));
    standing->scale_y = 1.0;
    standing->rotation = 180.0;
    scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    pixels = target->read_pixels();
    CHECK_FALSE(is_sprite(pixel_at(0.25)));
    standing->rotation = 0.0;
    scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    pixels = target->read_pixels();

    // Turned a quarter, the sprite still faces the camera (now lit from the side).
    camera.yaw = 90.0;
    scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    const float side = scene2d::brightness(render::Vec3{1, 0, 0}, light);
    CHECK(close_to(pixel(target->read_pixels(), 17, 8, 6), 0, 0, shade(side, 255)));
    camera.yaw = 0.0;

    // Fog from 0 to 1 tile: everything drawn is fog.
    const render::Fog fog{Color{200, 210, 220, 255}, 0.0F, 1.0F};
    scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light, fog);
    CHECK(pixel(target->read_pixels(), 17, 8, 10) == Color{200, 210, 220, 255});

    // Blocks: one tile 1 high is a top and four sides; two side by side hide two sides.
    map->tiles = "0";
    map->height = 1.0;
    w->get<scene2d::Sprite>(w->find_named("Hero").at(0))->visible = false;
    CHECK(scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light)
              .drawn == 5);
    map->tiles = "0,0";
    CHECK(scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light)
              .drawn == 8);
    // A broken map and a missing image are counted, not drawn.
    map->tiles = "x";
    w->get<scene2d::Sprite>(w->find_named("Hero").at(0))->visible = true;
    w->get<scene2d::Sprite>(w->find_named("Hero").at(0))->image = Guid::generate().to_string();
    stats = scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
    CHECK(stats.missing == 2);
    CHECK(stats.drawn == 0);
}

TEST_CASE("Diorama lighting and camera") {
    const scene2d::DioramaLight light{render::Vec3{0, 1, 0}, 0.25F, 0.75F};
    CHECK(scene2d::brightness(render::Vec3{0, 1, 0}, light) == doctest::Approx(1.0));
    CHECK(scene2d::brightness(render::Vec3{0, 5, 0}, light) == doctest::Approx(1.0));  // normalised
    CHECK(scene2d::brightness(render::Vec3{1, 0, 0}, light) == doctest::Approx(0.25)); // side
    CHECK(scene2d::brightness(render::Vec3{0, -1, 0}, light) == doctest::Approx(0.25)); // below

    // The camera looks at its point from the south, above; the point is at the centre.
    scene2d::DioramaCamera camera;
    camera.x = 3.0;
    camera.y = 5.0;
    camera.pitch = 30.0;
    camera.distance = 10.0;
    render::Vec3 eye;
    const render::Mat4 vp = scene2d::diorama_view_projection(camera, 1.0F, &eye);
    CHECK(eye.x == doctest::Approx(3.0));
    CHECK(eye.y == doctest::Approx(5.0)); // 10 * sin(30 degrees)
    CHECK(eye.z == doctest::Approx(5.0 + (10.0 * std::cos(3.14159265358979 / 6.0))));
    const auto clip = vp.transform(render::Vec3{3.0F, 0.0F, 5.0F});
    CHECK(clip[0] / clip[3] == doctest::Approx(0.0).epsilon(1e-5));
    CHECK(clip[1] / clip[3] == doctest::Approx(0.0).epsilon(1e-5));
    // North of the point (smaller y) is higher on screen, as in the 2D view.
    const auto north = vp.transform(render::Vec3{3.0F, 0.0F, 4.0F});
    CHECK(north[1] / north[3] > 0.0F);
    // Pitch is kept from 5 to 85 degrees, so the camera never looks straight down.
    camera.pitch = 90.0;
    static_cast<void>(scene2d::diorama_view_projection(camera, 1.0F, &eye));
    CHECK(eye.y == doctest::Approx(10.0 * std::sin(85.0 * 3.14159265358979 / 180.0)));
}

TEST_CASE("The camera stays inside the map") {
    reflect::Registry registry;
    scene2d::register_types(registry);
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    CHECK_FALSE(scene2d::map_bounds(w, 16).has_value()); // no tilemaps

    // A 20 x 10 map at (-2, 1), and a 4 x 4 map of 32-pixel tiles (8 x 8 units at 16 PPU)
    // reaching further down. Empty maps add nothing.
    const world::Entity ground = w.create("ground");
    w.add<scene2d::Transform2D>(ground)->x = -2.0;
    w.get<scene2d::Transform2D>(ground)->y = 1.0;
    w.add<scene2d::Tilemap>(ground)->tiles = "0\n\n\n\n\n\n\n\n\n,,,,,,,,,,,,,,,,,,,0";
    const world::Entity big = w.create("big");
    w.add<scene2d::Transform2D>(big)->y = 5.0;
    scene2d::Tilemap* tiles = w.add<scene2d::Tilemap>(big);
    tiles->tiles = "0,0,0,0\n0\n0\n0";
    tiles->tile_size = 32;
    const world::Entity empty = w.create("empty");
    w.add<scene2d::Transform2D>(empty)->x = -100.0;
    w.add<scene2d::Tilemap>(empty);
    const std::optional<scene2d::Box> found = scene2d::map_bounds(w, 16);
    REQUIRE(found.has_value());
    const scene2d::Box bounds = found.value_or(scene2d::Box{});
    CHECK(bounds.left == -2.0);
    CHECK(bounds.top == 1.0);
    CHECK(bounds.right == 18.0);
    CHECK(bounds.bottom == 13.0); // 5 + 4 tiles of 2 units

    // A 10 x 6 view inside a 20 x 12 map.
    const scene2d::Box map{0.0, 0.0, 20.0, 12.0};
    const scene2d::Box view{-5.0, -3.0, 5.0, 3.0};
    const auto at = [&](double x, double y, const scene2d::Box& v) {
        return scene2d::clamp_view(x, y, v, map);
    };
    CHECK(at(10.0, 6.0, view) == std::array<double, 2>{10.0, 6.0});  // free in the middle
    CHECK(at(1.0, 1.0, view) == std::array<double, 2>{5.0, 3.0});    // the top-left corner
    CHECK(at(19.5, 11.0, view) == std::array<double, 2>{15.0, 9.0}); // the bottom-right
    CHECK(at(-50.0, 6.0, view) == std::array<double, 2>{5.0, 6.0});
    // Wider than the map: centred on that axis only.
    CHECK(at(3.0, 1.0, scene2d::Box{-15.0, -3.0, 15.0, 3.0}) == std::array<double, 2>{10.0, 3.0});
    // Not centred on the point: 2 above it and 8 below, in a map 12 high.
    CHECK(at(10.0, 0.0, scene2d::Box{-5.0, -2.0, 5.0, 8.0}) == std::array<double, 2>{10.0, 2.0});
    CHECK(at(10.0, 12.0, scene2d::Box{-5.0, -2.0, 5.0, 8.0}) == std::array<double, 2>{10.0, 4.0});
    // An infinite edge is never limited; the other still is.
    constexpr double infinity = std::numeric_limits<double>::infinity();
    CHECK(at(10.0, 0.0, scene2d::Box{-5.0, -infinity, 5.0, 3.0}) ==
          std::array<double, 2>{10.0, 0.0});
    CHECK(at(10.0, 11.0, scene2d::Box{-5.0, -infinity, 5.0, 3.0}) ==
          std::array<double, 2>{10.0, 9.0});
    // Broken numbers change nothing.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(std::isnan(at(nan, 6.0, view)[0]));
    CHECK(scene2d::clamp_view(1.0, 1.0, view, scene2d::Box{nan, 0.0, 20.0, 12.0})[0] == 1.0);
}

TEST_CASE("The Diorama view extent meets the screen's edges on the ground") {
    for (const double pitch : {20.0, 35.0, 60.0, 85.0}) {
        for (const double fov : {10.0, 30.0, 60.0}) {
            CAPTURE(pitch);
            CAPTURE(fov);
            scene2d::DioramaCamera camera;
            camera.x = 7.0;
            camera.y = 4.0;
            camera.pitch = pitch;
            camera.fov = fov;
            camera.distance = 12.0;
            const double aspect = 16.0 / 9.0;
            const scene2d::Box view = scene2d::diorama_view_extent(camera, aspect);
            const render::Mat4 vp =
                scene2d::diorama_view_projection(camera, static_cast<float>(aspect));
            const auto ndc_y = [&](double map_y) {
                const auto clip = vp.transform(render::Vec3{7.0F, 0.0F, static_cast<float>(map_y)});
                return clip[1] / clip[3];
            };
            // The bottom edge of the screen meets the ground at view.bottom below the point.
            CHECK(ndc_y(4.0 + view.bottom) == doctest::Approx(-1.0).epsilon(1e-4));
            if (pitch > fov / 2.0) {
                CHECK(ndc_y(4.0 + view.top) == doctest::Approx(1.0).epsilon(1e-4));
                CHECK(-view.top > view.bottom); // more ground above the point than below
            } else {
                CHECK(std::isinf(view.top));
            }
            // The sides, at the point: the screen's left and right edges.
            const auto side =
                vp.transform(render::Vec3{static_cast<float>(7.0 + view.right), 0.0F, 4.0F});
            CHECK(side[0] / side[3] == doctest::Approx(1.0).epsilon(1e-4));
            CHECK(view.left == -view.right);
        }
    }
    scene2d::DioramaCamera turned;
    turned.yaw = 30.0;
    CHECK(std::isinf(scene2d::diorama_view_extent(turned, 1.0).right));
}

TEST_CASE("Lights: the nearest enabled ones, at their height, in their colour") {
    const Fixture f;
    const auto w = f.make_world();
    CHECK(scene2d::diorama_lights(*w, 0.0, 0.0).empty());
    const auto add = [&](std::string_view name, double x, double y) {
        const world::Entity e = w->create(name);
        scene2d::Transform2D* t = w->add<scene2d::Transform2D>(e);
        t->x = x;
        t->y = y;
        return w->add<scene2d::Light>(e);
    };
    scene2d::Light* torch = add("torch", 3.0, 4.0); // 5 from the origin
    torch->color_r = 255;
    torch->color_g = 102;
    torch->color_b = 0;
    torch->intensity = 2.0;
    torch->radius = 6.0;
    torch->height = 1.5;
    add("near", 1.0, 0.0);
    add("off", 0.5, 0.0)->enabled = false;
    add("no reach", 0.5, 0.0)->radius = 0.0;
    add("no strength", 0.5, 0.0)->intensity = 0.0;
    add("too far up", 2.0, 0.0)->height = 100.0; // kept to 16
    std::vector<render::PointLight> lights = scene2d::diorama_lights(*w, 0.0, 0.0);
    REQUIRE(lights.size() == 3);
    CHECK(lights[0].position == render::Vec3{1.0F, 1.0F, 0.0F}); // nearest first
    CHECK(lights[1].position == render::Vec3{2.0F, 16.0F, 0.0F});
    CHECK(lights[2].position == render::Vec3{3.0F, 1.5F, 4.0F}); // map (x, y) is world (x, z)
    CHECK(lights[2].radius == 6.0F);
    CHECK(lights[2].color[0] == doctest::Approx(2.0)); // colour times intensity
    CHECK(lights[2].color[1] == doctest::Approx(0.8));
    CHECK(lights[2].color[2] == 0.0F);
    // Nearest to another point: the torch comes first.
    CHECK(scene2d::diorama_lights(*w, 3.0, 4.0)[0].position.z == 4.0F);
    // No more than max_point_lights: the nearest ones.
    for (int i = 0; i < 20; ++i) {
        add("far", 10.0 + i, 0.0);
    }
    lights = scene2d::diorama_lights(*w, 0.0, 0.0);
    REQUIRE(lights.size() == render::max_point_lights);
    CHECK(lights.back().position.x == 22.0F); // 1, 2, 3 (torch, 5 away), then 10 to 22
}

TEST_CASE("A Light lights the Diorama ground around it, per pixel") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto w = f.make_world();
    // A red ground 6 x 6 tiles around the origin, in the dark.
    const world::Entity ground = w->create("ground");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(ground);
    at->x = -3.0;
    at->y = -3.0;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(ground);
    map->tileset = f.red_image.to_string();
    map->tiles = "0,0,0,0,0,0\n0,0,0,0,0,0\n0,0,0,0,0,0\n0,0,0,0,0,0\n0,0,0,0,0,0\n0,0,0,0,0,0";
    const world::Entity lamp = w->create("lamp");
    w->add<scene2d::Transform2D>(lamp);
    scene2d::Light* light = w->add<scene2d::Light>(lamp);
    light->color_r = 255;
    light->color_g = 255;
    light->color_b = 255;
    light->radius = 2.0;
    light->height = 1.0;

    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    scene2d::DioramaCamera camera;
    camera.pitch = 60.0;
    camera.distance = 8.0;
    camera.ppu = 2;
    scene2d::DioramaLight dark;
    dark.ambient = 0.0F;
    dark.diffuse = 0.0F;
    const auto draw = [&] {
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, dark);
        return target->read_pixels();
    };
    // Under the lamp, 1 tile below it and facing it: (1 - (1/2)^2)^2 = 0.5625 of red.
    const std::vector<std::uint8_t> pixels = draw();
    const Color under = pixel(pixels, 17, 8, 8);
    CHECK(std::abs(under.r - 143) <= 3);
    CHECK(under.g == 0);
    // Beyond its radius the ground stays dark (but is drawn, not the background).
    CHECK(pixel(pixels, 17, 0, 16) == Color{0, 0, 0, 255});
    // Between: dimmer than under it.
    const Color between = pixel(pixels, 17, 11, 8);
    CHECK(between.r < under.r);

    light->enabled = false;
    CHECK(pixel(draw(), 17, 8, 8) == Color{0, 0, 0, 255});
}

TEST_CASE("Sprites can cast a soft round shadow on the tile they stand on") {
    const Fixture f;
    const auto w = f.make_world();
    // Flat ground 2 x 2 around the origin, and a block 1.5 high from (2, 0).
    const world::Entity ground = w->create("ground");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(ground);
    at->x = -1.0;
    at->y = -1.0;
    at->layer = -1;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(ground);
    map->tileset = f.red_image.to_string();
    map->tiles = "0,0\n0,0";
    const world::Entity block = w->create("block");
    w->add<scene2d::Transform2D>(block)->x = 2.0;
    scene2d::Tilemap* blocks = w->add<scene2d::Tilemap>(block);
    blocks->tileset = f.red_image.to_string();
    blocks->tiles = "0";
    blocks->height = 1.5;
    CHECK(scene2d::ground_top(*w, 0.5, 0.5, 2).value_or(-9.0F) == doctest::Approx(-0.002));
    CHECK(scene2d::ground_top(*w, 2.5, 0.5, 2).value_or(-9.0F) == doctest::Approx(1.5));
    CHECK_FALSE(scene2d::ground_top(*w, 5.0, 5.0, 2).has_value());
    blocks->visible = false;
    CHECK_FALSE(scene2d::ground_top(*w, 2.5, 0.5, 2).has_value());
    blocks->visible = true;

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Fixture::place(*w, "hero", 0.0, 0.0, f.blue_image);
    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    scene2d::DioramaCamera camera;
    camera.pitch = 45.0;
    camera.distance = 4.0;
    camera.ppu = 2;
    const auto draw = [&](float shadow) {
        scene2d::DioramaLight light;
        light.sprite_shadow = shadow;
        light.shadows = scene2d::ShadowShape::Round;
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
        return target->read_pixels();
    };
    const std::vector<std::uint8_t> plain = draw(0.0F);
    const std::vector<std::uint8_t> shaded = draw(0.6F);
    CHECK(renderer->last_stats().shadows == 1);
    std::size_t darker = 0;
    for (std::uint32_t y = 0; y < 17; ++y) {
        for (std::uint32_t x = 0; x < 17; ++x) {
            const Color a = pixel(plain, 17, x, y);
            const Color b = pixel(shaded, 17, x, y);
            if (a == b) {
                continue;
            }
            CAPTURE(x);
            CAPTURE(y);
            CHECK(b.r <= a.r); // only ever darker
            CHECK(b.g <= a.g);
            CHECK(b.b <= a.b);
            CHECK(std::abs(static_cast<int>(x) - 8) <= 4); // only around the feet
            CHECK(std::abs(static_cast<int>(y) - 8) <= 4);
            ++darker;
        }
    }
    CHECK(darker > 0);
    // Off the map, nothing to stand on: no shadow.
    w->get<scene2d::Transform2D>(w->find_named("hero").at(0))->x = 6.0;
    static_cast<void>(draw(0.6F));
    CHECK(renderer->last_stats().shadows == 0);
}

TEST_CASE("The sun: where it is, and where shadows fall") {
    const auto near = [](render::Vec3 a, render::Vec3 b) {
        return std::abs(a.x - b.x) < 1e-5F && std::abs(a.y - b.y) < 1e-5F &&
               std::abs(a.z - b.z) < 1e-5F;
    };
    const auto half = static_cast<float>(std::sqrt(0.5));
    CHECK(near(scene2d::sun_direction(0.0, 90.0), {0, 1, 0}));         // overhead
    CHECK(near(scene2d::sun_direction(90.0, 0.0), {1, 0, 0}));         // east, on the horizon
    CHECK(near(scene2d::sun_direction(180.0, 45.0), {0, half, half})); // south: +z, down the map
    CHECK(near(scene2d::sun_direction(270.0, 30.0), {-0.8660254F, 0.5F, 0.0F}));
    // The default: south-west, so shadows fall up and to the right.
    const scene2d::DioramaLight light;
    CHECK(light.sun.x < 0.0F);
    CHECK(light.sun.z > 0.0F);

    // The shadow of a point one tile up: away from the sun, a tile long at 45 degrees.
    const auto offset = [](double angle, double height) {
        return scene2d::shadow_offset(scene2d::sun_direction(angle, height))
            .value_or(std::array<double, 2>{99.0, 99.0});
    };
    CHECK(offset(90.0, 45.0)[0] == doctest::Approx(-1.0)); // sun east: shadow west
    CHECK(offset(90.0, 45.0)[1] == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(offset(0.0, 45.0)[1] == doctest::Approx(1.0)); // sun north: shadow south
    CHECK(offset(0.0, 60.0)[1] == doctest::Approx(1.0 / std::tan(60.0 * 3.14159265358979 / 180.0)));
    // A low sun: as long as max_shadow_length, no longer.
    CHECK(offset(180.0, 5.0)[1] == doctest::Approx(-scene2d::max_shadow_length));
    CHECK(std::abs(offset(0.0, 90.0)[0]) < 1e-6); // noon: under the feet
    CHECK(std::abs(offset(0.0, 90.0)[1]) < 1e-6);
    // A set sun casts nothing.
    CHECK_FALSE(scene2d::shadow_offset(scene2d::sun_direction(0.0, -10.0)).has_value());
    CHECK_FALSE(scene2d::shadow_offset(render::Vec3{1, 0, 0}).has_value());
}

TEST_CASE("Sprites cast their silhouette on the ground, away from the sun") {
    Fixture f;
    // A figure one tile wide and three high (2 x 6 pixels at 2 per tile), standing in the
    // middle of a flat red ground 8 x 8 tiles.
    write_image(f.folder.path / "tall.png", 2, 6, std::vector<Color>(12, Color{0, 200, 0, 255}));
    f.db.refresh();
    REQUIRE(f.db.find("tall.png") != nullptr);
    const Guid tall = f.db.find("tall.png")->guid;
    const auto w = f.make_world();
    const world::Entity ground = w->create("ground");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(ground);
    at->x = -4.0;
    at->y = -4.0;
    at->layer = -1;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(ground);
    map->tileset = f.red_image.to_string();
    const std::string row = "0,0,0,0,0,0,0,0";
    map->tiles = row;
    for (int i = 1; i < 8; ++i) {
        map->tiles += "\n" + row;
    }
    Fixture::place(*w, "figure", 0.0, 0.0, tall);

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    constexpr std::uint32_t size = 64;
    const auto target = device->create_render_target(size, size);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    scene2d::DioramaCamera camera;
    camera.pitch = 50.0;
    camera.distance = 14.0;
    camera.ppu = 2;
    const auto draw = [&](double angle, double height, scene2d::ShadowShape shape, float dark) {
        scene2d::DioramaLight light;
        light.sun = scene2d::sun_direction(angle, height);
        light.sprite_shadow = dark;
        light.shadows = shape;
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light);
        return target->read_pixels();
    };
    // Where the feet are on the image.
    const std::array<float, 4> clip =
        scene2d::diorama_view_projection(camera, 1.0F).transform(render::Vec3{0, 0, 0});
    const double feet_x = (((clip[0] / clip[3]) + 1.0) / 2.0) * size;
    const double feet_y = ((1.0 - (clip[1] / clip[3])) / 2.0) * size;

    // How the shadow lies: the darkened pixels, their middle relative to the feet (in
    // pixels, y down), and the most any pixel was darkened (its ratio to the unshadowed).
    struct Shade {
        std::size_t count = 0;
        double x = 0.0;
        double y = 0.0;
        double deepest = 1.0;
    };
    const auto shade = [&](double angle, double height, scene2d::ShadowShape shape) {
        // The same sun lights the ground either way: only the shadow differs.
        const std::vector<std::uint8_t> plain = draw(angle, height, shape, 0.0F);
        const std::vector<std::uint8_t> shaded = draw(angle, height, shape, 0.5F);
        Shade s;
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                const Color a = pixel(plain, size, x, y);
                const Color b = pixel(shaded, size, x, y);
                if (a == b) {
                    continue;
                }
                CAPTURE(x);
                CAPTURE(y);
                REQUIRE(a.r > 0); // only the red ground is shadowed, not the figure
                CHECK(b.r < a.r);
                CHECK(b.g == a.g);
                ++s.count;
                s.x += x + 0.5 - feet_x;
                s.y += y + 0.5 - feet_y;
                s.deepest = std::min(s.deepest, static_cast<double>(b.r) / a.r);
            }
        }
        if (s.count > 0) {
            s.x /= static_cast<double>(s.count);
            s.y /= static_cast<double>(s.count);
        }
        return s;
    };

    using scene2d::ShadowShape;
    // The sun in the east: the shadow lies to the west, left on the image, three tiles
    // long; darkest (0.5) at the feet.
    const Shade west = shade(90.0, 45.0, ShadowShape::Cast);
    CHECK(renderer->last_stats().shadows == 1);
    CHECK(west.count > 20);
    CHECK(west.x < -5.0);
    CHECK(std::abs(west.y) < std::abs(west.x));
    CHECK(west.deepest >= 0.48);
    CHECK(west.deepest <= 0.56);
    // In the west: to the east, right.
    const Shade east = shade(270.0, 45.0, ShadowShape::Cast);
    CHECK(east.count > 20);
    CHECK(east.x > 5.0);
    CHECK(std::abs(east.y) < east.x);
    // In the north: to the south, towards the camera, down the image.
    const Shade south = shade(0.0, 45.0, ShadowShape::Cast);
    CHECK(south.count > 20);
    CHECK(south.y > 3.0);
    CHECK(std::abs(south.x) < south.y);
    // A lower sun, a longer shadow.
    const Shade evening = shade(90.0, 20.0, ShadowShape::Cast);
    CHECK(evening.count > west.count);
    CHECK(evening.x < west.x);
    // At noon the silhouette shrinks to the line under the feet: nothing to see.
    CHECK(shade(0.0, 90.0, ShadowShape::Cast).count == 0);
    // The round blob ignores the sun: around the feet.
    const Shade round = shade(90.0, 45.0, ShadowShape::Round);
    CHECK(round.count > 0);
    CHECK(std::abs(round.x) < 2.0);
    CHECK(std::abs(round.y) < 3.0);
}

TEST_CASE("See-through maps: water over a riverbed, in both styles") {
    scene2d::Tilemap probe;
    CHECK(probe.alpha() == 255);
    probe.opacity = 0.5;
    CHECK(probe.alpha() == 128);
    probe.opacity = -1.0;
    CHECK(probe.alpha() == 0);
    probe.opacity = 2.0;
    CHECK(probe.alpha() == 255);
    probe.opacity = std::nan("");
    CHECK(probe.alpha() == 255);

    const Fixture f;
    const auto w = f.make_world();
    // A red riverbed, and blue water over it a layer up, each 2 x 2 tiles from (-1, -1).
    const auto map = [&](std::string_view name, Guid image, std::int32_t layer) {
        const world::Entity e = w->create(name);
        scene2d::Transform2D* at = w->add<scene2d::Transform2D>(e);
        at->x = -1.0;
        at->y = -1.0;
        at->layer = layer;
        scene2d::Tilemap* tiles = w->add<scene2d::Tilemap>(e);
        tiles->tileset = image.to_string();
        tiles->tiles = "0,0\n0,0";
        return tiles;
    };
    static_cast<void>(map("riverbed", f.red_image, 0));
    scene2d::Tilemap* water = map("water", f.blue_image, 1);
    water->opacity = 0.5;
    const auto close = [](Color a, Color b) {
        return std::abs(a.r - b.r) <= 2 && std::abs(a.g - b.g) <= 2 && std::abs(a.b - b.b) <= 2 &&
               a.a == 255 && b.a == 255;
    };

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    scene2d::TextureCache textures(*device, f.db);

    // Classic 2D: half of each, 2 pixels per tile filling a 4 x 4 target.
    const auto flat_target = device->create_render_target(4, 4);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(flat_target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::draw_world(*w, textures, *sprites, *flat_target, scene2d::Camera2D{0.0, 0.0, 2, 1},
                        f.background);
    const Color flat = pixel(flat_target->read_pixels(), 4, 1, 1);
    CAPTURE(flat.r);
    CAPTURE(flat.b);
    CHECK(close(flat, Color{127, 0, 128, 255}));

    // Diorama: the lit riverbed and the lit water, blended by the water's opacity.
    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::DioramaCamera camera;
    camera.pitch = 45.0;
    camera.distance = 4.0;
    camera.ppu = 2;
    const auto middle = [&] {
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, {});
        return pixel(target->read_pixels(), 17, 8, 8);
    };
    const Color blended = middle();
    CHECK(renderer->last_stats().translucent == 4);
    water->opacity = 1.0;
    const Color solid_water = middle();
    CHECK(renderer->last_stats().translucent == 0);
    water->opacity = 0.0;
    const Color riverbed = middle(); // fully clear water: not drawn at all
    CHECK(renderer->last_stats().translucent == 0);
    const double a = 128.0 / 255.0;
    const auto mix = [a](std::uint8_t under, std::uint8_t over) {
        return static_cast<std::uint8_t>(std::lround((under * (1.0 - a)) + (over * a)));
    };
    CAPTURE(blended.r);
    CAPTURE(blended.b);
    CHECK(riverbed.r > 0);
    CHECK(solid_water.b > 0);
    CHECK(close(blended, Color{mix(riverbed.r, solid_water.r), mix(riverbed.g, solid_water.g),
                               mix(riverbed.b, solid_water.b), 255}));
}

TEST_CASE("Blocks shadow the ground on the side away from the sun") {
    const Fixture f;
    const auto w = f.make_world();
    // Red ground 8 x 8 tiles around the origin, and one block two tiles tall at (0, 0).
    const world::Entity ground = w->create("ground");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(ground);
    at->x = -4.0;
    at->y = -4.0;
    at->layer = -1;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(ground);
    map->tileset = f.red_image.to_string();
    const std::string row = "0,0,0,0,0,0,0,0";
    map->tiles = row;
    for (int i = 1; i < 8; ++i) {
        map->tiles += "\n" + row;
    }
    const world::Entity block = w->create("block");
    w->add<scene2d::Transform2D>(block);
    scene2d::Tilemap* blocks = w->add<scene2d::Tilemap>(block);
    blocks->tileset = f.blue_image.to_string();
    blocks->tiles = "0";
    blocks->height = 2.0;

    scene2d::DioramaCamera camera;
    camera.pitch = 50.0;
    camera.distance = 14.0;
    camera.ppu = 2;
    scene2d::DioramaLight light;
    light.sun = scene2d::sun_direction(90.0, 45.0); // east: shadows fall west
    light.sprite_shadow = 0.0F;
    // The sun's view covers the map, from below the ground to above the block.
    const std::optional<render::SunShadow> sun = scene2d::diorama_sun_shadow(*w, camera, light);
    REQUIRE(sun.has_value());
    const render::SunShadow& view = sun.value_or(render::SunShadow{});
    for (const render::Vec3 corner :
         {render::Vec3{-4, 0, -4}, render::Vec3{4, 0, 4}, render::Vec3{0.5F, 2, 0.5F}}) {
        const std::array<float, 4> c = view.view_projection.transform(corner);
        CHECK(std::abs(c[0] / c[3]) <= 1.0F);
        CHECK(std::abs(c[1] / c[3]) <= 1.0F);
        CHECK(c[2] / c[3] >= 0.0F);
        CHECK(c[2] / c[3] <= 1.0F);
    }
    // None without block shadows, with the sun set, or with no maps.
    scene2d::DioramaLight off = light;
    off.block_shadow = 0.0F;
    CHECK_FALSE(scene2d::diorama_sun_shadow(*w, camera, off).has_value());
    off = light;
    off.sun = scene2d::sun_direction(90.0, -5.0);
    CHECK_FALSE(scene2d::diorama_sun_shadow(*w, camera, off).has_value());

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    constexpr std::uint32_t size = 64;
    const auto target = device->create_render_target(size, size);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    const auto draw = [&](float darkness) {
        scene2d::DioramaLight l = light;
        l.block_shadow = darkness;
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, l);
        return target->read_pixels();
    };
    const std::vector<std::uint8_t> plain = draw(0.0F);
    const std::vector<std::uint8_t> shaded = draw(0.5F);
    CHECK(renderer->last_stats().sun_casters > 0);
    // Where a ground point is on the image.
    const render::Mat4 vp = scene2d::diorama_view_projection(camera, 1.0F);
    const auto at_ground = [&](float x, float y, const std::vector<std::uint8_t>& pixels) {
        const std::array<float, 4> c = vp.transform(render::Vec3{x, -0.002F, y});
        const auto px = static_cast<std::uint32_t>(((c[0] / c[3]) + 1.0F) / 2.0F * size);
        const auto py = static_cast<std::uint32_t>((1.0F - (c[1] / c[3])) / 2.0F * size);
        return pixel(pixels, size, px, py);
    };
    // West of the block, inside its shadow (two tiles long at 45 degrees): half as bright.
    const Color sunny = at_ground(-1.0F, 0.5F, plain);
    const Color shadowed = at_ground(-1.0F, 0.5F, shaded);
    CAPTURE(sunny.r);
    CAPTURE(shadowed.r);
    CHECK(std::abs(static_cast<int>(shadowed.r) - static_cast<int>(sunny.r) / 2) <= 2);
    // East of it, and beyond the shadow's end, the ground is as before.
    CHECK(at_ground(1.5F, 0.5F, shaded) == at_ground(1.5F, 0.5F, plain));
    CHECK(at_ground(-3.0F, 0.5F, shaded) == at_ground(-3.0F, 0.5F, plain));
    CHECK(at_ground(-1.0F, 2.5F, shaded) == at_ground(-1.0F, 2.5F, plain));
}

TEST_CASE("Animated tiles: water that moves, in both styles") {
    scene2d::Tilemap probe;
    probe.animation_frames = 3;
    probe.animation_fps = 2.0;
    CHECK(probe.frame() == 0);
    probe.elapsed = 0.5;
    CHECK(probe.frame() == 1);
    probe.elapsed = 1.49;
    CHECK(probe.frame() == 2);
    probe.elapsed = 1.5; // wraps
    CHECK(probe.frame() == 0);
    probe.elapsed = 0.5;
    CHECK(probe.shown(4, 10) == 5);
    CHECK(probe.shown(9, 10) == 9); // the next tile would be past the tileset
    probe.animation_frames = 1;
    CHECK(probe.frame() == 0); // still
    probe.animation_frames = 3;
    probe.elapsed = std::nan("");
    CHECK(probe.frame() == 0);

    Fixture f;
    // A tileset of two 2 x 2 tiles side by side: red, then blue.
    std::vector<Color> pixels;
    for (int y = 0; y < 2; ++y) {
        for (const Color c : {f.red, f.red, f.blue, f.blue}) {
            pixels.push_back(c);
        }
    }
    write_image(f.folder.path / "water.png", 4, 2, pixels);
    f.db.refresh();
    REQUIRE(f.db.find("water.png") != nullptr);
    const Guid water_image = f.db.find("water.png")->guid;
    const auto w = f.make_world();
    const world::Entity water = w->create("water");
    scene2d::Transform2D* at = w->add<scene2d::Transform2D>(water);
    at->x = -1.0;
    at->y = -1.0;
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(water);
    map->tileset = water_image.to_string();
    map->tiles = "0,0\n0,0";
    map->animation_frames = 2;
    map->animation_fps = 2.0;
    // animate() moves the map's time on, as it does sprites'.
    static_cast<void>(scene2d::animate(*w, 0.25));
    CHECK(map->elapsed == doctest::Approx(0.25));

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    scene2d::TextureCache textures(*device, f.db);
    const auto flat = device->create_render_target(4, 4);
    const auto sprites = render::SpriteRenderer::create(*device);
    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(flat != nullptr);
    REQUIRE(sprites != nullptr);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::DioramaCamera camera;
    camera.pitch = 45.0;
    camera.distance = 4.0;
    camera.ppu = 2;
    const auto colours = [&] {
        scene2d::draw_world(*w, textures, *sprites, *flat, scene2d::Camera2D{0.0, 0.0, 2, 1},
                            f.background);
        scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, {});
        return std::array<Color, 2>{pixel(flat->read_pixels(), 4, 1, 1),
                                    pixel(target->read_pixels(), 17, 8, 8)};
    };
    // A quarter second in: the first frame, red. Half a second on: blue. Then red again.
    std::array<Color, 2> now = colours();
    CHECK(now[0] == f.red);
    CHECK(now[1].r > 0);
    CHECK(now[1].b == 0);
    static_cast<void>(scene2d::animate(*w, 0.5));
    now = colours();
    CHECK(now[0] == f.blue);
    CHECK(now[1].r == 0);
    CHECK(now[1].b > 0);
    static_cast<void>(scene2d::animate(*w, 0.5));
    now = colours();
    CHECK(now[0] == f.red);
    CHECK(now[1].b == 0);
}

TEST_CASE("Diorama rays go through the image to the ground") {
    scene2d::DioramaCamera camera;
    camera.x = 4.0;
    camera.y = 6.0;
    camera.pitch = 40.0;
    camera.distance = 10.0;
    camera.fov = 30.0;
    const float aspect = 16.0F / 9.0F;
    // Through the middle of the image: the point looked at.
    const std::optional<std::array<double, 2>> centre =
        scene2d::ground_hit(scene2d::diorama_ray(camera, aspect, 0.0, 0.0));
    REQUIRE(centre.has_value());
    CHECK(centre.value_or(std::array<double, 2>{})[0] == doctest::Approx(4.0).epsilon(1e-5));
    CHECK(centre.value_or(std::array<double, 2>{})[1] == doctest::Approx(6.0).epsilon(1e-5));
    // Anywhere on the image: the ground point hit projects back to the same image point.
    const render::Mat4 vp = scene2d::diorama_view_projection(camera, aspect);
    for (const double nx : {-0.9, -0.3, 0.0, 0.5, 0.95}) {
        for (const double ny : {-0.9, -0.2, 0.4, 0.8}) {
            CAPTURE(nx);
            CAPTURE(ny);
            const auto hit = scene2d::ground_hit(scene2d::diorama_ray(camera, aspect, nx, ny));
            REQUIRE(hit.has_value());
            const auto p = hit.value_or(std::array<double, 2>{});
            const auto clip = vp.transform(
                render::Vec3{static_cast<float>(p[0]), 0.0F, static_cast<float>(p[1])});
            CHECK(clip[0] / clip[3] == doctest::Approx(nx).epsilon(1e-4));
            CHECK(clip[1] / clip[3] == doctest::Approx(ny).epsilon(1e-4));
        }
    }
    // Lower on the screen is nearer the camera, south of the point.
    const auto near = scene2d::ground_hit(scene2d::diorama_ray(camera, aspect, 0.0, -0.8));
    CHECK(near.value_or(std::array<double, 2>{})[1] > 6.0);
    // A plane above the eye, or a ray that never comes down, is not hit.
    CHECK_FALSE(scene2d::ground_hit(scene2d::diorama_ray(camera, aspect, 0.0, 0.0), 100.0));
    CHECK_FALSE(scene2d::ground_hit(scene2d::Ray{render::Vec3{0, 1, 0}, render::Vec3{1, 0, 0}}));
}

TEST_CASE("Tile heights: read and written, each tile's own, the ground under sprites") {
    const std::vector<std::vector<double>> h = scene2d::parse_heights("1,,2.5\n\n0.25,x,99\n\n");
    REQUIRE(h.size() == 3);
    CHECK(h[0][0] == 1.0);
    CHECK(std::isnan(h[0][1]));
    CHECK(h[0][2] == 2.5);
    CHECK(h[1].size() == 1);    // an empty row
    CHECK(std::isnan(h[2][1])); // not a number: empty
    CHECK(h[2][2] == 16.0);     // kept within 16
    CHECK(scene2d::format_heights(h) == "1,,2.5\n\n0.25,,16");
    CHECK(scene2d::parse_heights(scene2d::format_heights(h)).size() == 3);

    scene2d::Tilemap map;
    map.tiles = "0,0,0\n0,0,0";
    map.height = 1.0;
    map.heights = "2,,0.5";
    CHECK(map.height_at(0, 0) == 2.0);
    CHECK(map.height_at(1, 0) == 1.0); // empty: the map's
    CHECK(map.height_at(2, 0) == 0.5);
    CHECK(map.height_at(0, 1) == 1.0); // past the text
    map.heights = "4";
    CHECK(map.height_at(0, 0) == 4.0); // read again when the text changes

    // Sprites' shadows lie on the tile's own top.
    const Fixture f;
    const auto w = f.make_world();
    const world::Entity hill = w->create("hill");
    w->add<scene2d::Transform2D>(hill);
    scene2d::Tilemap* tiles = w->add<scene2d::Tilemap>(hill);
    tiles->tileset = f.red_image.to_string();
    tiles->tiles = "0,0";
    tiles->heights = "3,1";
    // 2 pixels per tile with 2 x 2 images: a tile is one unit.
    CHECK(scene2d::ground_top(*w, 0.5, 0.5, 2).value_or(-1.0F) == doctest::Approx(3.0));
    CHECK(scene2d::ground_top(*w, 1.5, 0.5, 2).value_or(-1.0F) == doctest::Approx(1.0));
}

TEST_CASE("Diorama blocks of different heights show sides down to their lower neighbours") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto w = f.make_world();
    const world::Entity hill = w->create("hill");
    w->add<scene2d::Transform2D>(hill);
    scene2d::Tilemap* map = w->add<scene2d::Tilemap>(hill);
    map->tileset = f.red_image.to_string();
    map->tiles = "0,0";
    map->height = 1.0;
    const auto target = device->create_render_target(17, 17);
    const auto renderer = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(renderer != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    scene2d::DioramaCamera camera;
    camera.ppu = 2;
    const scene2d::DioramaLight light;
    // Two blocks of one height: 2 tops and the 6 outer sides.
    CHECK(scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light)
              .drawn == 8);
    // The second one taller: its side over the first shows too.
    w->get<scene2d::Tilemap>(hill)->heights = "1,2";
    CHECK(scene2d::draw_diorama(*w, textures, *renderer, *target, camera, f.background, light)
              .drawn == 9);
}

TEST_CASE("Palette swaps: colours read, swapped once, and drawn recoloured") {
    const std::vector<scene2d::ColorSwap> swaps =
        scene2d::parse_recolor("#ff0000>#00ff00, 0000ff>#ffffff  nonsense #12>#345678");
    REQUIRE(swaps.size() == 2);
    CHECK(swaps[0].from == std::array<std::uint8_t, 3>{255, 0, 0});
    CHECK(swaps[1].to == std::array<std::uint8_t, 3>{255, 255, 255});
    CHECK(scene2d::recolor_text(swaps) == "#ff0000>#00ff00, #0000ff>#ffffff");
    CHECK(scene2d::parse_recolor(scene2d::recolor_text(swaps)).size() == 2);
    CHECK(scene2d::parse_recolor("").empty());

    // Red to blue and blue to red: a swap, each colour changed once.
    assets::Image image;
    image.width = 3;
    image.height = 1;
    image.rgba = {255, 0, 0, 255, 0, 0, 255, 128, 255, 0, 0, 0};
    CHECK(scene2d::image_colors(image).size() == 2); // the see-through red is not counted
    scene2d::recolor_image(image, scene2d::parse_recolor("#ff0000>#0000ff #0000ff>#ff0000"));
    CHECK(image.rgba == std::vector<std::uint8_t>{0, 0, 255, 255, 255, 0, 0, 128, 255, 0, 0, 0});

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    const scene2d::Camera2D camera{0.0, 0.0, 2, 1};
    const auto w = f.make_world();
    const world::Entity green = Fixture::place(*w, "Green slime", 0.0, 0.0, f.red_image);
    const world::Entity red = Fixture::place(*w, "Red slime", 1.5, 0.0, f.red_image);
    w->get<scene2d::Sprite>(green)->recolor = "#ff0000>#00ff00";
    scene2d::draw_world(*w, textures, *sprites, *target, camera, f.background);
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    CHECK(pixel(pixels, 8, 4, 3) == Color{0, 255, 0, 255}); // the same picture, recoloured
    CHECK(pixel(pixels, 8, 6, 3) == f.red);                 // and as it is
    CHECK(textures.size() == 1); // the plain one; the recoloured one is kept apart
    static_cast<void>(red);

    // Changing the picture forgets its recolourings too.
    assets::AssetChanges changes;
    changes.modified.push_back(f.red_image);
    textures.apply(changes);
    CHECK(textures.size() == 0);
    CHECK(textures.get(f.red_image, "#ff0000>#00ff00") != nullptr);
}

TEST_CASE("Classic 2D lights brighten what is drawn, in their colour, fading at the radius") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const Fixture f;
    const auto target = device->create_render_target(64, 64);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    scene2d::TextureCache textures(*device, f.db);
    REQUIRE(textures.light_falloff() != nullptr);
    // A quarter at noon, all of it in the dark.
    CHECK(scene2d::lights_2d_strength(1.0, 1.0, 1.0) == doctest::Approx(0.25));
    CHECK(scene2d::lights_2d_strength(0.0, 0.0, 0.0) == doctest::Approx(1.0));
    CHECK(scene2d::lights_2d_strength(2.0, 2.0, 2.0) == doctest::Approx(0.25));
    const scene2d::Camera2D camera{0.0, 0.0, 8, 1};
    const auto w = f.make_world();
    // A torch at the middle, its glow lifted one tile: at pixel (32, 24), 16 pixels round.
    const world::Entity torch = w->create("torch");
    w->add<scene2d::Transform2D>(torch);
    scene2d::Light* light = w->add<scene2d::Light>(torch);
    light->color_r = 255;
    light->color_g = 0;
    light->color_b = 0;
    light->radius = 2.0;
    light->height = 1.0;
    const render::Color grey{100, 100, 100, 255};

    sprites->begin(*target, grey);
    REQUIRE(sprites->end());
    CHECK(scene2d::draw_lights_2d(*w, textures, *sprites, *target, camera) == 1);
    std::vector<std::uint8_t> pixels = target->read_pixels();
    const Color middle = pixel(pixels, 64, 32, 24);
    CHECK(middle.r > 180);  // nearly doubled
    CHECK(middle.g == 100); // red light: green and blue as they were
    CHECK(middle.b == 100);
    const Color halfway = pixel(pixels, 64, 32, 32);
    CHECK(halfway.r > 100);
    CHECK(halfway.r < middle.r);
    CHECK(pixel(pixels, 64, 32, 42) == grey); // past its radius
    CHECK(pixel(pixels, 64, 2, 2) == grey);

    // Brighter than 1: brighter still, to white; switched off: nothing.
    light->intensity = 3.0;
    sprites->begin(*target, grey);
    REQUIRE(sprites->end());
    scene2d::draw_lights_2d(*w, textures, *sprites, *target, camera);
    pixels = target->read_pixels();
    CHECK(pixel(pixels, 64, 32, 24).r == 255);
    CHECK(pixel(pixels, 64, 32, 32).r > halfway.r);
    light->enabled = false;
    sprites->begin(*target, grey);
    REQUIRE(sprites->end());
    CHECK(scene2d::draw_lights_2d(*w, textures, *sprites, *target, camera) == 0);
    CHECK(pixel(target->read_pixels(), 64, 32, 24) == grey);
}

TEST_CASE("Tile shapes: half tiles block half their square, and some tiles none") {
    std::string error;
    using Shape = scene2d::TileShape;
    CHECK(scene2d::parse_tile_shapes("12 top, 13 Bottom,7 none", error) ==
          std::vector<std::pair<std::int32_t, Shape>>{
              {12, Shape::Top}, {13, Shape::Bottom}, {7, Shape::None}});
    CHECK(error.empty());
    CHECK(scene2d::parse_tile_shapes("5 sideways, x top, 2 left", error) ==
          std::vector<std::pair<std::int32_t, Shape>>{{2, Shape::Left}});
    CHECK(error.find("5 sideways") != std::string::npos);
    CHECK_FALSE(scene2d::tile_shape_box(Shape::None, 1.0).has_value());
    CHECK(scene2d::tile_shape_box(Shape::Right, 2.0)->left == 1.0);

    reflect::Registry registry;
    scene2d::register_types(registry);
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    // One row: a full tile (0), a top half (1), a bottom half (2), a tile that blocks
    // nothing (3), each 1 unit square, at the origin.
    const world::Entity map = w.create("ledges");
    w.add<scene2d::Transform2D>(map);
    scene2d::Tilemap* tiles = w.add<scene2d::Tilemap>(map);
    tiles->tiles = "0,1,2,3";
    tiles->solid = true;
    tiles->shapes = "1 top, 2 bottom, 3 none";
    constexpr std::uint32_t ppu = 16;
    using scene2d::Axis;
    // Walking up into each from below (a box 0.5 wide, its top at 2): the full tile stops
    // it at 1, the top half at 0.5, the bottom half at 1, the "none" tile not at all.
    const auto up_into = [&](double x) {
        return scene2d::sweep(w, scene2d::Box{x + 0.25, 2.0, x + 0.75, 2.5}, Axis::Y, -3.0, ppu);
    };
    CHECK(up_into(0.0) == doctest::Approx(-1.0));
    CHECK(up_into(1.0) == doctest::Approx(-1.5));
    CHECK(up_into(2.0) == doctest::Approx(-1.0));
    CHECK(up_into(3.0) == doctest::Approx(-3.0));
    // Falling from above onto the bottom half: stopped half a tile down.
    CHECK(scene2d::sweep(w, scene2d::Box{2.25, -1.0, 2.75, -0.5}, Axis::Y, 2.0, ppu) ==
          doctest::Approx(1.0));
    // Standing in the open half of a tile is allowed; in the blocked half, not.
    CHECK_FALSE(scene2d::solid_at(w, scene2d::Box{1.25, 0.6, 1.75, 0.9}, ppu));
    CHECK(scene2d::solid_at(w, scene2d::Box{1.25, 0.1, 1.75, 0.4}, ppu));
    CHECK_FALSE(scene2d::solid_at(w, scene2d::Box{3.1, 0.1, 3.9, 0.9}, ppu));
}
