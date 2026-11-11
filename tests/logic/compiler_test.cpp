#include "engine/logic/compiler.h"
#include "engine/logic/vm.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace ir = engine::logic::ir;

using logic::ArithmeticOp;
using logic::CompareOp;

namespace {

enum class Facing : std::uint8_t { North, East, South, West };

class Player {
public:
    [[nodiscard]] bool has_item(const std::string& item) const {
        return std::find(items.begin(), items.end(), item) != items.end();
    }
    [[nodiscard]] std::int32_t item_count() const {
        return static_cast<std::int32_t>(items.size());
    }
    void give_item(const std::string& item) { items.push_back(item); }
    void take_item(const std::string& item) {
        const auto found = std::find(items.begin(), items.end(), item);
        if (found != items.end()) {
            items.erase(found);
        }
    }
    void add_gold(std::int32_t amount) { gold += amount; }

    std::vector<std::string> items;
    std::int32_t gold = 0;
    Facing facing = Facing::South;
};

class Chest {
public:
    void unlock() { locked = false; }
    void open() {
        is_open = true;
        if (vm != nullptr) {
            // Emitted while a script runs, so it is delivered in the next update.
            emitted = vm->emit("chest", "opened");
        }
    }

    bool locked = true;
    bool is_open = false;
    logic::Vm* vm{nullptr};
    bool emitted = false;
};

void register_types(reflect::Registry& registry) {
    registry.add_choice_list<Facing>("facing")
        .entry(Facing::North, "north")
        .entry(Facing::East, "east")
        .entry(Facing::South, "south")
        .entry(Facing::West, "west");

    auto player = registry.add_type<Player>("player");
    player.property<&Player::gold>("gold").range(0, 9999);
    player.property<&Player::facing>("facing");
    player.condition<&Player::has_item>("has_item", {"item"});
    player.getter<&Player::item_count>("item_count");
    player.action<&Player::give_item>("give_item", {"item"});
    player.action<&Player::take_item>("take_item", {"item"});
    player.action<&Player::add_gold>("add_gold", {"amount"}).param("amount").range(1, 999);

    auto chest = registry.add_type<Chest>("chest");
    chest.property<&Chest::locked>("locked");
    chest.property<&Chest::is_open>("is_open").flags(reflect::property_flag::read_only);
    chest.action<&Chest::unlock>("unlock");
    chest.action<&Chest::open>("open");
    chest.event<>("interacted");
    chest.event<>("opened");
    chest.event<std::int32_t, Facing>("hit", {"damage", "from"});
}

struct World {
    World() {
        register_types(registry);
        REQUIRE(registry.errors().empty());
        chest.vm = &vm;
        REQUIRE(vm.bind("player", reflect::ObjectRef::of(player)));
        REQUIRE(vm.bind("chest", reflect::ObjectRef::of(chest)));
        vm.set_log_handler(
            [this](std::string_view, std::string_view text) { log.emplace_back(text); });
    }

    [[nodiscard]] logic::ObjectTypes types() const {
        return [this](std::string_view name) { return vm.object_types(name); };
    }

    [[nodiscard]] logic::CompiledLogic compile(const logic::LogicAsset& asset,
                                               logic::CompileOptions options = {}) const {
        return logic::compile(asset, registry, types(), options);
    }

    /// Compiles and loads an asset that must be valid.
    logic::ScriptId load(const logic::LogicAsset& asset) {
        const logic::CompiledLogic compiled = compile(asset);
        for (const logic::Diagnostic& error : compiled.errors) {
            INFO("block " << error.block.value << ": " << error.message);
        }
        REQUIRE(compiled.ok());
        const std::optional<logic::ScriptId> id = vm.load(asset.name, compiled.source);
        REQUIRE(vm.errors().empty());
        REQUIRE(id.has_value());
        return id.value_or(logic::ScriptId{});
    }

    reflect::Registry registry;
    logic::Vm vm{registry};
    Player player;
    Chest chest;
    std::vector<std::string> log;
};

logic::Expr text(const char* value) {
    return ir::literal(reflect::Value{std::string(value)});
}

logic::Expr whole(std::int64_t value) {
    return ir::literal(reflect::Value{value});
}

logic::Expr number(double value) {
    return ir::literal(reflect::Value{value});
}

logic::Expr yes_no(bool value) {
    return ir::literal(reflect::Value{value});
}

} // namespace

