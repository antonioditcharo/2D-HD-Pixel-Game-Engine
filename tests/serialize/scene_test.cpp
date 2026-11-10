#include "engine/serialize/json.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace serialize = engine::serialize;
namespace reflect = engine::reflect;
namespace world = engine::world;
using engine::Guid;

namespace {

enum class Kind : std::uint8_t { Prop, Npc, Light };

struct Transform {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Sprite {
    std::string image = "missing.png";
    Kind kind = Kind::Prop;
    std::int32_t layer = 0;
};

struct Fixture {
    Fixture() {
        registry.add_choice_list<Kind>("kind")
            .entry(Kind::Prop, "prop")
            .entry(Kind::Npc, "npc")
            .entry(Kind::Light, "light");
        auto transform = registry.add_type<Transform>("transform");
        transform.property<&Transform::x>("x");
        transform.property<&Transform::y>("y");
        transform.property<&Transform::z>("z");
        auto sprite = registry.add_type<Sprite>("sprite");
        sprite.property<&Sprite::image>("image");
        sprite.property<&Sprite::kind>("kind");
        sprite.property<&Sprite::layer>("layer").range(-10, 10);
        REQUIRE(registry.errors().empty());
    }

    /// A fresh world with the component types registered.
    [[nodiscard]] std::unique_ptr<world::World> make_world() const {
        auto w = std::make_unique<world::World>(registry);
        REQUIRE(w->register_component<Transform>());
        REQUIRE(w->register_component<Sprite>());
        return w;
    }

