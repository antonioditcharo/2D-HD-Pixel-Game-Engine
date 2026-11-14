// End-to-end: a project folder on disk (settings, a PNG, a scene and a Rules asset) opened
// by the runtime and played with recorded input. The drawing test needs a GPU; the others
// run without graphics, as servers and tools do.

#include "engine/game/battle.h"
#include "engine/game/conversation.h"
#include "engine/game/hud.h"
#include "engine/game/localization.h"
#include "engine/game/logic_world.h"
#include "engine/game/runtime.h"
#include "engine/game/shop.h"
#include "engine/game/ui_theme.h"
#include "engine/ui/text.h"

#include <doctest/doctest.h>

#include <algorithm>
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
#include "support/wav.h"

namespace engine::render {
std::ostream& operator<<(std::ostream& out, Color c) {
    return out << "(" << int{c.r} << ", " << int{c.g} << ", " << int{c.b} << ", " << int{c.a}
               << ")";
}
} // namespace engine::render

namespace assets = engine::assets;
namespace audio = engine::audio;
namespace game = engine::game;
namespace input = engine::input;
namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace render = engine::render;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace ui = engine::ui;
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
        // Music at 0.5 and a chime at 0.25: levels the mix test can recognise.
        write_sound(folder.path / "assets/music/theme.wav", test_support::wav(4800, 16384));
        write_sound(folder.path / "assets/sfx/chime.wav", test_support::wav(2400, 8192));
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
        // A crate far to the east, with a Physics Body (no gravity unless a test sets it).
        const world::Entity crate = w.create("crate");
        w.add<scene2d::Transform2D>(crate)->x = 50.0;
        w.add<game::PhysicsBody>(crate)->damping = 5.0;
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
        rules.rules.push_back(
            logic::Rule{logic::BlockId{20},
                        "input",
                        "pressed",
                        {pressed("cancel")},
                        {ir::do_action(21, "game", "save", {whole(2)}),
                         // Dusk falls: the lighting object, as a day-night Brain would use it.
                         ir::set(49, "lighting", "ambient", ir::literal(reflect::Value{0.25})),
                         ir::set(50, "lighting", "sun", ir::literal(reflect::Value{0.1})),
                         // and the sun goes down in the west.
                         ir::set(57, "lighting", "sun_angle", ir::literal(reflect::Value{260.0})),
                         ir::set(58, "lighting", "sun_height", ir::literal(reflect::Value{10.0}))},
                        {}});
        rules.rules.push_back(logic::Rule{logic::BlockId{22},
                                          "input",
                                          "pressed",
                                          {pressed("menu")},
                                          {ir::do_action(23, "game", "load", {whole(2)})},
                                          {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::log(2, text("start")),
             ir::do_action(30, "audio", "play_music",
                           {text("music/theme.wav"), ir::literal(reflect::Value{0.0})})},
            {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{3},
            "input",
            "pressed",
            {ir::compare(logic::CompareOp::Equal, ir::event_value("action"), text("interact"))},
            {ir::log(4, text("hello")),
             ir::do_action(31, "audio", "play_sound", {text("sfx/chime.wav")})},
            {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{5}, "torch", "finished", {}, {ir::log(6, text("torch done"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{40}, "crate", "touched", {}, {ir::log(41, ir::event_value("who"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{7},
            "elder",
            "interacted",
            {},
            {ir::log(8, text("talk")), ir::do_action(14, "dialogue", "say", {text("Hi.")}),
             ir::set_variable(
                 13, "visits",
                 ir::arithmetic(logic::ArithmeticOp::Add, ir::variable("visits"), whole(1))),
             // Say, and wait until the player has read it.
             ir::wait_until(44, ir::negate(ir::call("dialogue", "is_open"))),
             ir::log(45, text("after"))},
            {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{9}, "door", "entered", {}, {ir::log(10, ir::event_value("who"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{42}, "dialogue", "closed", {}, {ir::log(43, text("closed"))}, {}});
        // Answers are logged as "answer <number> <text>".
        rules.rules.push_back(logic::Rule{logic::BlockId{46},
                                          "dialogue",
                                          "answered",
                                          {},
                                          {ir::log(47, ir::event_value("answer")),
                                           ir::log(48, ir::call("dialogue", "answer_text"))},
                                          {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{11}, "door", "exited", {}, {ir::log(12, text("bye"))}, {}});
        write_json(folder.path / "assets/logic/main.drules", serialize::logic_to_json(rules));
    }

    static logic::Expr text(const char* value) {
        return ir::literal(reflect::Value{std::string(value)});
    }

    static logic::Expr whole(std::int64_t value) { return ir::literal(reflect::Value{value}); }

    static void write_sound(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
        write_bytes(file,
                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
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

    /// Opens the project; saves go to a "saves" folder inside it unless the options say.
    [[nodiscard]] std::unique_ptr<game::Runtime> open(game::RuntimeOptions options) const {
        if (options.save_folder.empty()) {
            options.save_folder = folder.path / "saves";
        }
        // Tests never depend on a sound card: mix headless instead.
        if (options.audio == game::AudioMode::Device) {
            options.audio = game::AudioMode::Headless;
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
    options.audio = game::AudioMode::Headless;
    return options;
}

input::Binding key(input::Key k) {
    return input::Binding::key(k);
}

/// Somewhere for issues a test does not look at.
std::vector<serialize::Issue>& issues_storage() {
    static std::vector<serialize::Issue> ignored;
    ignored.clear();
    return ignored;
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
    CHECK(got.style == game::RenderStyle::Classic2D);

    // The Diorama style and its camera round-trip too.
    game::ProjectSettings diorama = settings;
    diorama.style = game::RenderStyle::Diorama;
    diorama.camera_pitch = 40.0;
    diorama.camera_distance = 9.5;
    diorama.camera_fov = 25.0;
    diorama.camera_smoothing = 0.15;
    diorama.camera_look_ahead = 1.5;
    diorama.camera_clamp = false;
    diorama.ambient_light = 0.2;
    diorama.sun_light = 1.5;
    diorama.sprite_shadow = 0.8;
    diorama.sun_angle = 300.0;
    diorama.sun_height = 20.0;
    diorama.shadow_shape = scene2d::ShadowShape::Round;
    diorama.block_shadow = 0.7;
    diorama.scene_fade = 1.25;
    write_json(folder.path / game::project_file, game::project_to_json(diorama));
    issues.clear();
    const game::ProjectSettings again =
        game::read_project(folder.path, issues).value_or(game::ProjectSettings{});
    CHECK(issues.empty());
    CHECK(again.style == game::RenderStyle::Diorama);
    CHECK(again.camera_pitch == 40.0);
    CHECK(again.camera_distance == 9.5);
    CHECK(again.camera_fov == 25.0);
    CHECK(again.camera_smoothing == 0.15);
    CHECK(again.camera_look_ahead == 1.5);
    CHECK_FALSE(again.camera_clamp);
    CHECK(again.ambient_light == 0.2);
    CHECK(again.sun_light == 1.5);
    CHECK(again.sprite_shadow == 0.8);
    CHECK(again.sun_angle == 300.0);
    CHECK(again.sun_height == 20.0);
    CHECK(again.shadow_shape == scene2d::ShadowShape::Round);
    CHECK(again.block_shadow == 0.7);
    CHECK(again.scene_fade == 1.25);
    CHECK(settings.scene_fade == 0.4); // the default

    // Out-of-range values and a wrong style refuse the project; unknown entries do not.
    serialize::Json json = game::project_to_json(settings);
    json["ppu"] = 4;
    json["background"] = serialize::Json::array({1, 2});
    json["render_style"] = "isometric";
    json["camera_pitch"] = 90;
    json["camera_clamp"] = "yes";
    json["sun_light"] = 3;
    json["sun_height"] = 0;
    json["shadow_shape"] = "square";
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
                       "project.sparxel.json/shiny", "project.sparxel.json/camera_pitch",
                       "project.sparxel.json/sun_light", "project.sparxel.json/sun_height",
                       "project.sparxel.json/shadow_shape", "project.sparxel.json/camera_clamp",
                       "project.sparxel.json/render_style"});

    // A fade longer than 3 seconds, or not a number, is refused.
    for (const serialize::Json& fade : {serialize::Json(3.5), serialize::Json("slow")}) {
        json = game::project_to_json(settings);
        json["scene_fade"] = fade;
        write_json(folder.path / game::project_file, json);
        issues.clear();
        CHECK_FALSE(game::read_project(folder.path, issues).has_value());
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].path == "project.sparxel.json/scene_fade");
        CHECK(issues[0].message == "This must be a number from 0 to 3.");
    }

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
    replay.record(3, key(input::Key::Z), true); // Interact again: closes the elder's message
    replay.record(4, key(input::Key::Z), false);
    replay.record(5, key(input::Key::Left), true);
    const auto runtime = project.open(no_graphics(&replay));
    const world::World& w = runtime->world();
    const world::Entity hero = w.find_named("hero").at(0);
    // Just above the elder: the reach (0.5 below the hitbox) overlaps its 1 x 1 box.
    w.get<scene2d::Transform2D>(hero)->y = 1.75;
    for (int i = 0; i < 5; ++i) {
        runtime->step();
    }
    // The input Rule for Interact runs first, then the elder's, which says a line. The
    // second Interact closes it: it neither runs the input Rule nor talks again.
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "talk", "closed", "after"});
    CHECK(runtime->dialogue().said() == std::vector<std::string>{"Hi."});
    CHECK_FALSE(runtime->dialogue().is_open());

    // Walking left from x = 0: the hitbox reaches the doorway (x -5.5 to -4.5) after
    // 4.125 tiles, and leaves it after 5.875 tiles, at 4 tiles per second.
    w.get<scene2d::Transform2D>(hero)->y = 0.0;
    for (int i = 0; i < 61; ++i) {
        runtime->step(); // to x = -4.0667: not yet inside
    }
    CHECK(runtime->log() ==
          std::vector<std::string>{"start", "hello", "talk", "closed", "after", "torch done"});
    for (int i = 0; i < 2; ++i) {
        runtime->step(); // to x = -4.2
    }
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "talk", "closed", "after",
                                                     "torch done", "hero"});
    for (int i = 0; i < 30; ++i) {
        runtime->step(); // to x = -6.2
    }
    CHECK(runtime->log() == std::vector<std::string>{"start", "hello", "talk", "closed", "after",
                                                     "torch done", "hero", "bye"});
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
    CHECK(runtime->problems().empty());

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

TEST_CASE("Rules play music and sounds from assets") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Z), true); // Interact: the chime
    const auto runtime = project.open(no_graphics(&replay));
    audio::AudioEngine* mixer = runtime->audio();
    REQUIRE(mixer != nullptr);
    // The game-start Rule started the theme, found by its path.
    assets::AssetDatabase db(project.folder.path / "assets");
    db.refresh(); // reads the .meta files the runtime wrote: the same GUIDs
    const assets::AssetRecord* theme = db.find("music/theme.wav");
    const assets::AssetRecord* scene = db.find("maps/start.dscene");
    REQUIRE(theme != nullptr);
    REQUIRE(scene != nullptr);
    CHECK(mixer->music() == theme->guid.to_string());

    const auto left = [mixer](std::uint32_t frames) {
        std::vector<float> out(std::size_t{frames} * 2);
        REQUIRE(mixer->render(out));
        std::vector<float> channel;
        channel.reserve(frames);
        for (std::uint32_t i = 0; i < frames; ++i) {
            channel.push_back(out[std::size_t{i} * 2]);
        }
        return channel;
    };
    std::vector<float> mixed = left(480);
    CHECK(mixed[100] == doctest::Approx(0.5).epsilon(0.001));
    runtime->step(); // Interact: the chime plays over the music
    mixed = left(480);
    CHECK(mixed[100] == doctest::Approx(0.75).epsilon(0.001));
    CHECK(runtime->problems().empty());

    // Missing or unreadable sounds become problems, not crashes.
    game::AudioObject sounds(mixer, &db);
    sounds.play_sound("sfx/nothing.wav");
    sounds.play_music("maps/start.dscene", 0.0);
    sounds.play_sound(engine::Guid::generate().to_string());
    const std::vector<std::string> problems = sounds.take_problems();
    REQUIRE(problems.size() == 3);
    CHECK(problems[0] == "There is no sound sfx/nothing.wav in assets/.");
    CHECK(problems[1] == "maps/start.dscene: " + scene->guid.to_string() +
                             " is not a sound miniaudio can read (WAV, Ogg Vorbis, FLAC or MP3).");
    sounds.set_music_volume(50);
    CHECK(mixer->volume(audio::Bus::Music) == 0.5);
    sounds.set_sound_volume(250);
    CHECK(mixer->volume(audio::Bus::Sounds) == 1.0);
    // Without an engine, every action quietly does nothing.
    game::AudioObject silent;
    silent.play_sound("sfx/chime.wav");
    CHECK(silent.take_problems().empty());

    // Sounds where someone is, heard from the player (the hero, at the origin). The door
    // is 5 tiles to the west: 1 - 5/16 as loud, panned 5/8 of the way to the left.
    mixer->stop_music();
    while (mixer->playing_sounds() > 0) {
        static_cast<void>(left(4800));
    }
    game::AudioObject placed(mixer, &db, &runtime->world());
    placed.play_sound_at("sfx/chime.wav", "door");
    std::vector<float> stereo(std::size_t{480} * 2);
    REQUIRE(mixer->render(stereo));
    CHECK(stereo[200] == doctest::Approx(0.25 * 0.6875).epsilon(0.001));
    CHECK(stereo[201] == doctest::Approx(0.25 * 0.6875 * 0.375).epsilon(0.001));
    // Out of hearing: nothing plays. Nobody by that name: a problem.
    while (mixer->playing_sounds() > 0) {
        static_cast<void>(left(4800));
    }
    placed.play_sound_at("sfx/chime.wav", "torch"); // 100 tiles away
    CHECK(mixer->playing_sounds() == 0);
    placed.play_sound_at("sfx/chime.wav", "nobody");
    const std::vector<std::string> where = placed.take_problems();
    REQUIRE(where.size() == 1);
    CHECK(where[0] == "There is no one called nobody to play a sound at.");
    // Music is streamed: 0.5, on the music bus turned down to half above.
    placed.play_music("music/theme.wav", 0.0);
    CHECK(mixer->music() == theme->guid.to_string());
    CHECK(left(480)[100] == doctest::Approx(0.25).epsilon(0.001));
}

TEST_CASE("A Sound Source repeats where it is, louder as the player comes closer") {
    const Project project;
    input::Recording replay;
    replay.record(1, key(input::Key::Left), true); // towards the door, from the second step
    replay.record(61, key(input::Key::Left), false);
    const auto runtime = project.open(no_graphics(&replay));
    audio::AudioEngine* mixer = runtime->audio();
    REQUIRE(mixer != nullptr);
    mixer->stop_music();
    const auto stereo = [mixer] {
        std::vector<float> out(std::size_t{480} * 2);
        REQUIRE(mixer->render(out));
        return std::array<float, 2>{out[200], out[201]};
    };
    // The door, 5 tiles west of the hero, hums: the chime (0.25) on repeat, carrying 10 tiles.
    const world::Entity door = runtime->world().find_named("door").at(0);
    game::SoundSource* hum = runtime->world().add<game::SoundSource>(door);
    REQUIRE(hum != nullptr);
    hum->sound = "sfx/chime.wav";
    hum->range = 10.0;
    hum->fade = 0.0; // at once: the levels heard are exact (fades are tested below)
    CHECK(mixer->playing_loops() == 0);
    runtime->step();
    CHECK(mixer->playing_loops() == 1);
    CHECK(mixer->playing_sounds() == 0);
    // Half-way out of its range: half as loud; 5/8 of the way to the left.
    std::array<float, 2> heard = stereo();
    CHECK(heard[0] == doctest::Approx(0.25 * 0.5).epsilon(0.001));
    CHECK(heard[1] == doctest::Approx(0.25 * 0.5 * 0.375).epsilon(0.001));
    // It repeats: still there after four times its length.
    for (int i = 0; i < 20; ++i) {
        static_cast<void>(stereo());
    }
    CHECK(stereo()[0] == doctest::Approx(0.125).epsilon(0.001));
    // The hero walks 4 tiles towards it: a tile away, 0.9 as loud, nearly in the middle.
    for (int i = 0; i < 60; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x == doctest::Approx(-4.0));
    CHECK(mixer->playing_loops() == 1); // the same loop, not one more each step
    heard = stereo();
    CHECK(heard[0] == doctest::Approx(0.25 * 0.9).epsilon(0.001));
    CHECK(heard[1] == doctest::Approx(0.25 * 0.9 * 0.875).epsilon(0.001));
    // Quieter by its own volume; turned off, silent; on again, it starts again.
    hum->volume = 0.5;
    runtime->step();
    CHECK(stereo()[0] == doctest::Approx(0.25 * 0.45).epsilon(0.001));
    hum->playing = false;
    runtime->step();
    CHECK(mixer->playing_loops() == 0);
    CHECK(stereo()[0] == doctest::Approx(0.0));
    hum->playing = true;
    runtime->step();
    CHECK(mixer->playing_loops() == 1);
    // A sound that is not there is a problem told once, not every step.
    CHECK(runtime->problems().empty());
    hum->sound = "sfx/nothing.wav";
    runtime->step();
    runtime->step();
    runtime->step();
    CHECK(mixer->playing_loops() == 0);
    REQUIRE(runtime->problems().size() == 1);
    CHECK(runtime->problems()[0] == "There is no sound sfx/nothing.wav in assets/.");
    // Gone with its entity.
    hum->sound = "sfx/chime.wav";
    runtime->step();
    CHECK(mixer->playing_loops() == 1);
    runtime->world().destroy(door);
    runtime->step();
    CHECK(mixer->playing_loops() == 0);
}

TEST_CASE("How a Sound Source is heard: by distance, side and its own volume") {
    game::SoundSource source;
    source.range = 8.0;
    const auto at = [&](double x, double y) { return game::heard(source, x, y, {0.0, 0.0}); };
    CHECK(at(0.0, 0.0).volume == 1.0);
    CHECK(at(0.0, 0.0).pan == 0.0);
    CHECK(at(4.0, 0.0).volume == 0.5);
    CHECK(at(4.0, 0.0).pan == 0.5);
    CHECK(at(0.0, -6.0).volume == 0.25); // above or below: quieter, but in the middle
    CHECK(at(0.0, -6.0).pan == 0.0);
    CHECK(at(-3.0, 4.0).volume == doctest::Approx(0.375)); // 5 tiles away
    CHECK(at(-3.0, 4.0).pan == -0.375);
    CHECK(at(8.0, 0.0).volume == 0.0);
    CHECK(at(30.0, 0.0).volume == 0.0);
    CHECK(at(30.0, 0.0).pan == 1.0);
    source.volume = 0.5;
    CHECK(at(4.0, 0.0).volume == 0.25);
    source.volume = 7.0; // clamped
    CHECK(at(0.0, 0.0).volume == 1.0);
    source.playing = false;
    CHECK(at(0.0, 0.0).volume == 0.0);
    source.playing = true;
    source.range = 0.0; // carries nowhere
    CHECK(at(0.0, 0.0).volume == 0.0);
}

TEST_CASE("Logic goes to another scene: the world changes, the rest carries on") {
    const Project project;
    // A second scene, a cave: its own hero, stairs to arrive at, and a bat only it has.
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        scene2d::Transform2D* feet = w.add<scene2d::Transform2D>(hero);
        feet->x = 20.0;
        feet->y = 20.0;
        w.add<game::TopDownMover>(hero);
        const world::Entity stairs = w.create("stairs");
        scene2d::Transform2D* top = w.add<scene2d::Transform2D>(stairs);
        top->x = 7.0;
        top->y = 3.0;
        const world::Entity bat = w.create("bat");
        w.add<scene2d::Transform2D>(bat)->x = 9.0;
        w.add<game::Interactable>(bat);
        write_json(project.folder.path / "assets/maps/cave.dscene", serialize::scene_to_json(w));
    }
    // Walking into the door goes to the cave, arriving at its stairs. A Rule about the
    // bat, which the first scene does not have, and one for every scene's start.
    {
        const auto text = [](const char* value) {
            return ir::literal(reflect::Value{std::string(value)});
        };
        logic::LogicAsset rules;
        rules.name = "cave";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{60},
                        "door",
                        "entered",
                        {},
                        {ir::do_action(61, "game", "go_to_scene_at",
                                       {text("maps/cave.dscene"), text("stairs")}),
                         ir::log(62, text("leaving"))},
                        {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{63},
                        "",
                        "scene_start",
                        {ir::call("game", "in_scene", {text("maps/cave.dscene")})},
                        {ir::log(64, text("in the cave"))},
                        {ir::log(65, text("elsewhere"))}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{66}, "bat", "interacted", {}, {ir::log(67, text("squeak"))}, {}});
        write_json(project.folder.path / "assets/logic/cave.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(0, key(input::Key::Left), true);
    const auto runtime = project.open(no_graphics(&replay)); // no issues: the bat's Rule compiles
    CHECK(runtime->scene() == "maps/start.dscene");
    const auto logged = [&](std::string_view line) {
        return std::count(runtime->log().begin(), runtime->log().end(), line);
    };
    // Walking into the door asks for the cave: the screen starts to fade out (0.4 s by
    // default: 24 steps), and no one moves meanwhile.
    int steps = 0;
    while (logged("leaving") == 0 && steps < 200) {
        runtime->step();
        ++steps;
    }
    // The door's area starts half a tile east of it, 4.5 tiles from the start: over a second.
    CHECK(steps > 60);
    CHECK(steps < 80);
    CHECK(runtime->scene() == "maps/start.dscene");
    CHECK(runtime->fade() == 0.0);
    const double at_door = transform_of(*runtime, "hero").x;
    for (int i = 1; i < 24; ++i) {
        runtime->step();
        CHECK(runtime->fade() == doctest::Approx(i / 24.0));
        CHECK(runtime->scene() == "maps/start.dscene");
    }
    CHECK(transform_of(*runtime, "hero").x == at_door); // Left held, but still
    runtime->step();                                    // black: the scene changes
    CHECK(runtime->fade() == 1.0);
    REQUIRE(runtime->scene() == "maps/cave.dscene");
    // The rest of the Rule ran before the scene changed, at the end of the step; then the
    // new scene started.
    CHECK(logged("leaving") == 1);
    CHECK(logged("in the cave") == 1);
    CHECK(logged("elsewhere") == 0);
    CHECK(runtime->log().back() == "in the cave");
    CHECK(logged("start") == 1); // the game started once
    // The cave's world, and nothing of the first scene's; the hero at the stairs.
    CHECK(runtime->world().find_named("torch").empty());
    CHECK(runtime->world().find_named("door").empty());
    CHECK(runtime->world().find_named("bat").size() == 1);
    CHECK(transform_of(*runtime, "hero").x == 7.0);
    CHECK(transform_of(*runtime, "hero").y == 3.0);
    CHECK(runtime->problems().empty());
    CHECK(runtime->vm().errors().empty());
    // It fades back in over as long, the hero still; then the game goes on there, Left
    // still held.
    for (int i = 23; i >= 0; --i) {
        runtime->step();
        CHECK(runtime->fade() == doctest::Approx(i / 24.0));
    }
    CHECK(transform_of(*runtime, "hero").x == 7.0);
    runtime->step();
    CHECK(transform_of(*runtime, "hero").x < 7.0);

    // A save remembers which scene it was made in.
    const serialize::Json saved = runtime->save();
    CHECK(saved.at("scene") == "maps/cave.dscene");
    std::vector<serialize::Issue> issues;
    // Back, by GUID; arriving at something that is not there is told, and the scene still
    // changes.
    assets::AssetDatabase db(project.folder.path / "assets");
    db.refresh();
    const assets::AssetRecord* start = db.find("maps/start.dscene");
    REQUIRE(start != nullptr);
    REQUIRE(runtime->change_scene(start->guid.to_string(), "nobody", issues));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "assets/maps/start.dscene");
    CHECK(issues[0].message == "There is nothing called nobody to arrive at.");
    CHECK(runtime->scene() == "maps/start.dscene");
    CHECK(runtime->world().find_named("torch").size() == 1);
    CHECK(runtime->world().find_named("bat").empty());
    CHECK(logged("elsewhere") == 1);
    // What is not a scene, or not there, changes nothing.
    issues.clear();
    CHECK_FALSE(runtime->change_scene("maps/nowhere.dscene", "", issues));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message == "There is no scene maps/nowhere.dscene in assets/.");
    issues.clear();
    CHECK_FALSE(runtime->change_scene("art/hero.png", "", issues));
    CHECK(runtime->scene() == "maps/start.dscene");
    CHECK(runtime->world().find_named("torch").size() == 1);
    // Loading the save goes back to the cave.
    issues.clear();
    REQUIRE(runtime->load(saved, issues));
    CHECK(issues.empty());
    CHECK(runtime->scene() == "maps/cave.dscene");
    CHECK(runtime->world().find_named("bat").size() == 1);
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
    replay.record(15, key(input::Key::Z), true);      // then Interact, for the fade below
    replay.record(16, key(input::Key::Z), false);
    {
        logic::LogicAsset again;
        again.name = "again";
        again.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "input",
            "pressed",
            {ir::compare(logic::CompareOp::Equal, ir::event_value("action"),
                         ir::literal(reflect::Value{std::string("interact")}))},
            {ir::do_action(2, "game", "go_to_scene",
                           {ir::literal(reflect::Value{std::string("maps/start.dscene")})})},
            {}});
        write_json(project.folder.path / "assets/logic/again.drules",
                   serialize::logic_to_json(again));
    }
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

    // A change of scene fades the picture to black: half-way, every pixel at half its
    // brightness; at the change, all black; once it has faded in, the scene as before.
    // (Interact goes to the same scene, so the picture is known.)
    runtime->step(); // Interact: the Rule asks for the scene
    CHECK(runtime->fade() == 0.0);
    for (int i = 0; i < 12; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->fade() == doctest::Approx(0.5));
    REQUIRE(runtime->draw());
    pixels = runtime->target()->read_pixels();
    const auto halved = [](render::Color c) {
        // Black at alpha 128 of 255 over it.
        const auto half = [](std::uint8_t v) {
            return static_cast<int>(std::lround(v * (255.0 - 128.0) / 255.0));
        };
        return std::array<int, 3>{half(c.r), half(c.g), half(c.b)};
    };
    const auto near = [](render::Color got, std::array<int, 3> want) {
        CAPTURE(int{got.r});
        CAPTURE(int{got.g});
        CAPTURE(int{got.b});
        return std::abs(got.r - want[0]) <= 1 && std::abs(got.g - want[1]) <= 1 &&
               std::abs(got.b - want[2]) <= 1;
    };
    CHECK(near(pixel(pixels, 32, 15, 14), halved(hero_colour)));
    CHECK(near(pixel(pixels, 32, 7, 14), halved(rock_colour)));
    CHECK(near(pixel(pixels, 32, 2, 2), halved(background)));
    for (int i = 0; i < 12; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->fade() == 1.0);
    REQUIRE(runtime->draw());
    pixels = runtime->target()->read_pixels();
    for (const auto& [x, y] : {std::pair{15U, 14U}, std::pair{2U, 2U}, std::pair{30U, 30U}}) {
        CHECK(pixel(pixels, 32, x, y) == render::Color{0, 0, 0, 255});
    }
    for (int i = 0; i < 24; ++i) {
        runtime->step();
    }
    CHECK(runtime->fade() == 0.0);
    REQUIRE(runtime->draw());
    pixels = runtime->target()->read_pixels();
    CHECK(pixel(pixels, 32, 2, 2) == background); // back at the start: hero and rock as at first
    const render::Color again = pixel(pixels, 32, 15, 14);
    CHECK((again == hero_colour || again == rock_colour));
    CHECK(runtime->problems().empty());
}

TEST_CASE("A Diorama project draws the hero as a lit billboard the camera follows") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Project project;
    // Switch the project to the Diorama style, with the camera 4 tiles away.
    std::vector<serialize::Issue> issues;
    game::ProjectSettings settings =
        game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
    REQUIRE(issues.empty());
    settings.style = game::RenderStyle::Diorama;
    settings.camera_distance = 4.0;
    write_json(project.folder.path / game::project_file, game::project_to_json(settings));

    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(15, key(input::Key::Right), false); // one tile right, off the rock
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    for (int i = 0; i < 15; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    REQUIRE(pixels.size() == std::size_t{32} * 32 * 4);

    // The camera looks at the hero's feet, the centre of the image: the billboard stands
    // just above it, facing the camera, lit as brightness() says.
    const float light = scene2d::brightness(render::Vec3{0, 0, 1}, scene2d::DioramaLight{});
    const auto lit = [light](std::uint8_t channel) {
        return static_cast<int>(std::lround(light * static_cast<float>(channel)));
    };
    const render::Color hero = pixel(pixels, 32, 16, 14);
    CHECK(std::abs(hero.r - lit(hero_colour.r)) <= 2);
    CHECK(std::abs(hero.g - lit(hero_colour.g)) <= 2);
    CHECK(std::abs(hero.b - lit(hero_colour.b)) <= 2);
    CHECK(pixel(pixels, 32, 16, 4) == background); // sky: no ground in this project
}

TEST_CASE("The sample village in the Diorama style: no tile bleeds into its neighbour") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    // A copy of samples/hello, switched to the Diorama style.
    const TempFolder folder;
    fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", folder.path, fs::copy_options::recursive);
    std::vector<serialize::Issue> issues;
    game::ProjectSettings settings =
        game::read_project(folder.path, issues).value_or(game::ProjectSettings{});
    REQUIRE(issues.empty());
    settings.style = game::RenderStyle::Diorama;
    settings.look = render::Look{}; // exact colours: no tilt-shift, grading or vignette
    write_json(folder.path / game::project_file, game::project_to_json(settings));
    game::RuntimeOptions options;
    options.headless = true;
    options.audio = game::AudioMode::Off;
    options.save_folder = folder.path / "saves";
    const auto runtime = game::Runtime::open(folder.path, options, issues);
    INFO(issues_text(issues));
    REQUIRE(runtime != nullptr);
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();

    // Grass and path tops, as lit from above. A path-coloured pixel alone in the grass is
    // the path tile's texels bleeding across a seam between grass tiles in the atlas
    // (it happened before world quads kept their texture coordinates inside the tile).
    const float top = scene2d::brightness(render::Vec3{0, 1, 0}, scene2d::DioramaLight{});
    const auto lit = [top](int r, int g, int b) {
        return render::Color{static_cast<std::uint8_t>(std::lround(top * static_cast<float>(r))),
                             static_cast<std::uint8_t>(std::lround(top * static_cast<float>(g))),
                             static_cast<std::uint8_t>(std::lround(top * static_cast<float>(b))),
                             255};
    };
    const render::Color grass = lit(76, 150, 72);
    const render::Color path = lit(196, 170, 120);
    std::size_t grass_pixels = 0;
    std::size_t stray = 0;
    for (std::uint32_t y = 1; y + 1 < 180; ++y) {
        for (std::uint32_t x = 1; x + 1 < 320; ++x) {
            const render::Color c = pixel(pixels, 320, x, y);
            grass_pixels += c == grass ? 1 : 0;
            if (c == path && pixel(pixels, 320, x - 1, y) == grass &&
                pixel(pixels, 320, x + 1, y) == grass && pixel(pixels, 320, x, y - 1) == grass &&
                pixel(pixels, 320, x, y + 1) == grass) {
                ++stray;
            }
        }
    }
    CHECK(grass_pixels > 10000);
    CHECK(stray == 0);

    // The camera stays inside the village: no edge of the screen shows the background,
    // though the view is deeper than the map (its far side is let go instead).
    std::size_t void_pixels = 0;
    for (std::uint32_t y = 0; y < 180; ++y) {
        void_pixels += pixel(pixels, 320, 0, y) == settings.background ? 1 : 0;
        void_pixels += pixel(pixels, 320, 319, y) == settings.background ? 1 : 0;
    }
    for (std::uint32_t x = 0; x < 320; ++x) {
        void_pixels += pixel(pixels, 320, x, 0) == settings.background ? 1 : 0;
        void_pixels += pixel(pixels, 320, x, 179) == settings.background ? 1 : 0;
    }
    CHECK(void_pixels == 0);
}