TEST_CASE("The compiler emits readable Luau with one block per line") {
    const World world;
    logic::LogicAsset asset;
    asset.name = "golden";
    asset.variables.push_back(logic::Variable{"count", reflect::ValueKind::Int, "", std::nullopt});
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1},
        "greet",
        {
            ir::log(2, text("hi")),
            ir::wait(3, whole(1)),
            ir::set_variable(4, "count",
                             ir::arithmetic(ArithmeticOp::Add, ir::variable("count"), whole(-1))),
        }});
    asset.rules.push_back(logic::Rule{
        logic::BlockId{10},
        "chest",
        "interacted",
        {ir::with_id(ir::compare(CompareOp::Equal, ir::property("chest", "locked"), yes_no(true)),
                     11)},
        {ir::run(12, "greet")},
        {ir::do_action(13, "chest", "open")}});

    const logic::CompiledLogic compiled = world.compile(asset);
    REQUIRE(compiled.ok());
    CHECK(compiled.source == "local vars = {}; _VARS = vars; _BRAINS = {}\n"
                             "vars[\"count\"] = 0\n"
                             "function seq_greet()\n"
                             "  sx.log(\"hi\")\n"
                             "  sx.wait(1)\n"
                             "  vars[\"count\"] = (vars[\"count\"] + (-1))\n"
                             "end\n"
                             "sx.on(\"chest.interacted\", function()\n"
                             "  if (sx.get(\"chest\", \"locked\") == true)\n"
                             "  then\n"
                             "    seq_greet()\n"
                             "  else\n"
                             "    sx.call(\"chest\", \"open\")\n"
                             "  end\n"
                             "end)\n");
    const std::vector<std::uint64_t> expected{0, 0, 1, 2, 3, 4, 1, 10, 11, 10, 12, 10, 13, 10, 10};
    REQUIRE(compiled.line_blocks.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(compiled.line_blocks[i].value == expected[i]);
    }
    CHECK(compiled.block_at_line(4).value == 2);
    CHECK_FALSE(compiled.block_at_line(0).is_valid());
    CHECK_FALSE(compiled.block_at_line(99).is_valid());
}

TEST_CASE("A chest that needs a key (plan/05 9.2)") {
    World world;
    logic::LogicAsset asset;
    asset.name = "maps/village/chest";
    asset.rules.push_back(
        logic::Rule{logic::BlockId{1},
                    "chest",
                    "interacted",
                    {ir::call("player", "has_item", {text("rusty_key")})},
                    {
                        ir::do_action(2, "player", "take_item", {text("rusty_key")}),
                        ir::do_action(3, "chest", "unlock"),
                        ir::do_action(4, "chest", "open"),
                    },
                    {ir::log(5, text("It's locked."))}});
    asset.rules.push_back(logic::Rule{logic::BlockId{6},
                                      "chest",
                                      "opened",
                                      {},
                                      {ir::log(7, text("The chest creaks open."))},
                                      {}});
    world.load(asset);

    CHECK(world.vm.emit("chest", "interacted"));
    CHECK(world.log == std::vector<std::string>{"It's locked."});
    CHECK(world.chest.locked);

    world.player.give_item("rusty_key");
    CHECK(world.vm.emit("chest", "interacted"));
    CHECK_FALSE(world.chest.locked);
    CHECK(world.chest.is_open);
    CHECK(world.player.items.empty());
    // "opened" was emitted by C++ while the rule ran: it arrives in the next update.
    CHECK(world.chest.emitted);
    CHECK(world.log.size() == 1);
    world.vm.update(0.0);
    CHECK(world.log == std::vector<std::string>{"It's locked.", "The chest creaks open."});
    CHECK(world.vm.errors().empty());
}

