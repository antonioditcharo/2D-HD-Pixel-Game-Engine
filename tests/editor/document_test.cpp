// The editor's model, on a copy of the sample: opening, editing through reflection with
// undo and redo, saving, picking, and the preview the scene view shows.

#include "engine/assets/autotile.h"
#include "engine/audio/audio.h"
#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/game/hud.h"
#include "engine/game/localization.h"
#include "engine/game/logic_world.h"
#include "engine/game/runtime.h"
#include "engine/game/tables.h"
#include "engine/input/input.h"
#include "engine/logic/ir.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "editor/core/doctor.h"
#include "editor/core/document.h"
#include "editor/core/logic_text.h"
#include "editor/core/new_project.h"
#include "editor/core/preview.h"
#include "support/aseprite.h"
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

TEST_CASE("Several entities move and are deleted together, each as one step") {
    const Sample sample;
    const auto d = sample.open();
    const Guid hero = guid_of(*d, "hero");
    const Guid elder = guid_of(*d, "elder");
    const auto at = [&](Guid guid) {
        return *d->world().get<scene2d::Transform2D>(d->world().find(guid));
    };
    // A group dragged: each to its own point; the drag's moves merge into one step.
    for (int i = 1; i <= 3; ++i) {
        REQUIRE(d->move_entities({{hero, {10.0 + i, 9.0}}, {elder, {6.0 + i, 5.0}}}));
    }
    CHECK(at(hero).x == 13.0);
    CHECK(at(elder).x == 9.0);
    CHECK(d->undo_label() == "Move 2 entities");
    d->end_merge();
    // Another drag of the same two is its own step.
    REQUIRE(d->move_entities({{hero, {14.0, 9.0}}, {elder, {10.0, 5.0}}}));
    REQUIRE(d->undo());
    CHECK(at(hero).x == 13.0);
    REQUIRE(d->undo());
    CHECK(at(hero).x == 10.0);
    CHECK(at(elder).x == 6.0);
    CHECK_FALSE(d->can_undo());
    // What has no Position, or no number to go to, is passed over; nothing at all, false.
    const Guid empty = d->create_entity("empty", 0.0, 0.0);
    const reflect::TypeInfo* position = d->registry().find_type<scene2d::Transform2D>();
    REQUIRE(position != nullptr);
    REQUIRE(d->remove_component(empty, *position));
    CHECK_FALSE(d->move_entities({{empty, {1.0, 1.0}}, {hero, {std::nan(""), 1.0}}}));
    CHECK_FALSE(d->move_entities({}));
    // Deleted together: one step, and back together.
    const std::size_t before = d->entities().size();
    REQUIRE(d->delete_entities({hero, elder, Guid::generate()}));
    CHECK(d->undo_label() == "Delete 2 entities");
    CHECK(d->entities().size() == before - 2);
    CHECK_FALSE(d->delete_entities({hero, elder}));
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    CHECK(at(hero).x == 10.0);
    REQUIRE(d->delete_entities({elder})); // one alone reads as it always has
    CHECK(d->undo_label() == "Delete elder");
}

TEST_CASE("A property of several entities changes for all of them, as one step") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    const Guid hero = guid_of(*d, "hero");
    const Guid elder = guid_of(*d, "elder");
    const Guid lantern = guid_of(*d, "lantern");
    const Guid lamp = guid_of(*d, "lamp");
    const reflect::TypeInfo* transform = d->registry().find_type<scene2d::Transform2D>();
    const reflect::TypeInfo* light = d->registry().find_type<scene2d::Light>();
    const reflect::TypeInfo* sprite = d->registry().find_type<scene2d::Sprite>();
    REQUIRE(transform != nullptr);
    REQUIRE(light != nullptr);
    REQUIRE(sprite != nullptr);
    const auto x_of = [&](Guid g) {
        return d->world().get<scene2d::Transform2D>(d->world().find(g))->x;
    };
    // Typed in: both change, and the typing is one step.
    REQUIRE(d->set_property_many({hero, elder}, *transform, property(*transform, "x"),
                                 reflect::Value{3.0}, error));
    REQUIRE(d->set_property_many({hero, elder}, *transform, property(*transform, "x"),
                                 reflect::Value{4.0}, error));
    CHECK(x_of(hero) == 4.0);
    CHECK(x_of(elder) == 4.0);
    CHECK(d->undo_label() == "Change Across of 2 entities");
    REQUIRE(d->undo());
    CHECK(x_of(hero) == 10.0);
    CHECK(x_of(elder) == 6.0);
    CHECK_FALSE(d->can_undo());
    // All or none: one without the component, or a value one refuses, changes nothing.
    CHECK_FALSE(d->set_property_many({hero, lantern}, *sprite, property(*sprite, "visible"),
                                     reflect::Value{false}, error));
    CHECK(error == "lantern has no Sprite.");
    CHECK(d->world().get<scene2d::Sprite>(d->world().find(hero))->visible);
    const double radius = d->world().get<scene2d::Light>(d->world().find(lantern))->radius;
    CHECK_FALSE(d->set_property_many({lantern, lamp}, *light, property(*light, "radius"),
                                     reflect::Value{40.0}, error));
    CHECK(error == "Radius is out of its range.");
    CHECK(d->world().get<scene2d::Light>(d->world().find(lantern))->radius == radius);
    CHECK_FALSE(d->can_undo());
    CHECK_FALSE(d->dirty());
    CHECK_FALSE(
        d->set_property_many({}, *light, property(*light, "radius"), reflect::Value{1.0}, error));
    // Both lights at once, in range.
    REQUIRE(d->set_property_many({lantern, lamp}, *light, property(*light, "radius"),
                                 reflect::Value{6.0}, error));
    CHECK(d->world().get<scene2d::Light>(d->world().find(lamp))->radius == 6.0);
    // One entity reads as it always has.
    REQUIRE(d->set_property_many({hero}, *transform, property(*transform, "x"), reflect::Value{2.0},
                                 error));
    CHECK(d->undo_label() == "Change Across of hero");

    // Components added to and removed from several: those lacking or having them.
    REQUIRE(d->add_component_many({lantern, lamp, hero}, *sprite));
    CHECK(d->undo_label() == "Add Sprite to 2 entities");
    CHECK(d->world().get<scene2d::Sprite>(d->world().find(lamp)) != nullptr);
    CHECK_FALSE(d->add_component_many({lantern, lamp}, *sprite)); // all have it now
    REQUIRE(d->remove_component_many({lantern, lamp, hero}, *sprite));
    CHECK(d->undo_label() == "Remove Sprite from 3 entities");
    CHECK(d->world().get<scene2d::Sprite>(d->world().find(hero)) == nullptr);
    CHECK_FALSE(d->remove_component_many({lantern, lamp}, *sprite)); // none has it
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(d->world().get<scene2d::Sprite>(d->world().find(hero)) != nullptr);
    CHECK(d->world().get<scene2d::Sprite>(d->world().find(lamp)) == nullptr);

    // Named together, numbered, past names others have: one step, undone together.
    const Guid other = d->create_entity("tree_1", 0.0, 0.0);
    REQUIRE(d->rename_many({hero, elder}, "tree"));
    CHECK(d->world().name(d->world().find(hero)) == "tree_2");
    CHECK(d->world().name(d->world().find(elder)) == "tree_3");
    CHECK(d->world().name(d->world().find(other)) == "tree_1");
    CHECK(d->undo_label() == "Rename 2 entities");
    CHECK_FALSE(d->rename_many({hero, elder}, ""));
    REQUIRE(d->undo());
    CHECK(d->world().name(d->world().find(hero)) == "hero");
    CHECK(d->world().name(d->world().find(elder)) == "elder");
    // Their own names do not count as taken.
    REQUIRE(d->rename_many({hero, elder}, "hero"));
    CHECK(d->world().name(d->world().find(hero)) == "hero_1");
    CHECK(d->world().name(d->world().find(elder)) == "hero_2");
}