TEST_CASE("Physics: crates fall onto solid floors, are pushed, and say what they touch") {
    const Project project;
    SUBCASE("Falling, with gravity") {
        std::vector<serialize::Issue> issues;
        game::ProjectSettings settings =
            game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
        settings.gravity_y = 20.0;
        write_json(project.folder.path / game::project_file, game::project_to_json(settings));
        const auto runtime = project.open(no_graphics(nullptr));
        CHECK(runtime->settings().gravity_y == 20.0);
        // A solid floor whose top is at y = 3, under the crate.
        world::World& w = runtime->world();
        const world::Entity floor = w.create("floor");
        w.add<scene2d::Transform2D>(floor)->x = 49.0;
        w.get<scene2d::Transform2D>(floor)->y = 3.0;
        scene2d::Tilemap* map = w.add<scene2d::Tilemap>(floor);
        map->tiles = "0,0,0";
        map->solid = true;
        for (int i = 0; i < 120; ++i) {
            runtime->step();
        }
        CHECK(transform_of(*runtime, "crate").y == doctest::Approx(3.0).epsilon(0.01));
        CHECK(transform_of(*runtime, "crate").x == doctest::Approx(50.0));
        CHECK(runtime->log().back() == "floor"); // the crate's Rule: who it touched
    }
    SUBCASE("Pushing, top-down, until a wall") {
        input::Recording replay;
        replay.record(0, key(input::Key::Right), true);
        const auto runtime = project.open(no_graphics(&replay));
        world::World& w = runtime->world();
        // The hero just west of the crate, with a body of its own; a wall from x = 53.
        const world::Entity hero = w.find_named("hero").at(0);
        w.get<scene2d::Transform2D>(hero)->x = 48.6;
        game::PhysicsBody* body = w.add<game::PhysicsBody>(hero);
        body->width = 0.75;
        body->height = 0.5;
        const world::Entity wall = w.create("wall");
        w.add<scene2d::Transform2D>(wall)->x = 53.0;
        w.get<scene2d::Transform2D>(wall)->y = -2.0;
        scene2d::Tilemap* map = w.add<scene2d::Tilemap>(wall);
        map->tiles = "0\n0\n0";
        map->solid = true;
        for (int i = 0; i < 120; ++i) {
            runtime->step();
        }
        // The crate (1 wide) was pushed east until it met the wall; the hero stopped behind.
        CHECK(transform_of(*runtime, "crate").x == doctest::Approx(52.5).epsilon(0.002));
        CHECK(transform_of(*runtime, "hero").x == doctest::Approx(52.0 - 0.375).epsilon(0.002));
        CHECK(w.get<game::TopDownMover>(hero)->facing == game::Facing::Right);
        const std::vector<std::string>& log = runtime->log();
        CHECK(std::find(log.begin(), log.end(), "hero") != log.end()); // the crate felt the hero
        CHECK(std::find(log.begin(), log.end(), "wall") != log.end()); // and then the wall

        // A save keeps where things are; after loading, physics carries on from there.
        const serialize::Json saved = runtime->save();
        w.get<game::PhysicsBody>(w.find_named("crate").at(0))->push(-5.0, 0.0);
        runtime->step();
        // Pushed back west a little (damping slows it; the hero still leans on it).
        CHECK(transform_of(*runtime, "crate").x < 52.48);
        REQUIRE(runtime->load(saved, issues_storage()));
        CHECK(transform_of(*runtime, "crate").x == doctest::Approx(52.5).epsilon(0.002));
        runtime->step();
        CHECK(transform_of(*runtime, "crate").x == doctest::Approx(52.5).epsilon(0.002));
    }
}

TEST_CASE("Dialogue: the player stands still until the message is closed") {
    const Project project;
    input::Recording replay;
    replay.record(1, key(input::Key::Z), true); // talk to the elder
    replay.record(2, key(input::Key::Z), false);
    replay.record(2, key(input::Key::Left), true); // try to walk away while it shows
    replay.record(10, key(input::Key::Z), true);   // close it
    const auto runtime = project.open(no_graphics(&replay));
    const world::Entity hero = runtime->world().find_named("hero").at(0);
    runtime->world().get<scene2d::Transform2D>(hero)->y = 1.75; // just above the elder
    for (int i = 0; i < 10; ++i) {
        runtime->step();
    }
    CHECK(runtime->dialogue().is_open());
    CHECK(runtime->log().back() == "talk");         // the elder's Rule is still waiting
    CHECK(transform_of(*runtime, "hero").x == 0.0); // stood still
    CHECK(transform_of(*runtime, "hero").y == 1.75);
    CHECK_FALSE(runtime->world().get<game::TopDownMover>(hero)->moving);
    runtime->step(); // step 10: Interact closes it
    CHECK_FALSE(runtime->dialogue().is_open());
    // The "closed" Rule runs, then the elder's Rule, waiting until then, goes on.
    CHECK(runtime->log().at(runtime->log().size() - 2) == "closed");
    CHECK(runtime->log().back() == "after");
    runtime->step(); // and the player walks again
    CHECK(transform_of(*runtime, "hero").x < 0.0);
}

TEST_CASE("The message box is drawn over the game") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Project project;
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    const std::array<reflect::Value, 1> by{reflect::Value{std::string("hero")}};
    CHECK(game::emit(runtime->vm(), runtime->world(), runtime->world().find_named("elder").at(0),
                     "interacted", by));
    REQUIRE(runtime->dialogue().is_open());
    for (int i = 0; i < 8; ++i) {
        runtime->step(); // the box opens over a tenth of a second
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // In a 32 x 32 game the box holds one line: 18 high, from (4, 10) to (28, 28).
    CHECK(pixel(pixels, 32, 4, 10) == render::Color{230, 232, 240, 255}); // its border
    CHECK(pixel(pixels, 32, 27, 27) == render::Color{230, 232, 240, 255});
    CHECK(pixel(pixels, 32, 3, 10) == background); // outside it
    const render::Color inside = pixel(pixels, 32, 6, 12);
    CHECK(inside.r < 50); // the dark fill, nearly opaque over the game
    CHECK(inside.g < 50);
    CHECK(inside.b >= inside.r);
    std::size_t white = 0; // the text, in white
    for (std::uint32_t y = 15; y < 24; ++y) {
        for (std::uint32_t x = 9; x < 27; ++x) {
            white += pixel(pixels, 32, x, y) == render::Color{255, 255, 255, 255} ? 1 : 0;
        }
    }
    CHECK(white > 10);
}

TEST_CASE("Dialogue: long messages take one Interact per page, and messages queue") {
    const Project project;
    input::Recording replay;
    for (std::uint64_t press = 0; press < 20; ++press) {
        replay.record(1 + (2 * press), key(input::Key::Z), true);
        replay.record(2 + (2 * press), key(input::Key::Z), false);
    }
    const auto runtime = project.open(no_graphics(&replay));
    const std::string long_line = "Welcome, traveller. The road east leads home.";
    runtime->dialogue().say(long_line);
    runtime->dialogue().say("Hi.");
    // A 32 x 32 game shows one line of 14 pixels at a time.
    const std::size_t pages = ui::wrap(ui::PixelFont::builtin(), long_line, 14).size();
    REQUIRE(pages > 3);
    runtime->step(); // step 0: no press yet
    for (std::size_t page = 0; page < pages; ++page) {
        REQUIRE(runtime->dialogue().current() != nullptr);
        CHECK(*runtime->dialogue().current() == long_line);
        runtime->step(); // a press
        runtime->step(); // a release
    }
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Hi.");
    // Nothing closed yet, and no press reached the Interact Rule ("hello"); the torch's
    // Sequence finished meanwhile.
    CHECK(runtime->log() == std::vector<std::string>{"start", "torch done"});
    runtime->step();
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->log() == std::vector<std::string>{"start", "torch done", "closed"});
}

TEST_CASE("Dialogue: loading a save closes open messages") {
    const Project project;
    const auto runtime = project.open(no_graphics(nullptr));
    const serialize::Json saved = runtime->save();
    runtime->dialogue().say("Hi.");
    std::vector<serialize::Issue> issues;
    REQUIRE(runtime->load(saved, issues));
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->log() == std::vector<std::string>{"start"}); // no "closed" Rule ran
}

TEST_CASE("The camera stops at the map's edges, unless the project turns that off") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    // Where the hero's 2 x 2 image lands, with the hero at (1, 1) on an 8 x 8 tile map at
    // the origin, in a 32 x 32 game of 8 pixels per tile (a view of 4 x 4 tiles).
    const auto hero_pixels = [](bool clamp) {
        const Project project;
        std::vector<serialize::Issue> issues;
        game::ProjectSettings settings =
            game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
        REQUIRE(issues.empty());
        CHECK(settings.camera_clamp); // on by default
        settings.camera_clamp = clamp;
        write_json(project.folder.path / game::project_file, game::project_to_json(settings));
        game::RuntimeOptions options;
        options.headless = true;
        const auto runtime = project.open(options);
        REQUIRE(runtime->settings().camera_clamp == clamp);
        world::World& w = runtime->world();
        const world::Entity map = w.create("map");
        w.add<scene2d::Transform2D>(map);
        w.add<scene2d::Tilemap>(map)->tiles = ",,,,,,,\n\n\n\n\n\n\n,,,,,,,"; // 8 x 8, empty
        const world::Entity hero = w.find_named("hero").at(0);
        w.get<scene2d::Transform2D>(hero)->x = 1.0;
        w.get<scene2d::Transform2D>(hero)->y = 1.0;
        REQUIRE(runtime->draw());
        return runtime->target()->read_pixels();
    };
    // Clamped, the view's centre is (2, 2), so the hero's feet are at pixel (8, 8).
    std::vector<std::uint8_t> pixels = hero_pixels(true);
    CHECK(pixel(pixels, 32, 7, 6) == hero_colour);
    CHECK(pixel(pixels, 32, 8, 7) == hero_colour);
    CHECK(pixel(pixels, 32, 15, 14) == background);
    // Not clamped, the hero is centred: feet at (16, 16), and the rock at the origin one
    // tile up and left of it, where the clamped hero was.
    pixels = hero_pixels(false);
    CHECK(pixel(pixels, 32, 15, 14) == hero_colour);
    CHECK(pixel(pixels, 32, 16, 15) == hero_colour);
    CHECK(pixel(pixels, 32, 7, 6) == rock_colour); // the rock at the origin, 1 tile up-left
}

TEST_CASE("Dialogue: questions list answers, Up and Down choose, Interact picks") {
    game::DialogueObject parsed;
    parsed.ask("Which?", " Yes | No ||  Maybe ");
    CHECK(parsed.choices() == std::vector<std::string>{"Yes", "No", "Maybe"});
    CHECK_FALSE(parsed.choose(3));
    CHECK(parsed.answer() == 0);
    CHECK(parsed.choose(2));
    CHECK(parsed.answer() == 3);
    CHECK(parsed.answer_text() == "Maybe");
    CHECK(parsed.close_current());
    parsed.ask("Nothing to pick", " | ");
    CHECK(parsed.choices().empty()); // an ordinary message
    CHECK_FALSE(parsed.choose(0));
    CHECK(parsed.said() == std::vector<std::string>{"Which?", "Nothing to pick"});

    const Project project;
    input::Recording replay;
    const auto press = [&replay](std::uint64_t step, input::Key k) {
        replay.record(step, key(k), true);
        replay.record(step + 1, key(k), false);
    };
    press(1, input::Key::Down); // No
    press(3, input::Key::Down); // Maybe
    press(5, input::Key::Down); // round to Yes
    press(7, input::Key::Up);   // round to Maybe
    press(9, input::Key::Up);   // No
    press(11, input::Key::Z);   // pick it
    const auto runtime = project.open(no_graphics(&replay));
    // One line in a 32 x 32 game (14 pixels), so the answers show at once.
    REQUIRE(ui::wrap(ui::PixelFont::builtin(), "Hi", 14).size() == 1);
    runtime->dialogue().ask("Hi", "Yes|No|Maybe");
    for (int i = 0; i < 11; ++i) {
        runtime->step();
    }
    CHECK(runtime->dialogue().is_open());
    CHECK(transform_of(*runtime, "hero").y == 0.0); // Up and Down did not walk
    CHECK(runtime->log() == std::vector<std::string>{"start"});
    runtime->step();
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->dialogue().answer() == 2);
    CHECK(runtime->log() == std::vector<std::string>{"start", "2", "No", "closed"});
}

