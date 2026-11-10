// End-to-end: a project folder on disk (settings, a PNG, a scene and a Rules asset) opened
// by the runtime and played with recorded input. The drawing test needs a GPU; the others
// run without graphics, as servers and tools do.

#include "engine/game/runtime.h"

#include <doctest/doctest.h>

#include <cmath>
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
namespace game = engine::game;
namespace input = engine::input;
namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace render = engine::render;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace world = engine::world;
namespace ir = engine::logic::ir;
namespace fs = std::filesystem;
using engine::Guid;

namespace {

struct TempFolder {
    TempFolder() {
        path = fs::temp_directory_path() / ("sparxel-runtime-" + Guid::generate().to_string());
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

void write_bytes(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

void write_json(const fs::path& file, const serialize::Json& json) {
    write_bytes(file, serialize::to_text(json));
}

std::string issues_text(const std::vector<serialize::Issue>& issues) {
    std::string text;
    for (const serialize::Issue& issue : issues) {
        text += issue.path + ": " + issue.message + "\n";
    }
    return text;
}

const render::Color hero_colour{250, 200, 40, 255};
const render::Color rock_colour{90, 90, 100, 255};
const render::Color background{20, 24, 40, 255};

/// A small project: a 32 x 32 game at 8 pixels per tile, a hero that walks (2 x 2 pixels)
/// at the origin, a rock that does not, and Rules that log on start and on Interact.
struct Project {
    Project() {
        game::ProjectSettings settings;
        settings.name = "Test";
        settings.ppu = 8;
        settings.width = 32;
        settings.height = 32;
        settings.start_scene = "maps/start.dscene";
        settings.background = background;
        write_json(folder.path / game::project_file, game::project_to_json(settings));

        // The images, imported once to learn their GUIDs (kept in their .meta files).
        write_png(folder.path / "assets/art/hero.png", hero_colour);
        write_png(folder.path / "assets/art/rock.png", rock_colour);
        assets::AssetDatabase db(folder.path / "assets");
        db.refresh();
        REQUIRE(db.issues().empty());
        const Guid hero_image = db.find("art/hero.png")->guid;
        const Guid rock_image = db.find("art/rock.png")->guid;

        // The scene, built in a world and saved as the runtime will load it.
        reflect::Registry registry;
        game::register_types(registry);
        REQUIRE(registry.errors().empty());
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(w.register_component<game::TopDownMover>());
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        w.add<scene2d::Sprite>(hero)->image = hero_image.to_string();
        w.add<game::TopDownMover>(hero);
        const world::Entity rock = w.create("rock");
        w.add<scene2d::Transform2D>(rock);
        w.add<scene2d::Sprite>(rock)->image = rock_image.to_string();
        write_json(folder.path / "assets/maps/start.dscene", serialize::scene_to_json(w));

        logic::LogicAsset rules;
        rules.name = "main";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1}, "", "game_start", {}, {ir::log(2, text("start"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{3},
            "input",
            "pressed",
            {ir::compare(logic::CompareOp::Equal, ir::event_value("action"), text("interact"))},
            {ir::log(4, text("hello"))},
            {}});
        write_json(folder.path / "assets/logic/main.drules", serialize::logic_to_json(rules));
    }

    static logic::Expr text(const char* value) {
        return ir::literal(reflect::Value{std::string(value)});
    }

    /// Writes a solid-colour 2 x 2 PNG.
    static void write_png(const fs::path& file, render::Color c) {
        assets::Image image;
        image.width = 2;
        image.height = 2;
        for (int i = 0; i < 4; ++i) {
            image.rgba.insert(image.rgba.end(), {c.r, c.g, c.b, c.a});
        }
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        REQUIRE_FALSE(png.empty());
        write_bytes(file, std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
    }

    [[nodiscard]] std::unique_ptr<game::Runtime> open(const game::RuntimeOptions& options) const {
        std::vector<serialize::Issue> issues;
        std::unique_ptr<game::Runtime> runtime = game::Runtime::open(folder.path, options, issues);
        INFO(issues_text(issues));
        CHECK(issues.empty());
        REQUIRE(runtime != nullptr);
        return runtime;
    }

    TempFolder folder;
};

game::RuntimeOptions no_graphics(const input::Recording* replay) {
    game::RuntimeOptions options;
    options.graphics = false;
    options.replay = replay;
    return options;
}

input::Binding key(input::Key k) {
    return input::Binding::key(k);
}

const scene2d::Transform2D& transform_of(game::Runtime& runtime, std::string_view name) {
    const std::vector<world::Entity> found = runtime.world().find_named(name);
    REQUIRE(found.size() == 1);
    const scene2d::Transform2D* transform = runtime.world().get<scene2d::Transform2D>(found[0]);
    REQUIRE(transform != nullptr);
    return *transform;
}

render::Color pixel(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t x,
                    std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * width + x) * 4;
    return render::Color{pixels.at(i), pixels.at(i + 1), pixels.at(i + 2), pixels.at(i + 3)};
}

} // namespace

TEST_CASE("Project settings are read, checked and written back") {
    const TempFolder folder;
    std::vector<serialize::Issue> issues;
    CHECK_FALSE(game::read_project(folder.path, issues).has_value());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "project.sparxel.json");

    // A round trip keeps every setting.
    game::ProjectSettings settings;
    settings.name = "Lantern Vale";
    settings.ppu = 32;
    settings.width = 480;
    settings.height = 270;
    settings.start_scene = "maps/town.dscene";
    settings.background = render::Color{1, 2, 3, 255};
    write_json(folder.path / game::project_file, game::project_to_json(settings));
    issues.clear();
    const std::optional<game::ProjectSettings> read = game::read_project(folder.path, issues);
    INFO(issues_text(issues));
    CHECK(issues.empty());
    REQUIRE(read.has_value());
    const game::ProjectSettings got = read.value_or(game::ProjectSettings{});
    CHECK(got.name == "Lantern Vale");
    CHECK(got.ppu == 32);
    CHECK(got.width == 480);
    CHECK(got.height == 270);
    CHECK(got.start_scene == "maps/town.dscene");
    CHECK(got.background == render::Color{1, 2, 3, 255});

    // Out-of-range values and a wrong style refuse the project; unknown entries do not.
    serialize::Json json = game::project_to_json(settings);
    json["ppu"] = 4;
    json["background"] = serialize::Json::array({1, 2});
    json["render_style"] = "diorama";
    json["shiny"] = true;
    write_json(folder.path / game::project_file, json);
    issues.clear();
    CHECK_FALSE(game::read_project(folder.path, issues).has_value());
    std::vector<std::string> paths;
    paths.reserve(issues.size());
    for (const serialize::Issue& issue : issues) {
        paths.push_back(issue.path);
    }
    CHECK(paths == std::vector<std::string>{
                       "project.sparxel.json/ppu", "project.sparxel.json/background",
                       "project.sparxel.json/shiny", "project.sparxel.json/render_style"});

    json = game::project_to_json(settings);
    json["shiny"] = true;
    write_json(folder.path / game::project_file, json);
    issues.clear();
    CHECK(game::read_project(folder.path, issues).has_value());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message == "Unknown entry 'shiny'. It was ignored.");

