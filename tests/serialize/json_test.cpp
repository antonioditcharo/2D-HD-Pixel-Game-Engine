#include "engine/logic/compiler.h"
#include "engine/serialize/json.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace serialize = engine::serialize;
namespace reflect = engine::reflect;
namespace logic = engine::logic;
namespace ir = engine::logic::ir;

namespace {

enum class Facing : std::uint8_t { North, East, South, West };

struct Lamp {
    bool lit = true;
    std::int32_t brightness = 50;
    double radius = 4.5;
    std::string label = "Lamp";
    Facing facing = Facing::South;
    std::int8_t small = 0;
    std::int32_t serial = 7; // read-only: set by the game
};

void register_lamp(reflect::Registry& registry) {
    registry.add_choice_list<Facing>("facing")
        .entry(Facing::North, "north")
        .entry(Facing::East, "east")
        .entry(Facing::South, "south")
        .entry(Facing::West, "west");
    auto lamp = registry.add_type<Lamp>("lamp");
    lamp.property<&Lamp::lit>("lit");
    lamp.property<&Lamp::brightness>("brightness").range(0, 100);
    lamp.property<&Lamp::radius>("radius");
    lamp.property<&Lamp::label>("label");
    lamp.property<&Lamp::facing>("facing");
    lamp.property<&Lamp::small>("small");
    lamp.property<&Lamp::serial>("serial").flags(reflect::property_flag::read_only);
}

struct Fixture {
    Fixture() {
        register_lamp(registry);
        REQUIRE(registry.errors().empty());
        type = registry.find_type<Lamp>();
        REQUIRE(type != nullptr);
    }

    reflect::Registry registry;
    const reflect::TypeInfo* type{nullptr};
};

serialize::Json parse_ok(std::string_view text) {
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = serialize::parse(text, issues);
    REQUIRE(issues.empty());
    REQUIRE(json.has_value());
    return json.value_or(serialize::Json{});
}

/// A logic asset that uses every kind of block.
logic::LogicAsset sample_asset() {
    using logic::ArithmeticOp;
    using logic::CompareOp;
    logic::LogicAsset asset;
    asset.name = "maps/village/rules";
    asset.variables.push_back(logic::Variable{"count", reflect::ValueKind::Int, "", std::nullopt});
    asset.variables.push_back(
        logic::Variable{"speed", reflect::ValueKind::Float, "", reflect::Value{1.5}});
    asset.variables.push_back(logic::Variable{"heading", reflect::ValueKind::Choice, "facing",
                                              reflect::Value{std::string("east")}});
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1},
        "intro",
        {
            ir::log(2, ir::literal(reflect::Value{std::string("caf\xC3\xA9 \"quoted\"\n")})),
            ir::wait(3, ir::literal(reflect::Value{2.0})),
            ir::wait_until(40, ir::variable("flag")),
            ir::if_then(
                4,
                ir::with_id(
                    ir::all({ir::literal(reflect::Value{true}), ir::negate(ir::variable("flag"))}),
                    5),
                {ir::set_variable(6, "count",
                                  ir::arithmetic(ArithmeticOp::Divide, ir::variable("count"),
                                                 ir::literal(reflect::Value{std::int64_t{-3}})))},
                {ir::repeat(7, ir::literal(reflect::Value{std::int64_t{2}}),
                            {ir::run(8, "outro")})}),
            ir::set(9, "lamp", "facing", ir::literal(reflect::Value{std::string("north")})),
            ir::do_action(10, "lamp", "toggle",
                          {ir::property("lamp", "brightness"), ir::call("lamp", "is_on")}),
        }});
    asset.sequences.push_back(logic::Sequence{logic::BlockId{11}, "outro", {}});
    asset.rules.push_back(
        logic::Rule{logic::BlockId{20},
                    "lamp",
                    "switched",
                    {ir::compare(CompareOp::GreaterEqual, ir::event_value("level"),
                                 ir::literal(reflect::Value{std::int64_t{3}})),
                     ir::any({ir::compare(CompareOp::NotEqual, ir::variable("count"),
                                          ir::literal(reflect::Value{0.25})),
                              ir::literal(reflect::Value{false})})},
                    {ir::log(21, ir::event_value("level"))},
                    {ir::log(22, ir::literal(reflect::Value{std::string("too dim")}))}});
    asset.rules.push_back(
        logic::Rule{logic::BlockId{23}, "", "game_start", {}, {ir::run(24, "intro")}, {}});
    logic::Brain brain;
    brain.id = logic::BlockId{30};
    brain.name = "mood";
    brain.initial = "calm";
    logic::BrainState calm;
    calm.id = logic::BlockId{31};
    calm.name = "calm";
    calm.on_enter = {ir::log(32, ir::literal(reflect::Value{std::string("calm")}))};
    calm.transitions.push_back(
        logic::Transition{logic::BlockId{33},
                          "busy",
                          {ir::compare(CompareOp::Greater, ir::variable("count"),
                                       ir::literal(reflect::Value{std::int64_t{2}}))},
                          1.5});
    logic::BrainState busy;
    busy.id = logic::BlockId{34};
    busy.name = "busy";
    busy.while_in = {
        ir::set_variable(35, "count",
                         ir::arithmetic(ArithmeticOp::Subtract, ir::variable("count"),
                                        ir::literal(reflect::Value{std::int64_t{1}})))};
    busy.on_exit = {ir::log(36, ir::literal(reflect::Value{std::string("done")}))};
    busy.transitions.push_back(logic::Transition{logic::BlockId{37}, "calm", {}, 0.0});
    brain.states = {calm, busy};
    brain.from_any.push_back(
        logic::Transition{logic::BlockId{38}, "calm", {ir::literal(reflect::Value{false})}, 0.0});
    asset.brains.push_back(brain);
    return asset;
}

} // namespace