TEST_CASE("Sequences with variables, loops, branches, waits and nested sequences") {
    World world;
    logic::LogicAsset asset;
    asset.name = "quest";
    asset.variables.push_back(
        logic::Variable{"stage", reflect::ValueKind::Int, "", reflect::Value{std::int64_t{1}}});
    asset.variables.push_back(logic::Variable{"heading", reflect::ValueKind::Choice, "facing",
                                              reflect::Value{std::string("east")}});
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1},
        "reward",
        {
            ir::repeat(2, whole(3), {ir::do_action(3, "player", "add_gold", {whole(10)})}),
            ir::wait(4, number(0.5)),
            ir::set(5, "player", "facing", ir::variable("heading")),
        }});
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{10},
        "intro",
        {
            ir::log(11, ir::arithmetic(ArithmeticOp::Multiply, ir::variable("stage"), whole(7))),
            ir::if_then(12, ir::compare(CompareOp::GreaterEqual, ir::variable("stage"), whole(2)),
                        {ir::log(13, text("later visit"))},
                        {ir::log(14, text("first visit")),
                         ir::set_variable(
                             15, "stage",
                             ir::arithmetic(ArithmeticOp::Add, ir::variable("stage"), whole(1)))}),
            ir::run(16, "reward"),
            ir::log(17, ir::call("player", "item_count")),
            ir::log(18, ir::arithmetic(ArithmeticOp::Subtract, whole(5), whole(-3))),
            ir::log(19, ir::arithmetic(ArithmeticOp::Divide, whole(7), whole(2))),
        }});
    const logic::ScriptId script = world.load(asset);

    REQUIRE(world.vm.start(script, logic::sequence_entry("intro")).has_value());
    CHECK(world.log == std::vector<std::string>{"7", "first visit"});
    CHECK(world.player.gold == 30);
    CHECK(world.player.facing == Facing::South);
    world.vm.update(0.5);
    CHECK(world.player.facing == Facing::East);
    CHECK(world.log == std::vector<std::string>{"7", "first visit", "0", "8", "3.5"});

    world.log.clear();
    REQUIRE(world.vm.start(script, logic::sequence_entry("intro")).has_value());
    CHECK(world.log == std::vector<std::string>{"14", "later visit"});
    CHECK(world.vm.errors().empty());
}

TEST_CASE("Rules read their event's values, and rules for one event run in order") {
    World world;
    logic::LogicAsset asset;
    asset.name = "combat";
    asset.rules.push_back(
        logic::Rule{logic::BlockId{1},
                    "chest",
                    "hit",
                    {ir::compare(CompareOp::Greater, ir::event_value("damage"), whole(10)),
                     ir::compare(CompareOp::Equal, ir::event_value("from"), text("north"))},
                    {ir::log(2, text("smashed from the north"))},
                    {ir::log(3, ir::event_value("from"))}});
    asset.rules.push_back(logic::Rule{
        logic::BlockId{4}, "chest", "hit", {}, {ir::log(5, ir::event_value("damage"))}, {}});
    asset.rules.push_back(
        logic::Rule{logic::BlockId{6}, "", "game_start", {}, {ir::log(7, text("start"))}, {}});
    world.load(asset);

    const std::vector<reflect::Value> big_hit{reflect::Value{std::int64_t{25}},
                                              reflect::Value{std::int64_t{0}}};
    CHECK(world.vm.emit("chest", "hit", big_hit));
    CHECK(world.log == std::vector<std::string>{"smashed from the north", "25"});

    world.log.clear();
    const std::vector<reflect::Value> side_hit{reflect::Value{std::int64_t{25}},
                                               reflect::Value{std::int64_t{1}}};
    CHECK(world.vm.emit("chest", "hit", side_hit));
    CHECK(world.log == std::vector<std::string>{"east", "25"});

    world.log.clear();
    CHECK(world.vm.emit("", "game_start"));
    CHECK(world.log == std::vector<std::string>{"start"});

    // Invalid emits are refused and run nothing.
    world.log.clear();
    CHECK_FALSE(world.vm.emit("chest", "hit"));
    CHECK_FALSE(world.vm.emit("chest", "hit",
                              std::vector<reflect::Value>{reflect::Value{std::int64_t{1}},
                                                          reflect::Value{std::int64_t{9}}}));
    CHECK_FALSE(world.vm.emit("chest", "exploded"));
    CHECK_FALSE(world.vm.emit("ghost", "hit"));
    CHECK_FALSE(world.vm.emit("", "Not An Id"));
    CHECK(world.vm.emit("", "nobody_listens"));
    CHECK(world.log.empty());
}

TEST_CASE("Text from users cannot escape its string") {
    World world;
    const std::string nasty = "\"); print(\"pwned\") --\nnext line \\ \t caf\xC3\xA9 \x01"
                              "9";
    logic::LogicAsset asset;
    asset.name = "escape";
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1}, "say", {ir::log(2, ir::literal(reflect::Value{nasty}))}});
    const logic::CompiledLogic compiled = world.compile(asset);
    REQUIRE(compiled.ok());
    // Still one line per block.
    CHECK(compiled.line_blocks.size() == 4);
    const logic::ScriptId script = world.load(asset);
    REQUIRE(world.vm.start(script, logic::sequence_entry("say")).has_value());
    CHECK(world.log == std::vector<std::string>{nasty});
}