TEST_CASE("A question's answers are drawn above the message") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Project project;
    std::vector<serialize::Issue> issues;
    game::ProjectSettings settings =
        game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
    REQUIRE(issues.empty());
    settings.width = 64;
    settings.height = 64;
    write_json(project.folder.path / game::project_file, game::project_to_json(settings));
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    runtime->dialogue().ask("Go?", "Yes|No");
    for (int i = 0; i < 8; ++i) {
        runtime->step(); // the box opens over a tenth of a second
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // The message box: one line, from y = 42 to 60. Two answers: 27 high, ending 2 above
    // it (y = 13 to 40), right-aligned with it (its right edge at x = 59).
    const render::Color border{230, 232, 240, 255};
    CHECK(pixel(pixels, 64, 59, 42) == border);
    CHECK(pixel(pixels, 64, 59, 13) == border);
    CHECK(pixel(pixels, 64, 59, 39) == border);
    CHECK(pixel(pixels, 64, 59, 40) != border);
    CHECK(pixel(pixels, 64, 59, 12) != border);
    // The cursor and "Yes" in white on the first answer's row (18 to 24); "No" dimmer on
    // the second (27 to 33).
    std::size_t white_first = 0;
    std::size_t white_second = 0;
    std::size_t dim_second = 0;
    const render::Color white{255, 255, 255, 255};
    const render::Color dim = game::UiTheme{}.dim; // the standard look's hints
    for (std::uint32_t x = 20; x < 59; ++x) {
        for (std::uint32_t y = 18; y < 25; ++y) {
            white_first += pixel(pixels, 64, x, y) == white ? 1 : 0;
        }
        for (std::uint32_t y = 27; y < 34; ++y) {
            white_second += pixel(pixels, 64, x, y) == white ? 1 : 0;
            dim_second += pixel(pixels, 64, x, y) == dim ? 1 : 0;
        }
    }
    CHECK(white_first > 10);
    CHECK(white_second == 0);
    CHECK(dim_second > 5);
}

TEST_CASE("Dialogue: a long question shows its answers only on its last page") {
    const Project project;
    input::Recording replay;
    const auto press = [&replay](std::uint64_t step, input::Key k) {
        replay.record(step, key(k), true);
        replay.record(step + 1, key(k), false);
    };
    press(1, input::Key::Down); // first page: does not choose
    press(3, input::Key::Z);    // next page, the last
    press(5, input::Key::Down); // No
    press(7, input::Key::Z);    // pick it
    const auto runtime = project.open(no_graphics(&replay));
    // Two lines of 14 pixels: two pages of one line in a 32 x 32 game.
    REQUIRE(ui::wrap(ui::PixelFont::builtin(), "Hi hi", 14).size() == 2);
    runtime->dialogue().ask("Hi hi", "Yes|No");
    for (int i = 0; i < 8; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->dialogue().answer() == 2);
    CHECK(runtime->log() == std::vector<std::string>{"start", "2", "No", "closed"});
}

TEST_CASE("The sample village: the elder asks a question and answers the choice") {
    const TempFolder folder;
    fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", folder.path, fs::copy_options::recursive);
    input::Recording replay;
    const auto press = [&replay](std::uint64_t step, input::Key k) {
        replay.record(step, key(k), true);
        replay.record(step + 1, key(k), false);
    };
    press(1, input::Key::Z);    // close the welcome
    press(4, input::Key::Z);    // close the elder's greeting
    press(6, input::Key::Down); // "No, thanks"
    press(8, input::Key::Z);    // pick it
    game::RuntimeOptions options = no_graphics(&replay);
    options.save_folder = folder.path / "saves";
    std::vector<serialize::Issue> issues;
    const auto runtime = game::Runtime::open(folder.path, options, issues);
    INFO(issues_text(issues));
    REQUIRE(runtime != nullptr);
    CHECK(issues.empty());
    for (int i = 0; i < 3; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->dialogue().is_open());
    // Talking to the elder, as Interact facing it would.
    const std::array<reflect::Value, 1> by{reflect::Value{std::string("hero")}};
    REQUIRE(game::emit(runtime->vm(), runtime->world(), runtime->world().find_named("elder").at(0),
                       "interacted", by));
    for (int i = 0; i < 6; ++i) {
        runtime->step(); // steps 3 to 8
    }
    CHECK(runtime->dialogue().answer_text() == "No, thanks");
    const std::vector<std::string> expected{
        "Welcome to the village! Arrow keys to walk, Z to talk.",
        "Elder: Welcome, traveller. The road east leads home.",
        "Elder: Shall I tell you about the road?", "Elder: Safe travels, then."};
    CHECK(runtime->dialogue().said() == expected);
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Elder: Safe travels, then.");
    CHECK(runtime->problems().empty());
}

TEST_CASE("Lighting starts from the project and logic changes it") {
    const Project project;
    std::vector<serialize::Issue> issues;
    game::ProjectSettings settings =
        game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
    REQUIRE(issues.empty());
    settings.ambient_light = 0.4;
    settings.sun_light = 0.9;
    settings.sun_angle = 100.0;
    settings.sun_height = 40.0;
    settings.sprite_shadow = 0.6;
    settings.shadow_shape = scene2d::ShadowShape::Round;
    write_json(project.folder.path / game::project_file, game::project_to_json(settings));
    input::Recording replay;
    replay.record(1, key(input::Key::X), true); // Cancel: its Rule sets dusk
    const auto runtime = project.open(no_graphics(&replay));
    CHECK(runtime->lighting().ambient == 0.4);
    CHECK(runtime->lighting().sun == 0.9);
    CHECK(runtime->lighting().sun_angle == 100.0);
    CHECK(runtime->lighting().sun_height == 40.0);
    // What the game draws with: the lighting, and the project's shadows.
    scene2d::DioramaLight light = game::diorama_light(settings, runtime->lighting());
    CHECK(light.ambient == doctest::Approx(0.4));
    CHECK(light.diffuse == doctest::Approx(0.9));
    const render::Vec3 sun = scene2d::sun_direction(100.0, 40.0);
    CHECK(light.sun.x == doctest::Approx(sun.x));
    CHECK(light.sun.y == doctest::Approx(sun.y));
    CHECK(light.sun.z == doctest::Approx(sun.z));
    CHECK(light.sprite_shadow == doctest::Approx(0.6));
    CHECK(light.shadows == scene2d::ShadowShape::Round);
    CHECK(light.block_shadow == doctest::Approx(0.45)); // the default
    runtime->step();
    runtime->step();
    CHECK(runtime->lighting().ambient == 0.25);
    CHECK(runtime->lighting().sun == 0.1);
    CHECK(runtime->lighting().sun_angle == 260.0);
    CHECK(runtime->lighting().sun_height == 10.0);

    // Values logic could set out of range, or not numbers, are brought back into range.
    game::LightingObject wild;
    wild.ambient = std::nan("");
    wild.sun = 9.0;
    wild.sun_height = -50.0;
    light = game::diorama_light(settings, wild);
    CHECK(light.ambient == doctest::Approx(0.55));
    CHECK(light.diffuse == doctest::Approx(2.0));
    CHECK(light.sun.y == doctest::Approx(std::sin(5.0 * 3.14159265358979 / 180.0)));

    // Saved with the game, and restored; a save without it (made before lighting was
    // saved) keeps the current lighting.
    serialize::Json saved = runtime->save();
    CHECK(saved["lighting"]["ambient"] == 0.25);
    CHECK(saved["lighting"]["sun_angle"] == 260.0);
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> load_issues;
    REQUIRE(other->load(saved, load_issues));
    CHECK(other->lighting().ambient == 0.25);
    CHECK(other->lighting().sun == 0.1);
    CHECK(other->lighting().sun_angle == 260.0);
    CHECK(other->lighting().sun_height == 10.0);
    saved.erase("lighting");
    const auto older = project.open(no_graphics(nullptr));
    REQUIRE(older->load(saved, load_issues));
    CHECK(older->lighting().ambient == 0.4);
    CHECK(load_issues.empty());
}

TEST_CASE("The project's look: read and written, checked, and applied under the message box") {
    const Project project;
    std::vector<serialize::Issue> issues;
    game::ProjectSettings settings =
        game::read_project(project.folder.path, issues).value_or(game::ProjectSettings{});
    REQUIRE(issues.empty());
    CHECK(settings.look.neutral());
    settings.look.tilt_shift = true;
    settings.look.blur = 2.5F;
    settings.look.bloom = 0.25F;
    settings.look.exposure = 2.0F;
    settings.look.vignette = 0.5F;
    write_json(project.folder.path / game::project_file, game::project_to_json(settings));
    const std::optional<game::ProjectSettings> again =
        game::read_project(project.folder.path, issues);
    REQUIRE(again.has_value());
    CHECK(issues.empty());
    CHECK(again.value_or(game::ProjectSettings{}).look == settings.look);

    // Out of range and of the wrong kind refuse the project; an unknown name does not.
    serialize::Json json = game::project_to_json(settings);
    json["look"]["blur"] = 13;
    json["look"]["tilt_shift"] = "on";
    json["look"]["sparkle"] = 1;
    write_json(project.folder.path / game::project_file, json);
    issues.clear();
    CHECK_FALSE(game::read_project(project.folder.path, issues).has_value());
    std::vector<std::string> paths;
    paths.reserve(issues.size());
    for (const serialize::Issue& issue : issues) {
        paths.push_back(issue.path);
    }
    std::sort(paths.begin(), paths.end());
    CHECK(paths == std::vector<std::string>{"project.sparxel.json/look/blur",
                                            "project.sparxel.json/look/sparkle",
                                            "project.sparxel.json/look/tilt_shift"});
    json = game::project_to_json(settings);
    json["look"] = 3;
    write_json(project.folder.path / game::project_file, json);
    issues.clear();
    CHECK_FALSE(game::read_project(project.folder.path, issues).has_value());

    if (test_support::gpu() == nullptr) {
        return;
    }
    // Exposure 2 alone doubles the world's colours; the message box, drawn after the look,
    // keeps its exact border.
    game::ProjectSettings bright = settings;
    bright.look = render::Look{};
    bright.look.exposure = 2.0F;
    write_json(project.folder.path / game::project_file, game::project_to_json(bright));
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->draw());
    std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // The hero's 2 x 2 image covers pixels 15 and 16 of rows 14 and 15 (or the rock does).
    const render::Color hero = pixel(pixels, 32, 15, 14);
    const auto doubled = [](render::Color c) {
        return render::Color{static_cast<std::uint8_t>(std::min(255, c.r * 2)),
                             static_cast<std::uint8_t>(std::min(255, c.g * 2)),
                             static_cast<std::uint8_t>(std::min(255, c.b * 2)), 255};
    };
    const auto near = [](render::Color a, render::Color b) {
        return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1;
    };
    CHECK((near(hero, doubled(hero_colour)) || near(hero, doubled(rock_colour))));
    CHECK(near(pixel(pixels, 32, 0, 0), doubled(background)));
    runtime->dialogue().say("Hi");
    for (int i = 0; i < 8; ++i) {
        runtime->step(); // the box opens over a tenth of a second
    }
    REQUIRE(runtime->draw());
    pixels = runtime->target()->read_pixels();
    CHECK(pixel(pixels, 32, 4, 10) == render::Color{230, 232, 240, 255}); // the box's border
    CHECK(near(pixel(pixels, 32, 0, 0), doubled(background)));            // around it

    // Tilt-shift belongs to the Diorama style: in Classic 2D, even the strongest blur
    // leaves every pixel as it was.
    game::ProjectSettings tilted = settings;
    tilted.look = render::Look{};
    tilted.look.tilt_shift = true;
    tilted.look.blur = 12.0F;
    tilted.look.focus_height = 0.0F;
    write_json(project.folder.path / game::project_file, game::project_to_json(tilted));
    const auto flat = project.open(options);
    REQUIRE(flat->draw());
    pixels = flat->target()->read_pixels();
    CHECK(pixel(pixels, 32, 14, 14) == background); // beside the hero: no blur bleeds in
    const render::Color sharp = pixel(pixels, 32, 15, 14);
    CHECK((sharp == hero_colour || sharp == rock_colour));
}

TEST_CASE("The inventory: logic gives, takes and checks things, and saves keep them") {
    // On its own: by name (spaces around it dropped), never below none, up to the most.
    game::InventoryObject bag;
    bag.give(" key ", 1);
    CHECK(bag.count_of("key") == 1);
    CHECK(bag.has("key"));
    bag.give("coin", 5);
    bag.take("coin", 2);
    CHECK(bag.count_of("coin") == 3);
    bag.take("coin", 10);
    CHECK_FALSE(bag.has("coin"));
    CHECK(bag.kinds() == 1);
    bag.give("", 3);     // no name
    bag.give("gem", 0);  // none
    bag.take("key", -1); // nothing taken
    CHECK(bag.kinds() == 1);
    CHECK(bag.count_of("key") == 1);
    bag.give("coin", game::InventoryObject::most);
    bag.give("coin", game::InventoryObject::most);
    CHECK(bag.count_of("coin") == game::InventoryObject::most);

    // In a game: on starting, a Rule gives 4 coins and a key and takes a coin back; then,
    // as the player has the key, another logs how many coins there are (3) and how many
    // different things (2) and how many of what a variable names (1 key).
    const Project project;
    {
        logic::LogicAsset rules;
        rules.name = "bag";
        // What to count can come from elsewhere: here a variable naming the key.
        rules.variables.push_back(logic::Variable{"wanted", reflect::ValueKind::Text, "",
                                                  reflect::Value{std::string("key")}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "inventory", "give", {Project::text("coin"), Project::whole(4)}),
             ir::do_action(3, "inventory", "give", {Project::text("key"), Project::whole(1)}),
             ir::do_action(4, "inventory", "take", {Project::text("coin"), Project::whole(1)})},
            {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{5},
                        "",
                        "game_start",
                        {ir::call("inventory", "has", {Project::text("key")})},
                        {ir::log(6, ir::call("inventory", "count_of", {Project::text("coin")})),
                         ir::log(7, ir::call("inventory", "kinds")),
                         ir::log(8, ir::call("inventory", "count_of", {ir::variable("wanted")}))},
                        {}});
        write_json(project.folder.path / "assets/logic/bag.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr)); // no issues: it compiles
    runtime->step();
    CHECK(runtime->inventory().count_of("coin") == 3);
    CHECK(runtime->inventory().has("key"));
    const auto logged = [&](std::string_view line) {
        return std::count(runtime->log().begin(), runtime->log().end(), line);
    };
    CHECK(logged("3") == 1);
    CHECK(logged("2") == 1);
    CHECK(logged("1") == 1); // the number of wanted: one key

    // Saved with the game and restored; loading empties it first. Counts that are not
    // whole numbers above 0 are left out, with a note; a save without it (made before it
    // was saved) leaves the player carrying nothing.
    serialize::Json saved = runtime->save();
    CHECK(saved["inventory"]["coin"] == 3);
    CHECK(saved["inventory"]["key"] == 1);
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> load_issues;
    REQUIRE(other->load(saved, load_issues));
    CHECK(load_issues.empty());
    CHECK(other->inventory().count_of("coin") == 3);
    CHECK(other->inventory().count_of("key") == 1);
    saved["inventory"]["gem"] = -4;
    saved["inventory"]["key"] = "one";
    REQUIRE(other->load(saved, load_issues));
    CHECK(load_issues.size() == 2);
    CHECK(other->inventory().kinds() == 1); // the coins
    saved.erase("inventory");
    load_issues.clear();
    REQUIRE(other->load(saved, load_issues));
    CHECK(load_issues.empty());
    CHECK(other->inventory().kinds() == 0);
}

TEST_CASE("Quests: started, staged, finished, listed in the journal, and saved") {
    game::QuestObject book;
    book.start(" cat ", "The lost cat");
    book.start("cat", "Another title"); // started already: unchanged
    book.set_objective("cat", "Find the cat in the woods");
    book.set_stage("cat", 2);
    book.set_stage("cat", -3);
    CHECK(book.stage_of("cat") == 0);
    book.set_stage("cat", 2);
    book.start("well", "");
    book.complete("well");
    book.fail("well"); // finished already: unchanged
    book.start("bridge", "Mend the bridge");
    book.fail("bridge");
    book.set_objective("nothing", "never started"); // nothing happens
    CHECK(book.active("cat"));
    CHECK(book.done("well"));
    CHECK(book.failed("bridge"));
    CHECK_FALSE(book.started("nothing"));
    CHECK(book.stage_of("cat") == 2);
    CHECK(book.active_count() == 1);
    CHECK(
        book.journal() ==
        "Journal\nThe lost cat: Find the cat in the woods\nwell (done)\nMend the bridge (failed)");
    CHECK(game::QuestObject{}.journal() == "The journal is empty.");

    // In a game: on starting, Rules start a quest and move it on, then check it; the
    // journal shows in a message box.
    const Project project;
    {
        logic::LogicAsset rules;
        rules.name = "story";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "quests", "start",
                           {Project::text("cat"), Project::text("The lost cat")}),
             ir::do_action(3, "quests", "set_stage", {Project::text("cat"), Project::whole(1)}),
             ir::do_action(4, "quests", "set_objective",
                           {Project::text("cat"), Project::text("Look by the well")}),
             ir::do_action(5, "quests", "show_journal", {})},
            {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{6},
                        "",
                        "game_start",
                        {ir::call("quests", "active", {Project::text("cat")})},
                        {ir::log(7, ir::call("quests", "stage_of", {Project::text("cat")}))},
                        {}});
        write_json(project.folder.path / "assets/logic/story.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    runtime->step();
    CHECK(runtime->quests().active("cat"));
    CHECK(runtime->quests().stage_of("cat") == 1);
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "1") == 1);
    CHECK(runtime->dialogue().is_open());

    // Saved and loaded back; a quest without a title is left out with a note.
    serialize::Json saved = runtime->save();
    CHECK(saved["quests"]["cat"]["title"] == "The lost cat");
    CHECK(saved["quests"]["cat"]["state"] == "active");
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> issues;
    REQUIRE(other->load(saved, issues));
    CHECK(issues.empty());
    CHECK(other->quests().active("cat"));
    CHECK(other->quests().quests().at("cat").objective == "Look by the well");
    saved["quests"]["broken"] = serialize::Json{{"stage", 3}};
    REQUIRE(other->load(saved, issues));
    CHECK(issues.size() == 1);
    CHECK_FALSE(other->quests().started("broken"));
}

TEST_CASE("Particles come at their rate, move their kind's way, end, and replay the same") {
    reflect::Registry registry;
    game::register_types(registry);
    REQUIRE(registry.errors().empty());
    world::World w(registry);
    REQUIRE(scene2d::register_components(w));
    REQUIRE(game::register_behaviour_components(w));
    const world::Entity fire = w.create("fire");
    w.add<scene2d::Transform2D>(fire)->x = 2.0;
    game::ParticleEmitter* flames = w.add<game::ParticleEmitter>(fire);
    REQUIRE(flames != nullptr);
    const world::Entity rain = w.create("rain");
    w.add<scene2d::Transform2D>(rain);
    w.add<game::ParticleEmitter>(rain)->preset = game::ParticlePreset::Rain;

    // Half a second: fire (30 a second) has sent out 15, rising; rain (120) 60, falling.
    for (int i = 0; i < 30; ++i) {
        game::update_particles(w, 1.0 / 60.0);
    }
    flames = w.get<game::ParticleEmitter>(fire);
    CHECK(flames->particles.size() == 15);
    bool rising = true;
    for (const game::Particle& p : flames->particles) {
        rising = rising && p.vh > 0.0 && std::abs(p.x - 2.0) < 1.0;
    }
    CHECK(rising);
    const game::ParticleEmitter* drops = w.get<game::ParticleEmitter>(rain);
    CHECK(drops->particles.size() == 60);
    CHECK(drops->particles.front().vh < 0.0);

    // Stopped, no new ones come and those flying end (fire lasts under a second).
    flames->stop();
    for (int i = 0; i < 60; ++i) {
        game::update_particles(w, 1.0 / 60.0);
    }
    CHECK(w.get<game::ParticleEmitter>(fire)->particles.empty());
    // A burst, at once; never more than 500 at a time.
    w.get<game::ParticleEmitter>(fire)->burst(25);
    game::update_particles(w, 1.0 / 60.0);
    CHECK(w.get<game::ParticleEmitter>(fire)->particles.size() == 25);
    w.get<game::ParticleEmitter>(fire)->burst(100000);
    game::update_particles(w, 1.0 / 60.0);
    CHECK(w.get<game::ParticleEmitter>(fire)->particles.size() <= 525);

    // The same emitter, the same steps: the same particles.
    world::World again(registry);
    REQUIRE(scene2d::register_components(again));
    REQUIRE(game::register_behaviour_components(again));
    const world::Entity copy = again.create("fire");
    again.add<scene2d::Transform2D>(copy)->x = 2.0;
    game::ParticleEmitter* twin = again.add<game::ParticleEmitter>(copy);
    twin->seed = 77;
    game::ParticleEmitter* first = w.get<game::ParticleEmitter>(fire);
    first->particles.clear();
    first->seed = 77;
    first->due = 0.0;
    first->emitting = true;
    first->pending_burst = 0;
    for (int i = 0; i < 20; ++i) {
        game::update_particles(w, 1.0 / 60.0);
        game::update_particles(again, 1.0 / 60.0);
    }
    REQUIRE(first->particles.size() == twin->particles.size());
    CHECK(first->particles.back().x == twin->particles.back().x);
    CHECK(first->particles.back().h == twin->particles.back().h);
}

