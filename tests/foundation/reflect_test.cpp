#include "engine/foundation/reflect.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

namespace reflect = engine::reflect;

namespace {

enum class Facing : std::uint8_t { North, East, South, West };

// A stand-in for an engine component, with one member of every supported kind.
struct TestLight {
    bool enabled = true;
    std::int32_t priority = 2;
    std::int8_t flicker_steps = 0;
    float radius = 4.0F;
    double intensity = 1.0;
    std::string label = "Torch";
    Facing facing = Facing::South;
    float internal_cache = 0.0F;
};

struct OtherType {
    float radius = 1.0F;
};

void register_test_types(reflect::Registry& registry) {
    registry.add_choice_list<Facing>("facing")
        .display_name("Facing")
        .entry(Facing::North, "north")
        .entry(Facing::East, "east")
        .entry(Facing::South, "south")
        .entry(Facing::West, "west", "West side", "Faces the setting sun.");

    registry.add_type<TestLight>("test_light")
        .display_name("Light")
        .category("Lighting")
        .tooltip("Lights up the scene around it.")
        .property<&TestLight::enabled>("enabled")
        .tooltip("Turns the light on or off.")
        .property<&TestLight::priority>("priority")
        .range(0, 10)
        .flags(reflect::property_flag::advanced)
        .property<&TestLight::flicker_steps>("flicker_steps")
        .property<&TestLight::radius>("radius")
        .unit(reflect::Unit::Tiles)
        .range(0.0, 64.0)
        .step(0.5)
        .property<&TestLight::intensity>("intensity")
        .range(0.0, 10.0)
        .property<&TestLight::label>("label")
        .flags(reflect::property_flag::saved)
        .property<&TestLight::facing>("facing")
        .property<&TestLight::internal_cache>("internal_cache")
        .flags(reflect::property_flag::read_only | reflect::property_flag::hidden);

    registry.add_type<OtherType>("other_type").property<&OtherType::radius>("radius");
}

} // namespace

TEST_CASE("type ids are a stable FNV-1a hash of the registered name") {
    // Published FNV-1a 64-bit test vector for "a".
    CHECK(reflect::type_id_from_name("a").value == 0xaf63dc4c8601ec8cULL);
    CHECK(reflect::type_id_from_name("light") == reflect::type_id_from_name("light"));
    CHECK(reflect::type_id_from_name("light") != reflect::type_id_from_name("lights"));
    static_assert(reflect::type_id_from_name("a").is_valid());
}

TEST_CASE("ids are lowercase letters, digits and underscores starting with a letter") {
    CHECK(reflect::is_valid_id("radius"));
    CHECK(reflect::is_valid_id("radius_tiles_2"));
    CHECK_FALSE(reflect::is_valid_id(""));
    CHECK_FALSE(reflect::is_valid_id("Radius"));
    CHECK_FALSE(reflect::is_valid_id("2radius"));
    CHECK_FALSE(reflect::is_valid_id("_radius"));
    CHECK_FALSE(reflect::is_valid_id("radius-tiles"));
    CHECK_FALSE(reflect::is_valid_id("radius tiles"));
}

TEST_CASE("display names default to a readable form of the id") {
    CHECK(reflect::display_name_from_id("radius_tiles") == "Radius tiles");
    CHECK(reflect::display_name_from_id("hp") == "Hp");
    CHECK(reflect::display_name_from_id("max__hp") == "Max hp");
    CHECK(reflect::display_name_from_id("") == "");
}

TEST_CASE("kinds and units have plain-language names") {
    CHECK(reflect::kind_display_name(reflect::ValueKind::Bool) == "Yes/No");
    CHECK(reflect::kind_display_name(reflect::ValueKind::Int) == "Whole number");
    CHECK(reflect::kind_display_name(reflect::ValueKind::Float) == "Number");
    CHECK(reflect::kind_display_name(reflect::ValueKind::Text) == "Text");
    CHECK(reflect::kind_display_name(reflect::ValueKind::Choice) == "Choice");
    CHECK(reflect::unit_symbol(reflect::Unit::None).empty());
    CHECK(reflect::unit_symbol(reflect::Unit::Seconds) == "s");
    CHECK(reflect::unit_symbol(reflect::Unit::Tiles) == "tiles");
}

