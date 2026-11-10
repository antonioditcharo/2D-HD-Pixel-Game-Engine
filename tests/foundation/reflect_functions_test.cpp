#include "engine/foundation/reflect.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reflect = engine::reflect;

namespace {

enum class Mood : std::uint8_t { Calm, Angry, Scared };

// A stand-in for an NPC component with one function of every role.
class TestNpc {
public:
    // Actions
    void heal(std::int32_t amount) { hp_ = std::min(hp_ + amount, max_hp_); }
    void say(const std::string& line, double seconds) {
        last_line_ = line;
        line_seconds_ = seconds;
    }
    void set_mood(Mood mood) noexcept { mood_ = mood; }
    void wave() { ++waves_; }
    void set_small(std::int8_t value) { small_ = value; }

    // Conditions
    [[nodiscard]] bool hp_below(std::int32_t threshold) const { return hp_ < threshold; }
    [[nodiscard]] bool is_mood(Mood mood) const noexcept { return mood_ == mood; }

    // Getters
    [[nodiscard]] std::int32_t hp() const { return hp_; }
    [[nodiscard]] Mood mood() const { return mood_; }
    [[nodiscard]] std::string last_line() const { return last_line_; }

    std::int32_t hp_ = 50;
    std::int32_t max_hp_ = 100;
    Mood mood_ = Mood::Calm;
    std::string last_line_;
    double line_seconds_ = 0.0;
    std::int32_t waves_ = 0;
    std::int8_t small_ = 0;
};

class OtherNpc {
public:
    void wave() {}
};

void register_npc(reflect::Registry& registry) {
    registry.add_choice_list<Mood>("mood")
        .entry(Mood::Calm, "calm")
        .entry(Mood::Angry, "angry")
        .entry(Mood::Scared, "scared");

    auto npc = registry.add_type<TestNpc>("test_npc").category("Characters");
    npc.property<&TestNpc::hp_>("current_hp").range(0, 100);

    npc.action<&TestNpc::heal>("heal", {"amount"})
        .tooltip("Restores HP, up to the maximum.")
        .text("Heal {amount} HP")
        .param("amount")
        .range(1, 999)
        .unit(reflect::Unit::None);
    npc.action<&TestNpc::say>("say", {"line", "seconds"})
        .text("Say {line} for {seconds}")
        .param("seconds")
        .unit(reflect::Unit::Seconds)
        .range(0.5, 30.0)
        .default_value(3.0);
    npc.action<&TestNpc::set_mood>("set_mood", {"mood"});
    npc.action<&TestNpc::wave>("wave");
    npc.action<&TestNpc::set_small>("set_small", {"value"});

    npc.condition<&TestNpc::hp_below>("hp_below", {"threshold"}).text("HP is below {threshold}");
    npc.condition<&TestNpc::is_mood>("is_mood", {"mood"});

    npc.getter<&TestNpc::hp>("hp");
    npc.getter<&TestNpc::mood>("mood");
    npc.getter<&TestNpc::last_line>("last_line");

    npc.event<std::int32_t, Mood>("damaged", {"amount", "mood"})
        .text("When damaged by {amount}")
        .tooltip("Happens whenever the NPC loses HP.");
    npc.event<>("defeated");

    registry.add_type<OtherNpc>("other_npc").action<&OtherNpc::wave>("wave");
}

[[nodiscard]] bool has_error(const reflect::Registry& registry, reflect::ErrorCode code) {
    for (const reflect::RegistrationError& error : registry.errors()) {
        if (error.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("roles have plain-language names") {
    CHECK(reflect::role_display_name(reflect::FunctionRole::Action) == "Action");
    CHECK(reflect::role_display_name(reflect::FunctionRole::Condition) == "Condition");
    CHECK(reflect::role_display_name(reflect::FunctionRole::Getter) == "Value");
}

TEST_CASE("functions and events are described with their parameters") {
    reflect::Registry registry;
    register_npc(registry);
    REQUIRE(registry.errors().empty());
    const reflect::TypeInfo* npc = registry.find_type<TestNpc>();
    REQUIRE(npc != nullptr);
    CHECK(npc->functions.size() == 10);
    CHECK(npc->events.size() == 2);

    const reflect::FunctionInfo* heal = npc->find_function("heal");
    REQUIRE(heal != nullptr);
    CHECK(heal->role == reflect::FunctionRole::Action);
    CHECK_FALSE(heal->result_kind.has_value());
    CHECK(heal->meta.display_name == "Heal");
    CHECK(heal->meta.tooltip == "Restores HP, up to the maximum.");
    CHECK(reflect::block_text(*heal) == "Heal {amount} HP");
    REQUIRE(heal->parameters.size() == 1);
    const reflect::ParameterInfo& amount = heal->parameters[0];
    CHECK(amount.id == "amount");
    CHECK(amount.kind == reflect::ValueKind::Int);
    CHECK(amount.limits.min == 1.0);
    CHECK(amount.limits.max == 999.0);
    // The C++ default of 0 is outside 1..999, so it moved to the nearest bound.
    CHECK(std::get<std::int64_t>(amount.default_value) == 1);

    const reflect::FunctionInfo* say = npc->find_function("say");
    REQUIRE(say != nullptr);
    REQUIRE(say->parameters.size() == 2);
    CHECK(say->parameters[0].kind == reflect::ValueKind::Text); // const std::string& → Text
    CHECK(say->parameters[1].kind == reflect::ValueKind::Float);
    CHECK(say->parameters[1].unit == reflect::Unit::Seconds);
    CHECK(std::get<double>(say->find_parameter("seconds")->default_value) == 3.0);

    const reflect::FunctionInfo* set_mood = npc->find_function("set_mood");
    REQUIRE(set_mood != nullptr);
    CHECK(set_mood->parameters[0].kind == reflect::ValueKind::Choice);
    CHECK(set_mood->parameters[0].choice == registry.find_choice_list<Mood>());
    CHECK(std::get<std::int64_t>(set_mood->parameters[0].default_value) ==
          static_cast<std::int64_t>(Mood::Calm));               // first entry
    CHECK(reflect::block_text(*set_mood) == "Set mood {mood}"); // generated default

    const reflect::FunctionInfo* wave = npc->find_function("wave");
    REQUIRE(wave != nullptr);
    CHECK(wave->parameters.empty());
    CHECK(reflect::block_text(*wave) == "Wave");

    const reflect::FunctionInfo* hp_below = npc->find_function("hp_below");
    REQUIRE(hp_below != nullptr);
    CHECK(hp_below->role == reflect::FunctionRole::Condition);
    CHECK(hp_below->result_kind == reflect::ValueKind::Bool);

    const reflect::FunctionInfo* mood = npc->find_function("mood");
    REQUIRE(mood != nullptr);
    CHECK(mood->role == reflect::FunctionRole::Getter);
    CHECK(mood->result_kind == reflect::ValueKind::Choice);
    CHECK(mood->result_choice == registry.find_choice_list<Mood>());

    const reflect::EventInfo* damaged = npc->find_event("damaged");
    REQUIRE(damaged != nullptr);
    CHECK(reflect::block_text(*damaged) == "When damaged by {amount}");
    REQUIRE(damaged->parameters.size() == 2);
    CHECK(damaged->parameters[0].kind == reflect::ValueKind::Int);
    CHECK(damaged->parameters[1].kind == reflect::ValueKind::Choice);
    CHECK(reflect::block_text(*npc->find_event("defeated")) == "Defeated");

    CHECK(npc->has_member("current_hp"));
    CHECK(npc->has_member("heal"));
    CHECK(npc->has_member("damaged"));
    CHECK_FALSE(npc->has_member("jump"));
}

TEST_CASE("functions can be called through the registry") {
    reflect::Registry registry;
    register_npc(registry);
    const reflect::TypeInfo* npc_type = registry.find_type<TestNpc>();
    REQUIRE(npc_type != nullptr);
    TestNpc npc;

    const auto call = [&](std::string_view id, std::vector<reflect::Value> arguments) {
        const reflect::FunctionInfo* function = npc_type->find_function(id);
        REQUIRE(function != nullptr);
        return reflect::call(npc, *function, arguments);
    };

    SUBCASE("actions change the object") {
        CHECK(call("heal", {std::int64_t{20}}).ok());
        CHECK(npc.hp_ == 70);
        CHECK(call("say", {std::string("Hello!"), 2.5}).ok());
        CHECK(npc.last_line_ == "Hello!");
        CHECK(npc.line_seconds_ == doctest::Approx(2.5));
        CHECK(call("set_mood", {static_cast<std::int64_t>(Mood::Angry)}).ok());
        CHECK(npc.mood_ == Mood::Angry);
        CHECK(call("wave", {}).ok());
        CHECK(npc.waves_ == 1);
    }

    SUBCASE("conditions and getters return values") {
        // The call's result, or an empty Value if it failed.
        const auto result = [&](std::string_view id, std::vector<reflect::Value> arguments) {
            const reflect::CallResult r = call(id, std::move(arguments));
            CHECK(r.ok());
            return r.value.value_or(reflect::Value{});
        };
        CHECK(std::get<bool>(result("hp_below", {std::int64_t{60}})) == true);
        CHECK(std::get<bool>(result("hp_below", {std::int64_t{10}})) == false);
        CHECK(std::get<bool>(result("is_mood", {std::int64_t{0}})) == true);
        CHECK(std::get<std::int64_t>(result("hp", {})) == 50);
        CHECK(std::get<std::int64_t>(result("mood", {})) == static_cast<std::int64_t>(Mood::Calm));
        npc.last_line_ = "Hi";
        CHECK(std::get<std::string>(result("last_line", {})) == "Hi");
    }

    SUBCASE("bad calls are rejected with the offending argument and change nothing") {
        reflect::CallResult result = call("heal", {});
        CHECK(result.error == reflect::CallError::WrongArgumentCount);

        result = call("heal", {std::int64_t{0}});
        CHECK(result.error == reflect::CallError::OutOfRange);
        CHECK(result.argument == 0);

        result = call("say", {std::string("Hi"), std::int64_t{2}});
        CHECK(result.error == reflect::CallError::WrongKind);
        CHECK(result.argument == 1);

        result = call("set_mood", {std::int64_t{99}});
        CHECK(result.error == reflect::CallError::UnknownChoice);

        result = call("set_small", {std::int64_t{1000}});
        CHECK(result.error == reflect::CallError::NotRepresentable);
        CHECK(result.argument == 0);

        CHECK(npc.hp_ == 50);
        CHECK(npc.last_line_.empty());
        CHECK(npc.mood_ == Mood::Calm);
        CHECK(npc.small_ == 0);
    }

    SUBCASE("a function cannot be called on an object of another type") {
        OtherNpc other;
        const reflect::FunctionInfo* wave = npc_type->find_function("wave");
        REQUIRE(wave != nullptr);
        CHECK(reflect::call(other, *wave, {}).error == reflect::CallError::WrongType);
    }
}

namespace {

enum class Unlisted : std::uint8_t { A };

class Broken {
public:
    void act(std::int32_t /*value*/) {}
    void act_twice(std::int32_t /*a*/, std::int32_t /*b*/) {}
    void take_unlisted(Unlisted /*value*/) {}
    [[nodiscard]] Unlisted give_unlisted() const { return Unlisted::A; }
    std::int32_t value = 0;
};

} // namespace

TEST_CASE("function and event registration problems are collected") {
    reflect::Registry registry;
    auto broken = registry.add_type<Broken>("broken");
    broken.property<&Broken::value>("value");

    SUBCASE("wrong number of parameter ids") {
        broken.action<&Broken::act>("act", {});
        broken.action<&Broken::act_twice>("act_twice", {"only_one"});
        CHECK(has_error(registry, reflect::ErrorCode::ParameterCountMismatch));
        CHECK(registry.find_type<Broken>()->functions.empty());
    }

    SUBCASE("ids clash with existing members or are invalid") {
        broken.action<&Broken::act>("value", {"amount"});
        CHECK(has_error(registry, reflect::ErrorCode::DuplicateMember));
        broken.event<>("Bad Event");
        CHECK(has_error(registry, reflect::ErrorCode::InvalidId));
        broken.action<&Broken::act_twice>("act_twice", {"same", "same"});
        CHECK(has_error(registry, reflect::ErrorCode::DuplicateMember));
    }

    SUBCASE("block text must only name real parameters") {
        broken.action<&Broken::act>("act", {"amount"}).text("Act {amont}");
        CHECK(has_error(registry, reflect::ErrorCode::InvalidBlockText));
        CHECK(registry.find_type<Broken>()->functions[0].text.empty());

        reflect::Registry second;
        second.add_type<Broken>("broken")
            .action<&Broken::act>("act", {"amount"})
            .text("Act {amount");
        CHECK(has_error(second, reflect::ErrorCode::InvalidBlockText));
    }

    SUBCASE("describing a parameter that does not exist") {
        broken.action<&Broken::act>("act", {"amount"}).param("amnt").range(0, 1);
        CHECK(has_error(registry, reflect::ErrorCode::UnknownParameter));
    }

    SUBCASE("defaults must fit") {
        broken.action<&Broken::act>("act", {"amount"})
            .param("amount")
            .range(0, 10)
            .default_value(std::int64_t{50});
        CHECK(has_error(registry, reflect::ErrorCode::InvalidDefault));
    }

    SUBCASE("enums used by parameters or results must be registered first") {
        broken.action<&Broken::take_unlisted>("take_unlisted", {"value"});
        CHECK(has_error(registry, reflect::ErrorCode::UnknownChoiceList));

        reflect::Registry second;
        second.add_type<Broken>("broken").getter<&Broken::give_unlisted>("give_unlisted");
        CHECK(has_error(second, reflect::ErrorCode::UnknownChoiceList));
    }
}

TEST_CASE("objects can be used through a type-erased reference") {
    reflect::Registry registry;
    register_npc(registry);
    REQUIRE(registry.errors().empty());
    const reflect::TypeInfo* npc_type = registry.find_type<TestNpc>();
    REQUIRE(npc_type != nullptr);

    TestNpc npc;
    OtherNpc other;
    const reflect::ObjectRef ref = reflect::ObjectRef::of(npc);
    const reflect::ObjectRef other_ref = reflect::ObjectRef::of(other);
    const reflect::ObjectRef null_ref;

    SUBCASE("the reference knows its object and type") {
        CHECK(ref.object() == &npc);
        CHECK_FALSE(ref.is_null());
        CHECK(null_ref.is_null());
        CHECK(ref.as<TestNpc>() == &npc);
        CHECK(ref.as<OtherNpc>() == nullptr);
        CHECK(null_ref.as<TestNpc>() == nullptr);
        CHECK(registry.find_type(ref) == npc_type);
        CHECK(registry.find_type(other_ref) == registry.find_type<OtherNpc>());
        CHECK(registry.find_type(null_ref) == nullptr);
    }

    SUBCASE("properties") {
        const reflect::PropertyInfo* hp = npc_type->find_property("current_hp");
        REQUIRE(hp != nullptr);
        CHECK(reflect::set_property(ref, *hp, reflect::Value{std::int64_t{80}}) ==
              reflect::SetResult::Ok);
        CHECK(npc.hp_ == 80);
        CHECK(reflect::get_property(ref, *hp) == std::optional<reflect::Value>(std::int64_t{80}));
        CHECK(reflect::set_property(ref, *hp, reflect::Value{std::int64_t{101}}) ==
              reflect::SetResult::OutOfRange);
        CHECK(reflect::set_property(other_ref, *hp, reflect::Value{std::int64_t{1}}) ==
              reflect::SetResult::WrongType);
        CHECK(reflect::set_property(null_ref, *hp, reflect::Value{std::int64_t{1}}) ==
              reflect::SetResult::WrongType);
        CHECK_FALSE(reflect::get_property(other_ref, *hp).has_value());
        CHECK_FALSE(reflect::get_property(null_ref, *hp).has_value());
        CHECK(npc.hp_ == 80);
    }

    SUBCASE("functions") {
        const reflect::FunctionInfo* heal = npc_type->find_function("heal");
        REQUIRE(heal != nullptr);
        const std::array<reflect::Value, 1> amount{reflect::Value{std::int64_t{10}}};
        CHECK(reflect::call(ref, *heal, amount).ok());
        CHECK(npc.hp_ == 60);
        CHECK(reflect::call(other_ref, *heal, amount).error == reflect::CallError::WrongType);
        CHECK(reflect::call(null_ref, *heal, amount).error == reflect::CallError::WrongType);
        CHECK(npc.hp_ == 60);

        const reflect::FunctionInfo* hp = npc_type->find_function("hp");
        REQUIRE(hp != nullptr);
        CHECK(reflect::call(ref, *hp, {}).value == std::optional<reflect::Value>(std::int64_t{60}));
    }
}
