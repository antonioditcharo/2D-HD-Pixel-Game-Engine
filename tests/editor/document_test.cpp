// The editor's model, on a copy of the sample: opening, editing through reflection with
// undo and redo, saving, picking, and the preview the scene view shows.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/game/runtime.h"
#include "engine/input/input.h"
#include "engine/logic/ir.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "editor/core/document.h"
#include "editor/core/logic_text.h"
#include "editor/core/new_project.h"
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

TEST_CASE("Placing a picture from the assets makes a sprite there, as one step") {
    const Sample sample;
    const auto d = sample.open();
    const std::size_t before = d->entities().size();
    std::string error;
    // To whole pixels: 16 per tile, so 3.03 (48.48 pixels) becomes 3 and 4.94 (79.04
    // pixels) becomes 79 / 16.
    const Guid placed = d->place_picture("art/hero.png", 3.03, 4.94, error);
    REQUIRE(placed.is_valid());
    CHECK(error.empty());
    const world::Entity e = d->world().find(placed);
    CHECK(d->world().name(e) == "hero");
    CHECK(d->world().get<scene2d::Transform2D>(e)->x == 3.0);
    CHECK(d->world().get<scene2d::Transform2D>(e)->y == 4.9375);
    REQUIRE(d->world().get<scene2d::Sprite>(e) != nullptr);
    CHECK(d->world().get<scene2d::Sprite>(e)->image ==
          d->assets().find("art/hero.png")->guid.to_string());
    CHECK(d->entities().size() == before + 1);
    CHECK(d->undo_label() == "Place hero");
    CHECK(d->dirty());
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    CHECK_FALSE(d->world().find(placed).is_valid());

    // Only pictures that are assets, at a real point.
    CHECK_FALSE(d->place_picture("sfx/chime.wav", 1.0, 1.0, error).is_valid());
    CHECK(error == "Only pictures (PNG) can be placed in the scene; sfx/chime.wav is not one.");
    CHECK_FALSE(d->place_picture("art/nothing.png", 1.0, 1.0, error).is_valid());
    CHECK(error == "There is no asset art/nothing.png in assets/.");
    CHECK_FALSE(d->place_picture("art/hero.png", std::nan(""), 1.0, error).is_valid());
    CHECK(d->entities().size() == before);
    CHECK(d->redo_label() == "Place hero"); // refusals record nothing, so redo is kept
}