    // Another file format, or a newer version, is refused.
    json = game::project_to_json(settings);
    json["format"] = "sparxel.scene";
    write_json(folder.path / game::project_file, json);
    issues.clear();
    CHECK_FALSE(game::read_project(folder.path, issues).has_value());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "project.sparxel.json/format");
    json = game::project_to_json(settings);
    json["version"] = game::project_version + 1;
    write_json(folder.path / game::project_file, json);
    issues.clear();
    CHECK_FALSE(game::read_project(folder.path, issues).has_value());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "project.sparxel.json/version");
}

TEST_CASE("Recorded input walks the hero and runs Rules, step for step") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true); // held for steps 0 to 29
    replay.record(5, key(input::Key::X), true);     // Cancel: not what the Rule listens for
    replay.record(6, key(input::Key::X), false);
    replay.record(10, key(input::Key::Z), true); // Interact
    replay.record(11, key(input::Key::Z), false);
    replay.record(30, key(input::Key::Right), false);
    const auto runtime = project.open(no_graphics(&replay));

    CHECK(runtime->log() == std::vector<std::string>{"start"});
    CHECK(runtime->settings().ppu == 8);
    CHECK(runtime->target() == nullptr);
    CHECK_FALSE(runtime->draw());

    // Half a second at 60 steps per second, in uneven frames of one and two steps.
    for (int i = 0; i < 10; ++i) {
        CHECK(runtime->frame(1.0 / 60.0));
        CHECK(runtime->frame(1.0 / 30.0));
    }
    CHECK(runtime->step_count() == 30);
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(2.0)); // 4 tiles/s for 0.5 s
    CHECK(transform_of(*runtime, "hero").y == 0.0);
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello"});

    // Released at step 30: no further movement.
    for (int i = 0; i < 10; ++i) {
        runtime->step();
    }
    CHECK(runtime->step_count() == 40);
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(2.0));
    CHECK(transform_of(*runtime, "rock").x == 0.0);
    CHECK(runtime->vm().errors().empty());
}

