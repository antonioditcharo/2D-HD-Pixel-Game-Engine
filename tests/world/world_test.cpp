#include "engine/world/world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace reflect = engine::reflect;
namespace world = engine::world;
using engine::Guid;

namespace {

struct Position {
    double x = 0.0;
    double y = 0.0;
};

/// Has a std::string, so the copy and move hooks must be right for it to survive.
struct Label {
    std::string text = "unnamed";
    std::int32_t size = 12;
};

struct NotAComponent {
    int value = 0;
};

struct Fixture {
    Fixture() {
        auto position = registry.add_type<Position>("position");
        position.property<&Position::x>("x");
        position.property<&Position::y>("y");
        auto label = registry.add_type<Label>("label");
        label.property<&Label::text>("text");
        label.property<&Label::size>("size");
        registry.add_type<NotAComponent>("not_a_component")
            .property<&NotAComponent::value>("value");
        REQUIRE(registry.errors().empty());
        REQUIRE(w.register_component<Position>());
        REQUIRE(w.register_component<Label>());
        position_type = registry.find_type<Position>();
        label_type = registry.find_type<Label>();
    }

    reflect::Registry registry;
    world::World w{registry};
    const reflect::TypeInfo* position_type{nullptr};
    const reflect::TypeInfo* label_type{nullptr};
};

} // namespace

TEST_CASE("GUIDs are random, valid and round-trip through text") {
    std::set<Guid> seen;
    for (int i = 0; i < 1000; ++i) {
        const Guid guid = Guid::generate();
        CHECK(guid.is_valid());
        CHECK(seen.insert(guid).second);
        CHECK(Guid::parse(guid.to_string()) == std::optional<Guid>(guid));
        CHECK(guid.to_string()[12] == '4'); // version 4
    }
    const std::optional<Guid> parsed = Guid::parse("0123456789ABCDEFfedcba9876543210");
    REQUIRE(parsed.has_value());
    const Guid value = parsed.value_or(Guid{});
    CHECK(value.high == 0x0123456789ABCDEFULL);
    CHECK(value.low == 0xFEDCBA9876543210ULL);
    CHECK(value.to_string() == "0123456789abcdeffedcba9876543210");
    CHECK_FALSE(Guid::parse("").has_value());
    CHECK_FALSE(Guid::parse("0123456789abcdef0123456789abcde").has_value());  // 31 digits
    CHECK_FALSE(Guid::parse("0123456789abcdef0123456789abcdeg").has_value()); // not hex
    CHECK_FALSE(Guid::parse("00000000000000000000000000000000").has_value()); // zero
}

TEST_CASE("Entities have GUIDs and names") {
    Fixture f;
    const world::Entity hero = f.w.create("Hero");
    CHECK(f.w.is_alive(hero));
    CHECK(f.w.name(hero) == "Hero");
    CHECK(f.w.guid(hero).is_valid());
    CHECK(f.w.find(f.w.guid(hero)) == hero);
    CHECK(f.w.set_name(hero, "Hero 2"));
    CHECK(f.w.name(hero) == "Hero 2");

    const Guid fixed = Guid::generate();
    const world::Entity loaded = f.w.create(fixed, "Loaded");
    CHECK(f.w.guid(loaded) == fixed);
    CHECK_FALSE(f.w.create(fixed, "Duplicate").is_valid());
    CHECK_FALSE(f.w.create(Guid{}, "Zero").is_valid());
    CHECK(f.w.entity_count() == 2);
}

TEST_CASE("Components are added, read, changed and removed by type") {
    Fixture f;
    const world::Entity sign = f.w.create("Sign");
    Label* label = f.w.add<Label>(sign);
    REQUIRE(label != nullptr);
    CHECK(label->text == "unnamed");
    label->text = "Welcome to Brightwater, a village with a rather long name";

    const reflect::ObjectRef position = f.w.add(sign, *f.position_type);
    REQUIRE(position.as<Position>() != nullptr);
    position.as<Position>()->x = 3.5;

    // Adding a component moved the label to new storage: its text must have survived.
    REQUIRE(f.w.get<Label>(sign) != nullptr);
    CHECK(f.w.get<Label>(sign)->text ==
          "Welcome to Brightwater, a village with a rather long name");
    CHECK(f.w.get<Position>(sign)->x == 3.5);
    CHECK(f.w.components(sign) ==
          std::vector<const reflect::TypeInfo*>{f.position_type, f.label_type});

    // Adding again returns the existing component.
    CHECK(f.w.add<Position>(sign)->x == 3.5);

    CHECK(f.w.remove(sign, *f.position_type));
    CHECK_FALSE(f.w.remove(sign, *f.position_type));
    CHECK(f.w.get<Position>(sign) == nullptr);
    CHECK(f.w.get<Label>(sign)->text ==
          "Welcome to Brightwater, a village with a rather long name");

    // Types that are not components are refused.
    const reflect::TypeInfo* other = f.registry.find_type<NotAComponent>();
    REQUIRE(other != nullptr);
    CHECK_FALSE(f.w.is_component(*other));
    CHECK(f.w.add(sign, *other).is_null());
    CHECK(f.w.add<NotAComponent>(sign) == nullptr);
    CHECK(f.w.component_types() ==
          std::vector<const reflect::TypeInfo*>{f.position_type, f.label_type});
}

