#include "engine/game/behaviours.h"
#include "engine/game/logic_world.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game = engine::game;
namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace world = engine::world;
namespace ir = engine::logic::ir;

namespace {

struct Lock {
    bool locked = true;
    void unlock() { locked = false; }
};

struct Purse {
    std::int32_t gold = 0;
    void add(std::int32_t amount) { gold += amount; }
};

/// A door knocked on: its event names who knocked.
struct Knocker {
    std::int32_t knocks = 0;
};

/// Added later to force the entity's components into new storage.
struct Marker {
    std::int32_t value = 0;
};

struct Fixture {
    Fixture() {
        auto lock = registry.add_type<Lock>("lock");
        lock.property<&Lock::locked>("locked");
        lock.action<&Lock::unlock>("unlock");
        lock.event<>("opened");
        auto purse = registry.add_type<Purse>("purse");
        purse.property<&Purse::gold>("gold").range(0, 999);
        purse.action<&Purse::add>("add", {"amount"});
        registry.add_type<Marker>("marker").property<&Marker::value>("value");
        REQUIRE(registry.errors().empty());
        REQUIRE(w.register_component<Lock>());
        REQUIRE(w.register_component<Purse>());
        REQUIRE(w.register_component<Marker>());

        chest = w.create("chest");
        w.add<Lock>(chest);
        w.add<Purse>(chest)->gold = 5;
        goblin = w.create("goblin");
        w.add<Purse>(goblin)->gold = 1;
        w.add<Purse>(w.create("barrel"));
        w.add<Purse>(w.create("barrel"));

        game::attach(vm, w);
        vm.set_log_handler(
            [this](std::string_view, std::string_view text) { log.emplace_back(text); });
    }

    /// Compiles `asset` against the world and loads it; both must succeed.
    logic::ScriptId load(const logic::LogicAsset& asset) {
        const logic::CompiledLogic compiled =
            logic::compile(asset, registry, game::object_types(w));
        for (const logic::Diagnostic& error : compiled.errors) {
            INFO(error.message);
        }
        REQUIRE(compiled.ok());
        const std::optional<logic::ScriptId> id = vm.load(asset.name, compiled.source);
        REQUIRE(id.has_value());
        return id.value_or(logic::ScriptId{});
    }

    reflect::Registry registry;
    world::World w{registry};
    logic::Vm vm{registry};
    world::Entity chest;
    world::Entity goblin;
    std::vector<std::string> log;
};

logic::Expr whole(std::int64_t value) {
    return ir::literal(reflect::Value{value});
}

} // namespace

TEST_CASE("Names and GUIDs resolve to entities; shared names do not") {
    const Fixture f;
    CHECK(game::resolve(f.w, "chest") == f.chest);
    CHECK(game::resolve(f.w, f.w.guid(f.goblin).to_string()) == f.goblin);
    CHECK_FALSE(game::resolve(f.w, "barrel").is_valid());
    CHECK_FALSE(game::resolve(f.w, "dragon").is_valid());
    CHECK(f.w.find_named("barrel").size() == 2);

    const logic::ObjectTypes types = game::object_types(f.w);
    CHECK(types("chest") == std::vector<const reflect::TypeInfo*>{f.registry.find_type<Lock>(),
                                                                  f.registry.find_type<Purse>()});
    CHECK(types("barrel").empty());
}

TEST_CASE("An entity's events reach Rules written with its name or its GUID") {
    Fixture f;
    const std::string chest_guid = f.w.guid(f.chest).to_string();
    CHECK(game::names_of(f.w, f.chest) == std::vector<std::string>{chest_guid, "chest"});
    const world::Entity barrel = f.w.find_named("barrel").at(0);
    CHECK(game::names_of(f.w, barrel) == std::vector<std::string>{f.w.guid(barrel).to_string()});

    logic::LogicAsset asset;
    asset.name = "both_names";
    asset.rules.push_back(
        logic::Rule{logic::BlockId{1}, "chest", "opened", {}, {ir::log(2, whole(1))}, {}});
    asset.rules.push_back(
        logic::Rule{logic::BlockId{3}, chest_guid, "opened", {}, {ir::log(4, whole(2))}, {}});
    f.load(asset);
    CHECK(game::emit(f.vm, f.w, f.chest, "opened"));
    CHECK(f.log == std::vector<std::string>{"2", "1"}); // each Rule once: GUID first
    CHECK_FALSE(game::emit(f.vm, f.w, f.chest, "no_such_event"));
    CHECK_FALSE(game::emit(f.vm, f.w, f.goblin, "opened")); // a Purse has no "opened"

    f.w.destroy(f.chest);
    CHECK(game::names_of(f.w, f.chest).empty());
    CHECK_FALSE(game::emit(f.vm, f.w, f.chest, "opened"));
    CHECK(f.log.size() == 2);
}