TEST_CASE("Placing a picture from the assets makes a sprite there, as one step") {
    const Sample sample;
    const auto d = sample.open();
    const std::size_t before = d->entities().size();
    std::string error;
    // To whole pixels: 16 per tile, so 3.03 (48.48 pixels) becomes 3 and 4.94 (79.04
    // pixels) becomes 79 / 16.
    const Guid placed = d->place_asset("art/hero.png", 3.03, 4.94, error);
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

    // Only pictures and sounds that are assets, at a real point.
    CHECK_FALSE(d->place_asset("logic/main.drules", 1.0, 1.0, error).is_valid());
    CHECK(error == "Only pictures (PNG) and sounds (WAV, Ogg, FLAC, MP3) can be placed in the "
                   "scene; logic/main.drules is neither.");
    CHECK_FALSE(d->place_asset("art/nothing.png", 1.0, 1.0, error).is_valid());
    CHECK(error == "There is no asset art/nothing.png in assets/.");
    CHECK_FALSE(d->place_asset("art/hero.png", std::nan(""), 1.0, error).is_valid());
    CHECK(d->entities().size() == before);
    CHECK(d->redo_label() == "Place hero"); // refusals record nothing, so redo is kept

    // A sound becomes a Sound Source repeating it there, with the component's defaults.
    const Guid hum = d->place_asset("sfx/chime.wav", 12.5, 3.0, error);
    REQUIRE(hum.is_valid());
    const world::Entity source = d->world().find(hum);
    CHECK(d->world().name(source) == "chime");
    CHECK(d->world().get<scene2d::Transform2D>(source)->x == 12.5);
    CHECK(d->world().get<scene2d::Sprite>(source) == nullptr);
    const auto* sound = d->world().get<game::SoundSource>(source);
    REQUIRE(sound != nullptr);
    CHECK(sound->sound == d->assets().find("sfx/chime.wav")->guid.to_string());
    CHECK(sound->volume == 1.0);
    CHECK(sound->range == 8.0);
    CHECK(sound->playing);
    CHECK(d->undo_label() == "Place chime");
    // It saves and comes back as it was placed.
    REQUIRE(d->save(error));
    std::vector<serialize::Issue> issues;
    const auto again = editor::Document::open(sample.path, issues);
    REQUIRE(again != nullptr);
    CHECK(issues.empty());
    const auto* reread = again->world().get<game::SoundSource>(again->world().find(hum));
    REQUIRE(reread != nullptr);
    CHECK(reread->sound == sound->sound);
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

TEST_CASE("A project's scenes: made, opened, started in, and known to logic by name") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    CHECK(d->scene_path() == "maps/village.dscene");
    CHECK(d->scenes() == std::vector<std::string>{"maps/road.dscene", "maps/village.dscene"});
    const std::size_t village = d->entities().size();
    constexpr std::string_view logic = "logic/main.drules";
    const auto lines = [&] {
        std::vector<std::string> shown;
        for (const editor::LogicLine& line : editor::describe(d->logic().at(0).asset, *d)) {
            shown.push_back(line.text);
        }
        return shown;
    };
    const auto has_line = [&](std::string_view text) {
        const std::vector<std::string> shown = lines();
        return std::find(shown.begin(), shown.end(), text) != shown.end();
    };

    // A new scene: named as one, not over a file, and not while changes are unsaved.
    CHECK_FALSE(d->create_scene("maps/cave", error));
    CHECK(error == "A scene is named like maps/cave.dscene, in assets/.");
    CHECK_FALSE(d->create_scene("../cave.dscene", error));
    CHECK_FALSE(d->create_scene("maps/village.dscene", error));
    CHECK(error == "There is already a file maps/village.dscene.");
    REQUIRE(d->rename(guid_of(*d, "elder"), "sage"));
    CHECK_FALSE(d->create_scene("maps/cave.dscene", error));
    CHECK(error == "Save your changes before making another scene.");
    CHECK_FALSE(d->open_scene("maps/village.dscene.meta", error));
    REQUIRE(d->undo());
    REQUIRE(d->create_scene("maps/cave.dscene", error));
    CHECK(d->scene_path() == "maps/cave.dscene");
    CHECK(d->entities().empty());
    CHECK_FALSE(d->dirty());
    CHECK_FALSE(d->can_undo()); // undo starts again in the scene opened
    CHECK(fs::is_regular_file(sample.path / "assets/maps/cave.dscene"));
    CHECK(fs::is_regular_file(sample.path / "assets/maps/cave.dscene.meta"));
    CHECK(d->scenes() ==
          std::vector<std::string>{"maps/cave.dscene", "maps/road.dscene", "maps/village.dscene"});

    // A bat lives in the cave, and a Rule is about it. The village's elder is known here
    // by name, from its scene's file.
    const Guid bat = d->create_entity("bat", 3.0, 4.0);
    const reflect::TypeInfo* interactable = d->registry().find_type<game::Interactable>();
    REQUIRE(interactable != nullptr);
    REQUIRE(d->add_component(bat, *interactable));
    REQUIRE(
        d->add_logic_rule(logic, editor::LogicEvent{"bat", "interacted", ""}, error).is_valid());
    CHECK(d->check_logic(logic).empty()); // the elder's Rules too, though it is elsewhere
    CHECK_FALSE(d->types_elsewhere("elder").empty());
    CHECK(d->types_elsewhere("bat").empty()); // here, not elsewhere
    CHECK(has_line("When [who] interacts with it (elder)"));
    CHECK(has_line("When [who] interacts with it (bat)"));
    // Unsaved changes stay with their scene: save, then open the other.
    CHECK_FALSE(d->open_scene("maps/village.dscene", error));
    CHECK(error == "Save your changes before opening another scene.");
    CHECK(d->scene_path() == "maps/cave.dscene");
    REQUIRE(d->save(error));
    REQUIRE(d->open_scene("maps/village.dscene", error));
    CHECK(d->scene_path() == "maps/village.dscene");
    CHECK(d->entities().size() == village);
    CHECK(d->world().find_named("bat").empty());
    CHECK_FALSE(d->dirty());
    // The bat's Rule still checks and reads as it did: the bat is in the cave's file.
    CHECK(d->check_logic(logic).empty());
    CHECK_FALSE(d->types_elsewhere("bat").empty());
    CHECK(d->types_elsewhere("elder").empty());
    CHECK(has_line("When [who] interacts with it (bat)"));
    // What is not a scene of the project is refused, and the open scene stays.
    CHECK_FALSE(d->open_scene("maps/nothing.dscene", error));
    CHECK(error == "There is no scene maps/nothing.dscene in assets/.");
    CHECK_FALSE(d->open_scene("art/hero.png", error));
    CHECK(d->scene_path() == "maps/village.dscene");

    // Logic for moving between them: the game's actions, and the event of arriving.
    const std::vector<editor::LogicEvent> events = d->logic_events();
    CHECK(std::any_of(events.begin(), events.end(), [](const editor::LogicEvent& e) {
        return e.object.empty() && e.event == "scene_start" && e.text == "When a scene starts";
    }));
    const engine::logic::BlockId arriving =
        d->add_logic_rule(logic, editor::LogicEvent{"", "scene_start", ""}, error);
    REQUIRE(arriving.is_valid());
    const engine::logic::BlockId go = d->add_logic_step(
        logic, arriving, editor::LogicAction{"game", "go_to_scene_at", ""}, error);
    REQUIRE(go.is_valid());
    REQUIRE(
        d->set_logic_slot(logic, go, 0, reflect::Value{std::string("maps/cave.dscene")}, error));
    REQUIRE(d->set_logic_slot(logic, go, 1, reflect::Value{std::string("bat")}, error));
    CHECK(has_line("When a scene starts"));
    CHECK(has_line("Go to scene “maps/cave.dscene”, arriving at “bat” (game)"));
    CHECK(d->check_logic(logic).empty());

    // The scene the game starts in is a setting: one undo step, saved in the project.
    CHECK_FALSE(d->set_start_scene("maps/nothing.dscene", error));
    REQUIRE(d->set_start_scene("maps/cave.dscene", error));
    CHECK(d->undo_label() == "Start the game in maps/cave.dscene");
    CHECK(d->settings().start_scene == "maps/cave.dscene");
    REQUIRE(d->undo());
    CHECK(d->settings().start_scene == "maps/village.dscene");
    REQUIRE(d->redo());
    REQUIRE(d->save(error));
    std::vector<serialize::Issue> issues;
    const auto again = editor::Document::open(sample.path, issues);
    REQUIRE(again != nullptr);
    CHECK(issues.empty());
    CHECK(again->scene_path() == "maps/cave.dscene"); // a project opens in its start scene
    CHECK(again->world().find_named("bat").size() == 1);
    CHECK(again->check_logic(logic).empty());
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

    // Turned and scaled pictures are picked where they are drawn.
    scene2d::Sprite* sprite = d->world().get<scene2d::Sprite>(d->world().find_named("hero").at(0));
    sprite->rotation = 90.0; // about its feet, clockwise: its head goes to the right
    const std::optional<scene2d::Box> turned = editor::sprite_bounds(
        d->world(), d->assets(), d->world().find_named("hero").at(0), d->settings().ppu);
    REQUIRE(turned.has_value());
    const scene2d::Box lying = turned.value_or(scene2d::Box{});
    CHECK(lying.left == 10.0);
    CHECK(lying.right == 11.0);
    CHECK(lying.top == 8.5);
    CHECK(lying.bottom == 9.5);
    CHECK(name_at(10.5, 9.25) == "hero");
    CHECK(name_at(9.75, 8.25) != "hero"); // where it stood
    // A 45-degree turn: the middle is picked, a corner of the box around it is not.
    sprite->rotation = 45.0;
    const std::optional<scene2d::Box> slanted = editor::sprite_bounds(
        d->world(), d->assets(), d->world().find_named("hero").at(0), d->settings().ppu);
    REQUIRE(slanted.has_value());
    const scene2d::Box box = slanted.value_or(scene2d::Box{});
    CHECK(name_at((box.left + box.right) / 2.0, (box.top + box.bottom) / 2.0) == "hero");
    CHECK(name_at(box.left + 0.02, box.top + 0.02) != "hero");
    // Twice as wide, and flipped: from 9 to 11 across.
    sprite->rotation = 0.0;
    sprite->scale_x = -2.0;
    const std::optional<scene2d::Box> wide = editor::sprite_bounds(
        d->world(), d->assets(), d->world().find_named("hero").at(0), d->settings().ppu);
    REQUIRE(wide.has_value());
    CHECK(wide.value_or(scene2d::Box{}).left == 9.0);
    CHECK(wide.value_or(scene2d::Box{}).right == 11.0);
    CHECK(name_at(10.9, 8.5) == "hero");
}

TEST_CASE("A scene placed in another as a door to it, with its door back, which the game walks "
          "through") {
    const Sample sample;
    const auto d = sample.open();
    REQUIRE(d->scene_path() == "maps/village.dscene");
    const std::size_t before = d->entities().size();
    std::string error;
    const fs::path road_file = sample.path / "assets" / "maps" / "road.dscene";
    // The road's entity called door_to_village, as its file has it (null if none).
    const auto back_on_disk = [&]() -> serialize::Json {
        std::ifstream in(road_file, std::ios::binary);
        const serialize::Json road = serialize::Json::parse(in);
        for (const serialize::Json& entity : road.at("entities")) {
            if (entity.value("name", std::string()) == "door_to_village") {
                return entity;
            }
        }
        return nullptr;
    };
    REQUIRE(back_on_disk().is_null());
    // Where the hero stands, so the game walks in at once.
    const Guid door = d->place_door("maps/road.dscene", 10.01, 9.0, error);
    INFO(error);
    REQUIRE(door.is_valid());
    CHECK(d->undo_label() == "Place a door to road");
    CHECK(d->entities().size() == before + 1);
    const world::Entity e = d->world().find(door);
    CHECK(d->world().name(e) == "door_to_road");
    CHECK(d->world().get<game::Trigger>(e) != nullptr);
    CHECK(d->world().get<scene2d::Transform2D>(e)->x == 10.0); // to the nearest pixel
    const std::string doors = "logic/doors.drules";
    const auto doors_file = [&]() -> const engine::logic::LogicAsset* {
        for (const editor::LogicFile& file : d->logic()) {
            if (file.path == doors) {
                return &file.asset;
            }
        }
        return nullptr;
    };
    const auto lines = [&] {
        std::vector<std::string> found;
        for (const editor::LogicLine& line : editor::describe(*doors_file(), *d)) {
            found.push_back(line.text);
        }
        return found;
    };
    // The road had no door back, so one is made there, in the same step: each door's Rule
    // arrives at the other. Rules about it check, though it is in the road and not saved.
    REQUIRE(doors_file() != nullptr);
    CHECK(lines() == std::vector<std::string>{
                         "When [who] walks in (door_to_road)",
                         "Go to scene “maps/road.dscene”, arriving at “door_to_village” (game)",
                         "When [who] walks in (door_to_village)",
                         "Go to scene “maps/village.dscene”, arriving at “door_to_road” (game)"});
    CHECK(d->check_logic(doors).empty());
    CHECK(d->dirty());
    CHECK(back_on_disk().is_null()); // until saved
    // A second door to the same scene: numbered, arriving at the same door back (no other
    // is made), its Rule in the same file.
    const Guid second = d->place_door("maps/road.dscene", 3.0, 3.0, error);
    REQUIRE(second.is_valid());
    CHECK(d->world().name(d->world().find(second)) == "door_to_road_2");
    REQUIRE(doors_file()->rules.size() == 3);
    CHECK(lines().back() == "Go to scene “maps/road.dscene”, arriving at “door_to_village” (game)");
    // Refused, changing nothing: the scene open here, one that is not there, no place.
    CHECK_FALSE(d->place_door("maps/village.dscene", 3.0, 3.0, error).is_valid());
    CHECK(error == "That is the scene open here: a door leads to another one.");
    CHECK_FALSE(d->place_door("maps/nowhere.dscene", 3.0, 3.0, error).is_valid());
    CHECK(error == "There is no scene maps/nowhere.dscene in this project.");
    CHECK_FALSE(
        d->place_door("maps/road.dscene", std::numeric_limits<double>::quiet_NaN(), 3.0, error)
            .is_valid());
    CHECK(d->entities().size() == before + 2);
    CHECK(d->undo_label() == "Place a door to road");
    // One undo takes the second door and its Rule; another the first, the door back, and
    // the file.
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before + 1);
    CHECK(doors_file()->rules.size() == 2);
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    CHECK(doors_file() == nullptr);
    REQUIRE(d->redo());
    REQUIRE(d->save(error));
    CHECK(fs::exists(sample.path / "assets" / "logic" / "doors.drules"));
    CHECK_FALSE(d->dirty());
    // Saved, the door back is in the road's file: where the road's hero starts (3, 9),
    // with a Trigger Area.
    const serialize::Json made = back_on_disk();
    REQUIRE_FALSE(made.is_null());
    CHECK(made["components"]["transform_2d"]["x"] == 3.0);
    CHECK(made["components"]["transform_2d"]["y"] == 9.0);
    CHECK(made["components"].contains("trigger"));
    // Undone and saved, it leaves the road's file again; redone and saved, it is back.
    REQUIRE(d->undo());
    CHECK(d->dirty());
    REQUIRE(d->save(error));
    CHECK(back_on_disk().is_null());
    REQUIRE(d->redo());
    REQUIRE(d->save(error));
    REQUIRE_FALSE(back_on_disk().is_null());

    // In the road, the door back is there, its Rule checks, and it can be moved.
    REQUIRE(d->open_scene("maps/road.dscene", error));
    const std::vector<world::Entity> backs = d->world().find_named("door_to_village");
    REQUIRE(backs.size() == 1);
    CHECK(d->world().get<game::Trigger>(backs[0]) != nullptr);
    CHECK(d->check_logic(doors).empty());
    REQUIRE(d->move_entity(d->world().guid(backs[0]), 2.0, 3.0));
    REQUIRE(d->save(error));

    // Played: the hero stands in the door, so the game fades out to the road and in again,
    // arriving at the door back (2, 3), and stays: arriving in it is not walking in.
    game::RuntimeOptions options;
    options.graphics = false;
    options.audio = game::AudioMode::Headless;
    options.save_folder = sample.path / "saves";
    std::vector<serialize::Issue> issues;
    const std::unique_ptr<game::Runtime> runtime =
        game::Runtime::open(sample.path, options, issues);
    for (const serialize::Issue& issue : issues) {
        CAPTURE(issue.path);
        CAPTURE(issue.message);
        CHECK(false);
    }
    REQUIRE(runtime != nullptr);
    CHECK(runtime->scene() == "maps/village.dscene");
    for (int i = 0; i < 120 && runtime->scene() != "maps/road.dscene"; ++i) {
        runtime->step();
    }
    CHECK(runtime->scene() == "maps/road.dscene");
    for (int i = 0; i < 120; ++i) {
        runtime->step();
    }
    CHECK(runtime->scene() == "maps/road.dscene");
    const world::Entity arrived = runtime->world().find_named("hero").at(0);
    CHECK(runtime->world().get<scene2d::Transform2D>(arrived)->x == 2.0);
    CHECK(runtime->world().get<scene2d::Transform2D>(arrived)->y == 3.0);
}