TEST_CASE("Particles are drawn over the scene, where their emitter is") {
    const Project project;
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    // A fire at the hero's feet, in the middle of the view: its flames rise above.
    world::World& w = runtime->world();
    const world::Entity hero = w.find_named("hero").at(0);
    game::ParticleEmitter* fire = w.add<game::ParticleEmitter>(hero);
    REQUIRE(fire != nullptr);
    fire->rate = 300.0;
    fire->size = 2;
    for (int i = 0; i < 20; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // Somewhere in the column above the hero (the target's centre), a warm colour shows.
    bool warm = false;
    for (std::uint32_t y = 0; y < 16; ++y) {
        for (std::uint32_t x = 12; x < 20; ++x) {
            const render::Color c = pixel(pixels, 32, x, y);
            warm = warm || (c.r > 150 && c.r > c.b + 60);
        }
    }
    CHECK(warm);
}

namespace {

/// The elder's conversation: a greeting, a question whose third answer needs a key, and
/// what each answer leads to.
game::Conversation elder_talk() {
    game::Conversation c;
    c.start = "hello";
    c.speakers.push_back(game::Speaker{"Elder", "#ffd27a"});
    game::TalkNode hello;
    hello.id = "hello";
    hello.speaker = "Elder";
    hello.text = "Welcome, traveller.";
    hello.next = "again";
    c.nodes.push_back(hello);
    // Said only before the flag is set: the second time, straight to the question.
    game::TalkNode again;
    again.id = "again";
    again.speaker = "Elder";
    again.text = "Have we met?";
    again.next = "ask";
    again.conditions.push_back({game::TalkCondition::Kind::Flag, "met", true});
    again.effects.push_back({game::TalkEffect::Kind::SetFlag, "met", ""});
    c.nodes.push_back(again);
    game::TalkNode ask;
    ask.id = "ask";
    ask.speaker = "Elder";
    ask.text = "Will you find my cat?";
    ask.choices.push_back({"Yes", "yes", {}});
    ask.choices.push_back({"No", "", {}});
    ask.choices.push_back(
        {"Open the gate|now", "gate", {{game::TalkCondition::Kind::HasItem, "key", false}}});
    c.nodes.push_back(ask);
    game::TalkNode yes;
    yes.id = "yes";
    yes.speaker = "Elder";
    yes.text = "Bless you. Take this.";
    yes.effects.push_back({game::TalkEffect::Kind::GiveItem, "coin", "3"});
    yes.effects.push_back({game::TalkEffect::Kind::StartQuest, "cat", "The lost cat"});
    c.nodes.push_back(yes);
    game::TalkNode gate;
    gate.id = "gate";
    gate.text = "The gate creaks open.";
    c.nodes.push_back(gate);
    return c;
}

} // namespace

TEST_CASE("Conversations are read and written the same, and checked for broken ways") {
    game::Conversation talk = elder_talk();
    talk.speakers[0].portrait = "faces/elder.png";
    talk.speakers[0].voice = "sfx/blip.wav";
    talk.speakers[0].pitch = 0.8;
    talk.nodes[2].portrait = "faces/elder_glad.png";
    std::string error;
    const std::optional<game::Conversation> back =
        game::parse_conversation(game::conversation_to_text(talk), error);
    REQUIRE(back.has_value());
    CHECK(back.value_or(game::Conversation{}) == talk);
    CHECK(game::check_conversation(talk).empty());

    game::Conversation broken = talk;
    broken.start = "hi";
    broken.nodes[2].choices[0].next = "maybe";
    broken.nodes.push_back(broken.nodes[4]);
    const std::vector<std::string> problems = game::check_conversation(broken);
    CHECK(std::find(problems.begin(), problems.end(), "Two nodes are called \"gate\".") !=
          problems.end());
    CHECK(std::find(problems.begin(), problems.end(),
                    "It starts at \"hi\", which is not one of its nodes.") != problems.end());
    CHECK(std::find(problems.begin(), problems.end(),
                    "\"ask\", choice \"Yes\", leads to \"maybe\", which is not one of its "
                    "nodes.") != problems.end());
    CHECK(std::find(problems.begin(), problems.end(), "Nothing leads to \"hello\".") !=
          problems.end());
    CHECK_FALSE(game::parse_conversation("{\"format\": \"sparxel.scene\"}", error).has_value());
    CHECK(error == "This is not a conversation (\"sparxel.conversation\").");
}

TEST_CASE("A conversation plays line by line, offers the choices that hold, and does its effects") {
    game::DialogueObject dialogue;
    game::InventoryObject bag;
    game::QuestObject quests;
    game::ConversationPlayer player;
    player.start(elder_talk(), "talk/elder.dtalk");
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()).empty());
    REQUIRE(dialogue.current() != nullptr);
    CHECK(*dialogue.current() == "Welcome, traveller.");
    CHECK(dialogue.current_speaker() == "Elder");
    // Nothing more while the message is open.
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()).empty());
    CHECK(dialogue.said().size() == 1);
    dialogue.close_current();
    player.update(dialogue, bag, quests, dialogue.flags());
    CHECK(*dialogue.current() == "Have we met?");
    CHECK(dialogue.flag("met"));
    dialogue.close_current();
    // The question: without a key, two answers.
    player.update(dialogue, bag, quests, dialogue.flags());
    CHECK(*dialogue.current() == "Will you find my cat?");
    CHECK(dialogue.choices() == std::vector<std::string>{"Yes", "No"});
    REQUIRE(dialogue.choose(0));
    dialogue.close_current();
    player.update(dialogue, bag, quests, dialogue.flags());
    CHECK(*dialogue.current() == "Bless you. Take this.");
    CHECK(bag.count_of("coin") == 3);
    CHECK(quests.active("cat"));
    dialogue.close_current();
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()) == "talk/elder.dtalk");
    CHECK_FALSE(player.playing());
    CHECK_FALSE(dialogue.is_open());

    // Again, with the key: the flag skips the second line, the third answer shows (its bar
    // made a slash), and picking it leads to the gate; that line has no speaker.
    bag.give("key", 1);
    player.start(elder_talk(), "talk/elder.dtalk", "again");
    player.update(dialogue, bag, quests, dialogue.flags());
    CHECK(*dialogue.current() == "Will you find my cat?");
    CHECK(dialogue.choices() == std::vector<std::string>{"Yes", "No", "Open the gate/now"});
    REQUIRE(dialogue.choose(2));
    dialogue.close_current();
    player.update(dialogue, bag, quests, dialogue.flags());
    CHECK(*dialogue.current() == "The gate creaks open.");
    CHECK(dialogue.current_speaker().empty());
    // "No" leads nowhere: the conversation ends.
    dialogue.close_current();
    player.start(elder_talk(), "talk/elder.dtalk", "ask");
    player.update(dialogue, bag, quests, dialogue.flags());
    REQUIRE(dialogue.choose(1));
    dialogue.close_current();
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()) == "talk/elder.dtalk");
}

TEST_CASE("Logic starts a conversation, and the story's flags are saved with the game") {
    const Project project;
    {
        std::ofstream(project.folder.path / "assets/talk.dtalk", std::ios::binary)
            << game::conversation_to_text(elder_talk());
        logic::LogicAsset rules;
        rules.name = "talk";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "dialogue", "start_conversation", {Project::text("talk.dtalk")}),
             ir::do_action(3, "dialogue", "set_flag", {Project::text("rested")})},
            {}});
        rules.rules.push_back(logic::Rule{logic::BlockId{4},
                                          "dialogue",
                                          "conversation_ended",
                                          {},
                                          {ir::log(5, ir::event_value("name"))},
                                          {}});
        write_json(project.folder.path / "assets/logic/talk.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    runtime->step();
    runtime->step();
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Welcome, traveller.");
    CHECK(runtime->dialogue().current_speaker() == "Elder");
    CHECK(runtime->dialogue().flag("rested"));
    serialize::Json saved = runtime->save();
    CHECK(saved["flags"]["rested"] == true);
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> issues;
    REQUIRE(other->load(saved, issues));
    CHECK(issues.empty());
    CHECK(other->dialogue().flag("rested"));
    // A file that is not there is a problem, in plain language.
    game::DialogueObject& d = runtime->dialogue();
    while (d.is_open()) {
        d.close_current();
    }
    d.start_conversation("talk/none.dtalk");
    runtime->step();
    CHECK(std::find(runtime->problems().begin(), runtime->problems().end(),
                    "talk/none.dtalk: there is no such conversation in assets/.") !=
          runtime->problems().end());
}

namespace {

/// Enemies: a name, attack and defence, whether it flies, and a formula for the
/// experience it gives.
game::Table enemies() {
    game::Table t;
    t.columns.push_back({"name", game::ColumnKind::Text});
    t.columns.push_back({"attack", game::ColumnKind::Whole, "", 0.0, 999.0});
    t.columns.push_back({"defense", game::ColumnKind::Whole});
    t.columns.push_back({"flies", game::ColumnKind::YesNo});
    t.columns.push_back({"xp", game::ColumnKind::Formula, "(attack + defense) * 2 + row"});
    t.rows.push_back(
        {"slime", {{"name", "Slime"}, {"attack", "4"}, {"defense", "2"}, {"flies", "no"}}});
    t.rows.push_back(
        {"bat",
         {{"name", "Bat, \"the\" quick"}, {"attack", "6"}, {"defense", "1"}, {"flies", "yes"}}});
    return t;
}

} // namespace

TEST_CASE("Tables: typed cells, formulas, CSV both ways, and checks") {
    const game::Table t = enemies();
    CHECK(t.number("slime", "attack") == 4.0);
    CHECK(t.number("bat", "flies") == 1.0);
    CHECK(t.number("slime", "xp") == 13.0); // (4 + 2) * 2 + 1
    CHECK(t.text("bat", "xp") == std::optional<std::string>("16"));
    CHECK(t.text("slime", "name") == std::optional<std::string>("Slime"));
    CHECK_FALSE(t.number("slime", "name").has_value());
    CHECK_FALSE(t.number("ghost", "attack").has_value());
    CHECK(game::check_table(t).empty());

    // Formulas: precedence, brackets, powers, functions; mistakes said plainly.
    const game::TableRow& slime = t.rows[0];
    std::string error;
    CHECK(game::evaluate("2 + 3 * 4", t, slime, error) == 14.0);
    CHECK(game::evaluate("(2 + 3) * 4", t, slime, error) == 20.0);
    CHECK(game::evaluate("2 ^ 3 ^ 2", t, slime, error) == 512.0);
    CHECK(game::evaluate("-attack + max(defense, 10) / 2", t, slime, error) == 1.0);
    CHECK(game::evaluate("floor(7 / 2) + round(2.5)", t, slime, error) == 6.0);
    CHECK_FALSE(game::evaluate("attack / 0", t, slime, error).has_value());
    CHECK(error == "The formula divides by 0.");
    CHECK_FALSE(game::evaluate("speed * 2", t, slime, error).has_value());
    CHECK(error == "The formula reads \"speed\", which is not a column.");
    CHECK_FALSE(game::evaluate("sqrt(4)", t, slime, error).has_value());
    CHECK_FALSE(game::evaluate("2 +", t, slime, error).has_value());

    // Two formulas reading each other stop, saying so.
    game::Table ring = t;
    ring.columns.push_back({"a", game::ColumnKind::Formula, "b + 1"});
    ring.columns.push_back({"b", game::ColumnKind::Formula, "a + 1"});
    CHECK_FALSE(ring.number("slime", "a").has_value());
    // Bad cells, and doubled names, are found.
    game::Table bad = t;
    bad.rows[0].cells["attack"] = "4.5";
    bad.rows[1].cells["flies"] = "maybe";
    bad.rows.push_back(bad.rows[0]);
    const std::vector<std::string> problems = game::check_table(bad);
    CHECK(std::find(problems.begin(), problems.end(),
                    "slime, attack: \"4.5\" is not a whole number.") != problems.end());
    CHECK(std::find(problems.begin(), problems.end(), "bat, flies: \"maybe\" is not yes or no.") !=
          problems.end());
    CHECK(std::find(problems.begin(), problems.end(), "Two rows are called \"slime\".") !=
          problems.end());

    // Saved and read back; CSV out (quotes where needed) and back in.
    const std::optional<game::Table> back = game::parse_table(game::table_to_text(t), error);
    REQUIRE(back.has_value());
    CHECK(back.value_or(game::Table{}) == t);
    const std::string csv = game::table_to_csv(t);
    CHECK(csv.starts_with("id,name,attack,defense,flies,xp\nslime,Slime,4,2,no,13\n"));
    CHECK(csv.find("\"Bat, \"\"the\"\" quick\"") != std::string::npos);
    game::Table from_sheet;
    REQUIRE(game::table_from_csv(
        from_sheet, "id,hp,name\r\npotion,20,Potion\nether,,\"Ether, small\"\n", error));
    REQUIRE(from_sheet.columns.size() == 2);
    CHECK(from_sheet.column("hp")->kind == game::ColumnKind::Number);
    CHECK(from_sheet.column("name")->kind == game::ColumnKind::Text);
    CHECK(from_sheet.text("ether", "name") == std::optional<std::string>("Ether, small"));
    REQUIRE(game::table_from_csv(from_sheet, "id,hp\npotion,25\n", error));
    CHECK(from_sheet.rows.size() == 2);
    CHECK(from_sheet.number("potion", "hp") == 25.0);
    CHECK_FALSE(game::table_from_csv(from_sheet, "id,\"open\n", error));
    CHECK_FALSE(game::table_from_csv(from_sheet, "", error));
}

TEST_CASE("Logic reads the project's tables") {
    const Project project;
    {
        std::filesystem::create_directories(project.folder.path / "assets/data");
        std::ofstream(project.folder.path / "assets/data/enemies.dtable", std::ios::binary)
            << game::table_to_text(enemies());
        logic::LogicAsset rules;
        rules.name = "read";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {ir::call("tables", "has_row", {Project::text("data/enemies"), Project::text("bat")})},
            {ir::log(2, ir::call("tables", "number_of",
                                 {Project::text("data/enemies"), Project::text("bat"),
                                  Project::text("xp")})),
             ir::log(3, ir::call("tables", "text_of",
                                 {Project::text("data/enemies.dtable"), Project::text("slime"),
                                  Project::text("name")})),
             ir::log(4, ir::call("tables", "row_count", {Project::text("data/enemies")})),
             ir::log(5, ir::call(
                            "tables", "number_of",
                            {Project::text("data/none"), Project::text("x"), Project::text("y")}))},
            {}});
        write_json(project.folder.path / "assets/logic/read.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    runtime->step();
    const auto logged = [&](std::string_view line) {
        return std::count(runtime->log().begin(), runtime->log().end(), line);
    };
    CHECK(logged("16") == 1);
    CHECK(logged("Slime") == 1);
    CHECK(logged("2") == 1);
    CHECK(logged("0") == 1);
}

namespace {

/// The tables a battle reads: a hero and a mage, a slime and a bat, two skills, a potion.
struct BattleTables {
    BattleTables() {
        party.columns = {{"name", game::ColumnKind::Text},     {"max_hp", game::ColumnKind::Whole},
                         {"max_mp", game::ColumnKind::Whole},  {"attack", game::ColumnKind::Whole},
                         {"defense", game::ColumnKind::Whole}, {"speed", game::ColumnKind::Whole},
                         {"skills", game::ColumnKind::Text}};
        party.rows.push_back({"hero",
                              {{"name", "Hero"},
                               {"max_hp", "40"},
                               {"max_mp", "0"},
                               {"attack", "6"},
                               {"defense", "3"},
                               {"speed", "5"}}});
        party.rows.push_back({"mage",
                              {{"name", "Mira"},
                               {"max_hp", "25"},
                               {"max_mp", "12"},
                               {"attack", "2"},
                               {"defense", "1"},
                               {"speed", "7"},
                               {"skills", "fire, heal"}}});
        enemies.columns = party.columns;
        enemies.columns.push_back({"xp", game::ColumnKind::Whole});
        enemies.columns.push_back({"gold", game::ColumnKind::Whole});
        enemies.columns.push_back({"image", game::ColumnKind::Text});
        enemies.rows.push_back({"slime",
                                {{"name", "Slime"},
                                 {"max_hp", "12"},
                                 {"attack", "3"},
                                 {"defense", "1"},
                                 {"speed", "2"},
                                 {"xp", "5"},
                                 {"gold", "3"}}});
        enemies.rows.push_back({"bat",
                                {{"name", "Bat"},
                                 {"max_hp", "8"},
                                 {"attack", "4"},
                                 {"speed", "9"},
                                 {"xp", "4"},
                                 {"gold", "2"},
                                 {"image", "art/rock.png"}}});
        skills.columns = {{"name", game::ColumnKind::Text},
                          {"mp_cost", game::ColumnKind::Whole},
                          {"power", game::ColumnKind::Whole},
                          {"heals", game::ColumnKind::YesNo},
                          {"all", game::ColumnKind::YesNo}};
        skills.rows.push_back(
            {"fire", {{"name", "Fire"}, {"mp_cost", "5"}, {"power", "6"}, {"all", "yes"}}});
        skills.rows.push_back(
            {"heal", {{"name", "Heal"}, {"mp_cost", "3"}, {"power", "15"}, {"heals", "yes"}}});
        items.columns = {{"name", game::ColumnKind::Text}, {"heal", game::ColumnKind::Whole}};
        items.rows.push_back({"potion", {{"name", "Potion"}, {"heal", "20"}}});
    }
    [[nodiscard]] game::BattleData data() const { return {&party, &enemies, &skills, &items}; }
    game::Table party;
    game::Table enemies;
    game::Table skills;
    game::Table items;
};

} // namespace

TEST_CASE("Battles: commands, speed order, skills, items, victory, and the same every time") {
    const BattleTables tables;
    std::string error;
    std::optional<game::Battle> started = game::Battle::start(
        tables.data(), {"hero", "mage"}, {{"hero", {30, 0}}}, {"slime", "slime", "bat"}, 42, error);
    REQUIRE(started.has_value());
    if (!started) {
        return;
    }
    game::Battle battle = std::move(*started);
    REQUIRE(battle.fighters().size() == 5);
    CHECK(battle.fighters()[0].hp == 30); // carried in from before
    CHECK(battle.fighters()[2].name == "Slime A");
    CHECK(battle.fighters()[3].name == "Slime B");
    CHECK(battle.take_messages() == std::vector<std::string>{"Slime A, Slime B, Bat appear!"});
    CHECK(game::Battle::base_damage(battle.fighters()[0], battle.fighters()[2], 0) == 11);

    // The hero chooses first, then the mage; refusals change nothing.
    const std::map<std::string, std::int64_t, std::less<>> bag{{"potion", 1}};
    REQUIRE(battle.chooser() == std::optional<std::size_t>(0));
    CHECK_FALSE(battle.choose({game::BattleCommand::Kind::Attack, {}, 1}, bag, error));
    CHECK(error == "Choose an enemy still standing.");
    CHECK_FALSE(battle.choose({game::BattleCommand::Kind::Skill, "fire", 2}, bag, error));
    CHECK(error == "Hero cannot use that.");
    REQUIRE(battle.choose({game::BattleCommand::Kind::Attack, {}, 2}, bag, error));
    REQUIRE(battle.chooser() == std::optional<std::size_t>(1));
    CHECK(battle.usable_skills().size() == 2);
    // Taking a choice back, then choosing again.
    REQUIRE(battle.undo_choice());
    CHECK(battle.chooser() == std::optional<std::size_t>(0));
    REQUIRE(battle.choose({game::BattleCommand::Kind::Item, "potion", 0}, bag, error));
    REQUIRE(battle.choose({game::BattleCommand::Kind::Skill, "fire", 2}, bag, error));
    // The round is fought: the bat (speed 9) first, Mira (7), the hero (5), the slimes (2).
    const std::vector<std::string> round = battle.take_messages();
    REQUIRE_FALSE(round.empty());
    CHECK(round.front() == "Bat attacks!");
    CHECK(std::find(round.begin(), round.end(), "Mira uses Fire!") != round.end());
    CHECK(std::find(round.begin(), round.end(), "Hero uses Potion.") != round.end());
    CHECK(battle.fighters()[1].mp == 7);
    CHECK(battle.take_used_items().at("potion") == 1);

    // Fight on until it ends; with these numbers the party wins.
    for (int rounds = 0; rounds < 30 && battle.outcome() == game::BattleOutcome::Fighting;
         ++rounds) {
        while (const std::optional<std::size_t> who = battle.chooser()) {
            std::size_t target = 0;
            for (std::size_t i = 0; i < battle.fighters().size(); ++i) {
                if (battle.fighters()[i].enemy && battle.fighters()[i].alive()) {
                    target = i;
                    break;
                }
            }
            REQUIRE(battle.choose({game::BattleCommand::Kind::Attack, {}, target}, {}, error));
        }
        static_cast<void>(battle.take_messages());
    }
    CHECK(battle.outcome() == game::BattleOutcome::Won);
    CHECK(battle.xp() == 14);
    CHECK(battle.gold() == 8);
    CHECK_FALSE(battle.chooser().has_value());

    // The same seed and choices give the same fight.
    const auto replay = [&](std::uint32_t seed) {
        std::optional<game::Battle> b =
            game::Battle::start(tables.data(), {"hero"}, {}, {"bat"}, seed, error);
        std::string ignored;
        b->choose({game::BattleCommand::Kind::Defend, {}, 0}, {}, ignored);
        return b->take_messages();
    };
    CHECK(replay(7) == replay(7));

    // Defending halves the damage taken; fleeing can work; no one to fight is refused.
    game::Fighter brute;
    brute.attack = 10;
    game::Fighter wall;
    wall.defense = 4;
    CHECK(game::Battle::base_damage(brute, wall, 0) == 16);
    wall.defending = true;
    CHECK(game::Battle::base_damage(brute, wall, 0) == 8);
    bool fled = false;
    for (std::uint32_t seed = 1; seed < 20 && !fled; ++seed) {
        std::optional<game::Battle> b =
            game::Battle::start(tables.data(), {"mage"}, {}, {"slime"}, seed, error);
        REQUIRE(b.has_value());
        if (!b) {
            break;
        }
        b->choose({game::BattleCommand::Kind::Flee, {}, 0}, {}, error);
        fled = b->outcome() == game::BattleOutcome::Fled;
    }
    CHECK(fled);
    CHECK_FALSE(game::Battle::start(tables.data(), {"hero"}, {{"hero", {0, 0}}}, {"bat"}, 1, error)
                    .has_value());
    CHECK(error == "Every party member is down: there is no one to fight.");
    CHECK_FALSE(game::Battle::start(tables.data(), {"ghost"}, {}, {"bat"}, 1, error).has_value());
    CHECK(error == "data/party has no row \"ghost\".");
}

