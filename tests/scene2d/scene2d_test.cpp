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
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
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
}
