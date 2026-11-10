#include "engine/logic/vm.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace logic = engine::logic;
namespace reflect = engine::reflect;

namespace {

enum class Facing : std::uint8_t { North, East, South, West };

class Npc {
public:
    void heal(std::int32_t amount) { hp = std::min(hp + amount, 100); }
    void turn(Facing to) { facing = to; }
    void say(const std::string& line) { lines.push_back(line); }
    void poke();
    [[nodiscard]] bool hp_below(std::int32_t threshold) const { return hp < threshold; }
    [[nodiscard]] Facing facing_now() const { return facing; }
    [[nodiscard]] double speed_now() const { return speed; }

    std::int32_t hp = 50;
    double speed = 1.5;
    Facing facing = Facing::South;
    std::string name = "Elder";
    bool asleep = false;
    std::int32_t id = 7;

    std::vector<std::string> lines;
    logic::Vm* vm{nullptr};
    std::optional<logic::TaskId> poke_result;
    bool poke_bind_result = true;
};

class Unregistered {
public:
    int value = 0;
};

void register_types(reflect::Registry& registry) {
    registry.add_choice_list<Facing>("facing")
        .entry(Facing::North, "north")
        .entry(Facing::East, "east")
        .entry(Facing::South, "south")
        .entry(Facing::West, "west");

    auto npc = registry.add_type<Npc>("npc");
    npc.property<&Npc::hp>("hp").range(0, 100);
    npc.property<&Npc::speed>("speed").range(0.0, 10.0);
    npc.property<&Npc::facing>("facing");
    npc.property<&Npc::name>("name");
    npc.property<&Npc::asleep>("asleep");
    npc.property<&Npc::id>("id").flags(reflect::property_flag::read_only);
    npc.action<&Npc::heal>("heal", {"amount"})
        .text("Heal {amount} HP")
        .param("amount")
        .range(1, 999);
    npc.action<&Npc::turn>("turn", {"to"});
    npc.action<&Npc::say>("say", {"line"});
    npc.action<&Npc::poke>("poke");
    npc.condition<&Npc::hp_below>("hp_below", {"threshold"});
    npc.getter<&Npc::facing_now>("facing_now");
    npc.getter<&Npc::speed_now>("speed_now");
}

// The Vm is refused while it runs a script: this tries anyway, from inside an action.
void Npc::poke() {
    if (vm != nullptr) {
        poke_result = vm->start(logic::ScriptId{1}, "anything");
        poke_bind_result = vm->unbind("elder");
    }
}

struct Fixture {
    Fixture() {
        register_types(registry);
        vm.set_log_handler([this](std::string_view script, std::string_view text) {
            log.push_back(std::string(script) + ": " + std::string(text));
        });
        elder.vm = &vm;
        REQUIRE(registry.errors().empty());
        REQUIRE(vm.bind("elder", reflect::ObjectRef::of(elder)));
    }

    /// Loads a script that must run without errors.
    logic::ScriptId load_ok(std::string_view source, std::string_view name = "test") {
        const std::optional<logic::ScriptId> id = vm.load(name, source);
        for (const logic::ScriptError& error : vm.errors()) {
            INFO("line " << error.line << ": " << error.message);
        }
        REQUIRE(vm.errors().empty());
        REQUIRE(id.has_value());
        return id.value_or(logic::ScriptId{});
    }

    /// Loads a script that must fail with one error, and returns that error.
    logic::ScriptError load_error(std::string_view source) {
        vm.clear_errors();
        const std::optional<logic::ScriptId> id = vm.load("test", source);
        CHECK_FALSE(id.has_value());
        REQUIRE(vm.errors().size() == 1);
        return vm.errors().front();
    }

    reflect::Registry registry;
    logic::Vm vm{registry};
    Npc elder;
    std::vector<std::string> log;
};

} // namespace