TEST_CASE("Runtime errors map back to the block that caused them") {
    World world;
    logic::LogicAsset asset;
    asset.name = "mapping";
    asset.variables.push_back(
        logic::Variable{"amount", reflect::ValueKind::Int, "", reflect::Value{std::int64_t{5000}}});
    asset.sequences.push_back(
        logic::Sequence{logic::BlockId{1},
                        "pay",
                        {ir::log(2, text("paying")),
                         ir::do_action(3, "player", "add_gold", {ir::variable("amount")})}});
    const logic::CompiledLogic compiled = world.compile(asset);
    const logic::ScriptId script = world.load(asset);

    CHECK_FALSE(world.vm.start(script, logic::sequence_entry("pay")).has_value());
    REQUIRE(world.vm.errors().size() == 1);
    const logic::ScriptError& error = world.vm.errors()[0];
    CHECK(error.message == "'Amount' of 'Add gold' must be between 1 and 999, but was given 5000.");
    CHECK(compiled.block_at_line(error.line).value == 3);
}

TEST_CASE("Tracing reports every block as it runs") {
    World world;
    logic::LogicAsset asset;
    asset.name = "traced";
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1},
        "steps",
        {ir::log(2, text("a")),
         ir::if_then(3, yes_no(false), {ir::log(4, text("never"))}, {ir::log(5, text("b"))}),
         ir::wait(6, number(0.25)), ir::log(7, text("c"))}});
    const logic::CompiledLogic compiled = world.compile(asset, logic::CompileOptions{true});
    REQUIRE(compiled.ok());
    const std::optional<logic::ScriptId> script = world.vm.load(asset.name, compiled.source);
    REQUIRE(script.has_value());

    std::vector<std::uint64_t> fired;
    world.vm.set_trace_handler([&](std::string_view name, int line) {
        CHECK(name == "traced");
        fired.push_back(compiled.block_at_line(line).value);
    });
    REQUIRE(world.vm.start(script.value_or(logic::ScriptId{}), logic::sequence_entry("steps")));
    CHECK(fired == std::vector<std::uint64_t>{1, 2, 3, 5, 6});
    world.vm.update(0.25);
    CHECK(fired == std::vector<std::uint64_t>{1, 2, 3, 5, 6, 7});
}

TEST_CASE("Invalid logic is reported on the block, in plain language") {
    const World world;
    struct Case {
        logic::Stmt statement;
        std::uint64_t block;
        const char* message;
    };
    const double not_a_number = std::numeric_limits<double>::quiet_NaN();
    const std::vector<Case> cases{
        {ir::do_action(1, "ghost", "open"), 1, "There is no object called 'ghost'."},
        {ir::do_action(1, "chest", "fly"), 1, "'chest' has no action called 'fly'."},
        {ir::do_action(1, "player", "has_item", {text("key")}), 1,
         "'Has item' is a condition: it answers yes or no and changes nothing. Use it in an If."},
        {ir::do_action(1, "player", "item_count"), 1,
         "'Item count' is a value: it changes nothing. Use it in another block's slot."},
        {ir::do_action(1, "player", "take_item"), 1, "'Take item' needs 1 value, but has 0."},
        {ir::do_action(1, "player", "add_gold", {whole(5000)}), 1,
         "'Amount' of 'Add gold' must be between 1 and 999, but is 5000."},
        {ir::do_action(1, "player", "add_gold", {ir::with_id(text("lots"), 2)}), 2,
         "'Amount' of 'Add gold' must be a whole number, but is 'lots'."},
        {ir::do_action(1, "player", "add_gold", {ir::property("chest", "locked")}), 1,
         "'Amount' of 'Add gold' must be a whole number, but the value given is yes or no (true or "
         "false)."},
        {ir::set(1, "chest", "locked", whole(3)), 1,
         "'Locked' of 'chest' must be yes or no (true or false), but is 3."},
        {ir::set(1, "chest", "is_open", yes_no(true)), 1,
         "'Is open' of 'chest' cannot be changed."},
        {ir::set(1, "player", "facing", text("up")), 1,
         "'Facing' of 'player' must be one of: north, east, south, west, but is 'up'."},
        {ir::set(1, "player", "mana", whole(1)), 1, "'player' has no property called 'mana'."},
        {ir::if_then(1, ir::compare(CompareOp::Equal, ir::property("player", "gold"), text("x")),
                     {}),
         1, "The value compared must be a number, but is 'x'."},
        {ir::if_then(1,
                     ir::compare(CompareOp::Less, ir::property("chest", "locked"),
                                 ir::property("chest", "locked")),
                     {}),
         1, "Only numbers can be compared with less than or greater than."},
        {ir::if_then(1,
                     ir::compare(CompareOp::Equal, ir::property("player", "facing"),
                                 ir::property("chest", "locked")),
                     {}),
         1,
         "These two values cannot be compared: one is one of: north, east, south, west, the other "
         "yes or no (true or false)."},
        {ir::if_then(1, ir::call("chest", "open"), {}), 1,
         "'Open' is an action: it does something but gives no value, so it cannot be used here."},
        {ir::if_then(1, ir::property("player", "gold"), {}), 1,
         "A condition must be yes or no (true or false), but the value given is a whole number."},
        {ir::if_then(1, ir::all({yes_no(true)}), {}), 1, "'All of' needs at least two conditions."},
        {ir::log(1, ir::arithmetic(ArithmeticOp::Add, text("a"), whole(1))), 1,
         "Only numbers can be used in a calculation."},
        {ir::log(1, ir::event_value("damage")), 1, "'damage' is not a value of this rule's event."},
        {ir::log(1, ir::variable("score")), 1, "There is no variable called 'score'."},
        {ir::log(1, number(not_a_number)), 1,
         "This value must be a finite number, but is not a number."},
        {ir::wait(1, whole(-1)), 1, "The time to wait must be at least 0, but is -1."},
        {ir::repeat(1, number(2.5), {}), 1,
         "The number of repeats must be a whole number, but is 2.5."},
        {ir::run(1, "nope"), 1, "There is no sequence called 'nope'."},
        {ir::set_variable(1, "count", number(1.5)), 1,
         "The variable 'count' must be a whole number, but is 1.5."},
        {logic::Stmt{logic::StmtKind::Wait, logic::BlockId{1}, "", "", {}, {}, {}}, 1,
         "This block needs exactly one value."},
        {ir::wait_until(1, ir::variable("count")), 1,
         "A condition must be yes or no (true or false), but the value given is a whole number."},
        {ir::log(0, text("no id")), 0,
         "A block has no id. Every statement, sequence and rule needs one."},
    };
    for (const Case& c : cases) {
        CAPTURE(c.message);
        logic::LogicAsset asset;
        asset.name = "broken";
        asset.variables.push_back(
            logic::Variable{"count", reflect::ValueKind::Int, "", std::nullopt});
        asset.sequences.push_back(logic::Sequence{logic::BlockId{100}, "main", {c.statement}});
        const logic::CompiledLogic compiled = world.compile(asset);
        REQUIRE(compiled.errors.size() == 1);
        CHECK(compiled.errors[0].message == c.message);
        CHECK(compiled.errors[0].block.value == c.block);
        CHECK(compiled.source.empty());
        CHECK(compiled.line_blocks.empty());
    }
}