TEST_CASE("Undoing to before a door back was made puts the other scene's file back on save") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    const fs::path road_file = sample.path / "assets" / "maps" / "road.dscene";
    const auto road_has_back = [&] {
        std::ifstream in(road_file, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        return text.find("door_to_village") != std::string::npos;
    };
    // A step before the door, then the door (and its door back in the road), saved.
    REQUIRE(d->create_entity("marker", 1.0, 1.0).is_valid());
    REQUIRE(d->place_door("maps/road.dscene", 10.0, 9.0, error).is_valid());
    REQUIRE(d->save(error));
    CHECK(road_has_back());
    // Undone past the door to before the first step, and saved: the road as it was.
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(d->world().find_named("marker").empty());
    CHECK(d->dirty());
    REQUIRE(d->save(error));
    CHECK_FALSE(road_has_back());
    // The doors' logic file, made by the door, goes too, with its .meta.
    CHECK_FALSE(fs::exists(sample.path / "assets" / "logic" / "doors.drules"));
    CHECK_FALSE(fs::exists(sample.path / "assets" / "logic" / "doors.drules.meta"));
    CHECK_FALSE(d->dirty());
    // Redone and saved: the door back again.
    REQUIRE(d->redo());
    REQUIRE(d->redo());
    REQUIRE(d->save(error));
    CHECK(road_has_back());
    CHECK(fs::exists(sample.path / "assets" / "logic" / "doors.drules"));
    CHECK(fs::exists(sample.path / "assets" / "logic" / "doors.drules.meta"));
    CHECK_FALSE(d->dirty());
}

TEST_CASE("A door to a scene whose door back was made by hand arrives at that one") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    const std::string doors = "logic/doors.drules";
    // In the road, a door to the village made by hand: a Trigger Area, and a Rule that
    // simply goes there.
    REQUIRE(d->open_scene("maps/road.dscene", error));
    const Guid made = d->create_entity("door_to_village", 2.0, 3.0);
    const reflect::TypeInfo* trigger = d->registry().find_type<game::Trigger>();
    REQUIRE(trigger != nullptr);
    REQUIRE(d->add_component(made, *trigger));
    REQUIRE(d->create_logic_file(doors, error));
    const engine::logic::BlockId rule =
        d->add_logic_rule(doors, editor::LogicEvent{"door_to_village", "entered", ""}, error);
    const engine::logic::BlockId go =
        d->add_logic_step(doors, rule, editor::LogicAction{"game", "go_to_scene", ""}, error);
    REQUIRE(go.is_valid());
    REQUIRE(
        d->set_logic_slot(doors, go, 0, reflect::Value{std::string("maps/village.dscene")}, error));
    REQUIRE(d->save(error));
    const std::string road_before = [&] {
        std::ifstream in(sample.path / "assets" / "maps" / "road.dscene", std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }();
    // From the village, a door to the road: no other door is made there; the two arrive at
    // each other, in one step, and the road's file is left as it is.
    REQUIRE(d->open_scene("maps/village.dscene", error));
    REQUIRE(d->place_door("maps/road.dscene", 10.0, 9.0, error).is_valid());
    std::vector<std::string> lines;
    for (const editor::LogicFile& file : d->logic()) {
        if (file.path == doors) {
            for (const editor::LogicLine& line : editor::describe(file.asset, *d)) {
                lines.push_back(line.text);
            }
        }
    }
    CHECK(lines == std::vector<std::string>{
                       "When [who] walks in (door_to_village)",
                       "Go to scene “maps/village.dscene”, arriving at “door_to_road” (game)",
                       "When [who] walks in (door_to_road)",
                       "Go to scene “maps/road.dscene”, arriving at “door_to_village” (game)"});
    CHECK(d->check_logic(doors).empty());
    REQUIRE(d->save(error));
    std::ifstream in(sample.path / "assets" / "maps" / "road.dscene", std::ios::binary);
    CHECK(std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()} ==
          road_before);
}