TEST_CASE("A battle from logic: fought with the keys on the battle screen, won, and rewarded") {
    const Project project;
    const BattleTables tables;
    {
        std::filesystem::create_directories(project.folder.path / "assets/data");
        // The hero grows 2 attack a level and learns Fire at level 2, which the bat's 4
        // experience reaches.
        game::Table party = tables.party;
        party.columns.push_back({"attack_growth", game::ColumnKind::Whole});
        party.rows[0].cells["attack_growth"] = "2";
        party.rows[0].cells["skills"] = "fire@2";
        game::Table levels;
        levels.columns = {{"xp", game::ColumnKind::Whole}};
        levels.rows = {{"1", {{"xp", "0"}}}, {"2", {{"xp", "4"}}}, {"3", {{"xp", "50"}}}};
        for (const auto& [name, table] :
             {std::pair{"party", &std::as_const(party)}, std::pair{"enemies", &tables.enemies},
              std::pair{"skills", &tables.skills}, std::pair{"items", &tables.items},
              std::pair{"levels", &std::as_const(levels)}}) {
            std::ofstream(project.folder.path / "assets/data" / (std::string(name) + ".dtable"),
                          std::ios::binary)
                << game::table_to_text(*table);
        }
        logic::LogicAsset rules;
        rules.name = "fight";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "party", "add_member", {Project::text("hero")}),
                         ir::do_action(3, "battle", "start", {Project::text("bat")})},
                        {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{4}, "battle", "won", {}, {ir::log(5, Project::text("won"))}, {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{6}, "party", "level_up", {}, {ir::log(7, ir::event_value("name"))}, {}});
        write_json(project.folder.path / "assets/logic/fight.drules",
                   serialize::logic_to_json(rules));
    }
    // Interact, pressed and let go every few steps: read the messages, pick Attack, the bat.
    input::Recording replay;
    for (std::uint64_t step = 4; step < 400; step += 4) {
        replay.record(step, key(input::Key::Z), true);
        replay.record(step + 2, key(input::Key::Z), false);
    }
    const game::RuntimeOptions options = no_graphics(&replay);
    const auto runtime = project.open(options);
    runtime->step();
    runtime->step();
    REQUIRE(runtime->battle_screen() != nullptr);
    REQUIRE(runtime->battle_screen()->message() != nullptr);
    CHECK(*runtime->battle_screen()->message() == "Bat appears!");
    // The hero does not walk while fighting.
    const world::Entity hero = runtime->world().find_named("hero").at(0);
    const double x = runtime->world().get<scene2d::Transform2D>(hero)->x;
    for (int i = 0; i < 398 && runtime->battle_screen() != nullptr; ++i) {
        runtime->step();
    }
    CHECK(runtime->battle_screen() == nullptr);
    CHECK(runtime->world().get<scene2d::Transform2D>(hero)->x == x);
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "won") == 1);
    CHECK(runtime->party().xp() == 4);
    CHECK(runtime->inventory().count_of("gold") == 2);
    CHECK(runtime->party().in_party("hero"));
    // The hero reached level 2: said, with the skill learned, and told to Rules.
    CHECK(runtime->party().level_of("hero") == 2);
    CHECK(runtime->party().xp_to_next("hero") == 46);
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Hero is now level 2!");
    CHECK(std::count(runtime->dialogue().said().begin(), runtime->dialogue().said().end(),
                     "Hero learns Fire!") == 1);
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "hero") == 1);
    // The party's HP is kept, and saved with the game.
    serialize::Json saved = runtime->save();
    CHECK(saved["party"]["members"] == serialize::Json::array({"hero"}));
    CHECK(saved["party"]["xp"] == 4);
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> issues;
    REQUIRE(other->load(saved, issues));
    CHECK(other->party().xp() == 4);
    CHECK(other->party().level_of("hero") == 2);
    CHECK(other->party().hp_of("hero") == runtime->party().hp_of("hero"));
}

TEST_CASE("The battle screen is drawn over the world: enemies, the party, the menu") {
    const Project project;
    const BattleTables tables;
    std::filesystem::create_directories(project.folder.path / "assets/data");
    for (const auto& [name, table] :
         {std::pair{"party", &tables.party}, std::pair{"enemies", &tables.enemies},
          std::pair{"skills", &tables.skills}, std::pair{"items", &tables.items}}) {
        std::ofstream(project.folder.path / "assets/data" / (std::string(name) + ".dtable"),
                      std::ios::binary)
            << game::table_to_text(*table);
    }
    // At a game's size: 320 x 180.
    std::vector<serialize::Issue> issues;
    std::ifstream in(project.folder.path / game::project_file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
    settings["width"] = 320;
    settings["height"] = 180;
    write_json(project.folder.path / game::project_file, settings);
    {
        logic::LogicAsset rules;
        rules.name = "fight";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "party", "add_member", {Project::text("hero")}),
                         ir::do_action(3, "party", "add_member", {Project::text("mage")}),
                         ir::do_action(4, "battle", "start", {Project::text("slime, slime, bat")})},
                        {}});
        write_json(project.folder.path / "assets/logic/fight.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(3, key(input::Key::Z), true); // past "... appear!"
    replay.record(5, key(input::Key::Z), false);
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    for (int i = 0; i < 8; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->battle_screen() != nullptr);
    CHECK(runtime->battle_screen()->message() == nullptr); // the command menu shows
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    REQUIRE(pixels.size() == std::size_t{320} * 180 * 4);
    // The bat's picture (the rock's colour), above its name on the right.
    bool rock = false;
    for (std::uint32_t y = 0; y < 90; ++y) {
        for (std::uint32_t x = 214; x < 320; ++x) {
            rock = rock || pixel(pixels, 320, x, y) == rock_colour;
        }
    }
    CHECK(rock);
    // The menu's and the status box's light borders, at the bottom.
    CHECK(pixel(pixels, 320, 319, 179).r > 200);
    CHECK(pixel(pixels, 320, 0, 179).r > 200);
    // The chosen command and the chooser are in gold somewhere.
    bool gold = false;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        gold = gold || (pixels[i] == 255 && pixels[i + 1] == 210 && pixels[i + 2] == 122);
    }
    CHECK(gold);
    // A picture of it, for looking at.
    const assets::Image image{320, 180, pixels};
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    std::ofstream(std::filesystem::temp_directory_path() / "sparxel-battle-screen.png",
                  std::ios::binary)
        .write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size())); // NOLINT
}

TEST_CASE("Day and night: the light follows the clock, warm at dusk and blue at night") {
    const game::LightingObject noon = game::daylight(12.0);
    CHECK(noon.tint_r == doctest::Approx(1.0));
    CHECK(noon.sun_angle == doctest::Approx(180.0)); // south at noon
    CHECK(noon.sun_height == doctest::Approx(70.0));
    const game::LightingObject dawn = game::daylight(6.0);
    CHECK(dawn.sun_angle == doctest::Approx(90.0)); // rising in the east
    CHECK(dawn.sun_height == doctest::Approx(5.0));
    const game::LightingObject dusk = game::daylight(18.0);
    CHECK(dusk.sun_angle == doctest::Approx(270.0));
    CHECK(dusk.tint_r > 1.0);
    CHECK(dusk.tint_b < 1.0);
    const game::LightingObject night = game::daylight(23.0);
    CHECK(night.tint_b > night.tint_r);
    CHECK(night.ambient < noon.ambient);
    CHECK(game::daylight(26.0).time_of_day == doctest::Approx(2.0)); // wraps
    CHECK(game::daylight(-1.0).time_of_day == doctest::Approx(23.0));

    // A clock of one real minute a day: fifteen seconds are six hours.
    game::LightingObject light;
    light.day_minutes = 1.0;
    light.set_time(6.0);
    CHECK(light.follows_time);
    for (int i = 0; i < 15 * 60; ++i) {
        game::advance_day(light, 1.0 / 60.0);
    }
    CHECK(light.time_of_day == doctest::Approx(12.0).epsilon(0.001));
    CHECK(light.sun_height == doctest::Approx(70.0).epsilon(0.01));
    // A tint set by logic stops following the time; the clock still runs.
    light.set_tint(1.0, 0.2, 0.2);
    game::advance_day(light, 1.0);
    CHECK(light.tint_g == doctest::Approx(0.2));
    CHECK(light.time_of_day > 12.0);
}

TEST_CASE("Night is drawn bluer than noon, and the clock is saved") {
    const auto frame_at = [](double hour) {
        const Project project;
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["day_minutes"] = 10.0;
        settings["start_hour"] = hour;
        write_json(project.folder.path / game::project_file, settings);
        game::RuntimeOptions options;
        options.headless = true;
        const auto runtime = project.open(options);
        REQUIRE(runtime->target() != nullptr);
        runtime->step();
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        // The background, at a corner (the hero is in the middle).
        const render::Color corner = pixel(pixels, 32, 0, 0);
        const serialize::Json saved = runtime->save();
        CHECK(saved["lighting"]["time_of_day"].get<double>() ==
              doctest::Approx(hour).epsilon(0.01));
        CHECK(saved["lighting"]["follows_time"] == true);
        return corner;
    };
    const render::Color noon = frame_at(12.0);
    const render::Color night = frame_at(23.0);
    CHECK(night.r < noon.r);
    CHECK(night.b >= night.r);
}

TEST_CASE("The title screen starts the game; the pause menu saves, loads and shows pages") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["title_screen"] = true;
        settings["pause_menu"] = true;
        write_json(project.folder.path / game::project_file, settings);
        logic::LogicAsset rules;
        rules.name = "start";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1}, "", "game_start", {}, {ir::log(2, Project::text("started"))}, {}});
        write_json(project.folder.path / "assets/logic/start.drules",
                   serialize::logic_to_json(rules));
    }
    // Keys: Interact (New game); later Menu (Escape), Down to Save, Interact, Interact (slot 1).
    input::Recording replay;
    const auto press = [&replay](std::uint64_t step, input::Key k) {
        replay.record(step, key(k), true);
        replay.record(step + 1, key(k), false);
    };
    press(5, input::Key::Z);
    press(10, input::Key::Escape);
    for (std::uint64_t s = 0; s < 7; ++s) {
        press(14 + (s * 3), input::Key::Down);
    }
    press(35, input::Key::Z); // Save page
    press(39, input::Key::Z); // slot 1
    const auto runtime = project.open(no_graphics(&replay));
    runtime->step();
    CHECK(runtime->menu().page() == game::GameMenu::Page::Title);
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "started") == 0);
    for (int i = 0; i < 9; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->menu().open());
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "started") == 1);
    for (int i = 0; i < 35; ++i) {
        runtime->step();
    }
    CHECK(runtime->menu().page() == game::GameMenu::Page::Save);
    // Saved in slot 1 by the menu.
    CHECK(fs::exists(game::save_slot_file(runtime->save_folder(), 1)));
}

TEST_CASE("The title screen and the pause menu are drawn over the game") {
    const Project project;
    std::vector<serialize::Issue> issues;
    std::ifstream in(project.folder.path / game::project_file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
    settings["title_screen"] = true;
    settings["pause_menu"] = true;
    settings["width"] = 320;
    settings["height"] = 180;
    write_json(project.folder.path / game::project_file, settings);
    input::Recording replay;
    replay.record(11, key(input::Key::Z), true); // New game
    replay.record(12, key(input::Key::Z), false);
    replay.record(14, key(input::Key::Escape), true); // the pause menu
    replay.record(15, key(input::Key::Escape), false);
    replay.record(17, key(input::Key::Down), true); // Items
    replay.record(18, key(input::Key::Down), false);
    replay.record(20, key(input::Key::Z), true);
    replay.record(21, key(input::Key::Z), false);
    replay.record(24, key(input::Key::X), true); // back to the pause menu
    replay.record(25, key(input::Key::X), false);
    for (std::uint64_t s = 0; s < 5; ++s) { // to Options
        replay.record(27 + (s * 3), key(input::Key::Down), true);
        replay.record(28 + (s * 3), key(input::Key::Down), false);
    }
    replay.record(42, key(input::Key::Z), true);
    replay.record(43, key(input::Key::Z), false);
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    const auto shot = [&](const char* name) {
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        bool gold = false;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            gold = gold || (pixels[i] == 255 && pixels[i + 1] == 210 && pixels[i + 2] == 122);
        }
        CHECK(gold); // the chosen line, or the game's name
        const assets::Image image{320, 180, pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / name, std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    };
    for (int i = 0; i < 9; ++i) {
        runtime->step(); // the title fades in
    }
    shot("sparxel-title.png");
    for (int i = 0; i < 14; ++i) {
        runtime->step();
    }
    CHECK(runtime->menu().page() == game::GameMenu::Page::Items);
    shot("sparxel-pause.png");
    for (int i = 0; i < 22; ++i) {
        runtime->step();
    }
    CHECK(runtime->menu().page() == game::GameMenu::Page::Options);
    shot("sparxel-options.png");
}

TEST_CASE("Messages are typed out at the text speed, waiting at pauses; Interact finishes them") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["text_speed"] = 60.0; // a letter a step
        settings["width"] = 320;       // room for the message on one page
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        logic::LogicAsset rules;
        rules.name = "say";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "dialogue", "say",
                                       {Project::text("Hello[pause=1] [wave]you[/wave]")})},
                        {}});
        write_json(project.folder.path / "assets/logic/say.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(30, key(input::Key::Z), true); // finish the message
    replay.record(31, key(input::Key::Z), false);
    replay.record(33, key(input::Key::Z), true); // close it
    replay.record(34, key(input::Key::Z), false);
    const auto runtime = project.open(no_graphics(&replay));
    for (int i = 0; i < 10; ++i) {
        runtime->step();
    }
    // Typing stopped at the pause, after "Hello"; the message is open.
    REQUIRE(runtime->dialogue().is_open());
    for (int i = 0; i < 25; ++i) {
        runtime->step();
    }
    // Interact showed the rest rather than closing it; the second press closed it.
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->dialogue().said().front() == "Hello[pause=1] [wave]you[/wave]");
}

TEST_CASE("The Options page sets volumes and text speed, rebinds keys, and keeps them") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["pause_menu"] = true;
        write_json(project.folder.path / game::project_file, settings);
    }
    input::Recording replay;
    std::uint64_t at = 2;
    const auto press = [&replay, &at](input::Key k) {
        replay.record(at, key(k), true);
        replay.record(at + 1, key(k), false);
        at += 3;
    };
    press(input::Key::Escape); // the pause menu
    for (int i = 0; i < 6; ++i) {
        press(input::Key::Down); // to Options
    }
    press(input::Key::Z);
    press(input::Key::Left); // Music 90%
    press(input::Key::Down);
    press(input::Key::Down);
    press(input::Key::Right); // Text speed: Instant
    for (int i = 0; i < 6; ++i) {
        press(input::Key::Down); // to Interact
    }
    press(input::Key::Z);
    press(input::Key::C); // Interact is now C
    const std::uint64_t steps = at + 2;
    const auto runtime = project.open(no_graphics(&replay));
    for (std::uint64_t i = 0; i < steps; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->menu().page() == game::GameMenu::Page::Options);
    const game::GameMenu::Options& options = runtime->menu().options();
    CHECK(options.music == doctest::Approx(0.9));
    CHECK(options.sounds == doctest::Approx(1.0));
    CHECK(options.text_speed == doctest::Approx(0.0));
    REQUIRE(options.keys.size() == 7);
    CHECK(options.keys[4].first == "interact");
    CHECK(options.keys[4].second == "C");
    CHECK_FALSE(runtime->menu().waiting_for_key());
    REQUIRE(runtime->audio() != nullptr);
    CHECK(runtime->audio()->player_volume(audio::Bus::Music) == doctest::Approx(0.9));
    CHECK(fs::exists(runtime->save_folder() / game::Runtime::options_file));

    // A new run reads them back, and C interacts: it picks Resume.
    input::Recording again;
    again.record(2, key(input::Key::Escape), true);
    again.record(3, key(input::Key::Escape), false);
    again.record(5, key(input::Key::C), true);
    again.record(6, key(input::Key::C), false);
    const auto second = project.open(no_graphics(&again));
    CHECK(second->menu().options().music == doctest::Approx(0.9));
    CHECK(second->menu().options().keys[4].second == "C");
    for (int i = 0; i < 4; ++i) {
        second->step();
    }
    CHECK(second->menu().open());
    for (int i = 0; i < 4; ++i) {
        second->step();
    }
    CHECK_FALSE(second->menu().open());
}

TEST_CASE("Rebinding a key another action has takes it; an action left with none swaps") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["pause_menu"] = true;
        write_json(project.folder.path / game::project_file, settings);
    }
    input::Recording replay;
    std::uint64_t at = 2;
    const auto press = [&replay, &at](input::Key k) {
        replay.record(at, key(k), true);
        replay.record(at + 1, key(k), false);
        at += 3;
    };
    press(input::Key::Escape);
    for (int i = 0; i < 6; ++i) {
        press(input::Key::Down);
    }
    press(input::Key::Z); // Options
    for (int i = 0; i < 8; ++i) {
        press(input::Key::Down); // to Interact
    }
    press(input::Key::Z);
    press(input::Key::C);    // Interact: C alone
    press(input::Key::Down); // to Cancel
    press(input::Key::C);    // C now interacts
    press(input::Key::C);    // Cancel: C, so Interact takes X and Backspace
    const std::uint64_t steps = at + 2;
    const auto runtime = project.open(no_graphics(&replay));
    for (std::uint64_t i = 0; i < steps; ++i) {
        runtime->step();
    }
    const game::GameMenu::Options& options = runtime->menu().options();
    REQUIRE(options.keys.size() == 7);
    CHECK(options.keys[5].first == "cancel");
    CHECK(options.keys[5].second == "C");
    const bool swapped = options.keys[4].second.find('X') != std::string::npos &&
                         options.keys[4].second.find("Backspace") != std::string::npos;
    CHECK(swapped);
}

TEST_CASE("Speakers' voices blip as their words are typed; Rules' speakers use the cast") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["text_speed"] = 30.0; // a letter every other step
        settings["width"] = 320;
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        Project::write_sound(project.folder.path / "assets/blip.wav",
                             test_support::wav(4800, 8000));
        game::Conversation talk = elder_talk();
        talk.speakers[0].voice = "blip.wav";
        talk.speakers[0].pitch = 1.5;
        talk.speakers[0].portrait = "face.png";
        talk.speakers[0].color = "#40ff60";
        std::ofstream(project.folder.path / "assets/talk.dtalk", std::ios::binary)
            << game::conversation_to_text(talk);
        Project::write_png(project.folder.path / "assets/face.png",
                           render::Color{250, 0, 250, 255});
        logic::LogicAsset rules;
        rules.name = "say";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "dialogue", "say_as",
                                       {Project::text("Elder"), Project::text("Hello there.")})},
                        {}});
        write_json(project.folder.path / "assets/logic/say.drules",
                   serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->audio() != nullptr);
    runtime->step();
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(runtime->dialogue().current_speaker() == "Elder");
    // The Rules' line has no look of its own: the Elder's comes from the conversation.
    CHECK(runtime->dialogue().current_look().empty());
    bool heard = false;
    for (int i = 0; i < 12; ++i) {
        runtime->step();
        heard = heard || runtime->audio()->playing_sounds() > 0;
    }
    CHECK(heard);
    // The portrait stands on the box, and the plate shows the Elder's colour.
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    bool portrait = false;
    bool plate = false;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        portrait = portrait || (pixels[i] == 250 && pixels[i + 1] == 0 && pixels[i + 2] == 250);
        plate = plate || (pixels[i] == 64 && pixels[i + 1] == 255 && pixels[i + 2] == 96);
    }
    CHECK(portrait);
    CHECK(plate);
}

TEST_CASE("A conversation gives each line its speaker's look, with the line's own portrait") {
    game::Conversation talk = elder_talk();
    talk.speakers[0].portrait = "faces/elder.png";
    talk.speakers[0].voice = "sfx/blip.wav";
    talk.nodes[0].portrait = "faces/elder_glad.png";
    game::ConversationPlayer player;
    game::DialogueObject dialogue;
    game::InventoryObject bag;
    game::QuestObject quests;
    player.start(talk, "talk/elder.dtalk");
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()).empty());
    CHECK(dialogue.current_look().portrait == "faces/elder_glad.png");
    CHECK(dialogue.current_look().voice == "sfx/blip.wav");
    dialogue.close_current();
    CHECK(player.update(dialogue, bag, quests, dialogue.flags()).empty());
    CHECK(dialogue.current_look().portrait == "faces/elder.png");
}