TEST_CASE("Invalid assets, sequences and rules are reported") {
    const World world;
    const auto errors_of = [&](const logic::LogicAsset& asset) {
        std::vector<std::string> messages;
        for (const logic::Diagnostic& error : world.compile(asset).errors) {
            messages.push_back(error.message);
        }
        return messages;
    };

    logic::LogicAsset bad_name;
    bad_name.name = "bad name";
    CHECK(errors_of(bad_name) ==
          std::vector<std::string>{"The logic asset's name 'bad name' is not valid: use 1 to 128 "
                                   "letters, digits, '_', '-', '.' or '/'."});

    logic::LogicAsset sequences;
    sequences.name = "s";
    sequences.sequences.push_back(logic::Sequence{logic::BlockId{1}, "intro", {}});
    sequences.sequences.push_back(logic::Sequence{logic::BlockId{2}, "intro", {}});
    sequences.sequences.push_back(logic::Sequence{logic::BlockId{3}, "Bad", {}});
    sequences.sequences.push_back(logic::Sequence{logic::BlockId{3}, "other", {}});
    CHECK(
        errors_of(sequences) ==
        std::vector<std::string>{
            "There is already a sequence called 'intro'.",
            "The sequence name 'Bad' is not valid: use lowercase letters, digits and underscores, "
            "starting with a letter.",
            "Two blocks share the id 3."});

    logic::LogicAsset rules;
    rules.name = "r";
    rules.rules.push_back(logic::Rule{logic::BlockId{1}, "chest", "exploded", {}, {}, {}});
    rules.rules.push_back(logic::Rule{logic::BlockId{2}, "ghost", "hit", {}, {}, {}});
    rules.rules.push_back(logic::Rule{logic::BlockId{3}, "", "Game Start", {}, {}, {}});
    rules.rules.push_back(
        logic::Rule{logic::BlockId{4}, "", "game_start", {}, {}, {ir::log(5, text("x"))}});
    CHECK(errors_of(rules) ==
          std::vector<std::string>{
              "'chest' has no event called 'exploded'.", "There is no object called 'ghost'.",
              "The event name 'Game Start' is not valid.",
              "This rule has no conditions, so its Otherwise actions can never run."});

    logic::LogicAsset variables;
    variables.name = "v";
    variables.variables.push_back(logic::Variable{"x", reflect::ValueKind::Int, "", std::nullopt});
    variables.variables.push_back(logic::Variable{"x", reflect::ValueKind::Int, "", std::nullopt});
    variables.variables.push_back(logic::Variable{"Y", reflect::ValueKind::Int, "", std::nullopt});
    variables.variables.push_back(
        logic::Variable{"dir", reflect::ValueKind::Choice, "compass", std::nullopt});
    variables.variables.push_back(
        logic::Variable{"name", reflect::ValueKind::Text, "", reflect::Value{std::int64_t{3}}});
    CHECK(errors_of(variables) ==
          std::vector<std::string>{
              "There is already a variable called 'x'.",
              "The variable name 'Y' is not valid: use lowercase letters, digits and underscores, "
              "starting with a letter.",
              "The variable 'dir' uses the choice list 'compass', which does not exist.",
              "The starting value of 'name' must be text, but is 3."});
}

