// End-to-end: PNG files in an assets folder, a scene of entities that refer to them by
// GUID, saved to JSON and loaded again, rendered to pixels. Needs a GPU (lavapipe is
// enough); see tests/render/render_test.cpp for how a missing GPU is handled.

#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"

#include <doctest/doctest.h>

#include <chrono>
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

    std::unique_ptr<world::World> make_world() const {
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