TEST_CASE("Many entities keep their component values through storage changes") {
    Fixture f;
    std::vector<world::Entity> entities;
    for (int i = 0; i < 500; ++i) {
        const world::Entity e = f.w.create();
        f.w.add<Label>(e)->text =
            "label number " + std::to_string(i) + " with enough text to allocate";
        if (i % 2 == 0) {
            f.w.add<Position>(e)->y = i;
        }
        entities.push_back(e);
    }
    for (std::size_t i = 0; i < entities.size(); i += 3) {
        f.w.destroy(entities[i]);
    }
    int labels = 0;
    f.w.each(*f.label_type, [&](world::Entity e, reflect::ObjectRef ref) {
        ++labels;
        const auto index = static_cast<std::size_t>(std::find(entities.begin(), entities.end(), e) -
                                                    entities.begin());
        CHECK(ref.as<Label>()->text ==
              "label number " + std::to_string(index) + " with enough text to allocate");
    });
    CHECK(labels == 500 - 167);
    int positions = 0;
    f.w.each(*f.position_type, [&](world::Entity, reflect::ObjectRef) { ++positions; });
    CHECK(positions == 250 - 84); // even indices that are not multiples of 3
}

TEST_CASE("The hierarchy keeps order and refuses cycles") {
    Fixture f;
    const world::Entity village = f.w.create("Village");
    const world::Entity house = f.w.create("House");
    const world::Entity door = f.w.create("Door");
    const world::Entity well = f.w.create("Well");
    CHECK(f.w.set_parent(house, village));
    CHECK(f.w.set_parent(well, village));
    CHECK(f.w.set_parent(door, house));
    CHECK(f.w.children(village) == std::vector<world::Entity>{house, well});
    CHECK(f.w.parent(door) == house);
    CHECK(f.w.roots() == std::vector<world::Entity>{village});

    CHECK_FALSE(f.w.set_parent(village, door)); // would be a cycle
    CHECK_FALSE(f.w.set_parent(house, house));
    CHECK(f.w.parent(village) == world::Entity{});

    // Moving keeps the hierarchy consistent.
    CHECK(f.w.set_parent(well, house));
    CHECK(f.w.children(village) == std::vector<world::Entity>{house});
    CHECK(f.w.children(house) == std::vector<world::Entity>{door, well});
    CHECK(f.w.set_parent(house, world::Entity{}));
    CHECK(f.w.roots() == std::vector<world::Entity>{village, house});
}

TEST_CASE("Destroying an entity destroys its descendants, and old handles stay invalid") {
    Fixture f;
    const world::Entity village = f.w.create("Village");
    const world::Entity house = f.w.create("House");
    const world::Entity door = f.w.create("Door");
    const world::Entity tree = f.w.create("Tree");
    f.w.set_parent(house, village);
    f.w.set_parent(door, house);
    f.w.add<Label>(door);
    const Guid door_guid = f.w.guid(door);

    CHECK(f.w.destroy(house));
    CHECK_FALSE(f.w.is_alive(house));
    CHECK_FALSE(f.w.is_alive(door));
    CHECK(f.w.is_alive(village));
    CHECK(f.w.children(village).empty());
    CHECK_FALSE(f.w.find(door_guid).is_valid());
    CHECK(f.w.entity_count() == 2);
    CHECK_FALSE(f.w.destroy(house));

    // New entities may reuse storage, but old handles must not come back to life.
    for (int i = 0; i < 10; ++i) {
        f.w.create();
    }
    CHECK_FALSE(f.w.is_alive(door));
    CHECK(f.w.get<Label>(door) == nullptr);
    CHECK(f.w.add<Label>(door) == nullptr);
    CHECK_FALSE(f.w.set_parent(door, tree));
    CHECK(f.w.name(door).empty());

    // Handles that never existed are refused too.
    CHECK_FALSE(f.w.is_alive(world::Entity{}));
    CHECK_FALSE(f.w.is_alive(world::Entity{0xDEADBEEFULL}));
    CHECK(f.w.add<Label>(world::Entity{0xDEADBEEFULL}) == nullptr);
}