TEST_CASE("Deeply nested logic is refused instead of overflowing the stack") {
    const World world;
    logic::Expr condition = yes_no(true);
    for (int i = 0; i < 1'000; ++i) {
        condition = ir::negate(std::move(condition));
    }
    logic::LogicAsset asset;
    asset.name = "deep";
    asset.sequences.push_back(
        logic::Sequence{logic::BlockId{1}, "main", {ir::if_then(2, std::move(condition), {})}});
    const logic::CompiledLogic compiled = world.compile(asset);
    REQUIRE_FALSE(compiled.ok());
    CHECK(compiled.errors[0].message == "This block is nested too deeply.");

    logic::Stmt nested = ir::log(1'000'000, text("bottom"));
    for (std::uint64_t i = 0; i < 1'000; ++i) {
        nested = ir::if_then(10 + i, yes_no(true), {std::move(nested)});
    }
    logic::LogicAsset statements;
    statements.name = "deep_statements";
    statements.sequences.push_back(logic::Sequence{logic::BlockId{1}, "main", {std::move(nested)}});
    const logic::CompiledLogic deep = world.compile(statements);
    REQUIRE_FALSE(deep.ok());
    CHECK(deep.errors[0].message == "This block is nested too deeply.");
}

TEST_CASE("Names for the host") {
    CHECK(logic::sequence_entry("elder_intro") == "seq_elder_intro");
    CHECK(logic::event_key("chest", "opened") == "chest.opened");
    CHECK(logic::event_key("", "game_start") == "game_start");
}

TEST_CASE("A script's variables can be read and restored, as saved games need") {
    World world;
    logic::LogicAsset asset;
    asset.name = "story";
    asset.variables.push_back(
        logic::Variable{"stage", reflect::ValueKind::Int, "", reflect::Value{std::int64_t{2}}});
    asset.variables.push_back(
        logic::Variable{"met_elder", reflect::ValueKind::Bool, "", reflect::Value{true}});
    asset.variables.push_back(
        logic::Variable{"speed", reflect::ValueKind::Float, "", reflect::Value{1.5}});
    asset.variables.push_back(logic::Variable{"hero_name", reflect::ValueKind::Text, "",
                                              reflect::Value{std::string("Ari")}});
    asset.variables.push_back(logic::Variable{"heading", reflect::ValueKind::Choice, "facing",
                                              reflect::Value{std::string("east")}});
    asset.sequences.push_back(
        logic::Sequence{logic::BlockId{1}, "report", {ir::log(2, ir::variable("stage"))}});
    const logic::ScriptId script = world.load(asset);

    CHECK(world.vm.scripts() == std::vector<std::string>{"story"});
    using Pair = std::pair<std::string, reflect::Value>;
    CHECK(world.vm.variables("story") ==
          std::vector<Pair>{{"heading", reflect::Value{std::string("east")}},
                            {"hero_name", reflect::Value{std::string("Ari")}},
                            {"met_elder", reflect::Value{true}},
                            {"speed", reflect::Value{1.5}},
                            {"stage", reflect::Value{std::int64_t{2}}}});
    CHECK(world.vm.variables("nothing").empty());

    // Restoring: same kinds only, existing variables only.
    CHECK(world.vm.set_variable("story", "stage", reflect::Value{std::int64_t{5}}));
    CHECK(world.vm.set_variable("story", "speed", reflect::Value{std::int64_t{3}}));
    CHECK(world.vm.set_variable("story", "heading", reflect::Value{std::string("west")}));
    CHECK_FALSE(world.vm.set_variable("story", "stage", reflect::Value{std::string("five")}));
    CHECK_FALSE(world.vm.set_variable("story", "met_elder", reflect::Value{std::int64_t{1}}));
    CHECK_FALSE(world.vm.set_variable("story", "gold", reflect::Value{std::int64_t{1}}));
    CHECK_FALSE(world.vm.set_variable("nothing", "stage", reflect::Value{std::int64_t{1}}));
    REQUIRE(world.vm.start(script, logic::sequence_entry("report")));
    CHECK(world.log == std::vector<std::string>{"5"});
    CHECK(world.vm.variables("story")[0].second == reflect::Value{std::string("west")});
    CHECK(world.vm.variables("story")[3].second == reflect::Value{std::int64_t{3}});
}

