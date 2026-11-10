// End-to-end: a project folder on disk (settings, a PNG, a scene and a Rules asset) opened
// by the runtime and played with recorded input. The drawing test needs a GPU; the others
// run without graphics, as servers and tools do.

#include "engine/game/logic_world.h"
#include "engine/game/runtime.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
/// at the origin, a rock that does not, a torch with an animation that plays once, and
/// Rules that log on start, on Interact, and when the torch's animation finishes.
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
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        w.add<scene2d::Sprite>(hero)->image = hero_image.to_string();
        w.add<game::TopDownMover>(hero);
        const world::Entity rock = w.create("rock");
        w.add<scene2d::Transform2D>(rock);
        w.add<scene2d::Sprite>(rock)->image = rock_image.to_string();
        // A torch far away, whose three-frame animation plays once in 0.3 s.
        const world::Entity torch = w.create("torch");
        w.add<scene2d::Transform2D>(torch)->x = 100.0;
        w.add<scene2d::Sprite>(torch)->image = rock_image.to_string();
        scene2d::SpriteAnimation* flame = w.add<scene2d::SpriteAnimation>(torch);
        flame->frame_count = 3;
        flame->frames_per_second = 10.0;
        flame->loop = false;
        // An elder to talk to, below the start, and a doorway to the left.
        const world::Entity elder = w.create("elder");
        w.add<scene2d::Transform2D>(elder)->y = 3.0;
        w.add<game::Interactable>(elder);
        const world::Entity door = w.create("door");
        w.add<scene2d::Transform2D>(door)->x = -5.0;
        w.add<game::Trigger>(door);
        write_json(folder.path / "assets/maps/start.dscene", serialize::scene_to_json(w));

        logic::LogicAsset rules;
        rules.name = "main";
        rules.variables.push_back(logic::Variable{"visits", reflect::ValueKind::Int, "",
                                                  reflect::Value{std::int64_t{0}}});
        // Cancel saves in slot 2 and Menu loads it, as a pause menu would.
        const auto pressed = [](const char* action) {
            return ir::compare(logic::CompareOp::Equal, ir::event_value("action"), text(action));
        };
        rules.rules.push_back(logic::Rule{logic::BlockId{20},
                                          "input",
                                          "pressed",
                                          {pressed("cancel")},
                                          {ir::do_action(21, "game", "save", {whole(2)})},
                                          {}});
        rules.rules.push_back(logic::Rule{logic::BlockId{22},
                                          "input",
                                          "pressed",
                                          {pressed("menu")},
                                          {ir::do_action(23, "game", "load", {whole(2)})},
                                          {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1}, "", "game_start", {}, {ir::log(2, text("start"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{3},
            "input",
            "pressed",
            {ir::compare(logic::CompareOp::Equal, ir::event_value("action"), text("interact"))},
            {ir::log(4, text("hello"))},
            {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{5}, "torch", "finished", {}, {ir::log(6, text("torch done"))}, {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{7},
                        "elder",
                        "interacted",
                        {},
                        {ir::log(8, text("talk")),
                         ir::set_variable(13, "visits",
                                          ir::arithmetic(logic::ArithmeticOp::Add,
                                                         ir::variable("visits"), whole(1)))},
                        {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{9}, "door", "entered", {}, {ir::log(10, ir::event_value("who"))}, {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{11}, "door", "exited", {}, {ir::log(12, text("bye"))}, {}});
        write_json(folder.path / "assets/logic/main.drules", serialize::logic_to_json(rules));
    }

    static logic::Expr text(const char* value) {
        return ir::literal(reflect::Value{std::string(value)});
    }

    static logic::Expr whole(std::int64_t value) { return ir::literal(reflect::Value{value}); }

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

    /// Opens the project; saves go to a "saves" folder inside it unless the options say.
    [[nodiscard]] std::unique_ptr<game::Runtime> open(game::RuntimeOptions options) const {
        if (options.save_folder.empty()) {
            options.save_folder = folder.path / "saves";
        }
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
    // The torch's animation ended at step 17 (the 18th step: 0.3 s), after Interact at
    // step 10.
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "torch done"});

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

TEST_CASE("Walls stop the hero, who slides along them") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(60, key(input::Key::Down), true);
    const auto runtime = project.open(no_graphics(&replay));

    // A wall one tile thick, from x = 3, and from y = -2 to 2.
    world::World& w = runtime->world();
    const world::Entity walls = w.create("walls");
    w.add<scene2d::Transform2D>(walls)->x = 3.0;
    w.get<scene2d::Transform2D>(walls)->y = -2.0;
    scene2d::Tilemap* map = w.add<scene2d::Tilemap>(walls);
    map->tiles = "0\n0\n0\n0";
    map->solid = true;

    // One second right at 4 tiles per second would reach x = 4. The hitbox is 0.75 wide,
    // so the hero stops with its right edge on the wall: x = 3 - 0.375.
    for (int i = 0; i < 60; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(2.625));
    CHECK(transform_of(*runtime, "hero").y == 0.0);

    // Right and down: x stays against the wall while y moves at the diagonal speed.
    for (int i = 0; i < 30; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(2.625));
    CHECK(transform_of(*runtime, "hero").y == doctest::Approx(0.5 * 4.0 / std::sqrt(2.0)));

    const reflect::TypeInfo* mover = runtime->world().registry().find_type<game::TopDownMover>();
    REQUIRE(mover != nullptr);
    CHECK(mover->find_property("hitbox_width")->unit == reflect::Unit::Tiles);
    const scene2d::Box box = game::hitbox(game::TopDownMover{}, 1.0, 2.0);
    CHECK(box.left == 0.625);
    CHECK(box.top == 1.5);
    CHECK(box.right == 1.375);
    CHECK(box.bottom == 2.0);
}

TEST_CASE("Characters face the way they walk") {
    using game::Facing;
    using game::facing_for;
    CHECK(facing_for(Facing::Down, 0.0, 0.0) == Facing::Down); // standing: unchanged
    CHECK(facing_for(Facing::Down, 1.0, 0.0) == Facing::Right);
    CHECK(facing_for(Facing::Down, -1.0, 0.0) == Facing::Left);
    CHECK(facing_for(Facing::Left, 0.0, -1.0) == Facing::Up);
    CHECK(facing_for(Facing::Up, 0.0, 1.0) == Facing::Down);
    // Diagonals keep a matching facing, else turn horizontally.
    CHECK(facing_for(Facing::Up, 1.0, -1.0) == Facing::Up);
    CHECK(facing_for(Facing::Right, 1.0, -1.0) == Facing::Right);
    CHECK(facing_for(Facing::Down, 1.0, -1.0) == Facing::Right);
    CHECK(facing_for(Facing::Left, 1.0, 1.0) == Facing::Right);
}

TEST_CASE("Walk animations cycle the facing row and stand still on the standing frame") {
    game::TopDownMover mover;
    game::WalkAnimation walk; // 3 frames per direction, 6 per second, back and forth
    scene2d::Sprite sprite;
    const auto frames = [&](int steps) {
        std::vector<std::uint32_t> seen;
        for (int i = 0; i < steps; ++i) {
            game::update_walk(mover, walk, sprite, 1.0 / 60.0);
            seen.push_back(sprite.frame);
        }
        return seen;
    };
    // Standing, facing down (row 0): the middle frame.
    CHECK(frames(1) == std::vector<std::uint32_t>{1});
    // Walking right (row 2, frames 6 to 8): a new frame every 10 steps, 6, 7, 8, 7, 6, ...
    mover.facing = game::Facing::Right;
    mover.moving = true;
    std::vector<std::uint32_t> walked = frames(50);
    CHECK(walked[0] == 6);
    CHECK(walked[8] == 6);
    CHECK(walked[9] == 7);
    CHECK(walked[19] == 8);
    CHECK(walked[29] == 7);
    CHECK(walked[39] == 6);
    CHECK(walked[49] == 7);
    // Stopping shows the standing frame of the same row and resets the cycle.
    mover.moving = false;
    CHECK(frames(1) == std::vector<std::uint32_t>{7});
    CHECK(walk.elapsed == 0.0);
    // Starting over instead of back and forth; up is row 3.
    walk.ping_pong = false;
    mover.facing = game::Facing::Up;
    mover.moving = true;
    walked = frames(40);
    CHECK(walked[9] == 10);
    CHECK(walked[19] == 11);
    CHECK(walked[29] == 9);
    // Out-of-range settings are clamped.
    walk.standing_frame = 99;
    walk.frames_per_direction = 0;
    mover.moving = false;
    CHECK(frames(1) == std::vector<std::uint32_t>{3}); // one frame per row: row 3, frame 0
}

TEST_CASE("The runtime turns and animates a walking hero") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Left), true);
    replay.record(40, key(input::Key::Left), false);
    const auto runtime = project.open(no_graphics(&replay));
    world::World& w = runtime->world();
    const world::Entity hero = w.find_named("hero").at(0);
    REQUIRE(w.add<game::WalkAnimation>(hero) != nullptr);

    for (int i = 0; i < 25; ++i) {
        runtime->step();
    }
    const game::TopDownMover* mover = w.get<game::TopDownMover>(hero);
    CHECK(mover->facing == game::Facing::Left);
    CHECK(mover->moving);
    CHECK(w.get<scene2d::Sprite>(hero)->frame == 5); // row 1, third frame (steps 20-29)
    for (int i = 0; i < 20; ++i) {
        runtime->step();
    }
    CHECK_FALSE(w.get<game::TopDownMover>(hero)->moving);
    CHECK(w.get<game::TopDownMover>(hero)->facing == game::Facing::Left);
    CHECK(w.get<scene2d::Sprite>(hero)->frame == 4); // standing frame of row 1

    // A disabled mover stops walking in place.
    w.get<game::TopDownMover>(hero)->enabled = false;
    runtime->step();
    CHECK_FALSE(w.get<game::TopDownMover>(hero)->moving);
}

TEST_CASE("Talking to people and walking through doorways runs their Rules") {
    const Project project;
    input::Recording replay;
    replay.record(1, key(input::Key::Z), true); // Interact, facing down at the elder
    replay.record(2, key(input::Key::Z), false);
    replay.record(5, key(input::Key::Left), true);
    const auto runtime = project.open(no_graphics(&replay));
    const world::World& w = runtime->world();
    const world::Entity hero = w.find_named("hero").at(0);
    // Just above the elder: the reach (0.5 below the hitbox) overlaps its 1 x 1 box.
    w.get<scene2d::Transform2D>(hero)->y = 1.75;
    for (int i = 0; i < 5; ++i) {
        runtime->step();
    }
    // The input Rule for Interact runs first, then the elder's.
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "talk"});

    // Walking left from x = 0: the hitbox reaches the doorway (x -5.5 to -4.5) after
    // 4.125 tiles, and leaves it after 5.875 tiles, at 4 tiles per second.
    w.get<scene2d::Transform2D>(hero)->y = 0.0;
    for (int i = 0; i < 61; ++i) {
        runtime->step(); // to x = -4.0667: not yet inside
    }
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "talk", "torch done"});
    for (int i = 0; i < 2; ++i) {
        runtime->step(); // to x = -4.2
    }
    CHECK(runtime->log() ==
          std::vector<std::string>{"start", "hello", "talk", "torch done", "hero"});
    for (int i = 0; i < 30; ++i) {
        runtime->step(); // to x = -6.2
    }
    CHECK(runtime->log() ==
          std::vector<std::string>{"start", "hello", "talk", "torch done", "hero", "bye"});
    CHECK(runtime->vm().errors().empty());
}