TEST_CASE("Rules act on an entity through all its components") {
    Fixture f;
    logic::LogicAsset asset;
    asset.name = "chest_rules";
    asset.rules.push_back(
        logic::Rule{logic::BlockId{1},
                    "chest",
                    "opened",
                    {ir::compare(logic::CompareOp::Equal, ir::property("chest", "locked"),
                                 ir::literal(reflect::Value{true}))},
                    {ir::do_action(2, "chest", "unlock"),           // on the Lock component
                     ir::do_action(3, "chest", "add", {whole(10)}), // on the Purse component
                     ir::log(4, ir::property("chest", "gold"))},
                    {}});
    f.load(asset);

    CHECK(f.vm.emit("chest", "opened"));
    CHECK_FALSE(f.w.get<Lock>(f.chest)->locked);
    CHECK(f.w.get<Purse>(f.chest)->gold == 15);
    CHECK(f.log == std::vector<std::string>{"15"});
    CHECK(f.vm.errors().empty());
}

TEST_CASE("Logic keeps working after the world moves component storage") {
    Fixture f;
    logic::LogicAsset asset;
    asset.name = "income";
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1}, "earn", {ir::do_action(2, "goblin", "add", {whole(2)})}});
    const logic::ScriptId script = f.load(asset);

    REQUIRE(f.vm.start(script, logic::sequence_entry("earn")));
    CHECK(f.w.get<Purse>(f.goblin)->gold == 3);
    // Adding a component moves the goblin's data, and creating many entities grows tables.
    f.w.add<Marker>(f.goblin);
    for (int i = 0; i < 100; ++i) {
        f.w.add<Purse>(f.w.create());
    }
    REQUIRE(f.vm.start(script, logic::sequence_entry("earn")));
    CHECK(f.w.get<Purse>(f.goblin)->gold == 5);
    CHECK(f.vm.errors().empty());
}

TEST_CASE("Entities can be named by GUID, and missing or ambiguous ones are reported") {
    Fixture f;
    const std::string goblin = f.w.guid(f.goblin).to_string();
    logic::LogicAsset by_guid;
    by_guid.name = "by_guid";
    by_guid.sequences.push_back(
        logic::Sequence{logic::BlockId{1}, "pay", {ir::set(2, goblin, "gold", whole(42))}});
    const logic::ScriptId script = f.load(by_guid);
    REQUIRE(f.vm.start(script, logic::sequence_entry("pay")));
    CHECK(f.w.get<Purse>(f.goblin)->gold == 42);

    logic::LogicAsset ambiguous;
    ambiguous.name = "ambiguous";
    ambiguous.sequences.push_back(logic::Sequence{
        logic::BlockId{1}, "fill", {ir::do_action(2, "barrel", "add", {whole(1)})}});
    const logic::CompiledLogic compiled =
        logic::compile(ambiguous, f.registry, game::object_types(f.w));
    REQUIRE(compiled.errors.size() == 1);
    CHECK(compiled.errors[0].message == "There is no object called 'barrel'.");

    // An entity destroyed after its logic was compiled: the error names it at run time.
    f.w.destroy(f.goblin);
    CHECK_FALSE(f.vm.start(script, logic::sequence_entry("pay")).has_value());
    REQUIRE(f.vm.errors().size() == 1);
    CHECK(f.vm.errors()[0].message == "There is no object called '" + goblin + "'.");
    CHECK_FALSE(f.vm.emit(goblin, "opened"));
}