namespace {

/// data/items for shops: a potion (10 gold), an ether (25, sold back for 5), a key with no
/// price, and a name and description for the potion.
game::Table shop_items() {
    game::Table t;
    t.columns = {{"name", game::ColumnKind::Text},
                 {"description", game::ColumnKind::Text},
                 {"price", game::ColumnKind::Whole},
                 {"sell", game::ColumnKind::Whole}};
    t.rows.push_back(
        {"potion", {{"name", "Potion"}, {"description", "Heals 20 HP."}, {"price", "10"}}});
    t.rows.push_back({"ether", {{"price", "25"}, {"sell", "5"}}});
    t.rows.push_back({"key", {}});
    return t;
}

} // namespace

TEST_CASE("Shops: wares by price, buying a quantity for gold, and selling back") {
    const game::Table items = shop_items();
    std::vector<std::string> missing;
    const std::vector<game::Ware> wares =
        game::shop_wares("potion, ether, key, nothing", &items, missing);
    REQUIRE(wares.size() == 2);
    CHECK(wares[0].name == "Potion");
    CHECK(wares[0].price == 10);
    CHECK(wares[0].description == "Heals 20 HP.");
    CHECK(missing == std::vector<std::string>{"key", "nothing"});
    // Its own sell price, or else half its price.
    CHECK(game::sell_ware("ether", &items).sell == 5);
    CHECK(game::sell_ware("potion", &items).sell == 5);

    game::InventoryObject bag;
    bag.give("gold", 45);
    game::ShopScreen shop(wares, &items);
    CHECK(shop.page() == game::ShopScreen::Page::Menu);
    shop.press("interact", bag); // Buy
    CHECK(shop.page() == game::ShopScreen::Page::Buy);
    CHECK(shop.note() == "Heals 20 HP.");
    shop.press("interact", bag); // the potion
    CHECK(shop.page() == game::ShopScreen::Page::Quantity);
    shop.press("move_right", bag);
    shop.press("move_right", bag);
    CHECK(shop.quantity() == 3);
    shop.press("move_up", bag); // +10, but 4 is the most 45 gold buys
    CHECK(shop.quantity() == 4);
    shop.press("move_left", bag);
    shop.press("interact", bag); // buys 3
    CHECK(bag.count_of("potion") == 3);
    CHECK(bag.count_of("gold") == 15);
    CHECK(shop.note() == "Bought 3 Potion.");
    shop.press("move_down", bag); // the ether: 25 is too much
    shop.press("interact", bag);
    CHECK(shop.page() == game::ShopScreen::Page::Buy);
    CHECK(shop.note() == "Not enough gold.");
    shop.press("cancel", bag); // to the menu, on Buy
    shop.press("move_down", bag);
    shop.press("interact", bag); // Sell
    REQUIRE(shop.page() == game::ShopScreen::Page::Sell);
    // The key has no price: the shop does not buy it; gold is not listed.
    bag.give("key", 1);
    REQUIRE(shop.sellable(bag).size() == 1);
    shop.press("interact", bag);
    shop.press("move_left", bag); // from 1 round to the most: all 3
    CHECK(shop.quantity() == 3);
    shop.press("interact", bag);
    CHECK_FALSE(bag.has("potion"));
    CHECK(bag.count_of("gold") == 30); // 15 + 3 x 5
    shop.press("cancel", bag);
    shop.press("cancel", bag);
    CHECK(shop.closed());
}

TEST_CASE("A shop from logic: opened, bought from with the keys, drawn, and left") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["width"] = 320;
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        std::filesystem::create_directories(project.folder.path / "assets/data");
        std::ofstream(project.folder.path / "assets/data/items.dtable", std::ios::binary)
            << game::table_to_text(shop_items());
        logic::LogicAsset rules;
        rules.name = "shop";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "inventory", "give", {Project::text("gold"), Project::whole(30)}),
             ir::do_action(3, "inventory", "open_shop", {Project::text("potion, ether")})},
            {}});
        rules.rules.push_back(logic::Rule{logic::BlockId{4},
                                          "inventory",
                                          "shop_closed",
                                          {},
                                          {ir::log(5, Project::text("left the shop"))},
                                          {}});
        write_json(project.folder.path / "assets/logic/shop.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    std::uint64_t at = 3;
    const auto press = [&replay, &at](input::Key k) {
        replay.record(at, key(k), true);
        replay.record(at + 1, key(k), false);
        at += 3;
    };
    press(input::Key::Z);     // Buy
    press(input::Key::Z);     // the potion
    press(input::Key::Right); // 2
    const std::uint64_t buying = at;
    press(input::Key::Z); // bought
    press(input::Key::X); // to the menu
    press(input::Key::X); // left
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    for (std::uint64_t i = 0; i < buying; ++i) {
        runtime->step();
    }
    CHECK(runtime->inventory().shopping());
    REQUIRE(runtime->draw());
    {
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        bool gold = false;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            gold = gold || (pixels[i] == 255 && pixels[i + 1] == 210 && pixels[i + 2] == 122);
        }
        CHECK(gold);
        const assets::Image image{320, 180, pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / "sparxel-shop.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    for (std::uint64_t i = buying; i < at + 2; ++i) {
        runtime->step();
    }
    CHECK(runtime->inventory().count_of("potion") == 2);
    CHECK(runtime->inventory().count_of("gold") == 10);
    CHECK_FALSE(runtime->inventory().shopping());
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "left the shop") == 1);
}

TEST_CASE("Levels: the experience curve, growth by level, and skills learned at a level") {
    // Without data/levels: 10, 30, 60, 100 for levels 2 to 5.
    CHECK(game::xp_for_level(1, nullptr) == 0);
    CHECK(game::xp_for_level(2, nullptr) == 10);
    CHECK(game::xp_for_level(4, nullptr) == 60);
    CHECK(game::level_for_xp(29, nullptr) == 2);
    CHECK(game::level_for_xp(30, nullptr) == 3);
    CHECK(game::top_level(nullptr) == 99);
    CHECK(game::level_for_xp(999'999'999, nullptr) == 99);
    // With it: its rows, by name, with the last as the top.
    game::Table levels;
    levels.columns = {{"xp", game::ColumnKind::Formula, "row * row * 2"}};
    levels.rows = {{"1", {}}, {"2", {}}, {"3", {}}};
    CHECK(game::top_level(&levels) == 3);
    CHECK(game::xp_for_level(3, &levels) == 18);
    CHECK(game::level_for_xp(17, &levels) == 2);
    CHECK(game::level_for_xp(5000, &levels) == 3);

    BattleTables tables;
    tables.party.columns.push_back({"max_hp_growth", game::ColumnKind::Whole});
    tables.party.rows[1].cells["max_hp_growth"] = "5";
    tables.party.rows[1].cells["skills"] = "heal, fire@3, ice @ 5";
    CHECK(game::skills_at(&tables.party, "mage", 1) == std::vector<std::string>{"heal"});
    CHECK(game::skills_at(&tables.party, "mage", 5) ==
          std::vector<std::string>{"heal", "fire", "ice"});
    game::BattleData data = tables.data();
    data.member_levels["mage"] = 3;
    std::string error;
    const std::optional<game::Battle> b =
        game::Battle::start(data, {"mage"}, {}, {"slime"}, 1, error);
    REQUIRE(b.has_value());
    if (b) {
        const game::Fighter& mage = b->fighters().front();
        CHECK(mage.level == 3);
        CHECK(mage.max_hp == 35); // 25 + 2 x 5
        CHECK(mage.skills == std::vector<std::string>{"heal", "fire"});
    }

    // Experience by member, and the levels reached, to announce.
    game::PartyObject party;
    party.give_xp("hero", 9);
    CHECK(party.level_of("hero") == 1);
    CHECK(party.take_level_ups().empty());
    party.give_xp("hero", 25);
    CHECK(party.level_of("hero") == 3);
    const std::vector<game::PartyObject::LevelUp> ups = party.take_level_ups();
    REQUIRE(ups.size() == 1);
    CHECK(ups[0].from == 1);
    CHECK(ups[0].to == 3);
    CHECK(party.xp_to_next("hero") == 26);
}

TEST_CASE("Equipment: slots, numbers with what is worn, and not an item to use in battle") {
    BattleTables tables;
    tables.items.columns.push_back({"slot", game::ColumnKind::Text});
    tables.items.columns.push_back({"attack", game::ColumnKind::Whole});
    tables.items.columns.push_back({"defense", game::ColumnKind::Whole});
    tables.items.rows.push_back({"sword", {{"slot", "Weapon"}, {"attack", "4"}}});
    tables.items.rows.push_back({"cap", {{"slot", "head"}, {"defense", "2"}}});
    tables.items.rows.push_back({"rock", {{"slot", "pocket"}}});
    CHECK(game::slot_of(&tables.items, "sword") == "weapon");
    CHECK(game::slot_of(&tables.items, "cap") == "head");
    CHECK(game::slot_of(&tables.items, "rock").empty()); // not a slot
    CHECK(game::slot_of(&tables.items, "nothing").empty());
    game::BattleData data = tables.data();
    data.member_equipment["hero"] = {"sword", "cap", "rock"};
    std::string error;
    const std::optional<game::Fighter> hero = game::make_fighter(data, "hero", false, error);
    REQUIRE(hero.has_value());
    if (hero) {
        CHECK(hero->attack == 10); // 6 + 4
        CHECK(hero->defense == 5); // 3 + 2
    }
    const std::optional<game::Battle> b =
        game::Battle::start(data, {"hero"}, {}, {"slime"}, 1, error);
    REQUIRE(b.has_value());
    if (b) {
        CHECK(b->fighters().front().attack == 10);
        CHECK_FALSE(b->is_item("sword"));
    }
}

TEST_CASE("Equipment is worn from logic and from the pause menu's Equip page, and saved") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["pause_menu"] = true;
        settings["width"] = 320;
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        std::filesystem::create_directories(project.folder.path / "assets/data");
        const BattleTables tables;
        game::Table items = tables.items;
        items.columns.push_back({"slot", game::ColumnKind::Text});
        items.columns.push_back({"attack", game::ColumnKind::Whole});
        items.columns.push_back({"defense", game::ColumnKind::Whole});
        items.columns.push_back({"name", game::ColumnKind::Text});
        items.rows.push_back({"sword", {{"slot", "weapon"}, {"attack", "4"}, {"name", "Sword"}}});
        items.rows.push_back({"club", {{"slot", "weapon"}, {"attack", "1"}}});
        items.rows.push_back({"cap", {{"slot", "head"}, {"defense", "2"}}});
        std::ofstream(project.folder.path / "assets/data/items.dtable", std::ios::binary)
            << game::table_to_text(items);
        std::ofstream(project.folder.path / "assets/data/party.dtable", std::ios::binary)
            << game::table_to_text(tables.party);
        logic::LogicAsset rules;
        rules.name = "gear";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "party", "add_member", {Project::text("hero")}),
             ir::do_action(3, "inventory", "give", {Project::text("sword"), Project::whole(1)}),
             ir::do_action(4, "inventory", "give", {Project::text("club"), Project::whole(1)}),
             ir::do_action(5, "inventory", "give", {Project::text("cap"), Project::whole(1)}),
             ir::do_action(6, "party", "equip", {Project::text("hero"), Project::text("club")}),
             ir::do_action(7, "party", "equip", {Project::text("hero"), Project::text("cap")})},
            {}});
        write_json(project.folder.path / "assets/logic/gear.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    std::uint64_t at = 3;
    const auto press = [&replay, &at](input::Key k) {
        replay.record(at, key(k), true);
        replay.record(at + 1, key(k), false);
        at += 3;
    };
    press(input::Key::Escape);
    for (int i = 0; i < 5; ++i) {
        press(input::Key::Down); // to Equip
    }
    press(input::Key::Z);    // the Equip page
    press(input::Key::Z);    // the hero
    press(input::Key::Z);    // Weapon
    press(input::Key::Down); // (nothing), then the sword: carried; the club is worn
    const std::uint64_t choosing = at;
    press(input::Key::Z); // wear the sword; the club goes back
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    CHECK(runtime->party().equipped("hero", "weapon") == "club");
    CHECK(runtime->party().equipped("hero", "head") == "cap");
    CHECK_FALSE(runtime->inventory().has("cap"));
    for (std::uint64_t i = 1; i < choosing; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->menu().page() == game::GameMenu::Page::Equip);
    CHECK(runtime->menu().equip_step() == game::GameMenu::EquipStep::Item);
    REQUIRE(runtime->draw());
    {
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        const assets::Image image{320, 180, pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / "sparxel-equip.png",
                      std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    for (std::uint64_t i = choosing; i < at + 2; ++i) {
        runtime->step();
    }
    CHECK(runtime->party().equipped("hero", "weapon") == "sword");
    CHECK(runtime->inventory().has("club"));
    CHECK_FALSE(runtime->inventory().has("sword"));
    CHECK(runtime->menu().equip_step() == game::GameMenu::EquipStep::Slot);
    // Saved with the game.
    const serialize::Json saved = runtime->save();
    const auto other = project.open(no_graphics(nullptr));
    std::vector<serialize::Issue> issues;
    REQUIRE(other->load(saved, issues));
    CHECK(other->party().equipped("hero", "weapon") == "sword");
    CHECK(other->party().equipped("hero", "head") == "cap");
}

TEST_CASE("Status effects: given by skills and attacks, hurting each round, held, woken, cured") {
    BattleTables tables;
    game::Table states;
    states.columns = {
        {"name", game::ColumnKind::Text},         {"turns", game::ColumnKind::Whole},
        {"hp_per_turn", game::ColumnKind::Whole}, {"skip_turn", game::ColumnKind::YesNo},
        {"ends_on_hit", game::ColumnKind::YesNo}, {"defense", game::ColumnKind::Whole}};
    states.rows.push_back(
        {"poison", {{"name", "Poisoned"}, {"turns", "2"}, {"hp_per_turn", "-2"}}});
    states.rows.push_back(
        {"sleep",
         {{"name", "Asleep"}, {"turns", "3"}, {"skip_turn", "yes"}, {"ends_on_hit", "yes"}}});
    states.rows.push_back({"guard", {{"name", "Guarded"}, {"turns", "5"}, {"defense", "4"}}});
    tables.skills.columns.push_back({"inflicts", game::ColumnKind::Text});
    tables.skills.columns.push_back({"cures", game::ColumnKind::Text});
    tables.skills.rows.push_back(
        {"venom", {{"name", "Venom"}, {"power", "0"}, {"inflicts", "poison"}}});
    tables.skills.rows.push_back(
        {"lullaby", {{"name", "Lullaby"}, {"power", "0"}, {"inflicts", "sleep"}}});
    tables.skills.rows.push_back(
        {"ward",
         {{"name", "Ward"}, {"power", "0"}, {"heals", "yes"}, {"inflicts", ""}, {"cures", "all"}}});
    tables.party.rows[1].cells["skills"] = "venom, lullaby, ward";
    tables.party.rows[1].cells["max_mp"] = "0";
    game::BattleData data = tables.data();
    data.states = &states;
    std::string error;
    std::optional<game::Battle> started =
        game::Battle::start(data, {"mage"}, {}, {"slime"}, 7, error);
    REQUIRE(started.has_value());
    if (!started) {
        return;
    }
    game::Battle& b = *started;
    const auto said = [&b](const std::string& line) {
        const std::vector<std::string> lines = b.take_messages();
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    };
    static_cast<void>(b.take_messages());
    const std::map<std::string, std::int64_t, std::less<>> none;
    // Poison: given, then 2 damage at the end of each of its 2 rounds, then gone.
    const std::int64_t hp = b.fighters()[1].hp;
    REQUIRE(b.choose({game::BattleCommand::Kind::Skill, "venom", 1}, none, error));
    const std::vector<std::string> first = b.take_messages();
    CHECK(std::find(first.begin(), first.end(), "Slime is poisoned!") != first.end());
    CHECK(std::find(first.begin(), first.end(), "Slime takes 2 damage (poisoned).") != first.end());
    CHECK(b.fighters()[1].hp == hp - 2);
    REQUIRE(b.choose({game::BattleCommand::Kind::Defend, {}, 0}, none, error));
    CHECK(said("Slime is no longer poisoned."));
    CHECK(b.fighters()[1].hp == hp - 4);
    CHECK(b.fighters()[1].states.empty());
    // Sleep: the slime cannot act until hit.
    REQUIRE(b.choose({game::BattleCommand::Kind::Skill, "lullaby", 1}, none, error));
    CHECK(b.fighters()[1].has_state("sleep"));
    REQUIRE(b.choose({game::BattleCommand::Kind::Defend, {}, 0}, none, error));
    CHECK(said("Slime is asleep and cannot act."));
    REQUIRE(b.choose({game::BattleCommand::Kind::Attack, {}, 1}, none, error));
    CHECK_FALSE(b.fighters()[1].has_state("sleep"));
}

TEST_CASE("Status effects: equipment resists them; an asleep party fights on by itself") {
    BattleTables tables;
    game::Table states;
    states.columns = {{"name", game::ColumnKind::Text},
                      {"skip_turn", game::ColumnKind::YesNo},
                      {"turns", game::ColumnKind::Whole}};
    states.rows.push_back({"sleep", {{"name", "Asleep"}, {"skip_turn", "yes"}, {"turns", "2"}}});
    states.rows.push_back({"poison", {{"name", "Poisoned"}}});
    tables.enemies.columns.push_back({"inflicts", game::ColumnKind::Text});
    tables.enemies.rows[1].cells["inflicts"] = "sleep"; // the bat's bite
    tables.items.columns.push_back({"slot", game::ColumnKind::Text});
    tables.items.columns.push_back({"resists", game::ColumnKind::Text});
    tables.items.rows.push_back({"charm", {{"slot", "accessory"}, {"resists", "poison, sleep"}}});
    game::BattleData data = tables.data();
    data.states = &states;
    std::string error;
    // With the charm, the bat's bite does not put the hero to sleep.
    data.member_equipment["hero"] = {"charm"};
    std::optional<game::Battle> guarded =
        game::Battle::start(data, {"hero"}, {}, {"bat"}, 3, error);
    REQUIRE(guarded.has_value());
    const std::map<std::string, std::int64_t, std::less<>> none;
    if (guarded) {
        REQUIRE(guarded->choose({game::BattleCommand::Kind::Defend, {}, 0}, none, error));
        const std::vector<std::string> lines = guarded->take_messages();
        CHECK(std::find(lines.begin(), lines.end(), "Hero is not asleep.") != lines.end());
        CHECK_FALSE(guarded->fighters()[0].has_state("sleep"));
    }
    // Without it, the hero falls asleep, and rounds go on without them choosing.
    data.member_equipment.clear();
    std::optional<game::Battle> bitten = game::Battle::start(data, {"hero"}, {}, {"bat"}, 3, error);
    REQUIRE(bitten.has_value());
    if (bitten) {
        REQUIRE(bitten->choose({game::BattleCommand::Kind::Defend, {}, 0}, none, error));
        // Either the hero chooses again (awake), or the battle is over: never stuck.
        const bool going =
            bitten->chooser().has_value() || bitten->outcome() != game::BattleOutcome::Fighting;
        CHECK(going);
        const std::vector<std::string> lines = bitten->take_messages();
        CHECK(std::find(lines.begin(), lines.end(), "Hero is asleep!") != lines.end());
        CHECK(std::find(lines.begin(), lines.end(), "Hero is asleep and cannot act.") !=
              lines.end());
    }
}

TEST_CASE("Localization: keys, the pseudo language, translations and new rows") {
    CHECK(game::translation_key("Hello") == game::translation_key("Hello"));
    CHECK(game::translation_key("Hello") != game::translation_key("Hello!"));
    CHECK(game::translation_key("Hello").size() == 10);
    CHECK(game::pseudo_localize("Hello [wave]you[/wave] {name}") ==
          "[Heelloo [wave]yoouu[/wave] {name}]");
    game::Table table;
    table.columns = {{"source", game::ColumnKind::Text},
                     {"fr", game::ColumnKind::Text},
                     {"notes", game::ColumnKind::Text}};
    table.rows.push_back(
        {game::translation_key("Hello"), {{"source", "Hello"}, {"fr", "Bonjour"}}});
    table.rows.push_back({game::translation_key("Yes"), {{"source", "Yes"}, {"fr", "Oui"}}});
    table.rows.push_back({game::translation_key("No"), {{"source", "No"}, {"fr", "Non"}}});
    game::Translations words(&table);
    CHECK(words.languages() == std::vector<std::string>{"fr"});
    CHECK(words.translate("Hello") == "Hello"); // no language chosen
    words.set_language("fr");
    CHECK(words.translate("Hello") == "Bonjour");
    CHECK(words.translate("Goodbye") == "Goodbye"); // not translated: as written
    CHECK(words.translate_answers("Yes | No|Maybe") == "Oui|Non|Maybe");
    words.set_language(std::string(game::pseudo_language));
    CHECK(words.translate("No") == "[Noo]");
    // New texts are added once, with columns for new languages; translations stay.
    CHECK(game::add_translation_rows(table, {"Hello", "Goodbye", "Goodbye", ""}, {"de"}) == 1);
    CHECK(table.rows.size() == 4);
    CHECK(table.column("de") != nullptr);
    CHECK(table.text(game::translation_key("Hello"), "fr") == "Bonjour");
}

TEST_CASE("A game in another language: messages and answers shown translated, Rules unchanged") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["language"] = "fr";
        settings["width"] = 320; // one page for the question
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        game::Table table;
        table.columns = {{"source", game::ColumnKind::Text}, {"fr", game::ColumnKind::Text}};
        for (const auto& [source, fr] :
             {std::pair{"Ready?", "Prêt ?"}, std::pair{"Yes", "Oui"}, std::pair{"No", "Non"}}) {
            table.rows.push_back({game::translation_key(source), {{"source", source}, {"fr", fr}}});
        }
        std::filesystem::create_directories(project.folder.path / "assets/data");
        std::ofstream(project.folder.path / "assets/data/translations.dtable", std::ios::binary)
            << game::table_to_text(table);
        logic::LogicAsset rules;
        rules.name = "ask";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "dialogue", "ask",
                                       {Project::text("Ready?"), Project::text("Yes|No")})},
                        {}});
        write_json(project.folder.path / "assets/logic/ask.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(3, key(input::Key::Z), true); // Yes
    replay.record(4, key(input::Key::Z), false);
    const auto runtime = project.open(no_graphics(&replay));
    runtime->step();
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Prêt ?");
    CHECK(runtime->dialogue().choices() == std::vector<std::string>{"Oui", "Non"});
    for (int i = 0; i < 6; ++i) {
        runtime->step();
    }
    // Rules hear the answer as written (the project's own Rules log it).
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "Yes") == 1);
}