TEST_CASE("Diagonals are as fast as straight lines, and a disabled mover stays put") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(0, key(input::Key::Down), true);
    const auto runtime = project.open(no_graphics(&replay));
    for (int i = 0; i < 60; ++i) {
        runtime->step();
    }
    // One second at 4 tiles per second, split evenly between x and y.
    const double each = 4.0 / std::sqrt(2.0);
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(each));
    CHECK(transform_of(*runtime, "hero").y == doctest::Approx(each));

    const world::Entity hero = runtime->world().find_named("hero").at(0);
    runtime->world().get<game::TopDownMover>(hero)->enabled = false;
    runtime->step();
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(each));
}

TEST_CASE("Problems are reported with the file they are in") {
    const Project project;
    std::vector<serialize::Issue> issues;

    // A logic asset that does not compile is left out; the game still runs.
    logic::LogicAsset bad;
    bad.name = "bad";
    bad.rules.push_back(logic::Rule{
        logic::BlockId{7}, "dragon", "roared", {}, {ir::log(8, Project::text("x"))}, {}});
    write_json(project.folder.path / "assets/logic/bad.drules", serialize::logic_to_json(bad));
    std::unique_ptr<game::Runtime> runtime =
        game::Runtime::open(project.folder.path, no_graphics(nullptr), issues);
    REQUIRE(runtime != nullptr);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "assets/logic/bad.drules#block-7");
    CHECK(runtime->log() == std::vector<std::string>{"start"});
    runtime.reset();

    // A start scene that is not there stops the game.
    fs::remove(project.folder.path / "assets/maps/start.dscene");
    issues.clear();
    CHECK(game::Runtime::open(project.folder.path, no_graphics(nullptr), issues) == nullptr);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.back().path == "project.sparxel.json/start_scene");
}

TEST_CASE("The game draws pixel-perfect, with the camera following the hero") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(15, key(input::Key::Right), false); // 0.25 s at 4 tiles/s: one tile
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);

    // At the start the hero stands on the rock and is drawn in front of it (same y; the
    // tie is broken by GUID, so only check that one of them covers the centre).
    REQUIRE(runtime->draw());
    std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    REQUIRE(pixels.size() == std::size_t{32} * 32 * 4);
    // The camera is on the hero, whose 2 x 2 image sits with its bottom centre on the
    // target's centre pixel (16, 16): pixels x 15..16, y 14..15.
    const render::Color centre = pixel(pixels, 32, 15, 14);
    CHECK((centre == hero_colour || centre == rock_colour));
    CHECK(pixel(pixels, 32, 14, 14) == background);
    CHECK(pixel(pixels, 32, 16, 16) == background);

    // After walking one tile (8 pixels) right, the hero is still centred and the rock is
    // 8 pixels to the left.
    for (int i = 0; i < 15; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->draw());
    pixels = runtime->target()->read_pixels();
    for (std::uint32_t y = 14; y <= 15; ++y) {
        for (std::uint32_t x = 15; x <= 16; ++x) {
            CHECK(pixel(pixels, 32, x, y) == hero_colour);
        }
        for (std::uint32_t x = 7; x <= 8; ++x) {
            CHECK(pixel(pixels, 32, x, y) == rock_colour);
        }
    }
    CHECK(pixel(pixels, 32, 9, 14) == background);
    CHECK(pixel(pixels, 32, 14, 14) == background);
    CHECK(pixel(pixels, 32, 15, 13) == background);
}