TEST_CASE("Scripts call actions, conditions and getters on bound objects") {
    Fixture f;
    f.load_ok(R"(
        sx.call("elder", "heal", 20)
        sx.call("elder", "turn", "north")
        sx.call("elder", "say", "Welcome!")
        print(sx.call("elder", "hp_below", 80), sx.call("elder", "hp_below", 60))
        print(sx.call("elder", "facing_now"), sx.call("elder", "speed_now"))
    )");
    CHECK(f.elder.hp == 70);
    CHECK(f.elder.facing == Facing::North);
    REQUIRE(f.elder.lines.size() == 1);
    CHECK(f.elder.lines[0] == "Welcome!");
    REQUIRE(f.log.size() == 2);
    CHECK(f.log[0] == "test: true false");
    CHECK(f.log[1] == "test: north 1.5");
}

TEST_CASE("Scripts read and write properties") {
    Fixture f;
    f.load_ok(R"(
        sx.set("elder", "hp", 30)
        sx.set("elder", "speed", 2.25)
        sx.set("elder", "facing", "west")
        sx.set("elder", "name", "Old Elder")
        sx.set("elder", "asleep", true)
        sx.log(sx.get("elder", "hp"), sx.get("elder", "facing"), sx.get("elder", "name"))
    )");
    CHECK(f.elder.hp == 30);
    CHECK(f.elder.speed == doctest::Approx(2.25));
    CHECK(f.elder.facing == Facing::West);
    CHECK(f.elder.name == "Old Elder");
    CHECK(f.elder.asleep);
    REQUIRE(f.log.size() == 1);
    CHECK(f.log[0] == "test: 30 west Old Elder");
}

TEST_CASE("Choices also accept their numeric value") {
    Fixture f;
    f.load_ok(R"(sx.set("elder", "facing", 1))");
    CHECK(f.elder.facing == Facing::East);
}

TEST_CASE("Wrong use of the script API stops the task with a plain-language error") {
    Fixture f;
    struct Case {
        const char* source;
        const char* message;
    };
    const Case cases[] = {
        {R"(sx.call("chest", "open"))", "There is no object called 'chest'."},
        {R"(sx.call(42, "heal", 1))",
         "sx.call needs an object name as its first value, but was given 42."},
        {R"(sx.call("elder", "fly"))", "'elder' has no action, condition or value called 'fly'."},
        {R"(sx.call("elder", "heal"))", "'Heal' needs 1 value, but was given 0."},
        {R"(sx.call("elder", "heal", "lots"))",
         "'Amount' of 'Heal' must be a whole number, but was given 'lots'."},
        {R"(sx.call("elder", "heal", 2.5))",
         "'Amount' of 'Heal' must be a whole number, but was given 2.5."},
        {R"(sx.call("elder", "heal", 5000))",
         "'Amount' of 'Heal' must be between 1 and 999, but was given 5000."},
        {R"(sx.call("elder", "turn", "up"))",
         "'To' of 'Turn' must be one of: north, east, south, west, but was given 'up'."},
        {R"(sx.call("elder", "turn", 9))",
         "'To' of 'Turn' must be one of: north, east, south, west, but was given 9."},
        {R"(sx.get("elder", "mana"))", "'elder' has no property called 'mana'."},
        {R"(sx.set("elder", "hp", 101))",
         "'Hp' of 'elder' must be between 0 and 100, but was given 101."},
        {R"(sx.set("elder", "asleep", 1))",
         "'Asleep' of 'elder' must be yes or no (true or false), but was given 1."},
        {R"(sx.set("elder", "id", 3))", "'Id' of 'elder' cannot be changed."},
        {R"(sx.set("elder", "hp"))", "sx.set needs an object, a property and one value."},
        {R"(sx.wait(-1))", "sx.wait needs one number of seconds, 0 or more, but was given -1."},
        {R"(sx.wait("soon"))",
         "sx.wait needs one number of seconds, 0 or more, but was given 'soon'."},
        {R"(error("custom problem"))", "custom problem"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.source);
        const logic::ScriptError error = f.load_error(std::string("\n") + c.source);
        CHECK(error.kind == logic::ScriptErrorKind::Runtime);
        CHECK(error.script == "test");
        CHECK(error.line == 2);
        CHECK(error.message == c.message);
    }
    CHECK(f.elder.hp == 50);
}

