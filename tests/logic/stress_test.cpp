// The stress test from ADR-007 (docs/plan/07-tech-stack-adrs.md): 5,000 rules and 200
// concurrent sequences. It checks that everything runs correctly at that scale and
// reports timings with MESSAGE. Timings are not asserted, because CI machines vary; the
// numbers measured on the development machine are recorded in docs/dev/logic-runtime.md.

#include "engine/logic/compiler.h"
#include "engine/logic/vm.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace ir = engine::logic::ir;

namespace {

class Counter {
public:
    void add(std::int32_t amount) { value += amount; }

    std::int32_t value = 0;
};

constexpr int object_count = 50;
constexpr int rule_count = 5'000;
constexpr int sequence_count = 200;
constexpr int frames = 60;

using Clock = std::chrono::steady_clock;

[[nodiscard]] double milliseconds_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

[[nodiscard]] std::string object_name(int index) {
    return "counter_" + std::to_string(index);
}

} // namespace

TEST_CASE("Stress: 5,000 rules and 200 concurrent sequences") {
    reflect::Registry registry;
    auto counter = registry.add_type<Counter>("counter");
    counter.property<&Counter::value>("value");
    counter.action<&Counter::add>("add", {"amount"});
    counter.event<std::int32_t>("ping", {"amount"});
    REQUIRE(registry.errors().empty());

    std::vector<Counter> counters(object_count);
    logic::Vm vm(registry);
    for (int i = 0; i < object_count; ++i) {
        REQUIRE(
            vm.bind(object_name(i), reflect::ObjectRef::of(counters[static_cast<std::size_t>(i)])));
    }

    // Rule i listens to the ping of counter i % 50 and adds 1 to it when the ping's amount
    // is above i % 7. Pinging every counter with amount 5 therefore fires the rules with
    // i % 7 < 5: 5 out of every 7.
    logic::LogicAsset asset;
    asset.name = "stress";
    std::uint64_t next_id = 1;
    for (int i = 0; i < rule_count; ++i) {
        const std::string object = object_name(i % object_count);
        logic::Rule rule;
        rule.id = logic::BlockId{next_id++};
        rule.object = object;
        rule.event = "ping";
        rule.conditions.push_back(ir::compare(logic::CompareOp::Greater, ir::event_value("amount"),
                                              ir::literal(reflect::Value{std::int64_t{i % 7}})));
        rule.actions.push_back(ir::do_action(next_id++, object, "add",
                                             {ir::literal(reflect::Value{std::int64_t{1}})}));
        asset.rules.push_back(std::move(rule));
    }
    // Each sequence adds 1 to its counter every frame (wait 0), for `frames` frames.
    for (int i = 0; i < sequence_count; ++i) {
        const std::uint64_t sequence_id = next_id;
        const std::uint64_t repeat_id = next_id + 1;
        const std::uint64_t wait_id = next_id + 2;
        const std::uint64_t add_id = next_id + 3;
        next_id += 4;
        logic::Sequence sequence;
        sequence.id = logic::BlockId{sequence_id};
        sequence.name = "walker_" + std::to_string(i);
        sequence.steps.push_back(
            ir::repeat(repeat_id, ir::literal(reflect::Value{std::int64_t{frames}}),
                       {ir::wait(wait_id, ir::literal(reflect::Value{0.0})),
                        ir::do_action(add_id, object_name(i % object_count), "add",
                                      {ir::literal(reflect::Value{std::int64_t{1}})})}));
        asset.sequences.push_back(std::move(sequence));
    }

    auto start = Clock::now();
    const logic::CompiledLogic compiled = logic::compile(
        asset, registry, [&](std::string_view name) { return vm.object_types(name); });
    const double compile_ms = milliseconds_since(start);
    REQUIRE(compiled.ok());

    start = Clock::now();
    const std::optional<logic::ScriptId> script = vm.load(asset.name, compiled.source);
    const double load_ms = milliseconds_since(start);
    REQUIRE(script.has_value());
    REQUIRE(vm.errors().empty());

    start = Clock::now();
    for (int i = 0; i < sequence_count; ++i) {
        REQUIRE(vm.start(script.value_or(logic::ScriptId{}),
                         logic::sequence_entry("walker_" + std::to_string(i))));
    }
    const double start_ms = milliseconds_since(start);
    CHECK(vm.task_count() == sequence_count);

    // One round of events: every counter pinged once, firing 5,000 rules.
    start = Clock::now();
    const std::vector<reflect::Value> payload{reflect::Value{std::int64_t{5}}};
    for (int i = 0; i < object_count; ++i) {
        REQUIRE(vm.emit(object_name(i), "ping", payload));
    }
    const double events_ms = milliseconds_since(start);

    // Frames with all 200 sequences resuming.
    double worst_frame_ms = 0.0;
    start = Clock::now();
    for (int frame = 0; frame < frames; ++frame) {
        const auto frame_start = Clock::now();
        vm.update(1.0 / 60.0);
        const double frame_ms = milliseconds_since(frame_start);
        worst_frame_ms = frame_ms > worst_frame_ms ? frame_ms : worst_frame_ms;
    }
    const double frames_ms = milliseconds_since(start);

    CHECK(vm.errors().empty());
    CHECK(vm.task_count() == 0);
    // Rules: 5,000 / 50 = 100 per counter, of which those with i % 7 < 5 fired. Sequences:
    // 4 per counter, each adding `frames`.
    std::int64_t total = 0;
    for (const Counter& c : counters) {
        total += c.value;
    }
    std::int64_t fired = 0;
    for (int i = 0; i < rule_count; ++i) {
        fired += (i % 7) < 5 ? 1 : 0;
    }
    CHECK(total == fired + std::int64_t{sequence_count} * frames);

    MESSAGE("source lines: " << compiled.line_blocks.size());
    MESSAGE("compile: " << compile_ms << " ms, load: " << load_ms
                        << " ms, start 200 sequences: " << start_ms << " ms");
    MESSAGE("fire 5,000 rules: " << events_ms << " ms");
    MESSAGE("60 frames of 200 sequences: " << frames_ms << " ms in total, worst frame "
                                           << worst_frame_ms << " ms");
    MESSAGE("VM memory: " << vm.memory_used() / 1024 << " KiB");
}