TEST_CASE("Save files are written whole or not at all") {
    const TempFolder folder;
    CHECK(game::save_slot_file(folder.path, 7) == folder.path / "slot-07.sparxel-save");
    CHECK(game::save_slot_file(folder.path, 99).filename() == "slot-99.sparxel-save");
    CHECK(game::save_slot_file(folder.path, 0).empty());
    CHECK(game::save_slot_file(folder.path, 100).empty());

    std::string error;
    const fs::path file = folder.path / "deep" / "er" / "slot.json";
    REQUIRE(game::write_file_atomically(file, "first", error));  // creates the folders
    REQUIRE(game::write_file_atomically(file, "second", error)); // replaces the file
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "second");
    CHECK_FALSE(fs::exists(folder.path / "deep" / "er" / "slot.json.tmp"));
    // A folder that cannot be made (a file is in the way) fails with a message.
    CHECK_FALSE(game::write_file_atomically(file / "inside.json", "x", error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("A saved game restores the world, the variables and the step") {
    const Project project;
    const auto runtime = project.open(no_graphics(nullptr));
    world::World& w = runtime->world();
    const auto hero = [&w] { return w.get<scene2d::Transform2D>(w.find_named("hero").at(0)); };
    for (int i = 0; i < 3; ++i) {
        runtime->step();
    }
    hero()->x = 3.0;
    REQUIRE(runtime->vm().set_variable("main", "visits", reflect::Value{std::int64_t{2}}));
    const serialize::Json saved = runtime->save();
    CHECK(saved["format"] == "sparxel.save");
    CHECK(saved["game"] == "Test");
    CHECK(saved["step"] == 3);
    CHECK(saved["variables"]["main"]["visits"] == 2);

    // Change everything, then load.
    hero()->x = 7.0;
    w.destroy(w.find_named("rock").at(0));
    REQUIRE(runtime->vm().set_variable("main", "visits", reflect::Value{std::int64_t{9}}));
    runtime->step();
    std::vector<serialize::Issue> issues;
    REQUIRE(runtime->load(saved, issues));
    INFO(issues_text(issues));
    CHECK(issues.empty());
    CHECK(hero()->x == 3.0);
    CHECK(w.find_named("rock").size() == 1);
    CHECK(runtime->step_count() == 3);
    CHECK(runtime->vm().variables("main") == std::vector<std::pair<std::string, reflect::Value>>{
                                                 {"visits", reflect::Value{std::int64_t{2}}}});
    CHECK(runtime->save() == saved); // an exact round trip

    // Logic reaches the loaded entities: talking to the elder runs its Rule, which counts
    // a visit from the restored value.
    const std::array<reflect::Value, 1> by{reflect::Value{std::string("hero")}};
    CHECK(game::emit(runtime->vm(), w, w.find_named("elder").at(0), "interacted", by));
    CHECK(runtime->log().back() == "talk");
    CHECK(runtime->vm().variables("main")[0].second == reflect::Value{std::int64_t{3}});

    // Unusable saves change nothing.
    const auto refused = [&](const serialize::Json& bad, const char* path) {
        std::vector<serialize::Issue> problems;
        hero()->x = 5.0;
        CHECK_FALSE(runtime->load(bad, problems));
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].path == path);
        CHECK(hero()->x == 5.0);
    };
    serialize::Json bad = saved;
    bad["format"] = "sparxel.scene";
    refused(bad, "/format");
    bad = saved;
    bad["version"] = game::save_version + 1;
    refused(bad, "/version");
    bad = saved;
    bad["game"] = "Another Game";
    refused(bad, "/game");
    bad = saved;
    bad["world"] = serialize::Json::array();
    hero()->x = 5.0;
    issues.clear();
    CHECK_FALSE(runtime->load(bad, issues));
    CHECK_FALSE(issues.empty());
    CHECK(hero()->x == 5.0);

    // A variable that no longer exists is reported; the rest still loads.
    bad = saved;
    bad["variables"]["main"]["ghost"] = 1;
    issues.clear();
    CHECK(runtime->load(bad, issues));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "/variables/main/ghost");
    CHECK(hero()->x == 3.0);
}