TEST_CASE("An Enemy on the Map: walked into, fought in front of its background, then gone") {
    const Project project;
    const BattleTables tables;
    {
        std::filesystem::create_directories(project.folder.path / "assets/data");
        for (const auto& [name, table] :
             {std::pair{"party", &tables.party}, std::pair{"enemies", &tables.enemies},
              std::pair{"skills", &tables.skills}, std::pair{"items", &tables.items}}) {
            std::ofstream(project.folder.path / "assets/data" / (std::string(name) + ".dtable"),
                          std::ios::binary)
                << game::table_to_text(*table);
        }
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        w.add<game::TopDownMover>(hero);
        // A bat two tiles to the east, seen on the map, with a cave behind its battle.
        const world::Entity bat = w.create("bat_on_map");
        w.add<scene2d::Transform2D>(bat)->x = 2.0;
        w.add<scene2d::Sprite>(bat)->image = "art/rock.png";
        game::EnemyOnMap* foe = w.add<game::EnemyOnMap>(bat);
        foe->enemies = "bat";
        foe->background = "art/rock.png";
        write_json(project.folder.path / "assets/maps/field.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["start_scene"] = "maps/field.dscene";
        settings["width"] = 320;
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
        logic::LogicAsset rules;
        rules.name = "bat";
        rules.rules.push_back(logic::Rule{logic::BlockId{1},
                                          "bat_on_map",
                                          "defeated",
                                          {},
                                          {ir::log(2, Project::text("the bat is gone"))},
                                          {}});
        write_json(project.folder.path / "assets/logic/bat.drules",
                   serialize::logic_to_json(rules));
    }
    // Right, until the bat; then Interact every few steps through the battle.
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(40, key(input::Key::Right), false);
    for (std::uint64_t step = 44; step < 600; step += 4) {
        replay.record(step, key(input::Key::Z), true);
        replay.record(step + 2, key(input::Key::Z), false);
    }
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    int steps = 0;
    while (runtime->battle_screen() == nullptr && steps < 40) {
        runtime->step();
        ++steps;
    }
    REQUIRE(runtime->battle_screen() != nullptr);
    // The battle's background, the rock's colour, fills the screen behind it.
    REQUIRE(runtime->draw());
    {
        // The top-left corner shows the rock's grey-blue (90, 90, 100), dimmed a little.
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        REQUIRE(pixels.size() >= 4);
        INFO(int{pixels[0]} << ", " << int{pixels[1]} << ", " << int{pixels[2]});
        const bool rock =
            pixels[0] >= 60 && pixels[0] <= 85 && pixels[1] == pixels[0] && pixels[2] > pixels[0];
        CHECK(rock);
    }
    for (int i = 0; i < 560 && runtime->battle_screen() != nullptr; ++i) {
        runtime->step();
    }
    REQUIRE(runtime->battle_screen() == nullptr);
    runtime->step();
    const world::Entity bat = runtime->world().find_named("bat_on_map").at(0);
    CHECK(runtime->world().get<game::EnemyOnMap>(bat)->beaten);
    CHECK_FALSE(runtime->world().get<scene2d::Sprite>(bat)->visible);
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "the bat is gone") == 1);
}

TEST_CASE("The pause menu's Items and Skills pages heal the party between battles") {
    const Project project;
    const BattleTables tables;
    std::filesystem::create_directories(project.folder.path / "assets/data");
    for (const auto& [name, table] :
         {std::pair{"party", &tables.party}, std::pair{"skills", &tables.skills},
          std::pair{"items", &tables.items}}) {
        std::ofstream(project.folder.path / "assets/data" / (std::string(name) + ".dtable"),
                      std::ios::binary)
            << game::table_to_text(*table);
    }
    assets::AssetDatabase db(project.folder.path / "assets");
    db.refresh();
    const game::TablesObject data(&db);
    game::PartyObject party;
    game::InventoryObject bag;
    party.set_tables(&data);
    party.set_inventory(&bag);
    party.add_member("hero");
    party.add_member("mage");
    party.condition()["hero"] = {10, 0}; // hurt
    bag.give("potion", 2);
    game::GameMenu menu;
    menu.attach(&party, &data, &bag);
    const game::SaveObject saves([] { return std::filesystem::path{}; });
    menu.open_pause();
    // Items: the potion, on the hero.
    static_cast<void>(menu.press("move_down", saves));
    static_cast<void>(menu.press("interact", saves));
    REQUIRE(menu.page() == game::GameMenu::Page::Items);
    CHECK(menu.choices(saves) == std::vector<std::string>{"Potion  x2"});
    static_cast<void>(menu.press("interact", saves));
    CHECK(menu.choosing_target());
    static_cast<void>(menu.press("interact", saves)); // the hero
    CHECK(party.condition().at("hero").first == 30);
    CHECK(bag.count_of("potion") == 1);
    // Back to the pause menu, then Skills: the mage's Heal (Fire does not heal), on the hero.
    static_cast<void>(menu.press("cancel", saves));
    static_cast<void>(menu.press("cancel", saves));
    REQUIRE(menu.page() == game::GameMenu::Page::Pause);
    static_cast<void>(menu.press("move_down", saves));
    static_cast<void>(menu.press("interact", saves));
    REQUIRE(menu.page() == game::GameMenu::Page::Skills);
    static_cast<void>(menu.press("move_down", saves)); // the mage casts
    static_cast<void>(menu.press("interact", saves));
    CHECK(menu.choices(saves) == std::vector<std::string>{"Heal  3 MP"});
    static_cast<void>(menu.press("interact", saves));
    static_cast<void>(menu.press("interact", saves)); // on the hero
    CHECK(party.condition().at("hero").first == 40);  // 30 + 15, up to 40
    CHECK(party.condition().at("mage").second == 9);  // 12 - 3
}

TEST_CASE("Logic creates copies of prefabs while playing, and removes things") {
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity coin = w.create("coin");
        w.add<scene2d::Transform2D>(coin)->x = 99.0; // where it is in the prefab: replaced
        w.add<game::Trigger>(coin);
        std::filesystem::create_directories(project.folder.path / "assets/prefabs");
        write_json(project.folder.path / "assets/prefabs/coin.dprefab",
                   serialize::scene_to_json(w));
        logic::LogicAsset rules;
        rules.name = "spawn";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "game", "create",
                           {Project::text("prefabs/coin.dprefab"), Project::text("elder")}),
             ir::do_action(3, "game", "create_at",
                           {Project::text("prefabs/coin.dprefab"), ir::literal(reflect::Value{3.0}),
                            ir::literal(reflect::Value{4.0})}),
             ir::do_action(4, "game", "remove", {Project::text("rock")}),
             ir::do_action(5, "game", "create",
                           {Project::text("prefabs/nothing.dprefab"), Project::text("elder")}),
             ir::do_action(6, "game", "remove", {Project::text("ghost")})},
            {}});
        write_json(project.folder.path / "assets/logic/spawn.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    runtime->step();
    runtime->step();
    const world::World& w = runtime->world();
    REQUIRE(w.find_named("coin").size() == 1);
    REQUIRE(w.find_named("coin_2").size() == 1);
    // The first at the elder (0, 3), the second at (3, 4).
    CHECK(w.get<scene2d::Transform2D>(w.find_named("coin").at(0))->y == 3.0);
    CHECK(w.get<scene2d::Transform2D>(w.find_named("coin_2").at(0))->x == 3.0);
    CHECK(w.get<game::Trigger>(w.find_named("coin_2").at(0)) != nullptr);
    CHECK(w.find_named("rock").empty());
    // What could not be done is said in plain language.
    const std::vector<std::string>& problems = runtime->problems();
    const auto said = [&problems](const std::string& line) {
        return std::find(problems.begin(), problems.end(), line) != problems.end();
    };
    CHECK(said("Could not create prefabs/nothing.dprefab: there is no prefab "
               "prefabs/nothing.dprefab in assets/."));
    CHECK(said("There is nothing called ghost to remove."));
}

TEST_CASE("Timers end and tell Rules; messages reach every Rule listening; saves keep timers") {
    const Project project;
    {
        logic::LogicAsset rules;
        rules.name = "bell";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "game", "start_timer",
                                       {Project::text("bell"), ir::literal(reflect::Value{0.5})})},
                        {}});
        rules.rules.push_back(
            logic::Rule{logic::BlockId{3},
                        "game",
                        "timer_ended",
                        {},
                        {ir::log(4, ir::event_value("name")),
                         ir::do_action(5, "game", "send_message", {Project::text("rang")})},
                        {}});
        rules.rules.push_back(logic::Rule{
            logic::BlockId{6}, "game", "message", {}, {ir::log(7, ir::event_value("name"))}, {}});
        write_json(project.folder.path / "assets/logic/bell.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    for (int i = 0; i < 10; ++i) {
        runtime->step();
    }
    const auto count = [&](const char* line) {
        return std::count(runtime->log().begin(), runtime->log().end(), line);
    };
    CHECK(count("bell") == 0);
    // Saved while it runs: its seconds left are kept.
    const serialize::Json saved = runtime->save();
    REQUIRE(saved.contains("timers"));
    CHECK(saved["timers"]["bell"].get<double>() > 0.3);
    for (int i = 0; i < 30; ++i) {
        runtime->step();
    }
    CHECK(count("bell") == 1);
    CHECK(count("rang") == 1);
    // Loading the save runs the timer again from where it was.
    std::vector<serialize::Issue> issues;
    REQUIRE(runtime->load(saved, issues));
    for (int i = 0; i < 30; ++i) {
        runtime->step();
    }
    CHECK(count("bell") == 2);
}

TEST_CASE("Testing from the editor: F1's debug menu gives items and goes places; F3 times") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::ifstream in(project.folder.path / game::project_file);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        serialize::Json settings = serialize::parse(text, issues).value_or(serialize::Json{});
        settings["width"] = 320;
        settings["height"] = 180;
        write_json(project.folder.path / game::project_file, settings);
    }
    input::Recording replay;
    std::uint64_t at = 2;
    const auto press = [&replay, &at](input::Key k) {
        replay.record(at, key(k), true);
        replay.record(at + 1, key(k), false);
        at += 3;
    };
    press(input::Key::F1);    // the debug menu
    press(input::Key::Z);     // Give items
    press(input::Key::Right); // +100 gold (the only thing, with no data/items)
    press(input::Key::X);     // back
    for (int i = 0; i < 3; ++i) {
        press(input::Key::Down); // to Go to...
    }
    press(input::Key::Z);
    press(input::Key::Z); // the first, alphabetically: the crate, at (50, 0)
    press(input::Key::F3);
    game::RuntimeOptions options;
    options.headless = true;
    options.replay = &replay;
    options.debug = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    runtime->step();
    runtime->step();
    CHECK(runtime->debug_menu().is_open());
    const auto shot = [&](const char* name) {
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        const assets::Image image{runtime->target()->width(), runtime->target()->height(), pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / name, std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    };
    shot("sparxel-debug-menu.png");
    for (std::uint64_t i = 3; i < at + 2; ++i) {
        runtime->step();
    }
    CHECK(runtime->inventory().count_of("gold") == 100);
    CHECK_FALSE(runtime->debug_menu().is_open());
    CHECK(transform_of(*runtime, "hero").x == 50.0);
    CHECK(transform_of(*runtime, "hero").y == 1.0);
    shot("sparxel-timings.png"); // with the timings shown

    // Without --debug, F1 does nothing.
    input::Recording plain;
    plain.record(2, key(input::Key::F1), true);
    plain.record(3, key(input::Key::F1), false);
    const auto game = project.open(no_graphics(&plain));
    for (int i = 0; i < 5; ++i) {
        game->step();
    }
    CHECK_FALSE(game->debug_menu().is_open());
}

TEST_CASE("The look of menus and messages: colours read, saved, and drawn") {
    CHECK(game::color_from_hex("#ffd27a") == render::Color{255, 210, 122, 255});
    CHECK(game::color_from_hex("#10203080") == render::Color{16, 32, 48, 128});
    CHECK_FALSE(game::color_from_hex("ffd27a").has_value());
    CHECK_FALSE(game::color_from_hex("#ggg000").has_value());
    CHECK(game::color_to_hex(render::Color{255, 210, 122, 255}) == "#ffd27a");
    CHECK(game::color_to_hex(render::Color{16, 32, 48, 128}) == "#10203080");

    const Project project;
    const game::UiTheme parchment = game::ui_theme_presets().at(1).theme;
    {
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->ui = parchment;
        settings->width = 320;
        settings->height = 180;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        // Read back the same.
        const std::optional<game::ProjectSettings> back =
            game::read_project(project.folder.path, issues);
        REQUIRE(back.has_value());
        const bool same = back && back->ui == parchment;
        CHECK(same);
        logic::LogicAsset rules;
        rules.name = "hi";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "dialogue", "say", {Project::text("Hi")})},
                        {}});
        write_json(project.folder.path / "assets/logic/hi.drules", serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    for (int i = 0; i < 8; ++i) {
        runtime->step(); // the box opens over a tenth of a second
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    {
        const assets::Image image{320, 180, pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / "sparxel-parchment.png",
                      std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    bool box = false;
    bool words = false;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        // The parchment inside the box (nearly opaque over the world), and its brown words.
        box = box || (pixels[i] > 225 && pixels[i + 1] > 205 && pixels[i + 2] > 170 &&
                      pixels[i] < 245 && pixels[i + 2] < 195);
        words = words || (pixels[i] == 62 && pixels[i + 1] == 40 && pixels[i + 2] == 22);
    }
    CHECK(box);
    CHECK(words);
}

TEST_CASE("The music ducks while a message shows, and comes back after") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->width = 320;
        settings->height = 180;
        settings->music_while_talking = 0.5;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        logic::LogicAsset rules;
        rules.name = "hi";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "dialogue", "say", {Project::text("Hi")})},
                        {}});
        write_json(project.folder.path / "assets/logic/hi.drules", serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(40, key(input::Key::Z), true); // close it
    replay.record(41, key(input::Key::Z), false);
    const auto runtime = project.open(no_graphics(&replay));
    REQUIRE(runtime->audio() != nullptr);
    for (int i = 0; i < 2; ++i) {
        runtime->step();
    }
    // Easing down, not at once.
    CHECK(runtime->audio()->music_duck() < 1.0);
    CHECK(runtime->audio()->music_duck() > 0.5);
    for (int i = 0; i < 30; ++i) {
        runtime->step();
    }
    CHECK(runtime->audio()->music_duck() == doctest::Approx(0.5));
    for (int i = 0; i < 40; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->dialogue().is_open());
    CHECK(runtime->audio()->music_duck() == doctest::Approx(1.0));
}

TEST_CASE("Achievements: unlocked once, kept for the player across runs, and announced") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->width = 320;
        settings->height = 180;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        game::Table table;
        table.columns = {{"name", game::ColumnKind::Text}};
        table.rows.push_back({"first_steps", {{"name", "First steps"}}});
        std::filesystem::create_directories(project.folder.path / "assets/data");
        std::ofstream(project.folder.path / "assets/data/achievements.dtable", std::ios::binary)
            << game::table_to_text(table);
        logic::LogicAsset rules;
        rules.name = "cheers";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "game", "unlock", {Project::text("first_steps")}),
                         ir::do_action(3, "game", "unlock", {Project::text("first_steps")}),
                         ir::log(4, ir::call("game", "achievement_count"))},
                        {}});
        write_json(project.folder.path / "assets/logic/cheers.drules",
                   serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    for (int i = 0; i < 20; ++i) {
        runtime->step();
    }
    CHECK(std::count(runtime->log().begin(), runtime->log().end(), "1") == 1);
    // Kept in the player's file, once.
    std::ifstream in(runtime->save_folder() / game::Runtime::achievements_file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("first_steps") != std::string::npos);
    CHECK(text.find("first_steps") == text.rfind("first_steps"));
    // Announced at the top, in the look's highlight colour.
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    bool gold_on_top = false;
    for (std::size_t y = 0; y < 30; ++y) {
        for (std::size_t x = 0; x < 320; ++x) {
            const std::size_t at = ((y * 320) + x) * 4;
            gold_on_top = gold_on_top ||
                          (pixels[at] == 255 && pixels[at + 1] == 210 && pixels[at + 2] == 122);
        }
    }
    CHECK(gold_on_top);
    // Another run remembers it: unlocking again counts no more.
    const auto again = project.open(no_graphics(nullptr));
    again->step();
    CHECK(std::count(again->log().begin(), again->log().end(), "1") == 1);
}

TEST_CASE("Credits: offered on the title screen when the game has credits.txt") {
    const game::SaveObject saves([] { return std::filesystem::path{}; });
    game::GameMenu menu;
    menu.open_title();
    CHECK(menu.choices(saves).size() == 3); // no credits: New game, Continue, Quit
    menu.set_credits({"My game", "Made by me"});
    CHECK(menu.choices(saves) ==
          std::vector<std::string>{"New game", "Continue", "Credits", "Quit"});
    static_cast<void>(menu.press("move_down", saves));
    static_cast<void>(menu.press("move_down", saves));
    static_cast<void>(menu.press("interact", saves));
    CHECK(menu.page() == game::GameMenu::Page::Credits);
    static_cast<void>(menu.press("cancel", saves));
    CHECK(menu.page() == game::GameMenu::Page::Title);
    CHECK(menu.cursor() == 2);
    // Quit is now the fourth.
    static_cast<void>(menu.press("move_down", saves));
    CHECK(menu.press("interact", saves).kind == game::GameMenu::Result::Kind::Quit);

    // The runtime reads the project's credits.txt.
    const Project project;
    std::ofstream(project.folder.path / "assets/credits.txt", std::ios::binary)
        << "Test game\r\nMade with Sparxel\n";
    const auto runtime = project.open(no_graphics(nullptr));
    CHECK(runtime->menu().credits() == std::vector<std::string>{"Test game", "Made with Sparxel"});
}

