// The editor's model, on a copy of the sample: opening, editing through reflection with
// undo and redo, saving, picking, and the preview the scene view shows.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/logic/ir.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "editor/core/document.h"
#include "editor/core/logic_text.h"
#include "editor/core/preview.h"
#include "support/gpu.h"

namespace fs = std::filesystem;
namespace game = engine::game;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace world = engine::world;
using engine::Guid;

namespace {

struct Sample {
    Sample() {
        path = fs::temp_directory_path() / ("sparxel-editor-" + Guid::generate().to_string());
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

    [[nodiscard]] std::unique_ptr<editor::Document> open() const {
        std::vector<serialize::Issue> issues;
        auto document = editor::Document::open(path, issues);
        std::string text;
        for (const serialize::Issue& issue : issues) {
            text += issue.path + ": " + issue.message + "\n";
        }
        INFO(text);
        CHECK(issues.empty());
        REQUIRE(document != nullptr);
        return document;
    }

    fs::path path;
};

Guid guid_of(const editor::Document& d, std::string_view name) {
    const std::vector<world::Entity> found = d.world().find_named(name);
    REQUIRE(found.size() == 1);
    return d.world().guid(found[0]);
}

const reflect::PropertyInfo& property(const reflect::TypeInfo& type, std::string_view id) {
    const reflect::PropertyInfo* p = type.find_property(id);
    REQUIRE(p != nullptr);
    return *p;
}

} // namespace

TEST_CASE("The editor opens a project and lists its scene") {
    const Sample sample;
    const auto d = sample.open();
    CHECK(d->settings().name == "Hello Sparxel");
    CHECK(d->scene_path() == "maps/village.dscene");
    std::vector<std::string> names;
    for (const world::Entity e : d->entities()) {
        names.emplace_back(d->world().name(e));
    }
    CHECK(names ==
          std::vector<std::string>{"ground", "walls", "hero", "elder", "gate", "lantern", "lamp"});
    CHECK_FALSE(d->dirty());
    CHECK_FALSE(d->can_undo());
    // The component types, by display name, include every behaviour.
    std::vector<std::string> types;
    for (const reflect::TypeInfo* type : d->component_types()) {
        types.push_back(type->meta.display_name);
    }
    CHECK(std::is_sorted(types.begin(), types.end()));
    CHECK(std::find(types.begin(), types.end(), "Light") != types.end());
    const reflect::TypeInfo* position = d->registry().find_type<scene2d::Transform2D>();
    REQUIRE(position != nullptr);
    CHECK(position->properties.at(0).meta.display_name == "Across");
    CHECK(position->properties.at(1).meta.display_name == "Down");
    CHECK(std::find(types.begin(), types.end(), "Top-down Mover") != types.end());

    std::vector<serialize::Issue> issues;
    CHECK(editor::Document::open(sample.path / "nowhere", issues) == nullptr);
    CHECK_FALSE(issues.empty());
}

TEST_CASE("Editing a property is one step of undo; typing merges into it") {
    const Sample sample;
    const auto d = sample.open();
    const Guid hero = guid_of(*d, "hero");
    const reflect::TypeInfo* mover = d->registry().find_type<game::TopDownMover>();
    REQUIRE(mover != nullptr);
    const reflect::PropertyInfo& speed = property(*mover, "speed");
    const auto speed_now = [&] {
        return d->world().get<game::TopDownMover>(d->world().find(hero))->speed;
    };
    std::string error;
    REQUIRE(d->set_property(hero, *mover, speed, reflect::Value{6.0}, error));
    CHECK(speed_now() == 6.0);
    CHECK(d->dirty());
    CHECK(d->undo_label() == "Change Speed of hero");
    // Typing 7, then 8: the same property again merges into one step.
    REQUIRE(d->set_property(hero, *mover, speed, reflect::Value{7.0}, error));
    REQUIRE(d->set_property(hero, *mover, speed, reflect::Value{8.0}, error));
    REQUIRE(d->undo());
    CHECK(speed_now() == 4.0); // all three, from the scene's 4
    CHECK_FALSE(d->can_undo());
    CHECK_FALSE(d->dirty());
    REQUIRE(d->redo());
    CHECK(speed_now() == 8.0);
    // After the field loses focus, the next change is a step of its own.
    d->end_merge();
    REQUIRE(d->set_property(hero, *mover, speed, reflect::Value{2.0}, error));
    REQUIRE(d->undo());
    CHECK(speed_now() == 8.0);
    CHECK(d->can_redo());
    // A new edit clears what could be redone.
    REQUIRE(d->set_property(hero, *mover, speed, reflect::Value{3.0}, error));
    CHECK_FALSE(d->can_redo());

    // Refused values change nothing and add no step.
    d->end_merge();
    const std::string before = d->undo_label();
    CHECK_FALSE(d->set_property(hero, *mover, speed, reflect::Value{std::string("fast")}, error));
    CHECK_FALSE(error.empty());
    CHECK(speed_now() == 3.0);
    const reflect::TypeInfo* light = d->registry().find_type<scene2d::Light>();
    REQUIRE(light != nullptr);
    CHECK_FALSE(d->set_property(hero, *light, property(*light, "radius"), reflect::Value{2.0},
                                error)); // the hero has no Light
    CHECK(error == "That entity has no Light.");
    CHECK(d->undo_label() == before);
}

TEST_CASE("Entities and components are created, renamed and removed, and come back on undo") {
    const Sample sample;
    const auto d = sample.open();
    const Guid barrel = d->create_entity("barrel", 3.0, 4.0);
    const world::Entity e = d->world().find(barrel);
    REQUIRE(e.is_valid());
    CHECK(d->world().get<scene2d::Transform2D>(e)->x == 3.0);
    CHECK(d->undo_label() == "Create barrel");
    const reflect::TypeInfo* body = d->registry().find_type<game::Body>();
    REQUIRE(body != nullptr);
    REQUIRE(d->add_component(barrel, *body));
    CHECK_FALSE(d->add_component(barrel, *body)); // already there
    CHECK(d->undo_label() == "Add Solid Body to barrel");
    REQUIRE(d->rename(barrel, "crate"));
    CHECK(d->undo_label() == "Rename barrel");
    CHECK(d->world().name(d->world().find(barrel)) == "crate");

    // Deleting the elder, then undoing, brings it back with the same GUID and components.
    const Guid elder = guid_of(*d, "elder");
    const std::size_t components = d->world().components(d->world().find(elder)).size();
    REQUIRE(d->delete_entity(elder));
    CHECK_FALSE(d->world().find(elder).is_valid());
    CHECK(d->undo_label() == "Delete elder");
    REQUIRE(d->undo());
    REQUIRE(d->world().find(elder).is_valid());
    CHECK(d->world().components(d->world().find(elder)).size() == components);

    REQUIRE(d->remove_component(barrel, *body));
    CHECK(d->world().get<game::Body>(d->world().find(barrel)) == nullptr);
    REQUIRE(d->undo());
    CHECK(d->world().get<game::Body>(d->world().find(barrel)) != nullptr);
    // A drag: many moves of one entity are one step.
    d->end_merge();
    const Guid hero = guid_of(*d, "hero");
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(d->move_entity(hero, 10.0 + i, 9.0 - i));
    }
    CHECK(d->world().get<scene2d::Transform2D>(d->world().find(hero))->x == 15.0);
    CHECK(d->undo_label() == "Move hero");
    REQUIRE(d->undo());
    CHECK(d->world().get<scene2d::Transform2D>(d->world().find(hero))->x == 10.0);
    CHECK(d->world().get<scene2d::Transform2D>(d->world().find(hero))->y == 9.0);
    CHECK_FALSE(d->move_entity(hero, std::nan(""), 1.0));
    // Undo everything: back to the scene as opened.
    while (d->undo()) {
    }
    CHECK_FALSE(d->world().find(barrel).is_valid());
    CHECK_FALSE(d->dirty());
}