TEST_CASE("Logic saves and loads slots at the end of the step") {
    const Project project;
    input::Recording replay;
    replay.record(1, key(input::Key::X), true); // Cancel: save in slot 2
    replay.record(2, key(input::Key::X), false);
    replay.record(5, key(input::Key::Right), true);
    replay.record(35, key(input::Key::Right), false);
    replay.record(40, key(input::Key::Escape), true); // Menu: load slot 2
    const auto runtime = project.open(no_graphics(&replay));
    const fs::path slot = game::save_slot_file(runtime->save_folder(), 2);
    CHECK(runtime->save_folder() == project.folder.path / "saves");

    for (int i = 0; i < 2; ++i) {
        runtime->step();
    }
    CHECK(fs::is_regular_file(slot));
    for (int i = 0; i < 38; ++i) {
        runtime->step(); // walks right 2 tiles
    }
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(2.0));
    runtime->step(); // step 40: Menu loads slot 2
    CHECK(transform_of(*runtime, "hero").x == 0.0);
    CHECK(runtime->step_count() == 2); // the step count when it was saved
    CHECK(runtime->save_problems().empty());

    // Problems are collected, not thrown: loading an empty slot, saving to slot 0.
    std::vector<serialize::Issue> issues;
    CHECK_FALSE(runtime->load_from_slot(5, issues));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message == "Slot 5 has no save.");
    std::string error;
    CHECK_FALSE(runtime->save_to_slot(0, error));
    CHECK(error == "There is no save slot 0; slots go from 1 to 99.");
    CHECK(runtime->save_to_slot(99, error));
    issues.clear();
    CHECK(runtime->load_from_slot(99, issues));
    CHECK(issues.empty());
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