TEST_CASE("Turning and scaling a sprite, each a step of undo that a drag merges") {
    const Sample sample;
    const auto d = sample.open();
    const world::Entity hero_entity = d->world().find_named("hero").at(0);
    const Guid hero = d->world().guid(hero_entity);
    const auto sprite = [&] { return *d->world().get<scene2d::Sprite>(d->world().find(hero)); };
    // Turned past half a turn, it is kept between -180 and 180.
    CHECK(d->turn_sprite(hero, 30.0));
    CHECK(d->turn_sprite(hero, 270.0)); // the same drag: merged
    CHECK(sprite().rotation == -90.0);
    CHECK(d->undo_label() == "Turn hero");
    CHECK(d->turn_sprite(hero, -540.0));
    CHECK(sprite().rotation == 180.0);
    CHECK(d->turn_sprite(hero, 720.0));
    CHECK(sprite().rotation == 0.0);
    CHECK_FALSE(std::signbit(sprite().rotation)); // no -0 in the file
    d->end_merge();
    // Scaled both ways at once, within -16 to 16.
    CHECK(d->scale_sprite(hero, 2.0, -3.0));
    CHECK(d->scale_sprite(hero, 40.0, -0.5));
    CHECK(sprite().scale_x == 16.0);
    CHECK(sprite().scale_y == -0.5);
    CHECK(d->undo_label() == "Scale hero");
    // Refused: numbers that are not finite, and entities without a Sprite.
    CHECK_FALSE(d->turn_sprite(hero, std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(d->scale_sprite(hero, 1.0, std::numeric_limits<double>::infinity()));
    CHECK_FALSE(d->turn_sprite(d->world().guid(d->world().find_named("ground").at(0)), 10.0));
    CHECK_FALSE(d->scale_sprite(Guid::generate(), 1.0, 1.0));
    // One undo for the scaling, one for all the turning.
    d->undo();
    CHECK(sprite().scale_x == 1.0);
    CHECK(sprite().scale_y == 1.0);
    d->undo();
    CHECK(sprite().rotation == 0.0);
    CHECK_FALSE(d->can_undo());

    // A group turned or scaled about its middle: positions, rotations and scales at once,
    // one step merged while dragged, all or none.
    const Guid elder = d->world().guid(d->world().find_named("elder").at(0));
    const auto elder_sprite = [&] {
        return *d->world().get<scene2d::Sprite>(d->world().find(elder));
    };
    const auto at = [&](Guid who) {
        return *d->world().get<scene2d::Transform2D>(d->world().find(who));
    };
    using Shape = editor::Document::SpriteShape;
    CHECK(d->shape_sprites(
        {Shape{hero, 7.0, 9.0, 45.0, 1.0, 1.0}, Shape{elder, 9.0, 5.0, 45.0, 1.0, 1.0}}, true));
    CHECK(d->shape_sprites(
        {Shape{hero, 6.0, 9.0, 90.0, 1.0, 1.0}, Shape{elder, 10.0, 5.0, 200.0, 1.0, 1.0}}, true));
    CHECK(at(hero).x == 6.0);
    CHECK(at(elder).x == 10.0);
    CHECK(sprite().rotation == 90.0);
    CHECK(elder_sprite().rotation == -160.0); // brought into -180 to 180
    CHECK(d->undo_label() == "Turn 2 entities");
    CHECK(d->shape_sprites(
        {Shape{hero, 4.0, 11.0, 90.0, 2.0, 2.0}, Shape{elder, 12.0, 3.0, -160.0, -1.5, 40.0}},
        false));
    CHECK(elder_sprite().scale_x == -1.5);
    CHECK(elder_sprite().scale_y == 16.0); // kept within 16
    CHECK(d->undo_label() == "Scale 2 entities");
    const Guid ground = d->world().guid(d->world().find_named("ground").at(0));
    CHECK_FALSE(d->shape_sprites(
        {Shape{hero, 1.0, 1.0, 0.0, 1.0, 1.0}, Shape{ground, 1.0, 1.0, 0.0, 1.0, 1.0}},
        true)); // the ground has no Sprite
    CHECK_FALSE(d->shape_sprites(
        {Shape{hero, 1.0, 1.0, 0.0, 1.0, 1.0}, Shape{elder, std::nan(""), 1.0, 0.0, 1.0, 1.0}},
        true));
    CHECK(at(hero).x == 4.0);
    CHECK(sprite().scale_x == 2.0);
    CHECK_FALSE(d->shape_sprites({}, true));
    d->undo();
    d->undo();
    CHECK(at(hero).x == 10.0);
    CHECK(at(elder).y == 5.0);
    CHECK(sprite().rotation == 0.0);
    CHECK(elder_sprite().rotation == 0.0);
    CHECK(elder_sprite().scale_x == 1.0);
    CHECK_FALSE(d->can_undo());
}

TEST_CASE("Moving a sprite's pivot leaves the picture where it is") {
    const Sample sample;
    const auto d = sample.open();
    const world::Entity hero_entity = d->world().find_named("hero").at(0);
    const Guid hero = d->world().guid(hero_entity);
    const auto at = [&] { return *d->world().get<scene2d::Transform2D>(d->world().find(hero)); };
    const auto sprite = [&] { return *d->world().get<scene2d::Sprite>(d->world().find(hero)); };
    const auto box = [&] {
        return editor::sprite_bounds(d->world(), d->assets(), d->world().find(hero),
                                     d->settings().ppu)
            .value_or(scene2d::Box{});
    };
    // The hero's 1-tile picture covers (9.5, 8) to (10.5, 9), its feet the pivot.
    REQUIRE(box().left == 9.5);
    // The pivot to its middle: the position goes there, the picture stays.
    CHECK(d->set_sprite_pivot(hero, 0.5, 0.25));
    CHECK(d->set_sprite_pivot(hero, 0.5, 0.5)); // the same drag: merged
    CHECK(d->undo_label() == "Move the pivot of hero");
    CHECK(at().x == 10.0);
    CHECK(at().y == 8.5);
    CHECK(box().left == 9.5);
    CHECK(box().top == 8.0);
    CHECK(box().bottom == 9.0);
    // Turned a quarter about its middle, a square picture covers the same place.
    d->end_merge();
    REQUIRE(d->turn_sprite(hero, 90.0));
    CHECK(box().left == doctest::Approx(9.5));
    CHECK(box().top == doctest::Approx(8.0));
    // Turned, the pivot still moves without moving the picture; outside 0 to 1 is kept in.
    d->end_merge();
    CHECK(d->set_sprite_pivot(hero, 2.0, -1.0));
    CHECK(sprite().pivot_x == 1.0);
    CHECK(sprite().pivot_y == 0.0);
    CHECK(box().left == doctest::Approx(9.5));
    CHECK(box().bottom == doctest::Approx(9.0));
    // Refused: no number, no Sprite.
    CHECK_FALSE(d->set_sprite_pivot(hero, std::nan(""), 0.5));
    CHECK_FALSE(
        d->set_sprite_pivot(d->world().guid(d->world().find_named("ground").at(0)), 0.5, 0.5));
    // Three steps back to the start.
    d->undo();
    d->undo();
    d->undo();
    CHECK(at().x == 10.0);
    CHECK(at().y == 9.0);
    CHECK(sprite().pivot_y == 1.0);
    CHECK(sprite().rotation == 0.0);
    CHECK_FALSE(d->can_undo());
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

TEST_CASE("A new platformer: seen from the side, the hero jumps onto a ledge") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "Up We Go", error, editor::ProjectTemplate::Platformer));
    {
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        REQUIRE(d != nullptr);
        CHECK(issues.empty());
        CHECK(d->entities().size() == 5); // the level, the hero and three coins
        const world::Entity hero = d->world().find_named("hero").at(0);
        CHECK(d->world().get<game::Jumping>(hero) != nullptr);
        CHECK(d->world().get<game::TopDownMover>(hero)->hitbox_height == 1.0);
        // The coins: a picture and a Trigger Area each, and Rules that pick them up.
        for (const char* name : {"coin_1", "coin_2", "coin_3"}) {
            CAPTURE(name);
            REQUIRE(d->world().find_named(name).size() == 1);
            const world::Entity coin = d->world().find_named(name).at(0);
            CHECK(d->world().get<scene2d::Sprite>(coin) != nullptr);
            CHECK(d->world().get<game::Trigger>(coin) != nullptr);
        }
        CHECK(d->check_logic("logic/coins.drules").empty());
        const auto coins =
            std::find_if(d->logic().begin(), d->logic().end(),
                         [](const editor::LogicFile& f) { return f.path == "logic/coins.drules"; });
        REQUIRE(coins != d->logic().end());
        std::vector<std::string> first;
        for (const editor::LogicLine& line : editor::describe(coins->asset, *d)) {
            if (first.size() < 6) {
                first.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') +
                                line.text);
            }
        }
        CHECK(first == std::vector<std::string>{
                           "When [who] walks in (coin_1)", "  Give 1 “coin” (inventory)",
                           "  Set visible of coin_1 to no", "  Set enabled of coin_1 to no",
                           "  If the player has at least 3 “coin” (inventory):",
                           "    Say “You found all the coins!” (dialogue)"});
        if (test_support::gpu() != nullptr) {
            // Looking at (10, 6): the map's corner is at pixel (0, -6).
            const auto preview = editor::Preview::create(error);
            REQUIRE(preview != nullptr);
            const engine::assets::Image frame =
                preview->render(*d, 10.0, 6.0, error).value_or(engine::assets::Image{});
            REQUIRE(frame.width == 320);
            const auto at = [&frame](std::uint32_t x, std::uint32_t y) {
                const std::size_t i = (std::size_t{y} * frame.width + x) * 4;
                return std::array<int, 3>{frame.rgba.at(i), frame.rgba.at(i + 1),
                                          frame.rgba.at(i + 2)};
            };
            CHECK(at(160, 20) == std::array<int, 3>{132, 188, 236});  // the sky
            CHECK(at(100, 160) == std::array<int, 3>{72, 148, 76});   // grass, row 10
            CHECK(at(100, 172) == std::array<int, 3>{120, 122, 132}); // stone under it
            CHECK(at(120, 125) == std::array<int, 3>{120, 122, 132}); // the first ledge
            CHECK(at(5, 50) == std::array<int, 3>{120, 122, 132});    // the west wall
            CHECK(at(46, 148) == std::array<int, 3>{52, 96, 180});    // the hero's tunic
            CHECK(at(120, 116) == std::array<int, 3>{240, 196, 64});  // the first coin
        }
    }
    {
        // Played with recorded input: Right held for a second, Space pressed at a third of
        // it. The jump (2.87 tiles) clears the ledge's underside, and the hero lands on its
        // top (y = 8) and walks on along it to x = 3 + 5 tiles a second for a second.
        engine::input::Recording replay;
        const auto right = engine::input::Binding::key(engine::input::Key::Right);
        const auto space = engine::input::Binding::key(engine::input::Key::Space);
        replay.record(0, right, true);
        replay.record(20, space, true);
        replay.record(22, space, false);
        replay.record(60, right, false);
        game::RuntimeOptions options;
        options.graphics = false;
        options.replay = &replay;
        options.audio = game::AudioMode::Headless;
        options.save_folder = folder / "saves";
        std::vector<serialize::Issue> issues;
        const std::unique_ptr<game::Runtime> runtime = game::Runtime::open(folder, options, issues);
        CHECK(issues.empty());
        REQUIRE(runtime != nullptr);
        const auto hero = [&] {
            const world::Entity e = runtime->world().find_named("hero").at(0);
            return *runtime->world().get<scene2d::Transform2D>(e);
        };
        runtime->step();
        CHECK(hero().y == 10.0); // standing on the grass
        double highest = 10.0;
        for (int i = 1; i < 120; ++i) {
            runtime->step();
            highest = std::min(highest, hero().y);
        }
        CAPTURE(highest);
        CHECK(highest < 7.2);
        CHECK(hero().y == 8.0);
        CHECK(hero().x == doctest::Approx(8.0));
        const world::Entity e = runtime->world().find_named("hero").at(0);
        CHECK(runtime->world().get<game::Jumping>(e)->on_ground);
        // On the way, the hero walked into the first coin: one carried, that coin hidden
        // and its area off; the others still there.
        CHECK(runtime->inventory().count_of("coin") == 1);
        const world::Entity first = runtime->world().find_named("coin_1").at(0);
        CHECK_FALSE(runtime->world().get<scene2d::Sprite>(first)->visible);
        CHECK_FALSE(runtime->world().get<game::Trigger>(first)->enabled);
        const world::Entity second = runtime->world().find_named("coin_2").at(0);
        CHECK(runtime->world().get<scene2d::Sprite>(second)->visible);
        CHECK(runtime->world().get<game::Trigger>(second)->enabled);
    }
    {
        // The other two coins moved onto the first, and played the same way: all three
        // picked up at once, and the message says so (once).
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        REQUIRE(d != nullptr);
        for (const char* name : {"coin_2", "coin_3"}) {
            REQUIRE(d->move_entity(d->world().guid(d->world().find_named(name).at(0)), 7.5, 8.0));
        }
        REQUIRE(d->save(error));
        engine::input::Recording replay;
        const auto right = engine::input::Binding::key(engine::input::Key::Right);
        const auto space = engine::input::Binding::key(engine::input::Key::Space);
        replay.record(0, right, true);
        replay.record(20, space, true);
        replay.record(22, space, false);
        replay.record(60, right, false);
        game::RuntimeOptions options;
        options.graphics = false;
        options.replay = &replay;
        options.audio = game::AudioMode::Headless;
        options.save_folder = folder / "saves";
        const std::unique_ptr<game::Runtime> runtime = game::Runtime::open(folder, options, issues);
        CHECK(issues.empty());
        REQUIRE(runtime != nullptr);
        for (int i = 0; i < 120; ++i) {
            runtime->step();
        }
        CHECK(runtime->inventory().count_of("coin") == 3);
        REQUIRE(runtime->dialogue().is_open());
        REQUIRE(runtime->dialogue().current() != nullptr);
        CHECK(*runtime->dialogue().current() == "You found all the coins!");
        CHECK(runtime->dialogue().close_current());
        CHECK_FALSE(runtime->dialogue().is_open()); // said once
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("A new visual novel: a backdrop, Mira, and a story that answers back") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "A Story", error, editor::ProjectTemplate::VisualNovel));
    CHECK(fs::exists(folder / "assets" / "logic" / "story.drules.meta"));
    {
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        REQUIRE(d != nullptr);
        CHECK(issues.empty());
        CHECK(d->world().find_named("backdrop").size() == 1);
        CHECK(d->world().find_named("mira").size() == 1);
        if (test_support::gpu() != nullptr) {
            // Looking at the origin: the backdrop fills the screen, Mira stands right of the
            // middle with her feet at pixel (232, 122).
            const auto preview = editor::Preview::create(error);
            REQUIRE(preview != nullptr);
            const engine::assets::Image frame =
                preview->render(*d, 0.0, 0.0, error).value_or(engine::assets::Image{});
            REQUIRE(frame.width == 320);
            REQUIRE(frame.height == 180);
            const auto at = [&frame](std::uint32_t x, std::uint32_t y) {
                const std::size_t i = (std::size_t{y} * frame.width + x) * 4;
                return std::array<int, 3>{frame.rgba.at(i), frame.rgba.at(i + 1),
                                          frame.rgba.at(i + 2)};
            };
            CHECK(at(160, 5) == std::array<int, 3>{112, 160, 224});  // the sky, high
            CHECK(at(0, 0) == std::array<int, 3>{112, 160, 224});    // to the very corner
            CHECK(at(2, 170) == std::array<int, 3>{116, 176, 96});   // the meadow
            CHECK(at(319, 179) == std::array<int, 3>{116, 176, 96}); // to the other corner
            CHECK(at(231, 83) == std::array<int, 3>{124, 84, 168});  // Mira's dress
            CHECK(at(228, 72) == std::array<int, 3>{240, 202, 170}); // and face
        }
    }
    // Played in the game's own runtime, once for each answer.
    for (const std::size_t pick : {std::size_t{0}, std::size_t{1}}) {
        game::RuntimeOptions options;
        options.graphics = false;
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
        game::DialogueObject& dialogue = runtime->dialogue();
        const auto showing = [&] {
            return dialogue.current() != nullptr ? *dialogue.current() : std::string("nothing");
        };
        runtime->step();
        CHECK(showing() == "Mira: Oh! You came after all.");
        dialogue.close_current();
        runtime->step();
        CHECK(showing() == "Mira: Will you walk with me to the lake?");
        CHECK(dialogue.choices() == std::vector<std::string>{"Yes, let's go", "Not today"});
        REQUIRE(dialogue.choose(pick));
        dialogue.close_current();
        for (int i = 0; i < 3; ++i) {
            runtime->step();
        }
        CHECK(showing() == (pick == 0 ? "Mira: Wonderful! I know a shortcut through the meadow."
                                      : "Mira: Another time, then. I'll be here."));
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
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

TEST_CASE("The sample plays through its gate to the road, hears the stream, and comes back") {
    const Sample sample;
    // Right all the way to the gate, with Interact tapped now and then to close messages.
    engine::input::Recording replay;
    const auto key = [](engine::input::Key k) { return engine::input::Binding::key(k); };
    replay.record(0, key(engine::input::Key::Right), true);
    for (std::uint64_t step = 10; step < 2000; step += 20) {
        replay.record(step, key(engine::input::Key::Z), true);
        replay.record(step + 1, key(engine::input::Key::Z), false);
    }
    game::RuntimeOptions options;
    options.graphics = false;
    options.replay = &replay;
    options.audio = game::AudioMode::Headless;
    options.save_folder = sample.path / "saves";
    std::vector<serialize::Issue> issues;
    const std::unique_ptr<game::Runtime> runtime =
        game::Runtime::open(sample.path, options, issues);
    for (const serialize::Issue& issue : issues) {
        CAPTURE(issue.path);
        CAPTURE(issue.message);
        CHECK(false);
    }
    REQUIRE(runtime != nullptr);
    REQUIRE(runtime->audio() != nullptr);
    const auto hero = [&] {
        const world::Entity e = runtime->world().find_named("hero").at(0);
        return *runtime->world().get<scene2d::Transform2D>(e);
    };
    CHECK(runtime->scene() == "maps/village.dscene");
    CHECK(runtime->audio()->playing_loops() == 0);
    int steps = 0;
    while (runtime->scene() == "maps/village.dscene" && steps < 600) {
        runtime->step();
        ++steps;
    }
    // Eight tiles at four a second, and a moment for the first message: a few seconds.
    CAPTURE(steps);
    REQUIRE(runtime->scene() == "maps/road.dscene");
    CHECK(steps > 100);
    CHECK(hero().x == 3.0); // at the milestone
    CHECK(hero().y == 9.0);
    CHECK(runtime->world().find_named("elder").empty());
    // The stream's Sound Source plays on repeat from the step the scene changes, silent
    // while the screen is black, and is heard once the scene has faded in.
    const auto loud = [&] {
        std::vector<float> mixed(std::size_t{4800} * 2);
        REQUIRE(runtime->audio()->render(mixed));
        float peak = 0.0F;
        for (const float v : mixed) {
            peak = std::max(peak, std::abs(v));
        }
        return peak;
    };
    CHECK(runtime->fade() == 1.0);
    CHECK(runtime->audio()->playing_loops() == 1);
    CHECK(loud() < 0.0001F);
    while (runtime->fade() > 0.0) {
        runtime->step();
    }
    CHECK(runtime->audio()->playing_loops() == 1);
    CHECK(loud() > 0.001F);
    CHECK(runtime->problems().empty());
    CHECK(runtime->vm().errors().empty());
}

TEST_CASE("Logic reads as plain language") {
    const Sample sample;
    const auto d = sample.open();
    std::vector<serialize::Issue> issues;
    const std::vector<editor::LogicFile> files = editor::logic_files(*d, issues);
    CHECK(issues.empty());
    REQUIRE(files.size() == 2);
    CHECK(files[0].path == "logic/main.drules");
    CHECK(files[1].path == "logic/road.drules");
    // The road's logic: between the two scenes, and what is said on arriving.
    std::vector<std::string> road;
    for (const editor::LogicLine& line : editor::describe(files[1].asset, *d)) {
        road.push_back(std::string(static_cast<std::size_t>(line.depth) * 2, ' ') + line.text);
    }
    CHECK(
        road ==
        std::vector<std::string>{
            "When [who] walks in (gate)",
            "  Go to scene \u201cmaps/road.dscene\u201d, arriving at \u201cmilestone\u201d (game)",
            "When [who] walks in (way_back)",
            "  Go to scene \u201cmaps/village.dscene\u201d, arriving at \u201clantern\u201d (game)",
            "When a scene starts",
            "  If the scene is \u201cmaps/road.dscene\u201d (game)",
            "  Say \u201cThe road home. A stream runs beside it.\u201d (dialogue)",
            "  Otherwise:",
            "    Say \u201cThe village again.\u201d (dialogue)",
        });
    CHECK(d->check_logic("logic/road.drules").empty()); // way_back is in the other scene
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
    REQUIRE(d.logic().size() == 2); // main, then the road's
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
    REQUIRE(d->logic().size() == 3); // sorted by path: main, quests, road
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
    CHECK(d->logic().size() == 2);
    CHECK_FALSE(d->dirty());
    REQUIRE(d->redo());
    REQUIRE(d->logic().size() == 3);

    // Saved, the file is written, gets its GUID, and is read back on reopening.
    REQUIRE(d->add_logic_sequence(quests, error).is_valid());
    REQUIRE(d->save(error));
    CHECK(fs::exists(sample.path / "assets" / quests));
    CHECK(d->assets().find(quests) != nullptr);
    const auto again = sample.open();
    REQUIRE(again->logic().size() == 3);
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

TEST_CASE("The inventory in logic: give, take, check, and values with nothing to fill in") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/bag.drules";
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
    // Offered like the game's other objects: its actions and its conditions.
    const auto offered = [](const auto& list, std::string_view member) {
        return std::any_of(list.begin(), list.end(), [&](const auto& a) {
            return a.object == "inventory" && a.member == member;
        });
    };
    CHECK(offered(d->logic_actions(), "give"));
    CHECK(offered(d->logic_actions(), "take"));
    CHECK(offered(d->logic_conditions(), "has"));
    CHECK(offered(d->logic_conditions(), "has_at_least"));
    // When the game starts: give a key, and if the player has at least 3 coins, take them.
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const engine::logic::BlockId give =
        d->add_logic_step(file, rule, editor::LogicAction{"inventory", "give", ""}, error);
    REQUIRE(give.is_valid());
    REQUIRE(d->set_logic_slot(file, give, 0, reflect::Value{std::string("key")}, error));
    const engine::logic::BlockId coins = d->add_logic_branch(
        file, give, editor::LogicAction{"inventory", "has_at_least", ""}, false, error);
    REQUIRE(coins.is_valid());
    REQUIRE(d->set_logic_slot(file, coins, 0, reflect::Value{std::string("coin")}, error));
    REQUIRE(d->set_logic_slot(file, coins, 1, reflect::Value{std::int64_t{3}}, error));
    const engine::logic::BlockId take =
        d->add_logic_step(file, coins, editor::LogicAction{"inventory", "take", ""}, error);
    REQUIRE(take.is_valid());
    REQUIRE(d->set_logic_slot(file, take, 0, reflect::Value{std::string("coin")}, error));
    REQUIRE(d->set_logic_slot(file, take, 1, reflect::Value{std::int64_t{3}}, error));
    // A value with nothing to fill in, from an object: how many different things are
    // carried, set into a variable and compared.
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Int, error));
    const engine::logic::BlockId set =
        d->add_logic_step(file, take, editor::LogicAction{"", "set_variable", ""}, error);
    REQUIRE(set.is_valid());
    using Kind = editor::LogicSource::Kind;
    const std::vector<editor::LogicSource> sources = d->logic_sources(file, set, 0);
    const auto kinds = std::find_if(sources.begin(), sources.end(), [](const auto& s) {
        return s.kind == Kind::Getter && s.object == "inventory" && s.member == "kinds";
    });
    REQUIRE(kinds != sources.end());
    CHECK(kinds->text == "The number of different things carried (inventory)");
    CHECK(kinds->value == reflect::ValueKind::Int);
    REQUIRE(d->set_logic_source(file, set, 0, *kinds, error));
    // The step's value (its argument 0; argument 1 is the variable's name).
    const auto value_of = [&](engine::logic::BlockId step) {
        const std::vector<editor::LogicSlot> all = d->logic_slots(file, step);
        const auto found = std::find_if(all.begin(), all.end(),
                                        [](const editor::LogicSlot& s) { return s.argument == 0; });
        REQUIRE(found != all.end());
        return *found;
    };
    CHECK(value_of(set).inside.empty()); // nothing to fill in
    // A value with something to fill in: the number of a thing, its name a field of its own
    // inside the value, edited in place (and refused if it does not fit).
    const auto number = std::find_if(sources.begin(), sources.end(), [](const auto& s) {
        return s.kind == Kind::Getter && s.member == "count_of";
    });
    REQUIRE(number != sources.end());
    const engine::logic::BlockId counted =
        d->add_logic_step(file, set, editor::LogicAction{"", "set_variable", ""}, error);
    REQUIRE(d->set_logic_source(file, counted, 0, *number, error));
    const editor::LogicSlot counting = value_of(counted);
    CHECK_FALSE(counting.typed);
    REQUIRE(counting.inside.size() == 1);
    CHECK(counting.inside[0].label == "Item");
    CHECK(counting.inside[0].kind == reflect::ValueKind::Text);
    REQUIRE(d->set_logic_inside(file, counted, 0, 0, reflect::Value{std::string("co")}, error));
    REQUIRE(d->set_logic_inside(file, counted, 0, 0, reflect::Value{std::string("coin")}, error));
    CHECK(d->undo_label() == "Change Item in logic/bag.drules"); // typing merged
    CHECK_FALSE(d->set_logic_inside(file, counted, 0, 0, reflect::Value{std::int64_t{3}}, error));
    CHECK_FALSE(d->set_logic_inside(file, counted, 0, 1, reflect::Value{std::string("x")}, error));
    CHECK(error == "That value has nothing to fill in there.");
    const std::vector<editor::LogicSource> comparable = d->logic_comparables(file, rule);
    const auto answer_text = std::find_if(comparable.begin(), comparable.end(), [](const auto& s) {
        return s.kind == Kind::Getter && s.object == "dialogue" && s.member == "answer_text";
    });
    REQUIRE(answer_text != comparable.end());
    REQUIRE(d->add_logic_comparison(file, rule, *answer_text, error).is_valid());
    const std::string set_line =
        std::string("    Set variable_1 to the number of different things carried") +
        " (inventory)";
    const std::string count_line =
        std::string("    Set variable_1 to the number of “coin”") + " (inventory)";
    CHECK(lines() ==
          std::vector<std::string>{"When the game starts", "  If the answer's text (dialogue) = “”",
                                   "  Give 1 “key” (inventory)",
                                   "  If the player has at least 3 “coin” (inventory):",
                                   "    Take 3 “coin” (inventory)", set_line, count_line});
    CHECK(d->check_logic(file).empty());
}