TEST_CASE("Objects are written with sorted keys, choices as ids, and no read-only properties") {
    const Fixture f;
    Lamp lamp;
    lamp.facing = Facing::West;
    const serialize::Json json = serialize::object_to_json(reflect::ObjectRef::of(lamp), *f.type);
    CHECK(serialize::to_text(json) == "{\n"
                                      "  \"brightness\": 50,\n"
                                      "  \"facing\": \"west\",\n"
                                      "  \"label\": \"Lamp\",\n"
                                      "  \"lit\": true,\n"
                                      "  \"radius\": 4.5,\n"
                                      "  \"small\": 0\n"
                                      "}\n");
}

TEST_CASE("Objects round-trip through text") {
    const Fixture f;
    Lamp original;
    original.lit = false;
    original.brightness = 99;
    original.radius = 0.1;
    original.label = "Gate \"lamp\" \xE2\x9C\xA8";
    original.facing = Facing::North;
    original.small = -128;
    const std::string text =
        serialize::to_text(serialize::object_to_json(reflect::ObjectRef::of(original), *f.type));

    Lamp copy;
    const std::vector<serialize::Issue> issues =
        serialize::object_from_json(parse_ok(text), reflect::ObjectRef::of(copy), *f.type);
    CHECK(issues.empty());
    CHECK(copy.lit == original.lit);
    CHECK(copy.brightness == original.brightness);
    CHECK(copy.radius == original.radius); // exact: the shortest form reads back exactly
    CHECK(copy.label == original.label);
    CHECK(copy.facing == original.facing);
    CHECK(copy.small == original.small);
    // Saving again gives the same bytes.
    CHECK(serialize::to_text(serialize::object_to_json(reflect::ObjectRef::of(copy), *f.type)) ==
          text);
}

TEST_CASE("Problems in object data are reported, and those properties keep their values") {
    const Fixture f;
    Lamp lamp;
    const serialize::Json json = parse_ok(R"({
        "brightness": 101,
        "facing": "up",
        "label": 5,
        "lit": "yes",
        "radius": 2,
        "serial": 1,
        "small": 300,
        "colour/name~x": "red"
    })");
    const std::vector<serialize::Issue> issues =
        serialize::object_from_json(json, reflect::ObjectRef::of(lamp), *f.type, "/objects/0");
    const std::vector<serialize::Issue> expected{
        {"/objects/0/brightness",
         "'Brightness' must be between 0 and 100, but is 101. It was not loaded."},
        {"/objects/0/colour~1name~0x",
         "'Lamp' has no property called 'colour/name~x'. It was ignored."},
        {"/objects/0/facing",
         "'Facing' must be one of: north, east, south, west, but is 'up'. It was not loaded."},
        {"/objects/0/label", "'Label' must be text, but is 5. It was not loaded."},
        {"/objects/0/lit",
         "'Lit' must be yes or no (true or false), but is 'yes'. It was not loaded."},
        {"/objects/0/serial", "'Serial' cannot be changed, so it was not loaded."},
        {"/objects/0/small",
         "'Small' cannot hold 300: it is too large or too small. It was not loaded."},
    };
    CHECK(issues == expected);
    // Only "radius" was valid; a whole number is accepted for a number property.
    CHECK(lamp.radius == 2.0);
    CHECK(lamp.brightness == 50);
    CHECK(lamp.facing == Facing::South);
    CHECK(lamp.label == "Lamp");
    CHECK(lamp.lit);
    CHECK(lamp.serial == 7);
    CHECK(lamp.small == 0);

    // Whole numbers may be written with ".0", but not with a fraction.
    const std::vector<serialize::Issue> whole = serialize::object_from_json(
        parse_ok(R"({"brightness": 40.0})"), reflect::ObjectRef::of(lamp), *f.type);
    CHECK(whole.empty());
    CHECK(lamp.brightness == 40);
    const std::vector<serialize::Issue> fraction = serialize::object_from_json(
        parse_ok(R"({"brightness": 40.5})"), reflect::ObjectRef::of(lamp), *f.type);
    REQUIRE(fraction.size() == 1);
    CHECK(fraction[0].message ==
          "'Brightness' must be a whole number, but is 40.5. It was not loaded.");

    const std::vector<serialize::Issue> not_object =
        serialize::object_from_json(parse_ok("[1, 2]"), reflect::ObjectRef::of(lamp), *f.type);
    REQUIRE(not_object.size() == 1);
    CHECK(not_object[0].message ==
          "The properties of 'Lamp' must be a group of named values, but are a list.");
}