TEST_CASE("Sequences wait across updates") {
    Fixture f;
    const logic::ScriptId script = f.load_ok(R"(
        function intro(times)
            for i = 1, times do
                sx.log("step", i)
                sx.wait(0.5)
            end
            sx.log("done")
        end
    )");
    const std::optional<logic::TaskId> task =
        f.vm.start(script, "intro", std::vector<reflect::Value>{std::int64_t{2}});
    REQUIRE(task.has_value());
    const logic::TaskId id = task.value_or(logic::TaskId{});
    CHECK(f.vm.is_running(id));
    CHECK(f.vm.task_count() == 1);
    CHECK(f.log == std::vector<std::string>{"test: step 1"});

    f.vm.update(0.25);
    CHECK(f.log.size() == 1);
    f.vm.update(0.25);
    CHECK(f.log.back() == "test: step 2");
    f.vm.update(0.5);
    CHECK(f.log.back() == "test: done");
    CHECK_FALSE(f.vm.is_running(id));
    CHECK(f.vm.task_count() == 0);
    CHECK(f.vm.errors().empty());
}

TEST_CASE("Many small updates add up to a wait without rounding drift") {
    Fixture f;
    f.load_ok(R"(
        sx.wait(1)
        sx.log("resumed")
    )");
    for (int i = 0; i < 9; ++i) {
        f.vm.update(0.1);
    }
    CHECK(f.log.empty());
    f.vm.update(0.1);
    CHECK(f.log == std::vector<std::string>{"test: resumed"});
}

TEST_CASE("wait(0) resumes in the next update, and tasks resume in start order") {
    Fixture f;
    const logic::ScriptId script = f.load_ok(R"(
        function tick(label)
            sx.wait(0)
            sx.log(label)
        end
    )");
    for (const char* label : {"a", "b", "c"}) {
        REQUIRE(f.vm.start(script, "tick", std::vector<reflect::Value>{std::string(label)}));
    }
    CHECK(f.log.empty());
    f.vm.update(0.0);
    CHECK(f.log == std::vector<std::string>{"test: a", "test: b", "test: c"});
}

TEST_CASE("Tasks can be stopped") {
    Fixture f;
    const logic::ScriptId script = f.load_ok(R"(
        function forever()
            while true do
                sx.wait(1)
                sx.log("tick")
            end
        end
    )");
    const std::optional<logic::TaskId> task = f.vm.start(script, "forever");
    REQUIRE(task.has_value());
    f.vm.update(1.0);
    CHECK(f.log.size() == 1);
    CHECK(f.vm.stop(task.value_or(logic::TaskId{})));
    CHECK_FALSE(f.vm.stop(task.value_or(logic::TaskId{})));
    f.vm.update(1.0);
    CHECK(f.log.size() == 1);
    CHECK(f.vm.task_count() == 0);
}

TEST_CASE("An endless loop is stopped and reported, and other tasks keep running") {
    const reflect::Registry registry;
    logic::VmLimits limits;
    limits.steps_per_run = 10'000;
    logic::Vm vm(registry, limits);
    std::vector<std::string> log;
    vm.set_log_handler([&](std::string_view, std::string_view text) { log.emplace_back(text); });

    const std::optional<logic::ScriptId> script = vm.load("village", R"(
        function stuck()
            sx.wait(0)
            local n = 0
            while true do
                n = n + 1
            end
        end
        function sneaky()
            sx.wait(0)
            while true do
                pcall(function() while true do end end)
            end
        end
        function healthy()
            for i = 1, 3 do
                sx.wait(0)
                sx.log(i)
            end
        end
    )");
    REQUIRE(script.has_value());
    const logic::ScriptId id = script.value_or(logic::ScriptId{});
    REQUIRE(vm.start(id, "stuck"));
    REQUIRE(vm.start(id, "sneaky"));
    REQUIRE(vm.start(id, "healthy"));

    vm.update(0.1);
    REQUIRE(vm.errors().size() == 2);
    for (const logic::ScriptError& error : vm.errors()) {
        CHECK(error.kind == logic::ScriptErrorKind::Budget);
        CHECK(error.script == "village");
        CHECK(error.message.find("ran too long without waiting") != std::string::npos);
    }
    // The loop that was stopped. In sneaky(), the pcall swallows the stops of the inner
    // loop, so the stop takes effect at the outer loop.
    CHECK(vm.errors()[0].line == 5);
    CHECK(vm.errors()[1].line == 11);
    CHECK(log == std::vector<std::string>{"1"});
    CHECK(vm.task_count() == 1);

    vm.update(0.1);
    vm.update(0.1);
    CHECK(log == std::vector<std::string>{"1", "2", "3"});
    CHECK(vm.task_count() == 0);
}