    reflect::Registry registry;
};

serialize::Json parse_ok(std::string_view text) {
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = serialize::parse(text, issues);
    REQUIRE(issues.empty());
    REQUIRE(json.has_value());
    return json.value_or(serialize::Json{});
}

const char* const guid_a = "aaaaaaaaaaaa4aaa8aaaaaaaaaaaaaaa";
const char* const guid_b = "bbbbbbbbbbbb4bbb8bbbbbbbbbbbbbbb";
const char* const guid_c = "cccccccccccc4ccc8ccccccccccccccc";

/// The GUID written as `text`, which must be valid.
Guid known(const char* text) {
    const std::optional<Guid> guid = Guid::parse(text);
    REQUIRE(guid.has_value());
    return guid.value_or(Guid{});
}

} // namespace

TEST_CASE("A scene round-trips: GUIDs, names, hierarchy order and components") {
    const Fixture f;
    const auto original = f.make_world();
    const world::Entity village = original->create("Village");
    const world::Entity elder = original->create("Elder");
    const world::Entity lamp = original->create("Lamp");
    const world::Entity well = original->create("Well");
    original->set_parent(elder, village);
    original->set_parent(lamp, village);
    original->set_parent(well, elder);
    original->add<Transform>(elder)->x = 4.0;
    Sprite* sprite = original->add<Sprite>(elder);
    sprite->image = "elder.dsprite";
    sprite->kind = Kind::Npc;
    original->add<Sprite>(lamp)->kind = Kind::Light;

    const std::string text = serialize::to_text(serialize::scene_to_json(*original));

    const auto copy = f.make_world();
    const serialize::SceneLoad loaded = serialize::scene_from_json(parse_ok(text), *copy);
    CHECK(loaded.issues.empty());
    REQUIRE(loaded.entities.size() == 4);
    const world::Entity elder_copy = copy->find(original->guid(elder));
    REQUIRE(elder_copy.is_valid());
    CHECK(copy->name(elder_copy) == "Elder");
    CHECK(copy->guid(copy->parent(elder_copy)) == original->guid(village));
    CHECK(copy->get<Transform>(elder_copy)->x == 4.0);
    CHECK(copy->get<Sprite>(elder_copy)->image == "elder.dsprite");
    CHECK(copy->get<Sprite>(elder_copy)->kind == Kind::Npc);
    CHECK(copy->get<Transform>(copy->find(original->guid(lamp))) == nullptr);

    // Children keep their order, and saving again gives the same bytes.
    const world::Entity village_copy = copy->find(original->guid(village));
    REQUIRE(copy->children(village_copy).size() == 2);
    CHECK(copy->guid(copy->children(village_copy)[0]) == original->guid(elder));
    CHECK(copy->guid(copy->children(village_copy)[1]) == original->guid(lamp));
    CHECK(serialize::to_text(serialize::scene_to_json(*copy)) == text);
}

TEST_CASE("The scene file format is readable") {
    const Fixture f;
    const auto w = f.make_world();
    const world::Entity parent = w->create(known(guid_a), "House");
    const world::Entity door = w->create(known(guid_b), "Door");
    w->set_parent(door, parent);
    w->add<Sprite>(door)->image = "door.dsprite";
    CHECK(serialize::to_text(serialize::scene_to_json(*w)) == std::string(R"({
  "entities": [
    {
      "guid": "aaaaaaaaaaaa4aaa8aaaaaaaaaaaaaaa",
      "name": "House"
    },
    {
      "components": {
        "sprite": {
          "image": "door.dsprite",
          "kind": "prop",
          "layer": 0
        }
      },
      "guid": "bbbbbbbbbbbb4bbb8bbbbbbbbbbbbbbb",
      "name": "Door",
      "parent": "aaaaaaaaaaaa4aaa8aaaaaaaaaaaaaaa"
    }
  ],
  "format": "sparxel.scene",
  "version": 1
}
)"));
}

TEST_CASE("Component problems are reported and the rest of the scene loads") {
    const Fixture f;
    const auto w = f.make_world();
    const std::string text = std::string(R"({"format": "sparxel.scene", "version": 1, "entities": [
        {"guid": ")") + guid_a +
                             R"(", "name": "Tree", "colour": "green",
         "components": {"sprite": {"layer": 99, "image": "tree.dsprite"}, "physics_body": {}}}
    ]})";
    const serialize::SceneLoad loaded = serialize::scene_from_json(parse_ok(text), *w);
    REQUIRE(loaded.entities.size() == 1);
    const std::vector<serialize::Issue> expected{
        {"/entities/0/colour", "Unknown entry 'colour'. It was ignored."},
        {"/entities/0/components/physics_body",
         "There is no component type called 'physics_body'. It was ignored."},
        {"/entities/0/components/sprite/layer",
         "'Layer' must be between -10 and 10, but is 99. It was not loaded."},
    };
    CHECK(loaded.issues == expected);
    const Sprite* sprite = w->get<Sprite>(loaded.entities[0]);
    REQUIRE(sprite != nullptr);
    CHECK(sprite->image == "tree.dsprite");
    CHECK(sprite->layer == 0);
}