TEST_CASE("Painting tiles: cells change, the map grows, and a stroke is one step") {
    const Sample sample;
    const auto d = sample.open();
    const Guid ground = guid_of(*d, "ground");
    const auto cell = [&](std::uint32_t x, std::uint32_t y) {
        std::string error;
        const scene2d::TileGrid* grid =
            d->world().get<scene2d::Tilemap>(d->world().find(ground))->grid(error);
        REQUIRE(grid != nullptr);
        return grid->at(x, y);
    };
    const auto size = [&] {
        std::string error;
        const scene2d::TileGrid* grid =
            d->world().get<scene2d::Tilemap>(d->world().find(ground))->grid(error);
        REQUIRE(grid != nullptr);
        return std::pair{grid->width, grid->height};
    };
    CHECK(cell(3, 2) == 0); // grass
    CHECK(size() == std::pair{20U, 12U});
    // A stroke across three cells, with path (tile 1).
    REQUIRE(d->paint_tile(ground, 3.5, 2.5, 1));
    REQUIRE(d->paint_tile(ground, 4.2, 2.9, 1));
    REQUIRE(d->paint_tile(ground, 5.99, 2.0, 1));
    CHECK(cell(3, 2) == 1);
    CHECK(cell(4, 2) == 1);
    CHECK(cell(5, 2) == 1);
    CHECK(cell(6, 2) == 0);
    CHECK(d->undo_label() == "Paint ground");
    // Beyond the right edge, the map grows; above or left of its corner is refused.
    REQUIRE(d->paint_tile(ground, 22.5, 0.5, 2));
    CHECK(size() == std::pair{23U, 12U});
    CHECK(cell(22, 0) == 2);
    CHECK(cell(21, 0) == -1); // the new cells between are empty
    CHECK_FALSE(d->paint_tile(ground, -0.5, 3.0, 1));
    CHECK_FALSE(d->paint_tile(ground, 3.0, -0.5, 1));
    CHECK_FALSE(d->paint_tile(guid_of(*d, "hero"), 3.0, 3.0, 1)); // not a tilemap
    // Erasing.
    REQUIRE(d->paint_tile(ground, 4.5, 2.5, -1));
    CHECK(cell(4, 2) == -1);
    // The whole stroke undoes at once.
    REQUIRE(d->undo());
    CHECK(cell(3, 2) == 0);
    CHECK(cell(5, 2) == 0);
    CHECK(size() == std::pair{20U, 12U});
    // A new stroke after end_merge() is its own step.
    REQUIRE(d->redo());
    d->end_merge();
    REQUIRE(d->paint_tile(ground, 7.5, 7.5, 2));
    REQUIRE(d->undo());
    CHECK(cell(7, 7) == 0);
    CHECK(cell(3, 2) == 1);
}