TEST_CASE("Logic values from objects with what they need filled in taken from elsewhere") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view file = "logic/shop.drules";
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
    using Kind = editor::LogicSource::Kind;
    // A whole number set to the number of a thing; then a text variable naming the thing.
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Int, error)); // variable_1
    const engine::logic::BlockId rule =
        d->add_logic_rule(file, editor::LogicEvent{"", "game_start", ""}, error);
    const engine::logic::BlockId set =
        d->add_logic_step(file, rule, editor::LogicAction{"", "set_variable", ""}, error);
    REQUIRE(set.is_valid());
    const std::vector<editor::LogicSource> sources = d->logic_sources(file, set, 0);
    const auto number = std::find_if(sources.begin(), sources.end(), [](const auto& s) {
        return s.kind == Kind::Getter && s.object == "inventory" && s.member == "count_of";
    });
    REQUIRE(number != sources.end());
    REQUIRE(d->set_logic_source(file, set, 0, *number, error));
    REQUIRE(d->add_logic_variable(file, reflect::ValueKind::Text, error)); // variable_2
    // The thing's name can be written, or come from a text variable, an event value or a
    // property; not from a number, nor from another object's value.
    const std::vector<editor::LogicSource> inner = d->logic_inside_sources(file, set, 0, 0);
    REQUIRE_FALSE(inner.empty());
    CHECK(inner.front().kind == Kind::Written);
    const auto named = std::find_if(inner.begin(), inner.end(), [](const auto& s) {
        return s.kind == Kind::Variable && s.member == "variable_2";
    });
    REQUIRE(named != inner.end());
    CHECK(std::none_of(inner.begin(), inner.end(), [](const auto& s) {
        return s.member == "variable_1" || s.kind == Kind::Getter;
    }));
    CHECK(std::all_of(inner.begin(), inner.end(),
                      [](const auto& s) { return s.value == reflect::ValueKind::Text; }));
    CHECK(d->logic_inside_sources(file, set, 0, 1).empty()); // count_of has one value
    CHECK(d->logic_inside_sources(file, set, 1, 0).empty()); // the variable's name
    REQUIRE(d->set_logic_inside_source(file, set, 0, 0, *named, error));
    CHECK(d->undo_label() == "Change where Item comes from");
    const auto value_of = [&] {
        const std::vector<editor::LogicSlot> all = d->logic_slots(file, set);
        const auto found = std::find_if(all.begin(), all.end(),
                                        [](const editor::LogicSlot& s) { return s.argument == 0; });
        REQUIRE(found != all.end());
        return *found;
    };
    REQUIRE(value_of().inside.size() == 1);
    CHECK_FALSE(value_of().inside[0].typed);
    CHECK(value_of().inside[0].from == "variable_2");
    const std::string line =
        std::string("  Set variable_1 to the number of variable_2") + " (inventory)";
    CHECK(lines() == std::vector<std::string>{"When the game starts", line});
    CHECK(d->check_logic(file).empty()); // it compiles
    // Not written, so not edited in place; nor from what is not offered.
    CHECK_FALSE(d->set_logic_inside(file, set, 0, 0, reflect::Value{std::string("coin")}, error));
    CHECK(error == "Item comes from variable_2: choose “A value written here” to write it.");
    CHECK_FALSE(d->set_logic_inside_source(
        file, set, 0, 0, editor::LogicSource{Kind::Getter, "inventory", "kinds", "", {}}, error));
    CHECK(error == "That cannot give this value.");
    CHECK_FALSE(d->set_logic_inside_source(
        file, set, 0, 0, editor::LogicSource{Kind::Variable, "", "variable_1", "", {}}, error));
    // Written again: starting empty, then typed in place.
    REQUIRE(d->set_logic_inside_source(file, set, 0, 0, inner.front(), error));
    CHECK(value_of().inside[0].typed);
    REQUIRE(d->set_logic_inside(file, set, 0, 0, reflect::Value{std::string("coin")}, error));
    CHECK(lines()[1] == std::string("  Set variable_1 to the number of “coin”") + " (inventory)");
    // Undone step by step: back to the variable, then to written and empty.
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(lines()[1] == line);
    REQUIRE(d->undo());
    CHECK(value_of().inside[0].typed);
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
    // The last part as a bracket of its own: talks × (1 + 1). Its own last part is the
    // field, beside everything before it; Sum with and the sign work inside it; a bracket
    // can hold another; and taking the last part away closes them one at a time.
    REQUIRE(d->bracket_logic_sum(file, set, 0, Op::Add, error));
    CHECK(lines()[1] == "  Set talks to talks × (1 + 1)");
    slots = d->logic_slots(file, set);
    CHECK(value_slot().typed);
    CHECK(value_slot().from == "talks × (1 +");
    REQUIRE(d->set_logic_slot(file, set, 0, reflect::Value{std::int64_t{5}}, error));
    CHECK(lines()[1] == "  Set talks to talks × (1 + 5)");
    REQUIRE(d->set_logic_sum_with(file, set, 0, source(set, 0, Kind::EventValue, "answer"), error));
    CHECK(lines()[1] == "  Set talks to talks × (1 + [answer])");
    REQUIRE(d->set_logic_arithmetic(file, set, 0, Op::Subtract, error));
    CHECK(lines()[1] == "  Set talks to talks × (1 − [answer])");
    CHECK(d->check_logic(file).empty());
    REQUIRE(d->bracket_logic_sum(file, set, 0, Op::Multiply, error));
    CHECK(lines()[1] == "  Set talks to talks × (1 − [answer] × 1)");
    slots = d->logic_slots(file, set);
    CHECK(value_slot().from == "talks × (1 − [answer] ×");
    CHECK(d->check_logic(file).empty());
    {
        // In the asset: talks × (1 − ([answer] × 1)), the brackets on the right.
        const auto found = std::find_if(d->logic().begin(), d->logic().end(),
                                        [&](const editor::LogicFile& f) { return f.path == file; });
        REQUIRE(found != d->logic().end());
        const engine::logic::Expr& value = found->asset.rules.at(0).actions.at(0).arguments.at(0);
        CHECK(value.arithmetic == Op::Multiply);
        CHECK(value.operands.at(0).kind == engine::logic::ExprKind::Variable);
        const engine::logic::Expr& bracket = value.operands.at(1);
        CHECK(bracket.arithmetic == Op::Subtract);
        CHECK(bracket.operands.at(1).arithmetic == Op::Multiply);
    }
    REQUIRE(d->set_logic_arithmetic(file, set, 0, std::nullopt, error));
    CHECK(lines()[1] == "  Set talks to talks × (1 − [answer])");
    REQUIRE(d->set_logic_arithmetic(file, set, 0, std::nullopt, error));
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
    CHECK_FALSE(d->bracket_logic_sum(file, set, 0, Op::Add, error));
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