TEST_CASE("A scene that cannot be built faithfully creates nothing") {
    const Fixture f;
    struct Case {
        std::string entities;
        std::vector<serialize::Issue> issues;
    };
    const std::string a = guid_a;
    const std::string b = guid_b;
    const std::string c = guid_c;
    const std::vector<Case> cases{
        {R"([{"name": "No GUID"}])",
         {{"/entities/0/guid",
           "Every entity needs a GUID of 32 hexadecimal digits, but this one is missing."}}},
        {R"([{"guid": "xyz"}])",
         {{"/entities/0/guid",
           "Every entity needs a GUID of 32 hexadecimal digits, but this one is 'xyz'."}}},
        {R"([{"guid": ")" + a + R"("}, {"guid": ")" + a + R"("}])",
         {{"/entities/1/guid", "The GUID " + a + " is used by an earlier entity in this scene."}}},
        {R"([{"guid": ")" + b + R"(", "parent": ")" + c + R"("}, {"guid": ")" + c + R"("}])",
         {{"/entities/0/parent",
           "The parent " + c + " is neither an earlier entity in this scene nor in the world."}}},
        {R"([{"guid": ")" + b + R"(", "parent": ")" + b + R"("}])",
         {{"/entities/0/parent",
           "The parent " + b + " is neither an earlier entity in this scene nor in the world."}}},
        {R"([{"guid": ")" + b + R"(", "name": 5, "components": []}])",
         {{"/entities/0/name", "A name must be text, but is 5."},
          {"/entities/0/components",
           "Components must be a group of named values, but are a list."}}},
        {R"([7])", {{"/entities/0", "An entity must be a group of named values, but is 7."}}},
    };
    for (const Case& test : cases) {
        CAPTURE(test.entities);
        const auto w = f.make_world();
        const serialize::SceneLoad loaded = serialize::scene_from_json(
            parse_ok(R"({"format": "sparxel.scene", "version": 1, "entities": )" + test.entities +
                     "}"),
            *w);
        CHECK(loaded.entities.empty());
        CHECK(loaded.issues == test.issues);
        CHECK(w->entity_count() == 0);
    }

    // A GUID that is already in the world is refused too.
    const auto w = f.make_world();
    w->create(known(guid_a), "Existing");
    const serialize::SceneLoad clash = serialize::scene_from_json(
        parse_ok(R"({"format": "sparxel.scene", "version": 1, "entities": [{"guid": ")" + a +
                 R"("}]})"),
        *w);
    CHECK(clash.entities.empty());
    REQUIRE(clash.issues.size() == 1);
    CHECK(clash.issues[0].message == "The GUID " + a + " is already used in the world.");
    CHECK(w->entity_count() == 1);

    // A scene can be loaded under an entity that is already in the world.
    const serialize::SceneLoad nested = serialize::scene_from_json(
        parse_ok(R"({"format": "sparxel.scene", "version": 1, "entities": [{"guid": ")" + b +
                 R"(", "parent": ")" + a + R"("}]})"),
        *w);
    CHECK(nested.issues.empty());
    REQUIRE(nested.entities.size() == 1);
    CHECK(w->parent(nested.entities[0]) == w->find(known(guid_a)));
}

TEST_CASE("Scene documents are checked for format and version") {
    const Fixture f;
    const auto w = f.make_world();
    struct Case {
        const char* json;
        const char* path;
        const char* message;
    };
    const std::vector<Case> cases{
        {"[]", "", "This is not a scene: it must be a group of named values (a JSON object)."},
        {R"({"format": "sparxel.logic", "version": 1})", "/format",
         "This is not a scene: its format is not 'sparxel.scene'."},
        {R"({"format": "sparxel.scene"})", "/version",
         "The format version is missing or not a whole number above 0."},
        {R"({"format": "sparxel.scene", "version": 9})", "/version",
         "This scene was saved by a newer version of Sparxel (format version 9). This version "
         "reads up to 1. Update Sparxel to open it."},
        {R"({"format": "sparxel.scene", "version": 1, "entities": {}})", "/entities",
         "This must be a list, but is a group of values."},
    };
    for (const Case& c : cases) {
        CAPTURE(c.json);
        const serialize::SceneLoad loaded = serialize::scene_from_json(parse_ok(c.json), *w);
        CHECK(loaded.entities.empty());
        REQUIRE(loaded.issues.size() == 1);
        CHECK(loaded.issues[0].path == c.path);
        CHECK(loaded.issues[0].message == c.message);
    }
    // An empty scene is fine.
    const serialize::SceneLoad empty =
        serialize::scene_from_json(parse_ok(R"({"format": "sparxel.scene", "version": 1})"), *w);
    CHECK(empty.issues.empty());
    CHECK(empty.entities.empty());
}

TEST_CASE("Deep hierarchies are saved and loaded without recursion") {
    const Fixture f;
    const auto w = f.make_world();
    world::Entity parent = w->create("root");
    for (int i = 0; i < 20'000; ++i) {
        const world::Entity next = w->create();
        w->set_parent(next, parent);
        parent = next;
    }
    const std::string text = serialize::to_text(serialize::scene_to_json(*w));
    const auto copy = f.make_world();
    const serialize::SceneLoad loaded = serialize::scene_from_json(parse_ok(text), *copy);
    CHECK(loaded.issues.empty());
    CHECK(copy->entity_count() == 20'001);
}