TEST_CASE("The frame budget defers tasks to the next update instead of stopping them") {
    const reflect::Registry registry;
    logic::VmLimits limits;
    limits.steps_per_frame = 1'000;
    logic::Vm vm(registry, limits);
    std::vector<std::string> log;
    vm.set_log_handler([&](std::string_view, std::string_view text) { log.emplace_back(text); });

    const std::optional<logic::ScriptId> script = vm.load("busy", R"(
        function work(label)
            sx.wait(0)
            local total = 0
            for i = 1, 2000 do total = total + i end
            sx.log(label)
        end
    )");
    REQUIRE(script.has_value());
    const logic::ScriptId id = script.value_or(logic::ScriptId{});
    REQUIRE(vm.start(id, "work", std::vector<reflect::Value>{std::string("first")}));
    REQUIRE(vm.start(id, "work", std::vector<reflect::Value>{std::string("second")}));

    vm.update(0.0);
    CHECK(log == std::vector<std::string>{"first"});
    CHECK(vm.steps_last_update() >= 2000);
    vm.update(0.0);
    CHECK(log == std::vector<std::string>{"first", "second"});
    CHECK(vm.errors().empty());
}

TEST_CASE("Scripts that use too much memory are stopped, and the Vm keeps working") {
    const reflect::Registry registry;
    logic::VmLimits limits;
    limits.memory_bytes = std::size_t{4} * 1024 * 1024;
    logic::Vm vm(registry, limits);

    const std::optional<logic::ScriptId> hog = vm.load("hog", R"(
        local t = {}
        for i = 1, 10000000 do
            t[i] = "item" .. i
        end
    )");
    CHECK_FALSE(hog.has_value());
    REQUIRE(vm.errors().size() == 1);
    CHECK(vm.errors()[0].kind == logic::ScriptErrorKind::Memory);
    CHECK(vm.errors()[0].message == "Stopped because the scripts used more than 4 MB of memory.");
    CHECK(vm.errors()[0].line == 4);

    // The host collected the hog's memory once the task was stopped.
    CHECK(vm.memory_used() < limits.memory_bytes / 2);

    vm.clear_errors();
    std::string output;
    vm.set_log_handler([&](std::string_view, std::string_view text) { output = text; });
    const std::optional<logic::ScriptId> after = vm.load("after", R"(
        local t = {}
        for i = 1, 1000 do t[i] = "item" .. i end
        sx.log("still fine", #t)
    )");
    CHECK(vm.errors().empty());
    CHECK(after.has_value());
    CHECK(output == "still fine 1000");
}

TEST_CASE("The sandbox protects the libraries and keeps scripts apart") {
    Fixture f;
    CHECK(f.load_error(R"(sx.call = nil)").message.find("readonly") != std::string::npos);
    CHECK(f.load_error(R"(string.upper = nil)").message.find("readonly") != std::string::npos);

    f.vm.clear_errors();
    f.load_ok(R"(
        secret = 42
        print(io == nil, require == nil, os.execute == nil, loadstring == nil)
    )",
              "first");
    f.load_ok(R"(print(secret == nil))", "second");
    REQUIRE(f.log.size() == 2);
    CHECK(f.log[0] == "first: true true true true");
    CHECK(f.log[1] == "second: true");
}

TEST_CASE("Compile errors are reported with their line") {
    Fixture f;
    const logic::ScriptError error = f.load_error("local x = 1\nlocal y = = 2\n");
    CHECK(error.kind == logic::ScriptErrorKind::Compile);
    CHECK(error.script == "test");
    CHECK(error.line == 2);
    CHECK_FALSE(error.message.empty());
}

TEST_CASE("Starting a missing function is reported") {
    Fixture f;
    const logic::ScriptId script = f.load_ok("function present() end");
    CHECK(f.vm.start(script, "present").has_value());
    CHECK_FALSE(f.vm.start(script, "absent").has_value());
    REQUIRE(f.vm.errors().size() == 1);
    CHECK(f.vm.errors()[0].message == "The script has no function called 'absent'.");
    CHECK_FALSE(f.vm.start(logic::ScriptId{999}, "present").has_value());
}