TEST_CASE("Unsaved work is kept for recovery, restored as one undo step, and gone once saved") {
    const Sample sample;
    const fs::path file = sample.path / editor::recovery_file;
    {
        const std::unique_ptr<editor::Document> doc = sample.open();
        std::string error;
        // Nothing unsaved: nothing kept.
        REQUIRE(doc->write_recovery(error));
        CHECK_FALSE(fs::exists(file));
        const Guid hero = doc->world().guid(doc->world().find_named("hero").at(0));
        REQUIRE(doc->move_entity(hero, 9.0, 4.0));
        doc->end_merge();
        REQUIRE(doc->write_recovery(error));
        CHECK(fs::exists(file));
        CHECK(editor::recovery_scene(sample.path) ==
              std::optional<std::string>("maps/village.dscene"));
        // The editor closes without saving (a crash): the scene's file is as it was.
    }
    {
        const std::unique_ptr<editor::Document> doc = sample.open();
        const Guid hero = doc->world().guid(doc->world().find_named("hero").at(0));
        const auto x = [&] {
            return doc->world().get<scene2d::Transform2D>(doc->world().find(hero))->x;
        };
        CHECK(x() != 9.0);
        CHECK_FALSE(doc->dirty());
        std::string error;
        REQUIRE(doc->restore_recovery(error));
        CHECK(x() == 9.0);
        CHECK(doc->dirty());
        CHECK(doc->undo_label() == "Restore unsaved work");
        REQUIRE(doc->undo());
        CHECK(x() != 9.0);
        REQUIRE(doc->redo());
        CHECK(fs::exists(file)); // until saved
        REQUIRE(doc->save(error));
        CHECK_FALSE(fs::exists(file));
        CHECK_FALSE(editor::recovery_scene(sample.path).has_value());
        CHECK_FALSE(doc->restore_recovery(error));
        CHECK(error == "There is no unsaved work to restore.");
    }
    // Work in another scene opens that scene; a damaged file is refused, changing nothing.
    {
        const std::unique_ptr<editor::Document> doc = sample.open();
        std::string error;
        REQUIRE(doc->open_scene("maps/road.dscene", error));
        REQUIRE(doc->create_entity("signpost", 1.0, 1.0).is_valid());
        REQUIRE(doc->write_recovery(error));
    }
    {
        const std::unique_ptr<editor::Document> doc = sample.open();
        std::string error;
        REQUIRE(doc->restore_recovery(error));
        CHECK(doc->scene_path() == "maps/road.dscene");
        CHECK(doc->world().find_named("signpost").size() == 1);
        doc->discard_recovery();
        CHECK_FALSE(fs::exists(file));
        std::ofstream(file) << "{\"format\": \"sparxel.recovery\"}";
        CHECK_FALSE(doc->restore_recovery(error));
        CHECK(error == "The unsaved work could not be read: its file is damaged.");
        CHECK_FALSE(editor::recovery_scene(sample.path).has_value());
    }
}

TEST_CASE("Project Doctor finds missing files, broken logic, unreachable scenes and unused files") {
    const Sample sample;
    const std::unique_ptr<editor::Document> doc = sample.open();
    using Severity = editor::Finding::Severity;
    const auto count = [](const std::vector<editor::Finding>& found, Severity severity) {
        return std::count_if(found.begin(), found.end(), [severity](const editor::Finding& f) {
            return f.severity == severity;
        });
    };
    // The sample as it is: nothing wrong.
    const std::vector<editor::Finding> clean = editor::examine(*doc);
    CHECK(count(clean, Severity::Problem) == 0);
    CHECK(count(clean, Severity::Warning) == 0);

    // A picture that is gone, on the hero: a problem pointing at the hero.
    const world::Entity hero = doc->world().find_named("hero").at(0);
    doc->world().get<scene2d::Sprite>(hero)->image = Guid::generate().to_string();
    std::vector<editor::Finding> found = editor::examine(*doc);
    REQUIRE(count(found, Severity::Problem) == 1);
    CHECK(found.front().severity == Severity::Problem);
    CHECK(found.front().entity == doc->world().guid(hero));
    CHECK(found.front().scene == "maps/village.dscene");
    CHECK(found.front().message.find("\"hero\"") != std::string::npos);

    // Unused files are notes; logic that names a missing entity is a problem.
    std::ofstream(sample.path / "assets" / "art" / "spare.png", std::ios::binary)
        << std::string("not really a picture");
    static_cast<void>(doc->assets().refresh());
    REQUIRE(doc->rename(doc->world().guid(doc->world().find_named("elder").at(0)), "sage"));
    found = editor::examine(*doc);
    CHECK(std::any_of(found.begin(), found.end(), [](const editor::Finding& f) {
        return f.severity == Severity::Note && f.asset == "art/spare.png";
    }));
    CHECK(std::any_of(found.begin(), found.end(), [](const editor::Finding& f) {
        return f.severity == Severity::Problem && f.logic == "logic/main.drules" &&
               f.block.is_valid();
    }));
    // Problems come first.
    CHECK(std::is_sorted(found.begin(), found.end(),
                         [](const editor::Finding& a, const editor::Finding& b) {
                             return static_cast<int>(a.severity) < static_cast<int>(b.severity);
                         }));

    // A new scene, which nothing leads to.
    const std::unique_ptr<editor::Document> fresh = sample.open();
    std::string error;
    REQUIRE(fresh->create_scene("maps/attic.dscene", error));
    found = editor::examine(*fresh);
    CHECK(std::any_of(found.begin(), found.end(), [](const editor::Finding& f) {
        return f.severity == Severity::Warning && f.scene == "maps/attic.dscene";
    }));
}