TEST_CASE("Project settings change as one step, are checked as the game checks them, and save") {
    const Sample sample;
    const auto d = sample.open();
    const game::ProjectSettings original = d->settings();
    std::string error;
    // Two changes before the merge ends are one step, labelled for the project.
    game::ProjectSettings changed = original;
    changed.style = game::RenderStyle::Diorama;
    REQUIRE(d->set_settings(changed, error));
    changed.sun_angle = 90.0;
    changed.start_scene = "maps/elsewhere.dscene"; // the start scene is not changed here
    REQUIRE(d->set_settings(changed, error));
    CHECK(d->settings().style == game::RenderStyle::Diorama);
    CHECK(d->settings().sun_angle == 90.0);
    CHECK(d->settings().start_scene == original.start_scene);
    CHECK(d->undo_label() == "Change the project settings");
    CHECK(d->dirty());
    // Out of range: refused as the game would refuse its project file, changing nothing.
    changed.ppu = 3;
    CHECK_FALSE(d->set_settings(changed, error));
    CHECK(error == "This must be a whole number from 8 to 64.");
    CHECK(d->settings().ppu == original.ppu);
    // Undo puts every setting back at once.
    REQUIRE(d->undo());
    CHECK(d->settings().style == original.style);
    CHECK(d->settings().sun_angle == original.sun_angle);
    CHECK_FALSE(d->dirty());
    REQUIRE(d->redo());
    // Saved to the project file; reopening reads them.
    REQUIRE(d->save(error));
    const auto again = sample.open();
    CHECK(again->settings().style == game::RenderStyle::Diorama);
    CHECK(again->settings().sun_angle == 90.0);
    CHECK(again->settings().start_scene == original.start_scene);
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

TEST_CASE("Painting rectangles and fills: whole areas, each one step") {
    const Sample sample;
    const auto d = sample.open();
    const Guid ground = guid_of(*d, "ground");
    const auto grid = [&] {
        std::string error;
        const scene2d::TileGrid* parsed =
            d->world().get<scene2d::Tilemap>(d->world().find(ground))->grid(error);
        REQUIRE(parsed != nullptr);
        return *parsed;
    };
    const auto count = [&](std::int32_t tile) {
        const scene2d::TileGrid g = grid();
        return std::count(g.cells.begin(), g.cells.end(), tile);
    };
    const scene2d::TileGrid original = grid();
    REQUIRE(original.width == 20);
    REQUIRE(original.height == 12);
    const auto paths = count(1);
    // A rectangle between two points, in either order: 3 by 2 cells of path.
    REQUIRE(original.at(8, 5) == 0);
    REQUIRE(d->paint_tile_rect(ground, 10.9, 6.1, 8.2, 5.7, 1));
    CHECK(d->undo_label() == "Paint a rectangle of ground");
    for (std::uint32_t x = 8; x <= 10; ++x) {
        CHECK(grid().at(x, 5) == 1);
        CHECK(grid().at(x, 6) == 1);
    }
    CHECK(grid().at(7, 5) == original.at(7, 5));
    CHECK(grid().at(11, 6) == original.at(11, 6));
    CHECK(grid().at(8, 7) == original.at(8, 7));
    // Two rectangles are two steps, even with no end_merge() between.
    REQUIRE(d->paint_tile_rect(ground, 0.5, 0.5, 1.5, 0.5, 2));
    REQUIRE(d->undo());
    CHECK(grid().at(0, 0) == original.at(0, 0));
    CHECK(grid().at(9, 6) == 1);
    // Past the right edge the map grows; the part left of its corner is left out; a
    // rectangle wholly outside it is refused.
    REQUIRE(d->paint_tile_rect(ground, -3.0, 11.5, 21.5, 11.5, 2));
    CHECK(grid().width == 22);
    CHECK(grid().at(0, 11) == 2);
    CHECK(grid().at(21, 11) == 2);
    CHECK(grid().at(21, 0) == -1);
    CHECK_FALSE(d->paint_tile_rect(ground, -3.0, 1.0, -1.0, 2.0, 2));
    // Erasing never grows the map.
    REQUIRE(d->paint_tile_rect(ground, 20.5, 10.5, 40.0, 40.0, -1));
    CHECK(grid().width == 22);
    CHECK(grid().height == 12);
    CHECK(grid().at(21, 11) == -1);
    CHECK(grid().at(19, 11) == 2);
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(grid().cells == original.cells);

    // A fill: a pond of 2 inside a ring of path, then the grass around it.
    REQUIRE(d->paint_tile_rect(ground, 2.5, 2.5, 6.5, 6.5, 1));
    REQUIRE(d->paint_tile_rect(ground, 3.5, 3.5, 5.5, 5.5, 0));
    REQUIRE(d->fill_tiles(ground, 4.5, 4.5, 2));
    CHECK(d->undo_label() == "Fill ground");
    for (std::uint32_t y = 3; y <= 5; ++y) {
        for (std::uint32_t x = 3; x <= 5; ++x) {
            CHECK(grid().at(x, y) == 2);
        }
    }
    CHECK(grid().at(2, 2) == 1); // the ring holds it in
    CHECK(grid().at(1, 1) == original.at(1, 1));
    CHECK(grid().at(7, 7) == original.at(7, 7));
    // Filling with what is there already changes nothing; outside the map is refused.
    // (No step is recorded for it: the undos below reach the start in exactly as many
    // steps as there were changes.)
    REQUIRE(d->fill_tiles(ground, 4.5, 4.5, 2));
    CHECK(count(2) == 9);
    CHECK_FALSE(d->fill_tiles(ground, 25.0, 3.0, 2));
    CHECK_FALSE(d->fill_tiles(ground, -0.5, 3.0, 2));
    CHECK_FALSE(d->fill_tiles(guid_of(*d, "hero"), 3.0, 3.0, 2));
    // Erasing by fill: the pond goes, the ring stays; one undo brings it back.
    REQUIRE(d->fill_tiles(ground, 3.5, 5.5, -1));
    CHECK(grid().at(4, 4) == -1);
    CHECK(grid().at(6, 6) == 1);
    REQUIRE(d->undo());
    CHECK(grid().at(4, 4) == 2);
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(grid().cells == original.cells);
    CHECK(count(1) == paths);
}

TEST_CASE("A new project is a small game that opens cleanly and can be edited") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    CHECK_FALSE(editor::create_project(folder, "", error));
    CHECK(error == "Give the game a name.");
    CHECK_FALSE(fs::exists(folder)); // nothing made for a refused name
    REQUIRE(editor::create_project(folder, "My Quest", error));
    for (const char* file :
         {"project.sparxel.json", "assets/art/tiles.png", "assets/art/tiles.png.meta",
          "assets/art/hero.png", "assets/art/hero.png.meta", "assets/maps/start.dscene",
          "assets/maps/start.dscene.meta"}) {
        CAPTURE(file);
        CHECK(fs::is_regular_file(folder / file));
    }
    // Never over what is there.
    CHECK_FALSE(editor::create_project(folder, "Another", error));
    CHECK(error == "That folder already has something in it: choose an empty or a new one.");
    {
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        for (const serialize::Issue& issue : issues) {
            CAPTURE(issue.path);
            CAPTURE(issue.message);
            CHECK(false);
        }
        REQUIRE(d != nullptr);
        CHECK(d->settings().name == "My Quest");
        CHECK(d->settings().start_scene == "maps/start.dscene");
        CHECK(d->entities().size() == 3);
        const world::Entity hero = d->world().find(guid_of(*d, "hero"));
        const auto* at = d->world().get<scene2d::Transform2D>(hero);
        REQUIRE(at != nullptr);
        CHECK(at->x == 10.0);
        CHECK(at->y == 6.0);
        CHECK(d->world().get<game::TopDownMover>(hero) != nullptr);
        // The ground: 20 by 12 of grass, crossed by a path; the walls: a solid ring of stone.
        std::string parse_error;
        const auto* ground =
            d->world().get<scene2d::Tilemap>(d->world().find(guid_of(*d, "ground")));
        const auto* walls = d->world().get<scene2d::Tilemap>(d->world().find(guid_of(*d, "walls")));
        REQUIRE(ground != nullptr);
        REQUIRE(walls != nullptr);
        const scene2d::TileGrid* grass = ground->grid(parse_error);
        REQUIRE(grass != nullptr);
        CHECK(grass->width == 20);
        CHECK(grass->height == 12);
        CHECK(grass->at(3, 2) == 0);
        CHECK(grass->at(3, 6) == 1);
        const scene2d::TileGrid* ring = walls->grid(parse_error);
        REQUIRE(ring != nullptr);
        CHECK(walls->solid);
        CHECK(ring->at(0, 5) == 2);
        CHECK(ring->at(19, 11) == 2);
        CHECK(ring->at(5, 5) == -1);
        // Both pictures are found by the GUIDs the scene holds.
        const Guid tileset = Guid::parse(ground->tileset).value_or(Guid{});
        REQUIRE(tileset.is_valid());
        const engine::assets::AssetRecord* record = d->assets().find(tileset);
        REQUIRE(record != nullptr);
        CHECK(record->path == "art/tiles.png");
        const std::vector<std::uint8_t> bytes =
            d->assets().read(tileset).value_or(std::vector<std::uint8_t>{});
        const engine::assets::Image picture =
            engine::assets::decode_png(bytes, parse_error).value_or(engine::assets::Image{});
        CHECK(picture.width == 48);
        CHECK(picture.height == 16);
        // It edits and saves like any project.
        REQUIRE(d->paint_tile(guid_of(*d, "ground"), 4.5, 4.5, 1));
        REQUIRE(d->save(error));
    }
    std::vector<serialize::Issue> issues;
    const auto again = editor::Document::open(folder, issues);
    REQUIRE(again != nullptr);
    CHECK(issues.empty());
    std::string parse_error;
    CHECK(again->world()
              .get<scene2d::Tilemap>(again->world().find(guid_of(*again, "ground")))
              ->grid(parse_error)
              ->at(4, 4) == 1);
    std::error_code ignored;
    fs::remove_all(folder, ignored);
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

TEST_CASE("A new project draws: grass, a path, stone walls and the hero") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "My Quest", error));
    {
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        REQUIRE(d != nullptr);
        const auto preview = editor::Preview::create(error);
        INFO(error);
        REQUIRE(preview != nullptr);
        // Looking at the hero's feet (10, 6), the middle of the 320 by 180 picture: the map's
        // corner is at pixel (0, -6).
        const std::optional<engine::assets::Image> image = preview->render(*d, 10.0, 6.0, error);
        REQUIRE(image.has_value());
        const engine::assets::Image& frame = image.value_or(engine::assets::Image{});
        REQUIRE(frame.width == 320);
        REQUIRE(frame.height == 180);
        const auto at = [&frame](std::uint32_t x, std::uint32_t y) {
            const std::size_t i = (std::size_t{y} * frame.width + x) * 4;
            return std::array<int, 3>{frame.rgba.at(i), frame.rgba.at(i + 1), frame.rgba.at(i + 2)};
        };
        CHECK(at(2, 2) == std::array<int, 3>{120, 122, 132});    // the wall's stone
        CHECK(at(40, 30) == std::array<int, 3>{72, 148, 76});    // grass
        CHECK(at(40, 95) == std::array<int, 3>{196, 168, 112});  // the path, along row 6
        CHECK(at(160, 84) == std::array<int, 3>{52, 96, 180});   // the hero's tunic
        CHECK(at(160, 79) == std::array<int, 3>{238, 196, 160}); // and face
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("A new Diorama project draws in 3D, and not as the flat one does") {
    if (test_support::gpu() == nullptr) {
        return;
    }
    std::string error;
    const auto drawn = [&](editor::ProjectTemplate kind) {
        const fs::path folder =
            fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
        REQUIRE(editor::create_project(folder, "My Quest", error, kind));
        engine::assets::Image frame;
        {
            std::vector<serialize::Issue> issues;
            const auto d = editor::Document::open(folder, issues);
            REQUIRE(d != nullptr);
            CHECK(issues.empty());
            CHECK(d->settings().style == (kind == editor::ProjectTemplate::Diorama
                                              ? game::RenderStyle::Diorama
                                              : game::RenderStyle::Classic2D));
            const auto preview = editor::Preview::create(error);
            REQUIRE(preview != nullptr);
            frame = preview->render(*d, 10.0, 6.0, error).value_or(engine::assets::Image{});
        }
        std::error_code ignored;
        fs::remove_all(folder, ignored);
        return frame;
    };
    const engine::assets::Image flat = drawn(editor::ProjectTemplate::TopDown2D);
    const engine::assets::Image diorama = drawn(editor::ProjectTemplate::Diorama);
    REQUIRE(flat.width == 320);
    REQUIRE(diorama.width == 320);
    REQUIRE(diorama.height == 180);
    CHECK(diorama.rgba != flat.rgba);
    // The scene fills the middle of the picture: green (the grass) wins there.
    std::size_t green = 0;
    for (std::uint32_t y = 60; y < 120; ++y) {
        for (std::uint32_t x = 60; x < 260; ++x) {
            const std::size_t i = (std::size_t{y} * 320 + x) * 4;
            green += diorama.rgba.at(i + 1) > diorama.rgba.at(i) &&
                             diorama.rgba.at(i + 1) > diorama.rgba.at(i + 2)
                         ? 1U
                         : 0U;
        }
    }
    CAPTURE(green);
    CHECK(green > 6000); // of 12,000
}

TEST_CASE("A new project plays: the hero walks, and the walls stop it") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "My Quest", error));
    {
        // The game's own runtime, with recorded input: Right held from the first step.
        engine::input::Recording replay;
        replay.record(0, engine::input::Binding::key(engine::input::Key::Right), true);
        game::RuntimeOptions options;
        options.graphics = false;
        options.replay = &replay;
        options.audio = game::AudioMode::Headless;
        options.save_folder = folder / "saves";
        std::vector<serialize::Issue> issues;
        const std::unique_ptr<game::Runtime> runtime = game::Runtime::open(folder, options, issues);
        for (const serialize::Issue& issue : issues) {
            CAPTURE(issue.path);
            CAPTURE(issue.message);
            CHECK(false);
        }
        REQUIRE(runtime != nullptr);
        const auto hero_x = [&] {
            const world::Entity hero = runtime->world().find_named("hero").at(0);
            return runtime->world().get<scene2d::Transform2D>(hero)->x;
        };
        CHECK(hero_x() == 10.0);
        for (int i = 0; i < 60; ++i) {
            runtime->step();
        }
        CHECK(hero_x() == doctest::Approx(14.0)); // a second at 4 tiles a second
        for (int i = 0; i < 240; ++i) {
            runtime->step();
        }
        // The east wall is the column from x = 19: the hero stops against it and stays.
        const double stopped = hero_x();
        CHECK(stopped > 18.0);
        CHECK(stopped < 19.0);
        runtime->step();
        CHECK(hero_x() == stopped);
        CHECK(runtime->vm().errors().empty());
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
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

namespace {

constexpr std::string_view sample_logic = "logic/main.drules";

/// The logic file's lines as the editor shows them.
std::vector<editor::LogicLine> lines_of(const editor::Document& d) {
    REQUIRE(d.logic().size() == 1);
    return editor::describe(d.logic()[0].asset, d);
}

/// The block shown by the line reading `text` (exactly).
engine::logic::BlockId block_of(const editor::Document& d, std::string_view text) {
    for (const editor::LogicLine& line : lines_of(d)) {
        if (line.text == text) {
            return line.block;
        }
    }
    FAIL("no line reads: " << text);
    return {};
}

/// The index of the line reading `text`, or -1.
int line_index(const editor::Document& d, std::string_view text) {
    const std::vector<editor::LogicLine> lines = lines_of(d);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].text == text) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("Logic is edited in place, a step at a time, in the scene's undo") {
    const Sample sample;
    const auto d = sample.open();
    REQUIRE(d->logic_issues().empty());
    const engine::logic::LogicAsset original = d->logic().at(0).asset;
    CHECK(d->check_logic(sample_logic).empty()); // the sample compiles
    std::string error;

    // The text the elder says: one value, its text, changed in place. Typing merges.
    const std::string welcome = "Say “Elder: Welcome, traveller. The road east leads home.” "
                                "(dialogue)";
    const engine::logic::BlockId say = block_of(*d, welcome);
    std::vector<editor::LogicSlot> slots = d->logic_slots(sample_logic, say);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].label == "Text");
    CHECK(slots[0].kind == reflect::ValueKind::Text);
    CHECK(std::get<std::string>(slots[0].value) ==
          "Elder: Welcome, traveller. The road east leads home.");
    REQUIRE(
        d->set_logic_slot(sample_logic, say, 0, reflect::Value{std::string("Elder: Hel")}, error));
    REQUIRE(d->set_logic_slot(sample_logic, say, 0, reflect::Value{std::string("Elder: Hello.")},
                              error));
    CHECK(line_index(*d, "Say “Elder: Hello.” (dialogue)") >= 0);
    CHECK(d->undo_label() == "Change Text in logic/main.drules");
    CHECK(d->dirty());
    // A value of the wrong kind is refused, changing nothing.
    CHECK_FALSE(d->set_logic_slot(sample_logic, say, 0, reflect::Value{std::int64_t{3}}, error));
    CHECK(error == "Text must be text.");
    CHECK_FALSE(d->set_logic_slot(sample_logic, say, 1, reflect::Value{std::string("x")}, error));

    // A property set by a Brain state: its kind and range come from the registry.
    d->end_merge();
    const engine::logic::BlockId dusk = block_of(*d, "Set ambient light of lighting to 0.25");
    slots = d->logic_slots(sample_logic, dusk);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].label == "Ambient light");
    CHECK(slots[0].kind == reflect::ValueKind::Float);
    CHECK_FALSE(d->set_logic_slot(sample_logic, dusk, 0, reflect::Value{3.0}, error));
    CHECK(error == "Ambient light must be a number from 0 to 2.");
    REQUIRE(d->set_logic_slot(sample_logic, dusk, 0, reflect::Value{std::int64_t{1}}, error));
    CHECK(line_index(*d, "Set ambient light of lighting to 1") >= 0);
    // A Brain's value is its name; a Rule's header has none.
    slots = d->logic_slots(sample_logic, block_of(*d, "Brain weather"));
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].label == "Name");
    CHECK(d->logic_slots(sample_logic, block_of(*d, "When the game starts")).empty());

    // The actions a step can do: the game's objects' and the scene's entities'.
    const std::vector<editor::LogicAction> actions = d->logic_actions();
    const auto action = [&](std::string_view object, std::string_view member) {
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const auto& a) {
            return a.object == object && a.member == member;
        });
        REQUIRE(found != actions.end());
        return *found;
    };
    CHECK(action("audio", "play_sound").text == "Play sound {sound}");
    CHECK(action("dialogue", "say").object == "dialogue");
    CHECK(std::is_sorted(actions.begin(), actions.end(),
                         [](const auto& a, const auto& b) { return a.object < b.object; }));

    // Adding a step after the elder's greeting: a chime, its value the default, compiling.
    d->end_merge();
    const engine::logic::BlockId chime =
        d->add_logic_step(sample_logic, say, action("audio", "play_sound"), error);
    REQUIRE(chime.is_valid());
    CHECK(d->undo_label() == "Add Play sound step");
    const int greeting = line_index(*d, "Say “Elder: Hello.” (dialogue)");
    CHECK(line_index(*d, "Play sound “” (audio)") == greeting + 1);
    CHECK(d->is_logic_step(sample_logic, chime));
    CHECK(d->check_logic(sample_logic).empty());
    // Moved up, before the greeting; the first step cannot go higher.
    REQUIRE(d->move_logic_step(sample_logic, chime, -1));
    CHECK(line_index(*d, "Play sound “” (audio)") == greeting);
    CHECK_FALSE(d->move_logic_step(sample_logic, chime, -1));
    CHECK(d->undo_label() == "Move a step up");
    // Deleted.
    REQUIRE(d->remove_logic_step(sample_logic, chime));
    CHECK(line_index(*d, "Play sound “” (audio)") == -1);
    CHECK_FALSE(d->is_logic_step(sample_logic, chime));

    // Under a Rule's header, a step goes first; under a Brain, it cannot go.
    const engine::logic::BlockId gate = block_of(*d, "When [who] walks in (gate)");
    REQUIRE(d->add_logic_step(sample_logic, gate, action("audio", "play_sound"), error).is_valid());
    CHECK(line_index(*d, "Play sound “” (audio)") ==
          line_index(*d, "When [who] walks in (gate)") + 1);
    CHECK_FALSE(d->add_logic_step(sample_logic, block_of(*d, "Brain weather"),
                                  action("audio", "play_sound"), error)
                    .is_valid());
    CHECK(error == "Steps go under a Rule, a Sequence or a Brain state, or after another step.");
    CHECK_FALSE(
        d->add_logic_step(sample_logic, gate, editor::LogicAction{"nobody", "say", ""}, error)
            .is_valid());

    // The compiler checks it against the scene: renaming the elder leaves its Rule
    // naming nobody.
    const Guid elder = guid_of(*d, "elder");
    REQUIRE(d->rename(elder, "sage"));
    CHECK_FALSE(d->check_logic(sample_logic).empty());
    REQUIRE(d->undo());
    CHECK(d->check_logic(sample_logic).empty());

    // Undoing everything brings the logic back exactly, and redo goes forward again.
    while (d->can_undo()) {
        REQUIRE(d->undo());
    }
    CHECK(d->logic().at(0).asset == original);
    CHECK_FALSE(d->dirty());
    REQUIRE(d->redo());
    CHECK(line_index(*d, "Say “Elder: Hello.” (dialogue)") >= 0);

    // Saving writes the logic file; reopening reads the edit back.
    REQUIRE(d->save(error));
    CHECK_FALSE(d->dirty());
    const auto again = sample.open();
    CHECK(line_index(*again, "Say “Elder: Hello.” (dialogue)") >= 0);
}