TEST_CASE("A script whose top level waits keeps running as a task") {
    Fixture f;
    const logic::ScriptId script = f.load_ok(R"(
        sx.wait(1)
        sx.call("elder", "heal", 5)
    )");
    CHECK(script.is_valid());
    CHECK(f.vm.task_count() == 1);
    CHECK(f.elder.hp == 50);
    f.vm.update(1.0);
    CHECK(f.elder.hp == 55);
}

TEST_CASE("wait cannot be used inside a coroutine the script made itself") {
    Fixture f;
    const logic::ScriptError error = f.load_error(R"(
        local co = coroutine.wrap(function() sx.wait(1) end)
        co()
    )");
    CHECK(error.kind == logic::ScriptErrorKind::Runtime);
    CHECK(error.message.find("sx.wait can only be used directly in a sequence") !=
          std::string::npos);
}

TEST_CASE("wait cannot be used inside a callback") {
    Fixture f;
    const logic::ScriptError error = f.load_error(R"(
        local list = {3, 1, 2}
        table.sort(list, function(a, b) sx.wait(0) return a < b end)
    )");
    CHECK(error.kind == logic::ScriptErrorKind::Runtime);
    CHECK(error.message.find("sx.wait can only be used directly in a sequence") !=
          std::string::npos);
}

TEST_CASE("Several Vms can run side by side") {
    reflect::Registry registry;
    register_types(registry);
    logic::Vm first(registry);
    logic::Vm second(registry);
    Npc a;
    Npc b;
    REQUIRE(first.bind("npc", reflect::ObjectRef::of(a)));
    REQUIRE(second.bind("npc", reflect::ObjectRef::of(b)));
    REQUIRE(first.load("s", "shared = 1\nsx.wait(1)\nsx.call('npc', 'heal', 10)"));
    REQUIRE(second.load("s", "sx.wait(1)\nsx.call('npc', 'heal', shared == nil and 30 or 1)"));
    first.update(1.0);
    CHECK(a.hp == 60);
    CHECK(b.hp == 50);
    second.update(1.0);
    CHECK(b.hp == 80);
    CHECK(first.errors().empty());
    CHECK(second.errors().empty());
}

TEST_CASE("Binding checks its inputs") {
    Fixture f;
    Npc other;
    Unregistered stranger;
    CHECK_FALSE(f.vm.bind("elder", reflect::ObjectRef::of(other)));       // taken
    CHECK_FALSE(f.vm.bind("Bad Name", reflect::ObjectRef::of(other)));    // invalid id
    CHECK_FALSE(f.vm.bind("ghost", reflect::ObjectRef{}));                // null
    CHECK_FALSE(f.vm.bind("stranger", reflect::ObjectRef::of(stranger))); // unregistered
    CHECK(f.vm.bind("guard", reflect::ObjectRef::of(other)));
    CHECK(f.vm.unbind("guard"));
    CHECK_FALSE(f.vm.unbind("guard"));
    CHECK(f.load_error(R"(sx.call("guard", "heal", 1))").message ==
          "There is no object called 'guard'.");
}

TEST_CASE("Bound functions cannot call back into the running Vm") {
    Fixture f;
    f.load_ok(R"(sx.call("elder", "poke"))");
    CHECK_FALSE(f.elder.poke_result.has_value());
    CHECK_FALSE(f.elder.poke_bind_result);
}

TEST_CASE("Script names are checked") {
    Fixture f;
    CHECK_FALSE(f.vm.load("", "print(1)").has_value());
    CHECK_FALSE(f.vm.load("has space", "print(1)").has_value());
    REQUIRE(f.vm.errors().size() == 2);
    CHECK(f.vm.errors()[0].kind == logic::ScriptErrorKind::Compile);
    f.vm.clear_errors();
    f.load_ok("print(1)", "maps/village-01/intro.seq");
    CHECK(f.log.back() == "maps/village-01/intro.seq: 1");
}

TEST_CASE("Memory use is reported") {
    const Fixture f;
    CHECK(f.vm.memory_used() > 0);
}