TEST_CASE("Rules about a tag: every tagged object's events, with it as that object") {
    Fixture f;
    f.registry.add_type<game::Tags>("tags").property<&game::Tags::tags>("tags");
    REQUIRE(f.w.register_component<game::Tags>());
    // The chest and a crate are loot; the goblin is not.
    f.w.add<game::Tags>(f.chest)->tags = "Loot, wooden";
    const world::Entity crate = f.w.create("crate");
    f.w.add<Lock>(crate);
    f.w.add<Purse>(crate);
    f.w.add<game::Tags>(crate)->tags = "loot";
    f.w.add<game::Tags>(f.goblin)->tags = "monster";
    CHECK(game::tags_of(f.w, f.chest) == std::vector<std::string>{"loot", "wooden"});
    CHECK(game::tagged(f.w, "LOOT") == std::vector<world::Entity>{f.chest, crate});
    const logic::ObjectTypes types = game::object_types(f.w);
    CHECK(types("#loot") == std::vector<const reflect::TypeInfo*>{
                                f.registry.find_type<Lock>(), f.registry.find_type<Purse>(),
                                f.registry.find_type<game::Tags>()});
    CHECK(types("#nothing").empty());

    // "When an object tagged loot is opened: unlock it, wait, then add 10 to it."
    logic::LogicAsset asset;
    asset.name = "loot";
    asset.rules.push_back(logic::Rule{
        logic::BlockId{1},
        "#loot",
        "opened",
        {},
        {ir::do_action(2, "it", "unlock"), ir::wait(3, ir::literal(reflect::Value{0.5})),
         ir::do_action(4, "it", "add", {whole(10)}), ir::log(5, ir::property("it", "gold"))},
        {}});
    f.load(asset);
    CHECK(game::emit(f.vm, f.w, crate, "opened"));
    CHECK_FALSE(f.w.get<Lock>(crate)->locked);
    CHECK(f.w.get<Lock>(f.chest)->locked); // only the one opened
    // The chest opened while the crate's Rule waits: each keeps its own "it".
    CHECK(game::emit(f.vm, f.w, f.chest, "opened"));
    f.vm.update(1.0);
    CHECK(f.w.get<Purse>(crate)->gold == 10);
    CHECK(f.w.get<Purse>(f.chest)->gold == 15);
    CHECK(f.log == std::vector<std::string>{"10", "15"});
    CHECK_FALSE(game::emit(f.vm, f.w, f.goblin, "opened"));
    CHECK(f.vm.errors().empty());

    // "it" outside a Rule about a tag, and a tag nothing has, are refused plainly.
    logic::LogicAsset wrong;
    wrong.name = "wrong";
    wrong.rules.push_back(logic::Rule{
        logic::BlockId{1}, "chest", "opened", {}, {ir::do_action(2, "it", "unlock")}, {}});
    wrong.rules.push_back(
        logic::Rule{logic::BlockId{3}, "#dragon", "opened", {}, {ir::log(4, whole(1))}, {}});
    const logic::CompiledLogic compiled =
        logic::compile(wrong, f.registry, game::object_types(f.w));
    REQUIRE(compiled.errors.size() == 2);
    CHECK(compiled.errors[0].message.find("\"it\" is the object in a Rule about a tag") !=
          std::string::npos);
    CHECK(compiled.errors[1].message == "No object is tagged 'dragon'.");
}