TEST_CASE("Logic is authored from nothing: files, Rules, conditions, Sequences, Brains") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;

    // A new file, refused when its name is not a logic file's or it exists.
    CHECK_FALSE(d->create_logic_file("logic/Quests.drules", error));
    CHECK_FALSE(d->create_logic_file("logic/quests.txt", error));
    CHECK_FALSE(d->create_logic_file("../quests.drules", error));
    CHECK_FALSE(d->create_logic_file("logic/main.drules", error));
    CHECK(error == "There is already a file logic/main.drules.");
    constexpr std::string_view quests = "logic/quests.drules";
    REQUIRE(d->create_logic_file(quests, error));
    REQUIRE(d->logic().size() == 2);
    CHECK(d->logic()[1].path == quests);
    CHECK(d->logic()[1].asset.name == "quests");
    CHECK(d->check_logic(quests).empty());
    const auto lines = [&] {
        std::vector<std::string> shown;
        for (const editor::LogicLine& line : editor::describe(d->logic()[1].asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const auto block = [&](std::string_view text) {
        for (const editor::LogicLine& line : editor::describe(d->logic()[1].asset, *d)) {
            if (line.text == text) {
                return line.block;
            }
        }
        FAIL("no line reads: " << text);
        return engine::logic::BlockId{};
    };

    // A Rule on the elder's event, picked from the list; a condition from the list.
    const std::vector<editor::LogicEvent> events = d->logic_events();
    REQUIRE_FALSE(events.empty());
    CHECK(events[0].text == "When the game starts");
    const auto interacted =
        std::find_if(events.begin(), events.end(), [](const editor::LogicEvent& e) {
            return e.object == "elder" && e.event == "interacted";
        });
    REQUIRE(interacted != events.end());
    const engine::logic::BlockId rule = d->add_logic_rule(quests, *interacted, error);
    REQUIRE(rule.is_valid());
    CHECK_FALSE(
        d->add_logic_rule(quests, editor::LogicEvent{"elder", "sneezed", ""}, error).is_valid());
    CHECK(error == "elder has no event sneezed.");
    const std::vector<editor::LogicAction> conditions = d->logic_conditions();
    const auto open =
        std::find_if(conditions.begin(), conditions.end(), [](const editor::LogicAction& c) {
            return c.object == "dialogue" && c.member == "is_open";
        });
    REQUIRE(open != conditions.end());
    const engine::logic::BlockId condition = d->add_logic_condition(quests, rule, *open, error);
    REQUIRE(condition.is_valid());
    CHECK_FALSE(
        d->add_logic_condition(quests, rule, editor::LogicAction{"dialogue", "say", ""}, error)
            .is_valid());
    CHECK(error == "dialogue has no condition say."); // an action is not a condition
    // A step under the Rule; it all compiles.
    REQUIRE(d->add_logic_step(quests, rule, editor::LogicAction{"dialogue", "say", ""}, error)
                .is_valid());
    CHECK(d->check_logic(quests).empty());

    std::vector<editor::LogicSlot> slots;
    // A Sequence and a Brain, with free names; a second state; a "Go to" between them.
    const engine::logic::BlockId sequence = d->add_logic_sequence(quests, error);
    REQUIRE(sequence.is_valid());
    REQUIRE(d->add_logic_sequence(quests, error).is_valid());
    const engine::logic::BlockId brain = d->add_logic_brain(quests, error);
    REQUIRE(brain.is_valid());
    const engine::logic::BlockId idle = block("State idle");
    const engine::logic::BlockId second = d->add_logic_state(quests, brain, error);
    REQUIRE(second.is_valid());
    const engine::logic::BlockId go = d->add_logic_transition(quests, idle, error);
    REQUIRE(go.is_valid());
    CHECK_FALSE(d->add_logic_transition(quests, brain, error).is_valid()); // not a state
    CHECK(d->check_logic(quests).empty());
    std::vector<std::string> expected{
        "When [who] interacts with it (elder)",
        "  If a message is open (dialogue)",
        "  Say “” (dialogue)",
        "Sequence sequence_1",
        "Sequence sequence_2",
        "Brain brain_1",
        "  State idle",
        "    Go to state_1 after 1 seconds",
        "  State state_1",
    };
    CHECK(lines() == expected);

    // Names and times are values: renaming a state renames where "Go to" leads.
    REQUIRE(d->set_logic_slot(quests, second, 0, reflect::Value{std::string("alert")}, error));
    CHECK(lines()[7] == "    Go to alert after 1 seconds");
    REQUIRE(d->set_logic_slot(quests, go, 1, reflect::Value{2.5}, error));
    CHECK(lines()[7] == "    Go to alert after 2.5 seconds");
    CHECK_FALSE(d->set_logic_slot(quests, go, 0, reflect::Value{std::string("nowhere")}, error));
    CHECK(error == "There is no state called nowhere in this Brain.");
    CHECK_FALSE(d->set_logic_slot(quests, second, 0, reflect::Value{std::string("idle")}, error));
    CHECK(error == "That name is already used here.");
    CHECK_FALSE(
        d->set_logic_slot(quests, sequence, 0, reflect::Value{std::string("Bad Name")}, error));
    CHECK(error == "Names use lower-case letters, digits and _, and start with a letter.");
    // The built-in steps: wait, log, and run a Sequence, which names it as a value.
    const engine::logic::BlockId run =
        d->add_logic_step(quests, rule, editor::LogicAction{"", "run", ""}, error);
    REQUIRE(run.is_valid());
    CHECK(lines()[2] == "  Run sequence_1 and wait for it");
    slots = d->logic_slots(quests, run);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].label == "Sequence");
    REQUIRE(d->set_logic_slot(quests, run, 0, reflect::Value{std::string("sequence_2")}, error));
    CHECK_FALSE(d->set_logic_slot(quests, run, 0, reflect::Value{std::string("outro")}, error));
    CHECK(error == "There is no Sequence called outro.");
    REQUIRE(d->set_logic_slot(quests, run, 0, reflect::Value{std::string("sequence_1")}, error));
    REQUIRE(d->add_logic_step(quests, run, editor::LogicAction{"", "wait", ""}, error).is_valid());
    CHECK(lines()[3] == "  Wait 1 seconds");
    // Renaming a Sequence renames the steps that run it.
    REQUIRE(d->set_logic_slot(quests, sequence, 0, reflect::Value{std::string("intro")}, error));
    CHECK(lines()[2] == "  Run intro and wait for it");
    CHECK(d->check_logic(quests).empty());

    // Deleting: a condition, a "Go to", a state, the Brain, a Sequence, the Rule.
    for (const engine::logic::BlockId gone : {condition, go, second, brain, sequence, rule}) {
        CAPTURE(gone.value);
        CHECK(d->is_logic_block(quests, gone));
        REQUIRE(d->remove_logic_block(quests, gone));
        CHECK_FALSE(d->is_logic_block(quests, gone));
    }
    CHECK(lines() == std::vector<std::string>{"Sequence sequence_2"});

    // Undoing everything removes the new file too; redoing brings it back.
    while (d->can_undo()) {
        REQUIRE(d->undo());
    }
    CHECK(d->logic().size() == 1);
    CHECK_FALSE(d->dirty());
    REQUIRE(d->redo());
    REQUIRE(d->logic().size() == 2);

    // Saved, the file is written, gets its GUID, and is read back on reopening.
    REQUIRE(d->add_logic_sequence(quests, error).is_valid());
    REQUIRE(d->save(error));
    CHECK(fs::exists(sample.path / "assets" / quests));
    CHECK(d->assets().find(quests) != nullptr);
    const auto again = sample.open();
    REQUIRE(again->logic().size() == 2);
    CHECK(again->logic()[1].asset.sequences.size() == 1);
}

