#include "engine/game/logic_world.h"

#include <doctest/doctest.h>

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