TEST_CASE("a registered type exposes its properties, metadata and defaults") {
    reflect::Registry registry;
    register_test_types(registry);
    REQUIRE(registry.errors().empty());

    const reflect::TypeInfo* light = registry.find_type<TestLight>();
    REQUIRE(light != nullptr);
    CHECK(light->name == "test_light");
    CHECK(light->id == reflect::type_id_from_name("test_light"));
    CHECK(light->meta.display_name == "Light");
    CHECK(light->meta.category == "Lighting");
    CHECK(registry.find_type("test_light") == light);
    CHECK(registry.find_type(light->id) == light);
    CHECK(registry.find_type("missing") == nullptr);

    REQUIRE(light->properties.size() == 8);
    CHECK(light->properties[0].id == "enabled"); // registration order is kept

    const reflect::PropertyInfo* radius = light->find_property("radius");
    REQUIRE(radius != nullptr);
    CHECK(radius->kind == reflect::ValueKind::Float);
    CHECK(radius->meta.display_name == "Radius");
    CHECK(radius->unit == reflect::Unit::Tiles);
    CHECK(radius->limits.min == 0.0);
    CHECK(radius->limits.max == 64.0);
    CHECK(radius->limits.step == 0.5);
    CHECK(std::get<double>(radius->default_value) == doctest::Approx(4.0));

    const reflect::PropertyInfo* priority = light->find_property("priority");
    REQUIRE(priority != nullptr);
    CHECK(priority->kind == reflect::ValueKind::Int);
    CHECK(reflect::has_flag(priority->flags, reflect::property_flag::advanced));
    CHECK(std::get<std::int64_t>(priority->default_value) == 2);

    const reflect::PropertyInfo* label = light->find_property("label");
    REQUIRE(label != nullptr);
    CHECK(label->kind == reflect::ValueKind::Text);
    CHECK(std::get<std::string>(label->default_value) == "Torch");

    const reflect::PropertyInfo* facing = light->find_property("facing");
    REQUIRE(facing != nullptr);
    CHECK(facing->kind == reflect::ValueKind::Choice);
    REQUIRE(facing->choice == registry.find_choice_list<Facing>());
    CHECK(facing->choice->entries.size() == 4);
    CHECK(facing->choice->find_entry("west")->meta.display_name == "West side");
    CHECK(facing->choice->find_entry("north")->meta.display_name == "North");
    CHECK(std::get<std::int64_t>(facing->default_value) ==
          static_cast<std::int64_t>(Facing::South));

    const reflect::PropertyInfo* cache = light->find_property("internal_cache");
    REQUIRE(cache != nullptr);
    CHECK(reflect::has_flag(cache->flags, reflect::property_flag::read_only));
    CHECK(reflect::has_flag(cache->flags, reflect::property_flag::hidden));
    CHECK_FALSE(reflect::has_flag(cache->flags, reflect::property_flag::saved));

    CHECK(registry.types().size() == 2);
    CHECK(registry.choice_lists().size() == 1);
}

TEST_CASE("properties can be read and written through the registry") {
    reflect::Registry registry;
    register_test_types(registry);
    const reflect::TypeInfo* light = registry.find_type<TestLight>();
    REQUIRE(light != nullptr);
    TestLight object;

    const auto set = [&](std::string_view id, const reflect::Value& value) {
        const reflect::PropertyInfo* property = light->find_property(id);
        REQUIRE(property != nullptr);
        return reflect::set_property(object, *property, value);
    };
    const auto get = [&](std::string_view id) {
        const reflect::PropertyInfo* property = light->find_property(id);
        REQUIRE(property != nullptr);
        const std::optional<reflect::Value> value = reflect::get_property(object, *property);
        REQUIRE(value.has_value());
        return *value;
    };

    SUBCASE("every kind round-trips") {
        CHECK(set("enabled", false) == reflect::SetResult::Ok);
        CHECK(set("priority", std::int64_t{7}) == reflect::SetResult::Ok);
        CHECK(set("radius", 12.5) == reflect::SetResult::Ok);
        CHECK(set("intensity", 2.25) == reflect::SetResult::Ok);
        CHECK(set("label", std::string("Lantern")) == reflect::SetResult::Ok);
        CHECK(set("facing", static_cast<std::int64_t>(Facing::West)) == reflect::SetResult::Ok);

        CHECK(object.enabled == false);
        CHECK(object.priority == 7);
        CHECK(object.radius == doctest::Approx(12.5F));
        CHECK(object.intensity == doctest::Approx(2.25));
        CHECK(object.label == "Lantern");
        CHECK(object.facing == Facing::West);

        CHECK(std::get<bool>(get("enabled")) == false);
        CHECK(std::get<std::int64_t>(get("priority")) == 7);
        CHECK(std::get<std::string>(get("label")) == "Lantern");
    }

    SUBCASE("values outside the range are rejected and leave the object unchanged") {
        CHECK(set("priority", std::int64_t{11}) == reflect::SetResult::OutOfRange);
        CHECK(set("radius", -1.0) == reflect::SetResult::OutOfRange);
        CHECK(object.priority == 2);
        CHECK(object.radius == doctest::Approx(4.0F));
        CHECK(set("radius", 64.0) == reflect::SetResult::Ok); // bounds are inclusive
    }

    SUBCASE("a value of the wrong kind is rejected") {
        CHECK(set("radius", std::int64_t{3}) == reflect::SetResult::WrongKind);
        CHECK(set("enabled", std::string("yes")) == reflect::SetResult::WrongKind);
        CHECK(set("facing", 1.0) == reflect::SetResult::WrongKind);
    }

    SUBCASE("a choice must be one of the list's entries") {
        CHECK(set("facing", std::int64_t{42}) == reflect::SetResult::UnknownChoice);
        CHECK(object.facing == Facing::South);
    }

    SUBCASE("read-only properties cannot be set") {
        CHECK(set("internal_cache", 1.0) == reflect::SetResult::ReadOnly);
    }

    SUBCASE("values that do not fit the member's C++ type are rejected") {
        // flicker_steps is an int8 with no declared range.
        CHECK(set("flicker_steps", std::int64_t{1000}) == reflect::SetResult::NotRepresentable);
        CHECK(set("flicker_steps", std::int64_t{-128}) == reflect::SetResult::Ok);
        CHECK(object.flicker_steps == -128);
    }

    SUBCASE("a property cannot be used on an object of another type") {
        OtherType other;
        const reflect::PropertyInfo* radius = light->find_property("radius");
        REQUIRE(radius != nullptr);
        CHECK(reflect::set_property(other, *radius, 1.0) == reflect::SetResult::WrongType);
        CHECK_FALSE(reflect::get_property(other, *radius).has_value());
    }
}