TEST_CASE("Saving writes the scene; reopening finds the edits") {
    const Sample sample;
    const Guid lamp_guid = [&] {
        const auto d = sample.open();
        const Guid lamp = guid_of(*d, "lamp");
        const reflect::TypeInfo* light = d->registry().find_type<scene2d::Light>();
        REQUIRE(light != nullptr);
        std::string error;
        REQUIRE(
            d->set_property(lamp, *light, property(*light, "radius"), reflect::Value{7.5}, error));
        REQUIRE(d->save(error));
        CHECK_FALSE(d->dirty());
        CHECK(d->can_undo()); // saving keeps the history
        return lamp;
    }();
    const auto again = sample.open();
    const world::Entity lamp = again->world().find(lamp_guid);
    REQUIRE(lamp.is_valid());
    CHECK(again->world().get<scene2d::Light>(lamp)->radius == 7.5);
    CHECK_FALSE(again->dirty());
}

TEST_CASE("Picking finds what is drawn on top at a point") {
    const Sample sample;
    const auto d = sample.open();
    const auto name_at = [&](double x, double y) {
        const world::Entity e = editor::pick(d->world(), d->assets(), x, y, d->settings().ppu);
        return e.is_valid() ? std::string(d->world().name(e)) : std::string("nothing");
    };
    // The hero stands at (10, 9) on a 16 x 16 frame, bottom centre: (9.5, 8) to (10.5, 9).
    const std::optional<scene2d::Box> hero = editor::sprite_bounds(
        d->world(), d->assets(), d->world().find_named("hero").at(0), d->settings().ppu);
    REQUIRE(hero.has_value());
    CHECK(hero.value_or(scene2d::Box{}).left == 9.5);
    CHECK(hero.value_or(scene2d::Box{}).top == 8.0);
    CHECK(name_at(10.0, 8.5) == "hero");
    CHECK(name_at(6.0, 4.5) == "elder");
    CHECK(name_at(3.0, 3.0) == "ground");
    CHECK(name_at(0.5, 0.5) == "walls"); // drawn after the ground, on the same layer
    CHECK(name_at(-5.0, 3.0) == "nothing");
}