TEST_CASE("Wait until checks its condition at once, then once per update") {
    World world;
    logic::LogicAsset asset;
    asset.name = "gate";
    asset.variables.push_back(
        logic::Variable{"open", reflect::ValueKind::Bool, "", reflect::Value{false}});
    asset.sequences.push_back(
        logic::Sequence{logic::BlockId{1},
                        "wait_for_gate",
                        {ir::log(2, text("waiting")), ir::wait_until(3, ir::variable("open")),
                         ir::log(4, text("through"))}});
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{5},
        "already_open",
        {ir::wait_until(6, ir::negate(ir::variable("open"))), ir::log(7, text("no wait"))}});
    const logic::ScriptId script = world.load(asset);
    REQUIRE(world.vm.start(script, logic::sequence_entry("wait_for_gate")));
    CHECK(world.log == std::vector<std::string>{"waiting"});
    for (int i = 0; i < 3; ++i) {
        world.vm.update(0.25);
    }
    CHECK(world.log == std::vector<std::string>{"waiting"});
    CHECK(world.vm.task_count() == 1);
    // A condition that holds already does not wait, not even for the next update.
    REQUIRE(world.vm.start(script, logic::sequence_entry("already_open")));
    CHECK(world.log == std::vector<std::string>{"waiting", "no wait"});
    REQUIRE(world.vm.set_variable("gate", "open", reflect::Value{true}));
    world.vm.update(0.0);
    CHECK(world.log == std::vector<std::string>{"waiting", "no wait", "through"});
    CHECK(world.vm.task_count() == 0);
}

TEST_CASE("stop_all() ends waiting sequences but keeps Rules") {
    World world;
    logic::LogicAsset asset;
    asset.name = "cutscene";
    asset.sequences.push_back(logic::Sequence{
        logic::BlockId{1}, "walk", {ir::wait(2, number(1.0)), ir::log(3, text("late"))}});
    asset.rules.push_back(
        logic::Rule{logic::BlockId{4}, "", "game_start", {}, {ir::log(5, text("rule"))}, {}});
    const logic::ScriptId script = world.load(asset);
    REQUIRE(world.vm.start(script, logic::sequence_entry("walk")));
    CHECK(world.vm.task_count() == 1);
    CHECK(world.vm.stop_all());
    CHECK(world.vm.task_count() == 0);
    world.vm.update(2.0);
    CHECK(world.log.empty()); // the stopped sequence never logs
    CHECK(world.vm.emit("", "game_start"));
    CHECK(world.log == std::vector<std::string>{"rule"});
}