namespace {

struct BadIds {
    float value = 0.0F;
    float other = 0.0F;
};
struct Duplicate {
    float value = 0.0F;
};
struct BadRange {
    float value = 5.0F;
    std::string text;
};
enum class Unregistered : std::uint8_t { A, B };
struct UsesUnregistered {
    Unregistered choice = Unregistered::A;
};
enum class Repeated : std::uint8_t { A, B };

[[nodiscard]] bool has_error(const reflect::Registry& registry, reflect::ErrorCode code) {
    for (const reflect::RegistrationError& error : registry.errors()) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("registration problems are collected, not thrown") {
    reflect::Registry registry;

    SUBCASE("invalid ids") {
        registry.add_type<BadIds>("Bad Ids");
        CHECK(has_error(registry, reflect::ErrorCode::InvalidId));
        CHECK(registry.find_type<BadIds>() == nullptr);

        reflect::Registry second;
        second.add_type<BadIds>("bad_ids").property<&BadIds::value>("Value");
        CHECK(has_error(second, reflect::ErrorCode::InvalidId));
        CHECK(second.find_type<BadIds>()->properties.empty());
    }

    SUBCASE("a name or C++ type registered twice") {
        registry.add_type<Duplicate>("duplicate");
        registry.add_type<BadIds>("duplicate");
        CHECK(has_error(registry, reflect::ErrorCode::DuplicateType));

        reflect::Registry second;
        second.add_type<Duplicate>("first_name");
        second.add_type<Duplicate>("second_name");
        CHECK(has_error(second, reflect::ErrorCode::DuplicateType));
        CHECK(second.find_type("second_name") == nullptr);
    }

    SUBCASE("two properties with the same id") {
        registry.add_type<BadIds>("bad_ids")
            .property<&BadIds::value>("value")
            .property<&BadIds::other>("value");
        CHECK(has_error(registry, reflect::ErrorCode::DuplicateProperty));
        CHECK(registry.find_type<BadIds>()->properties.size() == 1);
    }

    SUBCASE("invalid ranges") {
        registry.add_type<BadRange>("bad_range")
            .property<&BadRange::value>("value")
            .range(10.0, 1.0);
        CHECK(has_error(registry, reflect::ErrorCode::InvalidRange));

        reflect::Registry second;
        second.add_type<BadRange>("bad_range").property<&BadRange::text>("text").range(0.0, 1.0);
        CHECK(has_error(second, reflect::ErrorCode::InvalidRange));

        reflect::Registry third;
        third.add_type<BadRange>("bad_range").property<&BadRange::value>("value").range(0.0, 1.0);
        CHECK(has_error(third, reflect::ErrorCode::DefaultOutOfRange));
    }

    SUBCASE("an enum used before it is registered") {
        registry.add_type<UsesUnregistered>("uses_unregistered")
            .property<&UsesUnregistered::choice>("choice");
        CHECK(has_error(registry, reflect::ErrorCode::UnknownChoiceList));
    }

    SUBCASE("a choice list that repeats an id or a value") {
        registry.add_choice_list<Repeated>("repeated")
            .entry(Repeated::A, "a")
            .entry(Repeated::B, "a")
            .entry(Repeated::A, "b");
        CHECK(has_error(registry, reflect::ErrorCode::DuplicateChoice));
        CHECK(registry.find_choice_list<Repeated>()->entries.size() == 1);
    }
}
