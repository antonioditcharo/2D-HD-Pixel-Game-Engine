// The editor's model, on a copy of the sample: opening, editing through reflection with
// undo and redo, saving, picking, and the preview the scene view shows.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "editor/core/document.h"
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