TEST_CASE("Logic branches and loops: If, Wait until and Repeat, with steps inside") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/flow.drules";
    REQUIRE(d->create_logic_file(file, error));
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    REQUIRE(rule.is_valid());
    const auto lines = [&] {
        std::vector<std::string> shown;
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        for (const editor::LogicLine& line : editor::describe(found->asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    // If there is a save in a slot: its slot is a value; a step added with it selected
    // goes inside it.
    const engine::logic::BlockId branch =
        d->add_logic_branch(file, rule, editor::LogicAction{"game", "has_save", ""}, false, error);
    REQUIRE(branch.is_valid());
    CHECK(d->undo_label() == "Add an If step");
    std::vector<editor::LogicSlot> slots = d->logic_slots(file, branch);
    REQUIRE(slots.size() == 1);
    CHECK(slots[0].kind == reflect::ValueKind::Int);
    REQUIRE(d->set_logic_slot(file, branch, 0, reflect::Value{std::int64_t{3}}, error));
    REQUIRE(d->add_logic_step(file, branch, editor::LogicAction{"", "wait", ""}, error).is_valid());
    // Wait until a message is open, after the If; a Repeat after that, with a step in it.
    const engine::logic::BlockId waiting = d->add_logic_branch(
        file, branch, editor::LogicAction{"dialogue", "is_open", ""}, true, error);
    REQUIRE(waiting.is_valid());
    // (Selected, the If takes the new step inside; so the Wait until went in first.)
    const engine::logic::BlockId loop =
        d->add_logic_step(file, rule, editor::LogicAction{"", "repeat", ""}, error);
    REQUIRE(loop.is_valid());
    REQUIRE(d->set_logic_slot(file, loop, 0, reflect::Value{std::int64_t{5}}, error));
    REQUIRE(d->add_logic_step(file, loop, editor::LogicAction{"", "log", ""}, error).is_valid());
    CHECK_FALSE(
        d->add_logic_branch(file, rule, editor::LogicAction{"dialogue", "say", ""}, false, error)
            .is_valid());
    const std::vector<std::string> expected{
        "When the game starts",
        "  Repeat 5 times:",
        "    Log “”",
        "  If slot 3 has a save (game):",
        "    Wait until a message is open (dialogue)",
        "    Wait 1 seconds",
    };
    CHECK(lines() == expected);
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic conditions turned around, matched any of, and Otherwise branches") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/choice.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto lines = [&] {
        std::vector<std::string> shown;
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        for (const editor::LogicLine& line : editor::describe(found->asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text +
                            (line.otherwise ? " [otherwise]" : ""));
        }
        return shown;
    };
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const engine::logic::BlockId open =
        d->add_logic_condition(file, rule, editor::LogicAction{"dialogue", "is_open", ""}, error);
    REQUIRE(open.is_valid());
    // Turned around: "not"; its values still edit through it; and back.
    CHECK(d->is_logic_test(file, open));
    CHECK_FALSE(d->is_logic_test(file, rule));
    REQUIRE(d->toggle_logic_not(file, open));
    CHECK(d->undo_label() == "Turn a condition around");
    CHECK(lines()[1] == "  If not a message is open (dialogue)");
    // A second condition; then any of them ("or").
    const engine::logic::BlockId saved =
        d->add_logic_condition(file, rule, editor::LogicAction{"game", "has_save", ""}, error);
    REQUIRE(saved.is_valid());
    CHECK(d->logic_any(file, rule) == std::optional<bool>(false));
    CHECK_FALSE(d->logic_any(file, open).has_value()); // not a Rule
    REQUIRE(d->set_logic_any(file, rule, true, error));
    CHECK(d->logic_any(file, rule) == std::optional<bool>(true));
    // A third joins the group; each is its own line, and its values edit in place.
    const engine::logic::BlockId later =
        d->add_logic_condition(file, rule, editor::LogicAction{"game", "has_save", ""}, error);
    REQUIRE(later.is_valid());
    REQUIRE(d->set_logic_slot(file, later, 0, reflect::Value{std::int64_t{2}}, error));
    // Steps, and an Otherwise for the Rule; an If with an Otherwise of its own.
    REQUIRE(d->add_logic_step(file, rule, editor::LogicAction{"", "wait", ""}, error).is_valid());
    REQUIRE(
        d->add_logic_step(file, rule, editor::LogicAction{"", "log", ""}, error, true).is_valid());
    const engine::logic::BlockId branch = d->add_logic_branch(
        file, rule, editor::LogicAction{"dialogue", "is_open", ""}, false, error);
    REQUIRE(branch.is_valid());
    REQUIRE(d->toggle_logic_not(file, branch));
    REQUIRE(d->add_logic_step(file, branch, editor::LogicAction{"", "repeat", ""}, error, true)
                .is_valid());
    CHECK_FALSE(
        d->add_logic_step(file, saved, editor::LogicAction{"", "log", ""}, error, true).is_valid());
    CHECK(error == "Only an If or a Rule has an Otherwise.");
    const std::vector<std::string> expected{
        "When the game starts",
        "  If any of these:",
        "    not a message is open (dialogue)",
        "    slot 1 has a save (game)",
        "    slot 2 has a save (game)",
        "  If not a message is open (dialogue):",
        "  Otherwise: [otherwise]",
        "    Repeat 2 times:",
        "  Wait 1 seconds",
        "  Otherwise: [otherwise]",
        "    Log “”",
    };
    CHECK(lines() == expected);
    CHECK(d->check_logic(file).empty());
    // Deleting down to one condition leaves just that one; all of them again: the group
    // splits.
    REQUIRE(d->remove_logic_block(file, saved));
    REQUIRE(d->set_logic_any(file, rule, false, error));
    CHECK(lines()[1] == "  If not a message is open (dialogue)");
    CHECK(lines()[2] == "  If slot 2 has a save (game)");
    REQUIRE(d->remove_logic_block(file, later));
    CHECK_FALSE(d->set_logic_any(file, rule, true, error)); // one condition: nothing to match
    CHECK(error == "A Rule needs two conditions or more to match any of them.");
    REQUIRE(d->undo());
    REQUIRE(d->set_logic_any(file, rule, true, error));
    REQUIRE(d->remove_logic_block(file, later));
    CHECK(lines()[1] == "  If not a message is open (dialogue)"); // a group of one dissolves
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic condition groups go inside groups, flip any/all, and split") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/groups.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto asset = [&]() -> const engine::logic::LogicAsset& {
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        return found->asset;
    };
    const auto lines = [&] {
        std::vector<std::string> shown;
        for (const editor::LogicLine& line : editor::describe(asset(), *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const editor::LogicAction has_save{"game", "has_save", ""};
    const engine::logic::BlockId open =
        d->add_logic_condition(file, rule, editor::LogicAction{"dialogue", "is_open", ""}, error);
    const engine::logic::BlockId one = d->add_logic_condition(file, rule, has_save, error);
    const engine::logic::BlockId two = d->add_logic_condition(file, rule, has_save, error);
    REQUIRE(two.is_valid());
    REQUIRE(d->set_logic_slot(file, two, 0, reflect::Value{std::int64_t{2}}, error));

    // The last condition has nothing after it to be grouped with.
    CHECK_FALSE(d->group_logic_conditions(file, two, error).is_valid());
    CHECK(error == "A condition is grouped with the one after it: there is none.");
    // Among conditions that must all hold, a group is "any of these".
    const engine::logic::BlockId outer = d->group_logic_conditions(file, open, error);
    REQUIRE(outer.is_valid());
    CHECK(d->undo_label() == "Group two conditions");
    CHECK(d->logic_any(file, outer) == std::optional<bool>(true));
    CHECK(d->logic_any(file, rule) == std::optional<bool>(false)); // two conditions on it
    CHECK_FALSE(d->is_logic_test(file, outer)); // not one test: Not leaves it alone
    CHECK_FALSE(d->toggle_logic_not(file, outer));
    // Its two conditions are all of it: grouping them again would say nothing new.
    CHECK_FALSE(d->group_logic_conditions(file, open, error).is_valid());
    CHECK(error == "These are all of the group's conditions: match them the other way instead.");
    // A condition added with the group (or one of its conditions) selected goes into it.
    const engine::logic::BlockId three = d->add_logic_condition(file, one, has_save, error);
    REQUIRE(three.is_valid());
    REQUIRE(d->set_logic_slot(file, three, 0, reflect::Value{std::int64_t{3}}, error));
    // Inside "any of these", a group is "all of these".
    const engine::logic::BlockId inner = d->group_logic_conditions(file, one, error);
    REQUIRE(inner.is_valid());
    CHECK(d->logic_any(file, inner) == std::optional<bool>(false));
    const std::vector<std::string> nested{
        "When the game starts",
        "  If any of these:",
        "    a message is open (dialogue)",
        "    All of these:",
        "      slot 1 has a save (game)",
        "      slot 3 has a save (game)",
        "  If slot 2 has a save (game)",
    };
    CHECK(lines() == nested);
    {
        const engine::logic::Rule& r = asset().rules.at(0);
        REQUIRE(r.conditions.size() == 2);
        CHECK(r.conditions[0].kind == engine::logic::ExprKind::Any);
        REQUIRE(r.conditions[0].operands.size() == 2);
        CHECK(r.conditions[0].operands[1].kind == engine::logic::ExprKind::All);
        CHECK(r.conditions[0].operands[1].operands.size() == 2);
    }
    CHECK(d->check_logic(file).empty());
    // Deep inside, a condition is still turned around and its values still edit.
    REQUIRE(d->toggle_logic_not(file, three));
    REQUIRE(d->set_logic_slot(file, three, 0, reflect::Value{std::int64_t{4}}, error));
    CHECK(lines()[5] == "      not slot 4 has a save (game)");
    // The inner group matched the other way; and back.
    REQUIRE(d->set_logic_any(file, inner, true, error));
    CHECK(lines()[3] == "    Any of these:");
    CHECK(d->undo_label() == "Match any condition");
    REQUIRE(d->set_logic_any(file, inner, false, error));
    CHECK(lines() == std::vector<std::string>{
                         "When the game starts",
                         "  If any of these:",
                         "    a message is open (dialogue)",
                         "    All of these:",
                         "      slot 1 has a save (game)",
                         "      not slot 4 has a save (game)",
                         "  If slot 2 has a save (game)",
                     });
    // Deleting one of the inner two leaves just the other, in the group's place.
    REQUIRE(d->remove_logic_block(file, three));
    CHECK(lines()[3] == "    slot 1 has a save (game)");
    CHECK(d->check_logic(file).empty());
    REQUIRE(d->undo());
    // Split: the inner group's conditions take its place; then the outer's.
    CHECK_FALSE(d->ungroup_logic_conditions(file, one, error));
    CHECK(error == "Only a group of conditions can be split.");
    REQUIRE(d->ungroup_logic_conditions(file, inner, error));
    CHECK(d->undo_label() == "Split a group of conditions");
    REQUIRE(d->ungroup_logic_conditions(file, outer, error));
    CHECK(lines() == std::vector<std::string>{
                         "When the game starts",
                         "  If a message is open (dialogue)",
                         "  If slot 1 has a save (game)",
                         "  If not slot 4 has a save (game)",
                         "  If slot 2 has a save (game)",
                     });
    CHECK(d->check_logic(file).empty());
    // Undo brings both groups back, as they were.
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(lines()[3] == "    All of these:");
    // A Rule with just one group matches any of its conditions when the group does; the
    // Rule's own switch flips that group.
    REQUIRE(d->remove_logic_block(file, two));
    CHECK(d->logic_any(file, rule) == std::optional<bool>(true));
    REQUIRE(d->set_logic_any(file, outer, false, error));
    CHECK(lines()[1] == "  If all of these:");
    CHECK(d->logic_any(file, rule) == std::optional<bool>(false));
    REQUIRE(d->set_logic_any(file, rule, true, error));
    CHECK(lines()[1] == "  If any of these:");
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic conditions move among a Rule's conditions and its groups") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/moves.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto lines = [&] {
        std::vector<std::string> shown;
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        for (const editor::LogicLine& line : editor::describe(found->asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const editor::LogicAction has_save{"game", "has_save", ""};
    const engine::logic::BlockId open =
        d->add_logic_condition(file, rule, editor::LogicAction{"dialogue", "is_open", ""}, error);
    const engine::logic::BlockId one = d->add_logic_condition(file, rule, has_save, error);
    const engine::logic::BlockId two = d->add_logic_condition(file, rule, has_save, error);
    REQUIRE(d->set_logic_slot(file, two, 0, reflect::Value{std::int64_t{2}}, error));
    const engine::logic::BlockId three = d->add_logic_condition(file, rule, has_save, error);
    REQUIRE(d->set_logic_slot(file, three, 0, reflect::Value{std::int64_t{3}}, error));
    const engine::logic::BlockId wait =
        d->add_logic_step(file, rule, editor::LogicAction{"", "wait", ""}, error);
    REQUIRE(wait.is_valid());
    const engine::logic::BlockId group = d->group_logic_conditions(file, open, error);
    REQUIRE(group.is_valid());
    // [any(open, 1), 2, 3]. Refusals first: not a condition; not onto a step; not into itself.
    CHECK_FALSE(d->move_logic_condition_to(file, wait, two, error));
    CHECK(error == "Only conditions can be moved among conditions.");
    CHECK_FALSE(d->move_logic_condition_to(file, two, wait, error));
    CHECK(error == "Conditions go on a Rule, in a group, or after another condition.");
    CHECK_FALSE(d->move_logic_condition_to(file, group, one, error));
    CHECK(error == "A group cannot go inside itself.");
    // Onto a group: first in it. Onto a condition: after it, in whichever list it is in.
    REQUIRE(d->move_logic_condition_to(file, three, group, error));
    CHECK(d->undo_label() == "Move a condition");
    REQUIRE(d->move_logic_condition_to(file, two, one, error));
    CHECK(lines() == std::vector<std::string>{
                         "When the game starts",
                         "  If any of these:",
                         "    slot 3 has a save (game)",
                         "    a message is open (dialogue)",
                         "    slot 1 has a save (game)",
                         "    slot 2 has a save (game)",
                         "  Wait 1 seconds",
                     });
    CHECK(d->check_logic(file).empty());
    // Onto its own group: first in it. Onto the Rule (which matches any): first of them.
    REQUIRE(d->move_logic_condition_to(file, two, group, error));
    CHECK(lines()[2] == "    slot 2 has a save (game)");
    REQUIRE(d->move_logic_condition_to(file, one, rule, error));
    CHECK(lines()[2] == "    slot 1 has a save (game)");
    // A whole group moves too; a group left with one condition is just that condition.
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    REQUIRE(d->undo()); // [any(open, 1), 2, 3] again
    REQUIRE(d->move_logic_condition_to(file, group, three, error));
    REQUIRE(d->move_logic_condition_to(file, open, two, error));
    CHECK(lines() == std::vector<std::string>{
                         "When the game starts",
                         "  If slot 2 has a save (game)",
                         "  If a message is open (dialogue)",
                         "  If slot 3 has a save (game)",
                         "  If slot 1 has a save (game)",
                         "  Wait 1 seconds",
                     });
    CHECK_FALSE(d->is_logic_block(file, group)); // dissolved
    CHECK(d->check_logic(file).empty());
    REQUIRE(d->undo());
    CHECK(lines()[3] == "  If any of these:"); // the group back, after slot 3
}

TEST_CASE("Logic variables: made, set, compared, renamed where used, and deleted") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/count.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto asset = [&]() -> const engine::logic::LogicAsset& {
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        return found->asset;
    };
    const auto lines = [&] {
        std::vector<std::string> shown;
        for (const editor::LogicLine& line : editor::describe(asset(), *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    // Setting a variable needs one first.
    CHECK_FALSE(d->add_logic_step(file, rule, editor::LogicAction{"", "set_variable", ""}, error)
                    .is_valid());
    CHECK(error == "There is no variable to set yet: add one first.");
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Int, error));
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Text, error));
    CHECK_FALSE(d->add_logic_variable(file, reflect::ValueKind::Choice, error));
    REQUIRE(asset().variables.size() == 2);
    CHECK(asset().variables[0].id == "variable_1");
    CHECK(asset().variables[1].id == "variable_2");
    // Its start: of its kind.
    REQUIRE(
        d->set_logic_variable_start(file, "variable_1", reflect::Value{std::int64_t{3}}, error));
    CHECK(asset().variables[0].initial == std::optional<reflect::Value>(std::int64_t{3}));
    CHECK_FALSE(d->set_logic_variable_start(file, "variable_1",
                                            reflect::Value{std::string("three")}, error));
    CHECK(error == "Its start must be a whole number.");

    // A Set variable step, then another variable for it (its value starts again).
    const engine::logic::BlockId set =
        d->add_logic_step(file, rule, editor::LogicAction{"", "set_variable", ""}, error);
    REQUIRE(set.is_valid());
    REQUIRE(d->set_logic_slot(file, set, 0, reflect::Value{std::int64_t{5}}, error));
    CHECK(lines()[1] == "  Set variable_1 to 5");
    REQUIRE(d->set_logic_slot(file, set, 1, reflect::Value{std::string("variable_2")}, error));
    CHECK(lines()[1] == "  Set variable_2 to “”");
    CHECK_FALSE(d->set_logic_slot(file, set, 1, reflect::Value{std::string("nope")}, error));
    CHECK(error == "There is no variable called nope in this file.");
    REQUIRE(d->set_logic_slot(file, set, 1, reflect::Value{std::string("variable_1")}, error));
    // Compared: as the Rule's condition, and as an If after the step.
    const engine::logic::BlockId condition =
        d->add_logic_comparison(file, rule, "variable_1", error);
    REQUIRE(condition.is_valid());
    REQUIRE(d->set_logic_slot(file, condition, 0, reflect::Value{std::string(">=")}, error));
    REQUIRE(d->set_logic_slot(file, condition, 1, reflect::Value{std::int64_t{2}}, error));
    CHECK_FALSE(d->set_logic_slot(file, condition, 0, reflect::Value{std::string("~")}, error));
    const engine::logic::BlockId branch = d->add_logic_comparison(file, set, "variable_1", error);
    REQUIRE(branch.is_valid());
    REQUIRE(d->set_logic_slot(file, branch, 0, reflect::Value{std::string("≠")}, error));
    std::vector<std::string> expected{"When the game starts", "  If variable_1 ≥ 2",
                                      "  Set variable_1 to 0", "  If variable_1 ≠ 0:"};
    CHECK(lines() == expected);
    CHECK(d->check_logic(file).empty());

    // Renamed: everything that reads or sets it follows. Deleting it is refused while
    // used; the unused one goes.
    REQUIRE(d->rename_logic_variable(file, "variable_1", "talks", error));
    expected = {"When the game starts", "  If talks ≥ 2", "  Set talks to 0", "  If talks ≠ 0:"};
    CHECK(lines() == expected);
    CHECK_FALSE(d->rename_logic_variable(file, "talks", "variable_2", error));
    CHECK(error == "That name is already used here.");
    CHECK_FALSE(d->remove_logic_variable(file, "talks", error));
    CHECK(error == "talks is still used: delete the blocks that use it first.");
    REQUIRE(d->remove_logic_variable(file, "variable_2", error));
    CHECK(asset().variables.size() == 1);
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic values from variables, event values and properties, with sums") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/sums.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto lines = [&] {
        std::vector<std::string> shown;
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        for (const editor::LogicLine& line : editor::describe(found->asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const auto source = [&](engine::logic::BlockId block, std::size_t argument,
                            editor::LogicSource::Kind kind, std::string_view member) {
        for (const editor::LogicSource& s : d->logic_sources(file, block, argument)) {
            if (s.kind == kind && s.member == member) {
                return s;
            }
        }
        FAIL("not offered: " << member);
        return editor::LogicSource{};
    };
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Int, error));
    REQUIRE(d->rename_logic_variable(file, "variable_1", "talks", error));
    // When an answer is picked: talks takes the answer, then talks + 1.
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"dialogue", "answered", ""}, error);
    REQUIRE(rule.is_valid());
    const engine::logic::BlockId set =
        d->add_logic_step(file, rule, editor::LogicAction{"", "set_variable", ""}, error);
    using Kind = editor::LogicSource::Kind;
    REQUIRE(d->set_logic_source(file, set, 0, source(set, 0, Kind::EventValue, "answer"), error));
    std::vector<editor::LogicSlot> slots = d->logic_slots(file, set);
    const auto value_slot = [&] {
        const auto it = std::find_if(slots.begin(), slots.end(),
                                     [](const editor::LogicSlot& s) { return s.argument == 0; });
        REQUIRE(it != slots.end());
        return *it;
    };
    CHECK_FALSE(value_slot().typed);
    CHECK(value_slot().from == "[answer]");
    CHECK(lines()[1] == "  Set talks to [answer]");
    REQUIRE(d->set_logic_source(file, set, 0, source(set, 0, Kind::Variable, "talks"), error));
    REQUIRE(d->set_logic_arithmetic(file, set, 0, engine::logic::ArithmeticOp::Add, error));
    slots = d->logic_slots(file, set);
    CHECK(value_slot().typed);
    CHECK(value_slot().from == "talks +");
    CHECK(value_slot().value == reflect::Value{std::int64_t{1}});
    REQUIRE(d->set_logic_slot(file, set, 0, reflect::Value{std::int64_t{2}}, error));
    CHECK(lines()[1] == "  Set talks to talks + 2");
    REQUIRE(d->set_logic_arithmetic(file, set, 0, engine::logic::ArithmeticOp::Multiply, error));
    CHECK(lines()[1] == "  Set talks to talks × 2");
    // Times another value: the answer; the slot then reads it all, not a written number.
    REQUIRE(d->set_logic_sum_with(file, set, 0, source(set, 0, Kind::EventValue, "answer"), error));
    CHECK(lines()[1] == "  Set talks to talks × [answer]");
    slots = d->logic_slots(file, set);
    CHECK_FALSE(value_slot().typed);
    CHECK(value_slot().from == "talks × [answer]");
    CHECK(value_slot().sum);
    // Longer sums: one more part, a written number to begin with, edited in place; the
    // parts work in order, and the line says so with brackets where it must.
    using Op = engine::logic::ArithmeticOp;
    REQUIRE(d->extend_logic_sum(file, set, 0, Op::Add, error));
    slots = d->logic_slots(file, set);
    CHECK(value_slot().typed);
    CHECK(value_slot().from == "talks × [answer] +");
    CHECK(value_slot().value == reflect::Value{std::int64_t{1}});
    REQUIRE(d->set_logic_slot(file, set, 0, reflect::Value{std::int64_t{3}}, error));
    CHECK(lines()[1] == "  Set talks to talks × [answer] + 3");
    REQUIRE(d->extend_logic_sum(file, set, 0, Op::Multiply, error));
    slots = d->logic_slots(file, set);
    CHECK(value_slot().from == "(talks × [answer] + 3) ×");
    REQUIRE(d->set_logic_sum_with(file, set, 0, source(set, 0, Kind::Variable, "talks"), error));
    CHECK(lines()[1] == "  Set talks to (talks × [answer] + 3) × talks");
    CHECK(d->check_logic(file).empty());
    {
        // In the asset: ((talks × answer) + 3) × talks, each part on the result so far.
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        const engine::logic::Stmt& step = found->asset.rules.at(0).actions.at(0);
        const engine::logic::Expr& value = step.arguments.at(0);
        REQUIRE(value.kind == engine::logic::ExprKind::Arithmetic);
        CHECK(value.arithmetic == Op::Multiply);
        CHECK(value.operands.at(1).kind == engine::logic::ExprKind::Variable);
        const engine::logic::Expr& so_far = value.operands.at(0);
        CHECK(so_far.arithmetic == Op::Add);
        CHECK(so_far.operands.at(0).arithmetic == Op::Multiply);
        CHECK(so_far.operands.at(1).literal == reflect::Value{std::int64_t{3}});
    }
    // Without the last part, twice: back to talks × [answer].
    REQUIRE(d->set_logic_arithmetic(file, set, 0, std::nullopt, error));
    CHECK(lines()[1] == "  Set talks to talks × [answer] + 3");
    REQUIRE(d->set_logic_arithmetic(file, set, 0, std::nullopt, error));
    CHECK(lines()[1] == "  Set talks to talks × [answer]");
    REQUIRE(
        d->set_logic_sum_with(file, set, 0, editor::LogicSource{Kind::Written, "", "", ""}, error));
    CHECK(lines()[1] == "  Set talks to talks × 1");
    CHECK(d->check_logic(file).empty());
    REQUIRE(d->set_logic_arithmetic(file, set, 0, std::nullopt, error));
    CHECK(lines()[1] == "  Set talks to talks");
    // Writing a value makes it written again.
    REQUIRE(d->set_logic_slot(file, set, 0, reflect::Value{std::int64_t{7}}, error));
    CHECK(lines()[1] == "  Set talks to 7");
    CHECK_FALSE(d->set_logic_arithmetic(file, set, 0, engine::logic::ArithmeticOp::Add, error));
    CHECK_FALSE(d->extend_logic_sum(file, set, 0, Op::Add, error));
    CHECK(error == "Add a sum to the value first.");

    // A number from a property: wait as many seconds as the hero's speed.
    const engine::logic::BlockId wait =
        d->add_logic_step(file, set, editor::LogicAction{"", "wait", ""}, error);
    REQUIRE(d->set_logic_source(file, wait, 0, source(wait, 0, Kind::Property, "speed"), error));
    CHECK(lines()[2] == "  Wait speed of hero seconds");
    // Only sources of the slot's kind are offered: no text property for a number.
    for (const editor::LogicSource& s : d->logic_sources(file, wait, 0)) {
        CHECK(s.member != "name");
    }
    CHECK_FALSE(d->set_logic_source(file, wait, 0,
                                    editor::LogicSource{Kind::Variable, "", "nope", ""}, error));
    CHECK(error == "That cannot give this value.");
    // Compared: the event's answer, and the hero's speed; their values are of their kind.
    const auto comparable = [&](engine::logic::BlockId block, Kind kind, std::string_view member) {
        for (const editor::LogicSource& c : d->logic_comparables(file, block)) {
            if (c.kind == kind && c.member == member) {
                return c;
            }
        }
        FAIL("not comparable: " << member);
        return editor::LogicSource{};
    };
    const engine::logic::BlockId answered =
        d->add_logic_comparison(file, rule, comparable(rule, Kind::EventValue, "answer"), error);
    REQUIRE(answered.is_valid());
    REQUIRE(d->set_logic_slot(file, answered, 1, reflect::Value{std::int64_t{1}}, error));
    const engine::logic::BlockId fast =
        d->add_logic_comparison(file, wait, comparable(wait, Kind::Property, "speed"), error);
    REQUIRE(fast.is_valid());
    slots = d->logic_slots(file, fast);
    REQUIRE(slots.size() == 2);
    CHECK(slots[1].kind == reflect::ValueKind::Float);
    REQUIRE(d->set_logic_slot(file, fast, 0, reflect::Value{std::string(">")}, error));
    REQUIRE(d->set_logic_slot(file, fast, 1, reflect::Value{4.0}, error));
    CHECK(lines()[1] == "  If [answer] = 1");
    CHECK(lines()[4] == "  If speed of hero > 4:");
    CHECK_FALSE(d->add_logic_comparison(
                     file, rule, editor::LogicSource{Kind::Property, "hero", "nope", ""}, error)
                    .is_valid());
    CHECK(d->check_logic(file).empty());
    // Outside a Rule with values, there are no event values to take.
    const engine::logic::BlockId start =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const engine::logic::BlockId log =
        d->add_logic_step(file, start, editor::LogicAction{"", "wait", ""}, error);
    for (const editor::LogicSource& s : d->logic_sources(file, log, 0)) {
        CHECK(s.kind != Kind::EventValue);
    }
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic steps move anywhere steps go, not into themselves") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/order.drules";
    REQUIRE(d->create_logic_file(file, error));
    const auto lines = [&] {
        std::vector<std::string> shown;
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        for (const editor::LogicLine& line : editor::describe(found->asset, *d)) {
            shown.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
        }
        return shown;
    };
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const engine::logic::BlockId loop =
        d->add_logic_step(file, rule, editor::LogicAction{"", "repeat", ""}, error);
    const engine::logic::BlockId wait =
        d->add_logic_step(file, loop, editor::LogicAction{"", "wait", ""}, error); // inside
    const engine::logic::BlockId log =
        d->add_logic_step(file, rule, editor::LogicAction{"", "log", ""}, error); // first
    REQUIRE(log.is_valid());
    std::vector<std::string> expected{"When the game starts", "  Log “”",
                                      "  Repeat 2 times:", "    Wait 1 seconds"};
    CHECK(lines() == expected);
    // Into the Repeat (dropped on it: first inside), then out after it.
    REQUIRE(d->move_logic_step_to(file, log, loop, error));
    CHECK(d->undo_label() == "Move a step");
    expected = {"When the game starts", "  Repeat 2 times:", "    Log “”", "    Wait 1 seconds"};
    CHECK(lines() == expected);
    REQUIRE(d->move_logic_step_to(file, wait, rule, error));
    expected = {"When the game starts", "  Wait 1 seconds", "  Repeat 2 times:", "    Log “”"};
    CHECK(lines() == expected);
    // Not into itself, not a Rule, not onto nothing.
    CHECK_FALSE(d->move_logic_step_to(file, loop, log, error));
    CHECK(error == "A step cannot go inside itself.");
    CHECK_FALSE(d->move_logic_step_to(file, rule, wait, error));
    CHECK(error == "Only steps can be moved.");
    CHECK_FALSE(d->move_logic_step_to(file, wait, engine::logic::BlockId{999}, error));
    CHECK(d->check_logic(file).empty());
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    expected = {"When the game starts", "  Log “”", "  Repeat 2 times:", "    Wait 1 seconds"};
    CHECK(lines() == expected);
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