TEST_CASE("The preview draws the scene as the game would") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const Sample sample;
    // Without the sample's look, so colours are exact.
    {
        std::vector<serialize::Issue> issues;
        game::ProjectSettings settings =
            game::read_project(sample.path, issues).value_or(game::ProjectSettings{});
        settings.look = engine::render::Look{};
        std::ofstream(sample.path / game::project_file, std::ios::binary)
            << serialize::to_text(game::project_to_json(settings));
    }
    const auto d = sample.open();
    std::string error;
    const auto preview = editor::Preview::create(error);
    INFO(error);
    REQUIRE(preview != nullptr);
    // Looking at (10, 6): the hero's feet (10, 9) are 3 tiles below the centre, at pixel
    // (160, 138). Its robe, rows 7 to 12 of its 16-pixel frame, is at y = 122 + 7 to 12.
    const std::optional<engine::assets::Image> image = preview->render(*d, 10.0, 6.0, error);
    REQUIRE(image.has_value());
    const engine::assets::Image& frame = image.value_or(engine::assets::Image{});
    CHECK(frame.width == 320);
    CHECK(frame.height == 180);
    const auto at = [&frame](std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (std::size_t{y} * frame.width + x) * 4;
        return std::array<int, 3>{frame.rgba.at(i), frame.rgba.at(i + 1), frame.rgba.at(i + 2)};
    };
    CHECK(at(158, 131) == std::array<int, 3>{60, 96, 200}); // the robe
    CHECK(at(2, 2) == std::array<int, 3>{128, 128, 140});   // the wall's stone
    // Moving the hero in the document moves it in the next preview.
    std::string why;
    const reflect::TypeInfo* transform = d->registry().find_type<scene2d::Transform2D>();
    REQUIRE(transform != nullptr);
    REQUIRE(d->set_property(guid_of(*d, "hero"), *transform, property(*transform, "x"),
                            reflect::Value{12.0}, why));
    const std::optional<engine::assets::Image> moved = preview->render(*d, 10.0, 6.0, error);
    REQUIRE(moved.has_value());
    const engine::assets::Image& after = moved.value_or(engine::assets::Image{});
    const std::size_t i = (std::size_t{131} * 320 + 158 + 32) * 4;
    CHECK(std::array<int, 3>{after.rgba.at(i), after.rgba.at(i + 1), after.rgba.at(i + 2)} ==
          std::array<int, 3>{60, 96, 200});
}