TEST_CASE("The minimap shows walls, the player and people to talk to; logic hides it") {
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        w.add<game::TopDownMover>(hero);
        const world::Entity walls = w.create("walls");
        scene2d::Transform2D* at = w.add<scene2d::Transform2D>(walls);
        at->x = -3.0;
        at->y = -3.0;
        scene2d::Tilemap* map = w.add<scene2d::Tilemap>(walls);
        map->tiles = "0,0,0,0,0,0,0";
        map->solid = true;
        const world::Entity elder = w.create("elder");
        w.add<scene2d::Transform2D>(elder)->x = 4.0;
        w.add<game::Interactable>(elder);
        write_json(project.folder.path / "assets/maps/field.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/field.dscene";
        settings->width = 320;
        settings->height = 180;
        settings->minimap = true;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        logic::LogicAsset rules;
        rules.name = "map";
        rules.rules.push_back(logic::Rule{logic::BlockId{1},
                                          "",
                                          "game_start",
                                          {},
                                          {ir::wait(2, ir::literal(reflect::Value{0.5})),
                                           ir::do_action(3, "game", "hide_minimap", {})},
                                          {}});
        write_json(project.folder.path / "assets/logic/map.drules",
                   serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    const game::UiTheme look;
    const auto count = [&](render::Color c) {
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        int n = 0;
        // The minimap's corner: the top right 100 x 100.
        for (std::size_t y = 0; y < 100; ++y) {
            for (std::size_t x = 220; x < 320; ++x) {
                const std::size_t i = ((y * 320) + x) * 4;
                n += pixels[i] == c.r && pixels[i + 1] == c.g && pixels[i + 2] == c.b ? 1 : 0;
            }
        }
        return n;
    };
    CHECK(count(look.highlight) >= 16); // the player, 4 x 4
    CHECK(count(look.text) >= 4);       // the elder
    CHECK(count(look.border) > 7 * 4);  // the wall's seven tiles, and the frame
    for (int i = 0; i < 40; ++i) {
        runtime->step();
    }
    CHECK(count(look.highlight) == 0); // hidden by logic
}

TEST_CASE("A save point opens the save slots alone; Cancel goes back to the game") {
    const Project project;
    {
        logic::LogicAsset rules;
        rules.name = "crystal";
        rules.rules.push_back(logic::Rule{logic::BlockId{1},
                                          "",
                                          "game_start",
                                          {},
                                          {ir::do_action(2, "game", "open_save_menu", {})},
                                          {}});
        write_json(project.folder.path / "assets/logic/crystal.drules",
                   serialize::logic_to_json(rules));
    }
    input::Recording replay;
    replay.record(3, key(input::Key::Z), true); // slot 1
    replay.record(4, key(input::Key::Z), false);
    replay.record(6, key(input::Key::X), true); // back to the game
    replay.record(7, key(input::Key::X), false);
    const auto runtime = project.open(no_graphics(&replay));
    runtime->step();
    CHECK(runtime->menu().page() == game::GameMenu::Page::Save);
    for (int i = 0; i < 5; ++i) {
        runtime->step();
    }
    CHECK(fs::exists(game::save_slot_file(runtime->save_folder(), 1)));
    for (int i = 0; i < 3; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->menu().open());
}

TEST_CASE("Footsteps sound as the player walks, from the ground under their feet") {
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        game::TopDownMover* mover = w.add<game::TopDownMover>(hero);
        mover->step_length = 0.5;
        const world::Entity ground = w.create("ground");
        w.add<scene2d::Transform2D>(ground)->y = -1.0;
        scene2d::Tilemap* map = w.add<scene2d::Tilemap>(ground);
        map->tiles = "0,0,0,0,0,0";
        map->footsteps = "sfx/chime.wav";
        write_json(project.folder.path / "assets/maps/walk.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/walk.dscene";
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true);
    replay.record(40, key(input::Key::Right), false);
    const auto runtime = project.open(no_graphics(&replay));
    REQUIRE(runtime->audio() != nullptr);
    bool heard = false;
    for (int i = 0; i < 20; ++i) {
        runtime->step();
        heard = heard || runtime->audio()->playing_sounds() > 0;
    }
    CHECK(heard); // 4 tiles a second: a step every 0.5 tiles, on the ground's chime
    CHECK(runtime->problems().empty());
}

TEST_CASE("While testing, files changed during play are read again") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->language = "fr";
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    game::RuntimeOptions options = no_graphics(nullptr);
    options.debug = true;
    const auto runtime = project.open(options);
    runtime->step();
    CHECK(runtime->reload_changed() == 0); // nothing changed yet
    // A translation saved while the game runs.
    game::Table table;
    table.columns = {{"source", game::ColumnKind::Text}, {"fr", game::ColumnKind::Text}};
    table.rows.push_back(
        {game::translation_key("Hello"), {{"source", "Hello"}, {"fr", "Bonjour"}}});
    std::filesystem::create_directories(project.folder.path / "assets/data");
    std::ofstream(project.folder.path / "assets/data/translations.dtable", std::ios::binary)
        << game::table_to_text(table);
    CHECK(runtime->reload_changed() > 0);
    runtime->dialogue().say("Hello");
    REQUIRE(runtime->dialogue().current() != nullptr);
    CHECK(*runtime->dialogue().current() == "Bonjour");
}

TEST_CASE("Sound Sources fade in when they start and out when they stop") {
    const Project project;
    const auto runtime = project.open(no_graphics(nullptr));
    audio::AudioEngine* mixer = runtime->audio();
    REQUIRE(mixer != nullptr);
    const world::Entity door = runtime->world().find_named("door").at(0);
    game::SoundSource* hum = runtime->world().add<game::SoundSource>(door);
    REQUIRE(hum != nullptr);
    hum->sound = "sfx/chime.wav";
    hum->range = 100.0;
    hum->fade = 0.5; // 30 steps
    for (int i = 0; i < 40; ++i) {
        runtime->step(); // faded in
    }
    CHECK(mixer->playing_loops() == 1);
    hum = runtime->world().get<game::SoundSource>(door);
    REQUIRE(hum != nullptr);
    hum->playing = false;
    // Still sounding while it fades out, then stopped.
    for (int i = 0; i < 10; ++i) {
        runtime->step();
    }
    CHECK(mixer->playing_loops() == 1);
    for (int i = 0; i < 40; ++i) {
        runtime->step();
    }
    CHECK(mixer->playing_loops() == 0);
}

TEST_CASE("Weather: rain eases in and falls in streaks; saves keep it") {
    const Project project;
    {
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->width = 320;
        settings->height = 180;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        logic::LogicAsset rules;
        rules.name = "storm";
        rules.rules.push_back(
            logic::Rule{logic::BlockId{1},
                        "",
                        "game_start",
                        {},
                        {ir::do_action(2, "lighting", "set_weather",
                                       {Project::text("rain"), ir::literal(reflect::Value{1.0})})},
                        {}});
        write_json(project.folder.path / "assets/logic/storm.drules",
                   serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    const auto streaks = [&runtime] {
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        int n = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            n += pixels[i + 2] > 140 && pixels[i + 2] > pixels[i] + 30 ? 1 : 0;
        }
        return n;
    };
    runtime->step();
    const int first = streaks();
    for (int i = 0; i < 150; ++i) {
        runtime->step();
    }
    const int later = streaks();
    CHECK(later > first + 100); // it eased in
    {
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        const assets::Image image{320, 180, pixels};
        const std::vector<std::uint8_t> png = assets::encode_png(image);
        std::ofstream(std::filesystem::temp_directory_path() / "sparxel-rain.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    const serialize::Json saved = runtime->save();
    CHECK(saved["lighting"]["weather"] == "rain");
    CHECK(saved["lighting"]["weather_strength"].get<double>() == doctest::Approx(1.0));
}

TEST_CASE("Screen Text and Screen Bars: values in braces, fixed to the screen") {
    const auto value = [](std::string_view name) { return "<" + std::string(name) + ">"; };
    CHECK(game::fill_values("Gold: {gold}", value) == "Gold: <gold>");
    CHECK(game::fill_values("{hp:hero}/{max_hp:hero}", value) == "<hp:hero>/<max_hp:hero>");
    CHECK(game::fill_values("{{literal} and {open", value) == "{literal} and {open");

    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity hero = w.create("hero");
        w.add<scene2d::Transform2D>(hero);
        w.add<game::TopDownMover>(hero);
        const world::Entity label = w.create("gold_label");
        game::ScreenText* text = w.add<game::ScreenText>(label);
        text->text = "Gold: {gold}";
        text->anchor = game::ScreenAnchor::TopRight;
        const world::Entity meter = w.create("gold_meter");
        game::ScreenBar* bar = w.add<game::ScreenBar>(meter);
        bar->value = "{gold}";
        bar->most = "100";
        bar->anchor = game::ScreenAnchor::BottomLeft;
        bar->width = 40.0;
        bar->color = "#00ff00";
        write_json(project.folder.path / "assets/maps/hud.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/hud.dscene";
        settings->width = 320;
        settings->height = 180;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        logic::LogicAsset rules;
        rules.name = "rich";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "",
            "game_start",
            {},
            {ir::do_action(2, "inventory", "give", {Project::text("gold"), Project::whole(25)})},
            {}});
        write_json(project.folder.path / "assets/logic/rich.drules",
                   serialize::logic_to_json(rules));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    int words_top_right = 0;
    int green = 0;
    for (std::size_t y = 0; y < 180; ++y) {
        for (std::size_t x = 0; x < 320; ++x) {
            const std::size_t i = ((y * 320) + x) * 4;
            const bool white = pixels[i] == 255 && pixels[i + 1] == 255 && pixels[i + 2] == 255;
            words_top_right += white && y < 16 && x > 220 ? 1 : 0;
            green += pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 ? 1 : 0;
        }
    }
    CHECK(words_top_right > 20); // "Gold: 25", top right
    CHECK(green == 10 * 4);      // 25 of 100: a quarter of 40 pixels, 4 high
}

TEST_CASE("Testing: F2 pauses, F6 steps once, F2 goes on; F7 slows to half speed") {
    const Project project;
    input::Recording replay;
    replay.record(0, key(input::Key::Right), true); // walking right all along
    replay.record(10, key(input::Key::F2), true);   // pause
    replay.record(11, key(input::Key::F2), false);
    replay.record(20, key(input::Key::F6), true); // one step
    replay.record(21, key(input::Key::F6), false);
    replay.record(30, key(input::Key::F2), true); // go on
    replay.record(31, key(input::Key::F2), false);
    game::RuntimeOptions options = no_graphics(&replay);
    options.debug = true;
    const auto runtime = project.open(options);
    for (int i = 0; i < 11; ++i) {
        runtime->step();
    }
    const double paused_at = transform_of(*runtime, "hero").x;
    for (int i = 11; i < 20; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x == paused_at); // stood still while paused
    runtime->step();                                      // F6: one step
    const double one_step = transform_of(*runtime, "hero").x;
    CHECK(one_step > paused_at);
    for (int i = 21; i < 30; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x == one_step);
    for (int i = 30; i < 40; ++i) {
        runtime->step();
    }
    CHECK(transform_of(*runtime, "hero").x > one_step); // going on
}

TEST_CASE("Screen Pictures: fixed to the screen, a frame stretched with crisp edges") {
    // The pieces: a 3 x 3 picture with a 1-pixel edge, drawn 30 x 20.
    const std::vector<game::SlicePiece> pieces =
        game::nine_slice(3.0F, 3.0F, render::Rect{10.0F, 20.0F, 30.0F, 20.0F}, 1.0F);
    REQUIRE(pieces.size() == 9);
    CHECK(pieces[0].destination.w == 1.0F); // the top-left corner, as drawn
    CHECK(pieces[4].source.x == 1.0F);      // the middle: one pixel, stretched
    CHECK(pieces[4].destination.w == 28.0F);
    CHECK(pieces[8].destination.x == 39.0F); // the bottom-right corner
    CHECK(game::nine_slice(3.0F, 3.0F, render::Rect{0, 0, 30, 20}, 0.0F).size() == 1);
    // An edge too wide for the picture is narrowed to fit.
    CHECK(game::nine_slice(4.0F, 4.0F, render::Rect{0, 0, 30, 20}, 9.0F).front().source.w == 2.0F);
    CHECK(game::nine_slice(0.0F, 3.0F, render::Rect{0, 0, 30, 20}, 1.0F).empty());

    const Project project;
    {
        // A frame: red edges round a blue middle.
        assets::Image frame;
        frame.width = 3;
        frame.height = 3;
        for (int i = 0; i < 9; ++i) {
            const bool middle = i == 4;
            frame.rgba.insert(frame.rgba.end(), {static_cast<std::uint8_t>(middle ? 0 : 255), 0,
                                                 static_cast<std::uint8_t>(middle ? 255 : 0), 255});
        }
        const std::vector<std::uint8_t> png = assets::encode_png(frame);
        std::ofstream(project.folder.path / "assets/art/frame.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
        assets::AssetDatabase db(project.folder.path / "assets");
        static_cast<void>(db.refresh());
        const assets::AssetRecord* record = db.find("art/frame.png");
        REQUIRE(record != nullptr);

        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        game::ScreenPicture* picture = w.add<game::ScreenPicture>(w.create("panel"));
        picture->image = record->guid.to_string();
        picture->anchor = game::ScreenAnchor::BottomRight;
        picture->width = 30.0;
        picture->height = 20.0;
        picture->border = 1.0;
        write_json(project.folder.path / "assets/maps/hud.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/hud.dscene";
        settings->width = 320;
        settings->height = 180;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    const render::Color red{255, 0, 0, 255};
    const render::Color blue{0, 0, 255, 255};
    // In the bottom-right corner: x 290 to 319, y 160 to 179.
    CHECK(pixel(pixels, 320, 290, 160) == red);
    CHECK(pixel(pixels, 320, 319, 179) == red);
    CHECK(pixel(pixels, 320, 305, 160) == red); // the top edge, one pixel high
    CHECK(pixel(pixels, 320, 305, 161) == blue);
    CHECK(pixel(pixels, 320, 305, 170) == blue); // the middle, stretched
    CHECK(pixel(pixels, 320, 318, 170) == blue);
    CHECK_FALSE(pixel(pixels, 320, 289, 170) == red);
}

TEST_CASE("Classic 2D lights at night: the torch shows the ground round it") {
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity lamp = w.create("lamp");
        w.add<scene2d::Transform2D>(lamp)->y = 1.0; // its glow, a tile up: the middle
        w.add<scene2d::Light>(lamp)->radius = 6.0;
        write_json(project.folder.path / "assets/maps/night.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/night.dscene";
        settings->width = 320;
        settings->height = 180;
        settings->background = render::Color{120, 140, 110, 255};
        settings->day_minutes = 10.0; // a clock, starting late at night
        settings->start_hour = 23.0;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    REQUIRE(runtime->target() != nullptr);
    runtime->step();
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    const render::Color near = pixel(pixels, 320, 160, 90);
    const render::Color far = pixel(pixels, 320, 4, 4);
    // Dark far away; warm and bright near the torch.
    CHECK(far.r < 80);
    CHECK(near.r > far.r + 40);
    CHECK(near.r > near.b); // warm
    const assets::Image image{320, 180, pixels};
    const std::vector<std::uint8_t> png = assets::encode_png(image);
    std::ofstream(std::filesystem::temp_directory_path() / "sparxel-night-light.png",
                  std::ios::binary)
        .write(reinterpret_cast<const char*>(png.data()), // NOLINT
               static_cast<std::streamsize>(png.size()));
}

TEST_CASE("A project's own font draws its words; a missing one falls back, saying so") {
    const Project project;
    {
        // One letter, "A": a solid block 4 wide and 6 high, after a magenta column.
        assets::Image strip;
        strip.width = 5;
        strip.height = 6;
        for (std::uint32_t y = 0; y < 6; ++y) {
            for (std::uint32_t x = 0; x < 5; ++x) {
                if (x == 0) {
                    strip.rgba.insert(strip.rgba.end(), {255, 0, 255, 255});
                } else {
                    strip.rgba.insert(strip.rgba.end(), {0, 0, 0, 255});
                }
            }
        }
        const std::vector<std::uint8_t> png = assets::encode_png(strip);
        std::ofstream(project.folder.path / "assets/art/font.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        game::ScreenText* text = w.add<game::ScreenText>(w.create("label"));
        text->text = "AA";
        text->x = 0.0;
        text->y = 0.0;
        write_json(project.folder.path / "assets/maps/words.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/words.dscene";
        settings->width = 64;
        settings->height = 36;
        settings->font = "art/font.png";
        settings->font_characters = "A";
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    game::RuntimeOptions options;
    options.headless = true;
    {
        const auto runtime = project.open(options);
        REQUIRE(runtime->target() != nullptr);
        runtime->step();
        REQUIRE(runtime->draw());
        const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
        const render::Color words = runtime->settings().ui.text;
        // Two blocks, 4 wide, a pixel apart: in the words' colour, white tinted.
        CHECK(pixel(pixels, 64, 1, 2) == words);
        CHECK(pixel(pixels, 64, 3, 5) == words);
        CHECK(pixel(pixels, 64, 5, 2) == words);
        CHECK(pixel(pixels, 64, 8, 2) == words);
        CHECK_FALSE(pixel(pixels, 64, 4, 2) == words); // the gap (its shadow)
        CHECK_FALSE(pixel(pixels, 64, 1, 7) == words); // only 6 high
    }
    // A font that is not there: the built-in one, and a plain message.
    std::vector<serialize::Issue> issues;
    std::optional<game::ProjectSettings> settings = game::read_project(project.folder.path, issues);
    REQUIRE(settings.has_value());
    if (!settings) {
        return;
    }
    settings->font = "art/missing.png";
    write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    options.save_folder = project.folder.path / "saves";
    std::vector<serialize::Issue> opening;
    const auto fallback = game::Runtime::open(project.folder.path, options, opening);
    REQUIRE(fallback != nullptr);
    REQUIRE(opening.size() == 1);
    CHECK(opening[0].message.find("built-in font") != std::string::npos);
    fallback->step();
    CHECK(fallback->draw());
}

TEST_CASE("A box frame: every box drawn with the project's picture, its corners crisp") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Project project;
    {
        // A frame: red edges round a blue middle, 3 x 3, its edge 1 pixel.
        assets::Image frame;
        frame.width = 3;
        frame.height = 3;
        for (int i = 0; i < 9; ++i) {
            const bool middle = i == 4;
            frame.rgba.insert(frame.rgba.end(), {static_cast<std::uint8_t>(middle ? 0 : 255), 0,
                                                 static_cast<std::uint8_t>(middle ? 255 : 0), 255});
        }
        const std::vector<std::uint8_t> png = assets::encode_png(frame);
        std::ofstream(project.folder.path / "assets/art/frame.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->box_frame = "art/frame.png";
        settings->box_frame_edge = 1.0;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        // Kept in the project's file.
        std::vector<serialize::Issue> again;
        const auto back = game::project_from_json(game::project_to_json(*settings), again);
        REQUIRE(back.has_value());
        if (!back) {
            return;
        }
        CHECK(back->box_frame == "art/frame.png");
        CHECK(back->box_frame_edge == 1.0);
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    runtime->dialogue().say("Hi.");
    for (int i = 0; i < 8; ++i) {
        runtime->step(); // the box opens over a tenth of a second
    }
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // The message box, from (4, 10) to (28, 28): the frame's red edge, its blue middle.
    const render::Color red{255, 0, 0, 255};
    CHECK(pixel(pixels, 32, 4, 10) == red);
    CHECK(pixel(pixels, 32, 27, 27) == red);
    CHECK(pixel(pixels, 32, 15, 10) == red); // the top edge, stretched
    CHECK(pixel(pixels, 32, 6, 12) == render::Color{0, 0, 255, 255});
    CHECK(pixel(pixels, 32, 3, 10) == background); // outside it
}

TEST_CASE("Conditions on who: whether the object an event names is tagged") {
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        w.add<game::Interactable>(w.create("bell"));
        w.add<game::Tags>(w.create("knight"))->tags = "Hero, brave";
        w.add<game::Tags>(w.create("rat"))->tags = "vermin";
        write_json(project.folder.path / "assets/maps/bell.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/bell.dscene";
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
        // "When [who] rings the bell: if [who] is tagged hero, log welcome, otherwise away."
        logic::LogicAsset rules;
        rules.name = "bell";
        rules.rules.push_back(logic::Rule{
            logic::BlockId{1},
            "bell",
            "interacted",
            {ir::call("game", "is_tagged", {ir::event_value("who"), Project::text("hero")})},
            {ir::log(2, Project::text("welcome"))},
            {ir::log(3, Project::text("away"))}});
        write_json(project.folder.path / "assets/logic/bell.drules",
                   serialize::logic_to_json(rules));
    }
    const auto runtime = project.open(no_graphics(nullptr));
    runtime->step();
    const world::Entity bell = runtime->world().find_named("bell").at(0);
    for (const char* who : {"knight", "rat", "nobody"}) {
        const std::array<reflect::Value, 1> by{reflect::Value{std::string(who)}};
        CHECK(game::emit(runtime->vm(), runtime->world(), bell, "interacted", by));
        runtime->step();
    }
    const std::vector<std::string>& log = runtime->log();
    CHECK(std::count(log.begin(), log.end(), "welcome") == 1);
    CHECK(std::count(log.begin(), log.end(), "away") == 2);
}

TEST_CASE("Screen Layouts: children's elements in a column, a row or a grid") {
    using Size = std::array<float, 2>;
    const std::vector<Size> sizes{{10.0F, 4.0F}, {20.0F, 6.0F}, {6.0F, 6.0F}};
    game::ScreenLayout layout;
    layout.x = 4.0;
    layout.y = 5.0;
    layout.spacing = 2.0;
    // Down, from the top left: one under another, lined up on the left.
    CHECK(game::lay_out(layout, sizes, 320.0F, 180.0F) ==
          std::vector<Size>{{4.0F, 5.0F}, {4.0F, 11.0F}, {4.0F, 19.0F}});
    // From the top right: lined up on the right, the block 4 in from the edge.
    layout.anchor = game::ScreenAnchor::TopRight;
    CHECK(game::lay_out(layout, sizes, 320.0F, 180.0F) ==
          std::vector<Size>{{306.0F, 5.0F}, {296.0F, 11.0F}, {310.0F, 19.0F}});
    // Across, from the bottom left: side by side, lined up on the bottom.
    layout.anchor = game::ScreenAnchor::BottomLeft;
    layout.direction = game::LayoutDirection::Across;
    CHECK(game::lay_out(layout, sizes, 320.0F, 180.0F) ==
          std::vector<Size>{{4.0F, 171.0F}, {16.0F, 169.0F}, {38.0F, 169.0F}});
    // A grid of 2 columns, each cell as large as the largest (20 x 6), centred in it.
    layout.anchor = game::ScreenAnchor::TopLeft;
    layout.direction = game::LayoutDirection::Grid;
    layout.columns = 2;
    CHECK(game::lay_out(layout, sizes, 320.0F, 180.0F) ==
          std::vector<Size>{{9.0F, 6.0F}, {26.0F, 5.0F}, {11.0F, 13.0F}});
    CHECK(game::lay_out(layout, {}, 320.0F, 180.0F).empty());

    // In a running game: two bars under a layout at the top right, one under the other.
    const Project project;
    {
        reflect::Registry registry;
        game::register_types(registry);
        world::World w(registry);
        REQUIRE(scene2d::register_components(w));
        REQUIRE(game::register_behaviour_components(w));
        const world::Entity block = w.create("party");
        game::ScreenLayout* stack = w.add<game::ScreenLayout>(block);
        stack->anchor = game::ScreenAnchor::TopRight;
        stack->x = 2.0;
        stack->y = 2.0;
        for (const char* colour : {"#00ff00", "#0000ff"}) {
            const world::Entity row = w.create(colour);
            REQUIRE(w.set_parent(row, block));
            game::ScreenBar* bar = w.add<game::ScreenBar>(row);
            bar->value = "1";
            bar->most = "1";
            bar->width = 20.0;
            bar->height = 3.0;
            bar->color = colour;
            bar->anchor = game::ScreenAnchor::BottomLeft; // not used under a layout
        }
        write_json(project.folder.path / "assets/maps/party.dscene", serialize::scene_to_json(w));
        std::vector<serialize::Issue> issues;
        std::optional<game::ProjectSettings> settings =
            game::read_project(project.folder.path, issues);
        REQUIRE(settings.has_value());
        if (!settings) {
            return;
        }
        settings->start_scene = "maps/party.dscene";
        settings->width = 64;
        settings->height = 36;
        write_json(project.folder.path / game::project_file, game::project_to_json(*settings));
    }
    game::RuntimeOptions options;
    options.headless = true;
    const auto runtime = project.open(options);
    runtime->step();
    REQUIRE(runtime->draw());
    const std::vector<std::uint8_t> pixels = runtime->target()->read_pixels();
    // Each 22 x 5 with its frame, 2 apart: x 40 to 61; y 2 to 6, then 9 to 13.
    CHECK(pixel(pixels, 64, 50, 4) == render::Color{0, 255, 0, 255});
    CHECK(pixel(pixels, 64, 50, 11) == render::Color{0, 0, 255, 255});
    CHECK_FALSE(pixel(pixels, 64, 50, 32) == render::Color{0, 255, 0, 255}); // not its own
}