TEST_CASE("Raising and lowering tiles builds Diorama terrain, a stroke one undo step") {
    const Sample sample;
    const std::unique_ptr<editor::Document> doc = sample.open();
    // The village's ground map: its first tilemap.
    Guid ground;
    for (const world::Entity e : doc->entities()) {
        if (!ground.is_valid() && doc->world().get<scene2d::Tilemap>(e) != nullptr) {
            ground = doc->world().guid(e);
        }
    }
    REQUIRE(ground.is_valid());
    scene2d::Tilemap* map = doc->world().get<scene2d::Tilemap>(doc->world().find(ground));
    const auto* at = doc->world().get<scene2d::Transform2D>(doc->world().find(ground));
    REQUIRE(map != nullptr);
    const double x = at->x + 0.5;
    const double y = at->y + 0.5;
    const double was = map->height_at(0, 0);
    CHECK(doc->raise_tile(ground, x, y, 0.5));
    CHECK(doc->raise_tile(ground, x + 1.0, y, 0.5));
    map = doc->world().get<scene2d::Tilemap>(doc->world().find(ground));
    CHECK(map->height_at(0, 0) == was + 0.5);
    CHECK(doc->undo_label().starts_with("Raise "));
    doc->end_merge();
    CHECK(doc->raise_tile(ground, x, y, -100.0)); // kept at 0
    map = doc->world().get<scene2d::Tilemap>(doc->world().find(ground));
    CHECK(map->height_at(0, 0) == 0.0);
    CHECK(doc->undo_label().starts_with("Lower "));
    CHECK_FALSE(doc->raise_tile(ground, at->x - 5.0, y, 1.0)); // off the map
    REQUIRE(doc->undo());
    REQUIRE(doc->undo()); // the stroke of two, at once
    map = doc->world().get<scene2d::Tilemap>(doc->world().find(ground));
    CHECK(map->height_at(0, 0) == was);
    CHECK(map->height_at(1, 0) == was);
}

TEST_CASE("Prefabs: made from an entity, placed as copies, and the copies updated") {
    const Sample sample;
    const std::unique_ptr<editor::Document> doc = sample.open();
    const Guid elder = doc->world().guid(doc->world().find_named("elder").at(0));
    std::string error;
    REQUIRE(doc->make_prefab(elder, "villager", error));
    CHECK(fs::exists(sample.path / "assets" / "prefabs" / "villager.dprefab"));
    CHECK(doc->prefab_of(elder) == "prefabs/villager.dprefab");
    CHECK(doc->undo_label() == "Make prefab villager");
    CHECK_FALSE(doc->make_prefab(elder, "villager", error));
    CHECK(error == "There is a prefab called villager already.");
    CHECK_FALSE(doc->make_prefab(elder, "a/b", error));

    // Placed twice: new GUIDs, at the points given, each linked to the prefab.
    const Guid first = doc->place_prefab("prefabs/villager.dprefab", 3.0, 4.0, error);
    const Guid second = doc->place_asset("prefabs/villager.dprefab", 7.25, 2.0, error);
    REQUIRE(first.is_valid());
    REQUIRE(second.is_valid());
    CHECK(first != elder);
    const auto at = [&](Guid g) {
        return *doc->world().get<scene2d::Transform2D>(doc->world().find(g));
    };
    CHECK(at(first).x == 3.0);
    CHECK(at(second).x == 7.25);
    CHECK(doc->prefab_of(second) == "prefabs/villager.dprefab");
    CHECK(doc->world().get<scene2d::Sprite>(doc->world().find(first)) != nullptr);
    CHECK(doc->undo_label() == "Place villager");

    // The prefab file changes (here: a faster walk animation... any component); copies match
    // it again, each keeping its name, place and GUID.
    REQUIRE(doc->rename(first, "farmer"));
    doc->world().get<scene2d::Sprite>(doc->world().find(first))->tint_r = 10;
    const std::size_t updated = doc->update_prefab_copies("prefabs/villager.dprefab", error);
    CHECK(updated == 3); // the elder and the two placed
    CHECK(doc->world().find(first).is_valid());
    CHECK(doc->world().name(doc->world().find(first)) == "farmer");
    CHECK(at(first).x == 3.0);
    CHECK(doc->world().get<scene2d::Sprite>(doc->world().find(first))->tint_r == 255);
    CHECK(doc->undo_label() == "Update 3 copies of villager");
    REQUIRE(doc->undo());
    CHECK(doc->world().get<scene2d::Sprite>(doc->world().find(first))->tint_r == 10);
    CHECK(doc->update_prefab_copies("maps/village.dscene", error) == 0);
    CHECK(error == "That is not one of the project's prefabs.");
}

TEST_CASE("Auto-tiles: made from a picture's blocks, and painted by their neighbours") {
    const Sample sample;
    {
        // A 2 x 3 block of 16-pixel tiles.
        engine::assets::Image block;
        block.width = 32;
        block.height = 48;
        block.rgba.assign(std::size_t{32} * 48 * 4, 200);
        const std::vector<std::uint8_t> png = engine::assets::encode_png(block);
        std::ofstream(sample.path / "assets/art/water.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    const auto d = sample.open();
    std::string error;
    CHECK(d->make_autotiles("art/tiles.png", error).empty()); // 48 x 16: too small
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK(d->make_autotiles("art/nothing.png", error).empty());
    const std::string made = d->make_autotiles("art/water.png", error);
    CHECK(made == "art/water_auto.png");
    const engine::assets::AssetRecord* record = d->assets().find(made);
    REQUIRE(record != nullptr);
    {
        std::string bad;
        const std::optional<std::vector<std::uint8_t>> bytes = d->assets().read(record->guid);
        REQUIRE(bytes.has_value());
        const std::optional<engine::assets::Image> tiles =
            engine::assets::decode_png(bytes.value_or(std::vector<std::uint8_t>{}), bad);
        REQUIRE(tiles.has_value());
        if (tiles) {
            CHECK(tiles->width == 8 * 16);
            CHECK(tiles->height == 6 * 16);
        }
    }

    // The ground, cleared in the middle, painted as an auto-tile map.
    const Guid ground = guid_of(*d, "ground");
    const auto cell = [&](std::uint32_t x, std::uint32_t y) {
        std::string bad;
        const scene2d::TileGrid* grid =
            d->world().get<scene2d::Tilemap>(d->world().find(ground))->grid(bad);
        REQUIRE(grid != nullptr);
        return grid->at(x, y);
    };
    REQUIRE(d->paint_tile_rect(ground, 2.5, 2.5, 7.5, 7.5, -1));
    const reflect::TypeInfo* tilemap = d->registry().find_type<scene2d::Tilemap>();
    REQUIRE(tilemap != nullptr);
    REQUIRE(d->set_property(ground, *tilemap, property(*tilemap, "autotile"), reflect::Value{true},
                            error));
    using engine::assets::autotile_shape;
    // Alone: its own shape, of terrain 0 (the preview chosen, 47, picks it).
    REQUIRE(d->paint_tile(ground, 4.5, 4.5, 47));
    CHECK(cell(4, 4) == static_cast<std::int32_t>(autotile_shape(0)));
    // A neighbour to the east: both change shape.
    REQUIRE(d->paint_tile(ground, 5.5, 4.5, 47));
    CHECK(cell(4, 4) == static_cast<std::int32_t>(autotile_shape(engine::assets::east)));
    CHECK(cell(5, 4) == static_cast<std::int32_t>(autotile_shape(engine::assets::west)));
    // Painting a cell of the same terrain changes nothing.
    d->end_merge();
    const std::string label = d->undo_label();
    REQUIRE(d->paint_tile(ground, 4.5, 4.5, 47));
    CHECK(d->undo_label() == label);
    // A 3 x 3 rectangle: the middle is surrounded.
    REQUIRE(d->paint_tile_rect(ground, 3.5, 3.5, 5.5, 5.5, 47));
    CHECK(cell(4, 4) == 46);
    CHECK(cell(3, 3) ==
          static_cast<std::int32_t>(autotile_shape(engine::assets::east | engine::assets::south |
                                                   engine::assets::south_east)));
    // Erasing the middle: its neighbours now have a hole beside them.
    REQUIRE(d->paint_tile(ground, 4.5, 4.5, -1));
    CHECK(cell(4, 4) == -1);
    CHECK(cell(4, 3) ==
          static_cast<std::int32_t>(autotile_shape(engine::assets::east | engine::assets::west)));
}

TEST_CASE("Collecting texts to translate: Rules' words, the menus', once each, kept on again") {
    const Sample sample;
    const auto d = sample.open();
    const std::vector<std::string> texts = d->texts_to_translate();
    CHECK(std::find(texts.begin(), texts.end(), "Resume") != texts.end());
    CHECK(std::find(texts.begin(), texts.end(), "New game") != texts.end());
    CHECK(std::set<std::string>(texts.begin(), texts.end()).size() == texts.size());
    // The project translates into French and German.
    engine::game::ProjectSettings settings = d->settings();
    settings.languages = "fr, de";
    std::string error;
    REQUIRE(d->set_settings(settings, error));
    const std::int64_t added = d->collect_translations(error);
    CHECK(added == static_cast<std::int64_t>(texts.size()));
    const engine::assets::AssetRecord* record = d->assets().find("data/translations.dtable");
    REQUIRE(record != nullptr);
    const std::optional<std::vector<std::uint8_t>> bytes = d->assets().read(record->guid);
    REQUIRE(bytes.has_value());
    if (!bytes) {
        return;
    }
    const std::optional<engine::game::Table> table = engine::game::parse_table(
        std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), // NOLINT
        error);
    REQUIRE(table.has_value());
    if (!table) {
        return;
    }
    CHECK(table->column("fr") != nullptr);
    CHECK(table->column("de") != nullptr);
    CHECK(table->text(engine::game::translation_key("Resume"), "source") == "Resume");
    // Collecting again adds nothing new.
    CHECK(d->collect_translations(error) == 0);
}

TEST_CASE("Random tile variants: each cell painted with one of them, at random") {
    const Sample sample;
    const auto d = sample.open();
    const Guid ground = guid_of(*d, "ground");
    const auto cell = [&](std::uint32_t x, std::uint32_t y) {
        std::string bad;
        const scene2d::TileGrid* grid =
            d->world().get<scene2d::Tilemap>(d->world().find(ground))->grid(bad);
        REQUIRE(grid != nullptr);
        return grid->at(x, y);
    };
    d->set_tile_variants({1, 2});
    CHECK(d->tile_variants().size() == 2);
    REQUIRE(d->paint_tile_rect(ground, 2.5, 2.5, 7.5, 7.5, 1));
    std::set<std::int32_t> seen;
    for (std::uint32_t y = 2; y <= 7; ++y) {
        for (std::uint32_t x = 2; x <= 7; ++x) {
            seen.insert(cell(x, y));
        }
    }
    CHECK(seen == std::set<std::int32_t>{1, 2}); // both, and nothing else
    // A pencil over a cell holding one of them leaves it.
    d->end_merge();
    const std::int32_t before = cell(4, 4);
    REQUIRE(d->paint_tile(ground, 4.5, 4.5, before == 1 ? 2 : 1));
    CHECK(cell(4, 4) == before);
    // A tile not among them paints itself; without variants, the tile chosen.
    REQUIRE(d->paint_tile(ground, 10.5, 1.5, 0));
    CHECK(cell(10, 1) == 0);
    d->set_tile_variants({});
    REQUIRE(d->paint_tile_rect(ground, 2.5, 2.5, 3.5, 3.5, 1));
    CHECK(cell(3, 3) == 1);
    CHECK(cell(2, 2) == 1);
}

TEST_CASE("Aseprite files are imported as a sprite sheet beside them, with their tags") {
    const Sample sample;
    {
        const std::vector<std::uint8_t> bytes =
            test_support::aseprite(3, 2, {{255, 0, 0, 255}, {0, 0, 255, 255}}, "blink");
        std::ofstream(sample.path / "assets/art/blob.aseprite", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), // NOLINT
                   static_cast<std::streamsize>(bytes.size()));
    }
    const auto d = sample.open();
    std::string error;
    const std::string made = d->import_aseprite("art/blob.aseprite", error);
    INFO(error);
    CHECK(made == "art/blob.png");
    REQUIRE(d->assets().find("art/blob.png") != nullptr);
    REQUIRE(d->assets().find("art/blob.sheet") != nullptr);
    const std::optional<std::vector<std::uint8_t>> sheet_bytes =
        d->assets().read(d->assets().find("art/blob.sheet")->guid);
    REQUIRE(sheet_bytes.has_value());
    if (sheet_bytes) {
        const std::optional<scene2d::SheetInfo> sheet = scene2d::parse_sheet(
            std::string_view(reinterpret_cast<const char*>(sheet_bytes->data()), // NOLINT
                             sheet_bytes->size()),
            error);
        REQUIRE(sheet.has_value());
        if (sheet) {
            CHECK(sheet->frame_width == 3);
            CHECK(sheet->durations_ms.size() == 2);
            REQUIRE(sheet->tags.size() == 1);
            CHECK(sheet->tags[0].name == "blink");
            CHECK(sheet->tags[0].to == 1);
        }
    }
    CHECK(d->import_aseprite("art/tiles.png", error).empty()); // not an Aseprite file
}