TEST_CASE("Logic assets round-trip exactly and deterministically") {
    const logic::LogicAsset asset = sample_asset();
    const std::string text = serialize::to_text(serialize::logic_to_json(asset));
    const serialize::LogicLoad loaded = serialize::logic_from_json(parse_ok(text));
    CHECK(loaded.issues.empty());
    REQUIRE(loaded.asset.has_value());
    CHECK(loaded.asset == std::optional<logic::LogicAsset>(asset));
    CHECK(serialize::to_text(
              serialize::logic_to_json(loaded.asset.value_or(logic::LogicAsset{}))) == text);
    // Whole numbers and numbers stay distinct in the file.
    CHECK(text.find("\"value\": 2.0") != std::string::npos);
    CHECK(text.find("\"value\": -3") != std::string::npos);
    // Brains: states with enter, while and exit steps, transitions with "after" seconds.
    CHECK(text.find("\"brains\"") != std::string::npos);
    CHECK(text.find("\"after\": 1.5") != std::string::npos);
    CHECK(text.find("\"from_any\"") != std::string::npos);
    CHECK(text.find("\"while\"") != std::string::npos);

    // A malformed brain is reported where it is.
    serialize::Json broken = serialize::logic_to_json(asset);
    broken["brains"][0]["states"][0]["go"][0]["after"] = "soon";
    broken["brains"][0]["states"][1]["sleep"] = true;
    const serialize::LogicLoad bad = serialize::logic_from_json(broken);
    REQUIRE(bad.issues.size() == 2);
    CHECK(bad.issues[0].path == "/brains/0/states/0/go/0/after");
    CHECK(bad.issues[0].message == "This must be a number of seconds, but is 'soon'.");
    CHECK(bad.issues[1].path == "/brains/0/states/1/sleep");
}

TEST_CASE("The logic file format is readable") {
    logic::LogicAsset asset;
    asset.name = "chest";
    asset.rules.push_back(logic::Rule{
        logic::BlockId{1},
        "chest",
        "opened",
        {ir::call("player", "has_item", {ir::literal(reflect::Value{std::string("key")})})},
        {ir::do_action(2, "chest", "unlock")},
        {}});
    CHECK(serialize::to_text(serialize::logic_to_json(asset)) == R"({
  "format": "sparxel.logic",
  "name": "chest",
  "rules": [
    {
      "block": 1,
      "do": [
        {
          "block": 2,
          "kind": "do",
          "member": "unlock",
          "object": "chest"
        }
      ],
      "event": "opened",
      "if": [
        {
          "kind": "call",
          "member": "has_item",
          "object": "player",
          "operands": [
            {
              "kind": "literal",
              "value": "key"
            }
          ]
        }
      ],
      "object": "chest"
    }
  ],
  "version": 1
}
)");
}