TEST_CASE("For each object tagged: every one in turn, as it") {
    Fixture f;
    f.registry.add_type<game::Tags>("tags").property<&game::Tags::tags>("tags");
    REQUIRE(f.w.register_component<game::Tags>());
    f.w.add<game::Tags>(f.chest)->tags = "loot";
    const world::Entity crate = f.w.create("crate");
    f.w.add<Lock>(crate);
    f.w.add<Purse>(crate)->gold = 2;
    f.w.add<game::Tags>(crate)->tags = "loot";
    f.w.add<game::Tags>(f.goblin)->tags = "monster";

    // When the chest is opened: every loot is unlocked and given 1 gold, and every
    // monster 100.
    logic::LogicAsset asset;
    asset.name = "all_loot";
    asset.rules.push_back(logic::Rule{
        logic::BlockId{1},
        "chest",
        "opened",
        {},
        {ir::for_each_tagged(2, "loot",
                             {ir::do_action(3, "it", "unlock"),
                              ir::do_action(4, "it", "add", {whole(1)}),
                              ir::log(5, ir::property("it", "gold"))}),
         ir::for_each_tagged(6, "monster", {ir::do_action(7, "it", "add", {whole(100)})})},
        {}});
    f.load(asset);
    CHECK(f.vm.emit("chest", "opened"));
    CHECK_FALSE(f.w.get<Lock>(f.chest)->locked);
    CHECK_FALSE(f.w.get<Lock>(crate)->locked);
    CHECK(f.log == std::vector<std::string>{"6", "3"});
    CHECK(f.w.get<Purse>(f.goblin)->gold == 101);
    CHECK(f.vm.errors().empty());

    // Inside a Rule about a tag, a loop's "it" is the loop's; after it, the Rule's again.
    logic::LogicAsset nested;
    nested.name = "nested";
    nested.rules.push_back(
        logic::Rule{logic::BlockId{1},
                    "#loot",
                    "opened",
                    {},
                    {ir::for_each_tagged(2, "monster", {ir::do_action(3, "it", "add", {whole(5)})}),
                     ir::do_action(4, "it", "add", {whole(1000)})},
                    {}});
    f.load(nested);
    CHECK(game::emit(f.vm, f.w, crate, "opened"));
    CHECK(f.w.get<Purse>(f.goblin)->gold == 106);
    CHECK(f.w.get<Purse>(crate)->gold == 1003);

    // A tag nothing has, or not a tag at all: refused.
    logic::LogicAsset wrong;
    wrong.name = "wrong";
    wrong.rules.push_back(logic::Rule{
        logic::BlockId{1}, "chest", "opened", {}, {ir::for_each_tagged(2, "dragon", {})}, {}});
    wrong.rules.push_back(logic::Rule{
        logic::BlockId{3}, "chest", "opened", {}, {ir::for_each_tagged(4, "Not A Tag", {})}, {}});
    const logic::CompiledLogic compiled =
        logic::compile(wrong, f.registry, game::object_types(f.w));
    REQUIRE(compiled.errors.size() == 2);
    CHECK(compiled.errors[0].message == "No object is tagged 'dragon'.");
    CHECK(compiled.errors[1].message.find("is not valid") != std::string::npos);
}

TEST_CASE("A step acts on who: the object the Rule's event names") {
    Fixture f;
    auto knocker = f.registry.add_type<Knocker>("knocker");
    knocker.property<&Knocker::knocks>("knocks");
    knocker.event<std::string>("knocked", {"who"});
    REQUIRE(f.w.register_component<Knocker>());
    f.w.add<Knocker>(f.chest);

    // "When [who] knocks on the chest: add 7 to who."
    logic::LogicAsset asset;
    asset.name = "knocking";
    asset.rules.push_back(logic::Rule{
        logic::BlockId{1},
        "chest",
        "knocked",
        {},
        {ir::do_action(2, "who", "add", {whole(7)}), ir::log(3, ir::property("who", "gold"))},
        {}});
    f.load(asset);
    const std::array<reflect::Value, 1> goblin{reflect::Value{std::string("goblin")}};
    CHECK(game::emit(f.vm, f.w, f.chest, "knocked", goblin));
    CHECK(f.w.get<Purse>(f.goblin)->gold == 8);
    // By GUID too, as events usually name objects.
    const std::array<reflect::Value, 1> chest{reflect::Value{f.w.guid(f.chest).to_string()}};
    CHECK(game::emit(f.vm, f.w, f.chest, "knocked", chest));
    CHECK(f.w.get<Purse>(f.chest)->gold == 12);
    CHECK(f.log == std::vector<std::string>{"8", "12"});
    CHECK(f.vm.errors().empty());

    // An event that names no one has no who.
    logic::LogicAsset wrong;
    wrong.name = "wrong";
    wrong.rules.push_back(logic::Rule{logic::BlockId{1},
                                      "chest",
                                      "opened",
                                      {},
                                      {ir::do_action(2, "who", "add", {whole(1)})},
                                      {}});
    const logic::CompiledLogic compiled =
        logic::compile(wrong, f.registry, game::object_types(f.w));
    REQUIRE(compiled.errors.size() == 1);
    CHECK(compiled.errors[0].message.find("names none") != std::string::npos);
}