TEST_CASE("A new RPG: a village with an elder, a shop, a slime, menus and a HUD, that plays") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-" + Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "Village Quest", error, editor::ProjectTemplate::Rpg));
    {
        std::vector<serialize::Issue> issues;
        const auto d = editor::Document::open(folder, issues);
        REQUIRE(d != nullptr);
        CHECK(issues.empty());
        CHECK(d->entities().size() == 9);
        CHECK(d->world().get<game::ScreenPicture>(d->world().find_named("gold_panel").at(0)) !=
              nullptr);
        CHECK(d->world().get<game::EnemyOnMap>(d->world().find_named("slime").at(0)) != nullptr);
        CHECK(d->world().get<game::ScreenText>(d->world().find_named("gold").at(0)) != nullptr);
        CHECK(d->check_logic("logic/village.drules").empty());
        CHECK(d->settings().title_screen);
        CHECK(d->settings().box_frame == "art/panel.png"); // boxes framed as the gold is
        CHECK(d->settings().pause_menu);
    }
    engine::input::Recording replay;
    const auto z = engine::input::Binding::key(engine::input::Key::Z);
    replay.record(2, z, true); // New game
    replay.record(3, z, false);
    game::RuntimeOptions options;
    options.graphics = false;
    options.replay = &replay;
    options.audio = game::AudioMode::Headless;
    options.save_folder = folder / "saves";
    std::vector<serialize::Issue> issues;
    const std::unique_ptr<game::Runtime> runtime = game::Runtime::open(folder, options, issues);
    CHECK(issues.empty());
    REQUIRE(runtime != nullptr);
    CHECK(runtime->menu().page() == game::GameMenu::Page::Title);
    CHECK(runtime->menu().credits().size() >= 1);
    for (int i = 0; i < 6; ++i) {
        runtime->step();
    }
    CHECK_FALSE(runtime->menu().open());
    CHECK(runtime->party().in_party("hero"));
    CHECK(runtime->inventory().count_of("gold") == 20);
    // Talking to the elder starts the conversation.
    const std::array<engine::reflect::Value, 1> by{engine::reflect::Value{std::string("hero")}};
    CHECK(game::emit(runtime->vm(), runtime->world(), runtime->world().find_named("elder").at(0),
                     "interacted", by));
    runtime->step();
    runtime->step();
    REQUIRE(runtime->dialogue().is_open());
    CHECK(runtime->dialogue().current_speaker() == "Elder");
    CHECK(runtime->problems().empty());
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("Tags in the editor: Rules about every tagged object, acting on it") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view logic = "logic/main.drules";
    const reflect::TypeInfo* tags = d->registry().find_type<game::Tags>();
    const reflect::TypeInfo* interactable = d->registry().find_type<game::Interactable>();
    REQUIRE(tags != nullptr);
    REQUIRE(interactable != nullptr);
    // The elder and a new guard are villagers.
    const Guid elder = d->world().guid(d->world().find_named("elder").at(0));
    const Guid guard = d->create_entity("guard", 3.0, 4.0);
    REQUIRE(d->add_component(guard, *interactable));
    // Villagers walk: a Walker, whose actions ("Stand still") a Rule can give "it".
    const reflect::TypeInfo* walker = d->registry().find_type<game::Walker>();
    REQUIRE(walker != nullptr);
    REQUIRE(d->add_component(guard, *walker));
    for (const Guid villager : {elder, guard}) {
        REQUIRE(d->add_component(villager, *tags));
        REQUIRE(d->set_property(villager, *tags, *tags->find_property("tags"),
                                reflect::Value{std::string("Villager")}, error));
    }
    CHECK(d->logic_tags() == std::vector<std::string>{"#villager"});
    const std::vector<editor::LogicEvent> events = d->logic_events();
    CHECK(std::any_of(events.begin(), events.end(), [](const editor::LogicEvent& e) {
        return e.object == "#villager" && e.event == "interacted";
    }));
    // "When an object tagged villager is interacted with: …it…"
    const engine::logic::BlockId rule =
        d->add_logic_rule(logic, editor::LogicEvent{"#villager", "interacted", ""}, error);
    REQUIRE(rule.is_valid());
    const std::vector<editor::LogicAction> actions = d->logic_actions();
    const auto on_it =
        std::find_if(actions.begin(), actions.end(), [](const editor::LogicAction& a) {
            return a.object == "it" && a.member == "stand";
        });
    REQUIRE(on_it != actions.end());
    REQUIRE(d->add_logic_step(logic, rule, *on_it, error).is_valid());
    CHECK(d->check_logic(logic).empty());
    bool shown = false;
    for (const editor::LogicLine& line : editor::describe(d->logic().at(0).asset, *d)) {
        shown = shown || line.text == "When [who] interacts with it (any object tagged villager)";
    }
    CHECK(shown);
}

TEST_CASE("The For each step in the editor: a tag of the project, steps inside on it") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view logic = "logic/main.drules";
    const engine::logic::BlockId rule =
        d->add_logic_rule(logic, editor::LogicEvent{"", "game_start", ""}, error);
    REQUIRE(rule.is_valid());
    // With no tags yet, there is nothing to go through.
    CHECK_FALSE(
        d->add_logic_step(logic, rule, editor::LogicAction{"", "for_each_tagged", ""}, error)
            .is_valid());
    CHECK(error == "No object has a tag yet: give some a Tags component first.");

    const reflect::TypeInfo* tags = d->registry().find_type<game::Tags>();
    const reflect::TypeInfo* walker = d->registry().find_type<game::Walker>();
    const Guid guard = d->create_entity("guard", 3.0, 4.0);
    REQUIRE(d->add_component(guard, *tags));
    REQUIRE(d->add_component(guard, *walker));
    REQUIRE(d->set_property(guard, *tags, *tags->find_property("tags"),
                            reflect::Value{std::string("villager")}, error));
    const engine::logic::BlockId each =
        d->add_logic_step(logic, rule, editor::LogicAction{"", "for_each_tagged", ""}, error);
    REQUIRE(each.is_valid());
    // Steps added on it go inside it.
    REQUIRE(
        d->add_logic_step(logic, each, editor::LogicAction{"it", "stand", ""}, error).is_valid());
    CHECK(d->check_logic(logic).empty());
    std::vector<std::string> shown;
    for (const editor::LogicLine& line : editor::describe(d->logic().at(0).asset, *d)) {
        shown.push_back(line.text);
    }
    const auto at =
        std::find(shown.begin(), shown.end(), "For each object tagged villager (as it):");
    REQUIRE(at != shown.end());
    CHECK(*(at + 1) == "Stand still (it)");
    // Its tag is one the project has.
    CHECK_FALSE(d->set_logic_slot(logic, each, 0, reflect::Value{std::string("dragon")}, error));
    CHECK(error == "No object is tagged dragon.");
    // Saved and read back as it was.
    REQUIRE(d->save(error));
    std::vector<serialize::Issue> issues;
    const auto again = editor::Document::open(sample.path, issues);
    REQUIRE(again != nullptr);
    CHECK(again->check_logic(logic).empty());
    bool kept = false;
    for (const editor::LogicLine& line : editor::describe(again->logic().at(0).asset, *again)) {
        kept = kept || line.text == "For each object tagged villager (as it):";
    }
    CHECK(kept);
}

TEST_CASE("Who in the editor: steps on the object an event names") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    constexpr std::string_view logic = "logic/main.drules";
    // A walker, so who has something to do: "When [who] interacts with the elder: who
    // stands still."
    const reflect::TypeInfo* walker = d->registry().find_type<game::Walker>();
    const Guid guard = d->create_entity("guard", 3.0, 4.0);
    REQUIRE(d->add_component(guard, *walker));
    const engine::logic::BlockId rule =
        d->add_logic_rule(logic, editor::LogicEvent{"elder", "interacted", ""}, error);
    REQUIRE(rule.is_valid());
    REQUIRE(
        d->add_logic_step(logic, rule, editor::LogicAction{"who", "stand", ""}, error).is_valid());
    CHECK(d->check_logic(logic).empty());
    bool shown = false;
    for (const editor::LogicLine& line : editor::describe(d->logic().at(0).asset, *d)) {
        shown = shown || line.text == "Stand still (who)";
    }
    CHECK(shown);
    // On a game-wide event, there is no who.
    const engine::logic::BlockId start =
        d->add_logic_rule(logic, editor::LogicEvent{"", "game_start", ""}, error);
    REQUIRE(
        d->add_logic_step(logic, start, editor::LogicAction{"who", "stand", ""}, error).is_valid());
    const std::vector<engine::logic::Diagnostic> problems = d->check_logic(logic);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("names none") != std::string::npos);
}

TEST_CASE("Copy, paste and duplicate: whole entities, new names, one undo step each") {
    const Sample sample;
    const auto d = sample.open();
    std::string error;
    const world::World& w = d->world();
    const Guid hero = w.guid(w.find_named("hero").at(0));
    const Guid elder = w.guid(w.find_named("elder").at(0));
    // A lantern under the elder comes with it.
    const Guid lantern = d->create_entity("held_lantern", 0.0, 0.0);
    REQUIRE(d->set_parent(lantern, elder));
    const std::size_t before = d->entities().size();

    const std::string text = d->copy_entities({elder, lantern, hero}); // the lantern once
    const std::vector<Guid> pasted = d->paste_entities(text, 2.0, 0.0, error);
    REQUIRE(pasted.size() == 2); // the elder and the hero, the lantern under the copy
    CHECK(d->entities().size() == before + 3);
    CHECK(d->undo_label() == "Paste 2 entities");
    const world::Entity copy = w.find(pasted[0]);
    CHECK(w.name(copy) == "elder_2");
    CHECK(pasted[0] != elder);
    REQUIRE(w.children(copy).size() == 1);
    CHECK(w.name(w.children(copy).front()) == "held_lantern_2");
    CHECK(w.get<scene2d::Transform2D>(copy)->x ==
          w.get<scene2d::Transform2D>(w.find(elder))->x + 2.0);
    CHECK(w.components(copy).size() == w.components(w.find(elder)).size());
    // Pasted again: the next free number.
    CHECK(w.name(w.find(d->paste_entities(text, 0.0, 0.0, error).at(0))) == "elder_3");
    REQUIRE(d->undo());
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    CHECK(d->paste_entities("not entities", 0.0, 0.0, error).empty());
    CHECK(error == "There are no entities to paste.");

    // Duplicate: beside it, under the same parent, as one step.
    const std::vector<Guid> twins = d->duplicate({lantern}, error);
    REQUIRE(twins.size() == 1);
    CHECK(d->undo_label() == "Duplicate held_lantern");
    CHECK(w.parent(w.find(twins[0])) == w.find(elder));
    CHECK(w.get<scene2d::Transform2D>(w.find(twins[0]))->x == 1.0);
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    CHECK(d->duplicate({}, error).empty());
}