TEST_CASE("Invalid logic documents are reported with their place") {
    struct Case {
        const char* json;
        bool loads;
        std::vector<serialize::Issue> issues;
    };
    const std::vector<Case> cases{
        {"[]",
         false,
         {{"", "This is not a logic asset: it must be a group of named values (a JSON object)."}}},
        {R"({"format": "sparxel.scene", "version": 1})",
         false,
         {{"/format", "This is not a logic asset: its format is not 'sparxel.logic'."}}},
        {R"({"format": "sparxel.logic", "version": "1"})",
         false,
         {{"/version", "The format version is missing or not a whole number above 0."}}},
        {R"({"format": "sparxel.logic", "version": 2, "name": "x"})",
         false,
         {{"/version",
           "This logic asset was saved by a newer version of Sparxel (format version 2). "
           "This version reads up to 1. Update Sparxel to open it."}}},
        {R"({"format": "sparxel.logic", "version": 1})",
         false,
         {{"/name", "This entry is missing."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x", "colour": 1})",
         true,
         {{"/colour", "Unknown entry 'colour'. It was ignored."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x", "rules": {}})",
         false,
         {{"/rules", "This must be a list, but is a group of values."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x",
             "rules": [{"block": 1, "event": "e", "do": [{"kind": "jump", "block": 2}]}]})",
         false,
         {{"/rules/0/do/0/kind", "'jump' is not one of: do, set, set_variable, wait, wait_until, "
                                 "if, repeat, run, log, for_each_tagged."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x",
             "rules": [{"block": 0, "event": "e"}]})",
         false,
         {{"/rules/0/block", "A block id must be a whole number above 0, but is 0."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x",
             "sequences": [{"name": "s", "steps": [{"kind": "log", "block": 2, "values": [{"kind": "literal"}]}]}]})",
         false,
         {{"/sequences/0/block", "This block has no id."},
          {"/sequences/0/steps/0/values/0/value", "This entry is missing."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x",
             "variables": [{"id": "v", "kind": "colour", "initial": null}]})",
         false,
         {{"/variables/0/kind",
           "'colour' is not one of: yes_no, whole_number, number, text, choice."},
          {"/variables/0/initial", "A value must be yes/no, a number or text, but is nothing."}}},
        {R"({"format": "sparxel.logic", "version": 1, "name": "x",
             "sequences": [{"block": 1, "name": "s", "steps": [{"kind": "if", "block": 2,
               "values": [{"kind": "compare", "op": "about", "operands": []}]}]}]})",
         false,
         {{"/sequences/0/steps/0/values/0/op",
           "'about' is not one of: equal, not_equal, less, less_equal, greater, greater_equal."}}},
    };
    for (const Case& c : cases) {
        CAPTURE(c.json);
        const serialize::LogicLoad loaded = serialize::logic_from_json(parse_ok(c.json));
        CHECK(loaded.asset.has_value() == c.loads);
        CHECK(loaded.issues == c.issues);
    }
}

TEST_CASE("Hostile input is refused without crashing") {
    std::vector<serialize::Issue> issues;
    CHECK_FALSE(serialize::parse("{\"name\": ", issues).has_value());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message.rfind("The file is not valid JSON: ", 0) == 0);

    // 100,000 nested lists: parsing must not overflow the stack.
    issues.clear();
    const std::string deep = std::string(100'000, '[') + std::string(100'000, ']');
    const std::optional<serialize::Json> parsed = serialize::parse(deep, issues);
    CHECK(parsed.has_value());

    // Steps nested beyond the limit are refused by the reader.
    serialize::Json step = {{"kind", "log"}, {"block", 1}};
    for (int i = 0; i < 100; ++i) {
        step = serialize::Json{
            {"kind", "if"}, {"block", 2 + i}, {"body", serialize::Json::array({step})}};
    }
    const serialize::Json document = {
        {"format", "sparxel.logic"},
        {"version", 1},
        {"name", "deep"},
        {"sequences",
         serialize::Json::array(
             {{{"block", 1000}, {"name", "s"}, {"steps", serialize::Json::array({step})}}})}};
    const serialize::LogicLoad loaded = serialize::logic_from_json(document);
    CHECK_FALSE(loaded.asset.has_value());
    REQUIRE_FALSE(loaded.issues.empty());
    CHECK(loaded.issues[0].message == "This block is nested too deeply.");

    // Invalid UTF-8 is replaced when writing, not thrown.
    const std::string text = serialize::to_text(serialize::Json("bad \xFF byte"));
    CHECK(text == "\"bad \xEF\xBF\xBD byte\"\n");
}

TEST_CASE("A loaded logic asset compiles like the original") {
    const reflect::Registry registry;
    const logic::LogicAsset asset = sample_asset();
    const std::string text = serialize::to_text(serialize::logic_to_json(asset));
    const serialize::LogicLoad loaded = serialize::logic_from_json(parse_ok(text));
    REQUIRE(loaded.asset.has_value());
    // No objects exist here, so both fail the same way: the comparison is of the outcome.
    const auto no_objects = [](std::string_view) {
        return std::vector<const reflect::TypeInfo*>{};
    };
    const logic::CompiledLogic a = logic::compile(asset, registry, no_objects);
    const logic::CompiledLogic b =
        logic::compile(loaded.asset.value_or(logic::LogicAsset{}), registry, no_objects);
    REQUIRE(a.errors.size() == b.errors.size());
    for (std::size_t i = 0; i < a.errors.size(); ++i) {
        CHECK(a.errors[i].message == b.errors[i].message);
        CHECK(a.errors[i].block == b.errors[i].block);
    }
}