TEST_CASE("Logic reads as plain language") {
    const Sample sample;
    const auto d = sample.open();
    std::vector<serialize::Issue> issues;
    const std::vector<editor::LogicFile> files = editor::logic_files(*d, issues);
    CHECK(issues.empty());
    REQUIRE(files.size() == 1);
    CHECK(files[0].path == "logic/main.drules");
    std::vector<std::string> shown;
    for (const editor::LogicLine& line : editor::describe(files[0].asset, *d)) {
        shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        CHECK(line.block.is_valid()); // every line can be traced to its block
    }
    const std::vector<std::string> expected{
        "When the game starts",
        "  Say \u201cWelcome to the village! Arrow keys to walk, Z to talk.\u201d (dialogue)",
        "When [who] interacts with it (elder)",
        "  Say \u201cElder: Welcome, traveller. The road east leads home.\u201d (dialogue)",
        "  Play sound \u201csfx/chime.wav\u201d (audio)",
        "  Set talks to talks + 1",
        std::string("  Ask \u201cElder: Shall I tell you about the road?\u201d with answers ") +
            "\u201cYes, please|No, thanks\u201d (dialogue)",
        "  Wait until not a message is open (dialogue)",
        "  If the answer chosen (dialogue) = 1:",
        std::string("    Say \u201cElder: Follow the path east, through the gate. ") +
            "Home is not far.\u201d (dialogue)",
        "  Otherwise:",
        "    Say \u201cElder: Safe travels, then.\u201d (dialogue)",
        "When [who] walks in (gate)",
        "  Say \u201cThe gate creaks. Beyond it, the road home.\u201d (dialogue)",
        "Brain weather",
        "  State sunny",
        "    On enter:",
        "      Log \u201cThe sun is out.\u201d",
        "      Set ambient light of lighting to 0.55",
        "      Set sunlight of lighting to 0.45",
        "    Go to rain after 30 seconds",
        "  State rain",
        "    On enter:",
        "      Log \u201cIt starts to rain.\u201d",
        "      Set ambient light of lighting to 0.25",
        "      Set sunlight of lighting to 0.1",
        "    Go to sunny after 15 seconds",
    };
    CHECK(shown == expected);

    // Operators, conditions, otherwise and missing objects read sensibly too.
    namespace ir = engine::logic::ir;
    engine::logic::LogicAsset asset;
    asset.rules.push_back(engine::logic::Rule{
        engine::logic::BlockId{1},
        "",
        "game_start",
        {ir::all({ir::compare(engine::logic::CompareOp::GreaterEqual, ir::variable("hp"),
                              ir::literal(reflect::Value{std::int64_t{3}})),
                  ir::negate(ir::literal(reflect::Value{false}))})},
        {ir::wait(2, ir::literal(reflect::Value{0.5})),
         ir::repeat(3, ir::literal(reflect::Value{std::int64_t{2}}), {ir::run(4, "intro")}),
         ir::do_action(5, "nobody", "dance")},
        {ir::log(6, ir::event_value("who"))}});
    shown.clear();
    for (const editor::LogicLine& line : editor::describe(asset, *d)) {
        shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
    }
    CHECK(shown == std::vector<std::string>{"When the game starts", "  If (hp \u2265 3 and not no)",
                                            "  Wait 0.5 seconds",
                                            "  Repeat 2 times:", "    Run intro and wait for it",
                                            "  dance (nobody)", "  Otherwise:", "    Log [who]"});
}

TEST_CASE("Picking in the Diorama style goes through the camera") {
    const Sample sample;
    const auto d = sample.open();
    const scene2d::DioramaCamera camera = editor::diorama_camera(d->settings(), 10.0, 7.0);
    const float aspect = 320.0F / 180.0F;
    const auto name_at = [&](double nx, double ny) {
        const world::Entity e =
            editor::pick_diorama(d->world(), d->assets(), camera, aspect, nx, ny);
        return e.is_valid() ? std::string(d->world().name(e)) : std::string("nothing");
    };
    // The hero's billboard on the screen: pick its middle.
    const world::Entity hero = d->world().find_named("hero").at(0);
    const std::optional<std::array<double, 4>> rect =
        editor::diorama_sprite_rect(d->world(), d->assets(), hero, camera, aspect);
    REQUIRE(rect.has_value());
    const std::array<double, 4> r = rect.value_or(std::array<double, 4>{});
    CHECK(r[0] < r[2]);
    CHECK(r[1] < r[3]);
    CHECK(name_at((r[0] + r[2]) / 2.0, (r[1] + r[3]) / 2.0) == "hero");
    // The hero stands below the point looked at (10, 7): lower on the screen, near the middle.
    CHECK((r[1] + r[3]) / 2.0 < 0.0);
    CHECK(std::abs((r[0] + r[2]) / 2.0) < 0.05);
    // Just beside the billboard: the ground under it, not the hero.
    CHECK(name_at(r[2] + 0.05, r[1] + 0.01) == "ground");
    // The elder, picked the same way.
    const std::optional<std::array<double, 4>> elder = editor::diorama_sprite_rect(
        d->world(), d->assets(), d->world().find_named("elder").at(0), camera, aspect);
    REQUIRE(elder.has_value());
    const std::array<double, 4> e = elder.value_or(std::array<double, 4>{});
    CHECK(name_at((e[0] + e[2]) / 2.0, (e[1] + e[3]) / 2.0) == "elder");
    // The wall along the bottom of the map, near the bottom edge of the screen.
    CHECK(name_at(0.0, -0.99) == "walls");
    // Looking up into the sky: nothing.
    const scene2d::DioramaCamera low = [&] {
        scene2d::DioramaCamera c = camera;
        c.pitch = 10.0;
        return c;
    }();
    CHECK_FALSE(editor::pick_diorama(d->world(), d->assets(), low, aspect, 0.0, 0.99).is_valid());
}