TEST_CASE("Brains enter, run, take transitions by priority and time, and never wait") {
    World world;
    logic::LogicAsset asset;
    asset.name = "ai";
    logic::Brain guard;
    guard.id = logic::BlockId{1};
    guard.name = "guard";
    // Idle until the player has the bell; Alert pays a gold per update and calms down
    // after half a second; any state goes to Down once the chest is unlocked.
    logic::BrainState idle;
    idle.id = logic::BlockId{2};
    idle.name = "idle";
    idle.on_enter = {ir::log(3, text("idle"))};
    idle.transitions.push_back(logic::Transition{
        logic::BlockId{4}, "alert", {ir::call("player", "has_item", {text("bell")})}, 0.0});
    logic::BrainState alert;
    alert.id = logic::BlockId{5};
    alert.name = "alert";
    alert.on_enter = {ir::log(6, text("alert"))};
    alert.while_in = {ir::do_action(7, "player", "add_gold", {whole(1)})};
    alert.on_exit = {ir::log(8, text("calm"))};
    alert.transitions.push_back(logic::Transition{logic::BlockId{9}, "idle", {}, 0.5});
    logic::BrainState down;
    down.id = logic::BlockId{10};
    down.name = "down";
    down.on_enter = {ir::log(11, text("down"))};
    guard.states = {idle, alert, down};
    guard.from_any.push_back(logic::Transition{
        logic::BlockId{12},
        "down",
        {ir::compare(CompareOp::Equal, ir::property("chest", "locked"), yes_no(false))},
        0.0});
    asset.brains.push_back(guard);
    world.load(asset);

    const auto state = [&world] {
        for (const auto& [name, value] : world.vm.variables("ai")) {
            if (name == "@brain.guard.state") {
                return std::get<std::string>(value);
            }
        }
        return std::string();
    };
    CHECK(state() == "idle");
    CHECK(world.log.empty()); // nothing runs before the first update

    world.vm.update(0.25);
    CHECK(world.log == std::vector<std::string>{"idle"});
    world.vm.update(0.25); // no bell: stays
    CHECK(world.log.size() == 1);

    world.player.give_item("bell");
    world.vm.update(0.25); // to Alert, which then runs once
    CHECK(state() == "alert");
    CHECK(world.log == std::vector<std::string>{"idle", "alert"});
    CHECK(world.player.gold == 1);
    world.player.take_item("bell");
    world.vm.update(0.25); // 0.25 s in Alert: too soon to calm down
    CHECK(state() == "alert");
    CHECK(world.player.gold == 2);
    world.vm.update(0.25); // 0.5 s: back to Idle, leaving through On Exit
    CHECK(state() == "idle");
    CHECK(world.log == std::vector<std::string>{"idle", "alert", "calm", "idle"});
    CHECK(world.player.gold == 2);

    // "From any state" wins, and is not taken again once there.
    world.player.give_item("bell");
    world.chest.locked = false;
    world.vm.update(0.25);
    CHECK(state() == "down");
    world.vm.update(0.25);
    CHECK(world.log == std::vector<std::string>{"idle", "alert", "calm", "idle", "down"});
    CHECK(world.vm.errors().empty());

    // A brain's state is a variable, so saved games keep it: restoring Idle resumes there,
    // without running On Enter again.
    REQUIRE(world.vm.set_variable("ai", "@brain.guard.state", reflect::Value{std::string("idle")}));
    world.chest.locked = true;
    world.vm.update(0.25); // has the bell: to Alert
    CHECK(state() == "alert");
    CHECK(world.log.back() == "alert");
}

TEST_CASE("Brain mistakes are reported at their blocks") {
    World world;
    const auto errors_of = [&world](const logic::Brain& brain) {
        logic::LogicAsset asset;
        asset.name = "ai";
        asset.brains.push_back(brain);
        std::vector<std::string> messages;
        for (const logic::Diagnostic& error : world.compile(asset).errors) {
            messages.push_back(std::to_string(error.block.value) + ": " + error.message);
        }
        return messages;
    };
    logic::Brain empty;
    empty.id = logic::BlockId{1};
    empty.name = "empty";
    CHECK(errors_of(empty) == std::vector<std::string>{"1: The brain 'empty' has no states."});

    logic::Brain bad;
    bad.id = logic::BlockId{1};
    bad.name = "Bad Name";
    bad.initial = "sleeping";
    logic::BrainState first;
    first.id = logic::BlockId{2};
    first.name = "idle";
    first.on_enter = {ir::wait(3, number(1.0)), ir::wait_until(8, yes_no(true))};
    first.while_in = {ir::if_then(4, yes_no(true), {ir::run(5, "anything")})};
    first.transitions.push_back(logic::Transition{logic::BlockId{6}, "nowhere", {}, -1.0});
    logic::BrainState twin = first;
    twin.id = logic::BlockId{7};
    twin.on_enter.clear();
    twin.while_in.clear();
    twin.transitions.clear();
    bad.states = {first, twin};
    const std::vector<std::string> errors = errors_of(bad);
    const std::vector<std::string> expected{
        std::string("1: The brain name 'Bad Name' is not valid: use lowercase letters, ") +
            "digits and underscores, starting with a letter.",
        "7: There is already a state called 'idle'.",
        "1: The brain starts in 'sleeping', which is not one of its states.",
        "6: There is no state called 'nowhere' to go to.",
        "6: The time in the state before leaving must be 0 seconds or more.",
        "3: A brain cannot wait. Start a sequence from a rule instead.",
        "8: A brain cannot wait. Start a sequence from a rule instead.",
        "5: A brain cannot run a sequence, which may wait. Start it from a rule instead.",
    };
    CHECK(errors == expected);
}
