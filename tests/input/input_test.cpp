#include "engine/input/input.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace input = engine::input;
namespace reflect = engine::reflect;
using input::Binding;
using input::Button;
using input::Key;

TEST_CASE("The RPG preset binds keys and buttons to actions") {
    const input::InputMap map = input::InputMap::rpg_preset();
    CHECK(map.actions() == std::vector<std::string>{"cancel", "interact", "jump", "menu",
                                                    "move_down", "move_left", "move_right",
                                                    "move_up"});
    CHECK(map.actions_for(Binding::key(Key::Z)) == std::vector<std::string>{"interact"});
    // One button can do two things: Space and South talk, and jump in games seen from the side.
    CHECK(map.actions_for(Binding::key(Key::Space)) ==
          std::vector<std::string>{"interact", "jump"});
    CHECK(map.actions_for(Binding::button(Button::South)) ==
          std::vector<std::string>{"interact", "jump"});
    CHECK(map.bindings("move_up").size() == 3);
    CHECK(map.actions_for(Binding::key(Key::Q)).empty());
}

TEST_CASE("Binding checks action names and duplicates") {
    input::InputMap map;
    CHECK(map.bind("jump", Binding::key(Key::Space)));
    CHECK_FALSE(map.bind("jump", Binding::key(Key::Space)));
    CHECK_FALSE(map.bind("Jump Now", Binding::key(Key::J)));
    CHECK(map.unbind("jump", Binding::key(Key::Space)));
    CHECK_FALSE(map.has_action("jump"));
    CHECK_FALSE(map.unbind("jump", Binding::key(Key::Space)));
}

TEST_CASE("Actions are held while any binding is down, and pressed once") {
    input::InputState state(input::InputMap::rpg_preset());
    state.begin_step();
    state.set(Binding::key(Key::W), true);
    CHECK(state.held("move_up"));
    CHECK(state.pressed("move_up"));
    state.set(Binding::key(Key::Up), true); // a second control for the same action
    state.set(Binding::key(Key::W), true);  // a repeat
    CHECK(state.pressed_actions() == std::vector<std::string>{"move_up"});

    state.begin_step();
    CHECK(state.held("move_up"));
    CHECK_FALSE(state.pressed("move_up"));
    state.set(Binding::key(Key::W), false); // Up is still down
    CHECK(state.held("move_up"));
    CHECK_FALSE(state.released("move_up"));
    state.set(Binding::key(Key::Up), false);
    CHECK_FALSE(state.held("move_up"));
    CHECK(state.released("move_up"));
    CHECK_FALSE(state.held("not_an_action"));
}

TEST_CASE("Rebinding keeps working with held controls") {
    input::InputState state(input::InputMap::rpg_preset());
    input::InputMap custom;
    custom.bind("interact", Binding::key(Key::E));
    state.set_map(custom);
    state.begin_step();
    state.set(Binding::key(Key::E), true);
    CHECK(state.pressed("interact"));
    state.set(Binding::key(Key::Z), true);
    CHECK(state.map().actions_for(Binding::key(Key::Z)).empty());
}

TEST_CASE("Recorded input replays to exactly the same states") {
    input::Recording recording;
    recording.record(0, Binding::key(Key::Right), true);
    recording.record(2, Binding::key(Key::Z), true);
    recording.record(3, Binding::key(Key::Z), false);
    recording.record(3, Binding::key(Key::Right), false);

    const auto run = [&]() {
        input::InputState state(input::InputMap::rpg_preset());
        std::vector<std::string> trace;
        for (std::uint64_t step = 0; step < 5; ++step) {
            state.begin_step();
            recording.replay(step, state);
            trace.push_back(std::to_string(step) + (state.held("move_right") ? " right" : "") +
                            (state.pressed("interact") ? " interact!" : "") +
                            (state.released("interact") ? " interact-up" : ""));
        }
        return trace;
    };
    const std::vector<std::string> expected{"0 right", "1 right", "2 right interact!",
                                            "3 interact-up", "4"};
    CHECK(run() == expected);
    CHECK(run() == expected); // deterministic
}

TEST_CASE("Operating system events reach the input state") {
    REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
    input::InputState state(input::InputMap::rpg_preset());
    input::Recording recording;
    state.begin_step();

    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = SDL_SCANCODE_Z;
    down.key.down = true;
    REQUIRE(SDL_PushEvent(&down));
    SDL_Event repeat = down;
    repeat.key.repeat = true;
    REQUIRE(SDL_PushEvent(&repeat));
    SDL_Event arrow{};
    arrow.type = SDL_EVENT_KEY_DOWN;
    arrow.key.scancode = SDL_SCANCODE_LEFT;
    arrow.key.down = true;
    REQUIRE(SDL_PushEvent(&arrow));
    CHECK(input::pump_events(state, &recording, 7));
    CHECK(state.pressed("interact"));
    CHECK(state.held("move_left"));
    REQUIRE(recording.changes().size() == 2); // the repeat is not recorded
    CHECK(recording.changes()[0] == input::Recording::Change{7, Binding::key(Key::Z), true});

    SDL_Event up{};
    up.type = SDL_EVENT_KEY_UP;
    up.key.scancode = SDL_SCANCODE_Z;
    REQUIRE(SDL_PushEvent(&up));
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));
    state.begin_step();
    CHECK_FALSE(input::pump_events(state));
    CHECK(state.released("interact"));
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

TEST_CASE("No-code logic can ask about actions") {
    reflect::Registry registry;
    input::register_types(registry);
    REQUIRE(registry.errors().empty());
    const reflect::TypeInfo* type = registry.find_type<input::InputObject>();
    REQUIRE(type != nullptr);
    const reflect::FunctionInfo* was_pressed = type->find_function("was_pressed");
    REQUIRE(was_pressed != nullptr);
    CHECK(reflect::block_text(*was_pressed) == "{action} was pressed");
    REQUIRE(type->find_event("pressed") != nullptr);

    input::InputState state(input::InputMap::rpg_preset());
    input::InputObject object(&state);
    state.begin_step();
    state.set(Binding::key(Key::Enter), true);
    const std::array<reflect::Value, 1> interact{reflect::Value{std::string("interact")}};
    CHECK(reflect::call(object, *was_pressed, interact).value ==
          std::optional<reflect::Value>(true));
    const std::array<reflect::Value, 1> cancel{reflect::Value{std::string("cancel")}};
    CHECK(reflect::call(object, *was_pressed, cancel).value ==
          std::optional<reflect::Value>(false));
}

TEST_CASE("Controls that went down this step are listed in order, and named for players") {
    input::InputState state(input::InputMap::rpg_preset());
    state.begin_step();
    state.set(input::Binding::key(input::Key::C), true);
    state.set(input::Binding::key(input::Key::Enter), true);
    state.set(input::Binding::key(input::Key::C), true); // a repeat is not new
    REQUIRE(state.pressed_bindings().size() == 2);
    CHECK(state.pressed_bindings()[0] == input::Binding::key(input::Key::C));
    state.begin_step();
    CHECK(state.pressed_bindings().empty()); // still held, not new
    CHECK(input::binding_name(input::Binding::key(input::Key::C)) == "C");
    CHECK(input::binding_name(input::Binding::key(input::Key::Num7)) == "7");
    CHECK(input::binding_name(input::Binding::key(input::Key::F10)) == "F10");
    CHECK(input::binding_name(input::Binding::key(input::Key::Escape)) == "Esc");
    CHECK(input::binding_name(input::Binding::button(input::Button::South)) == "Pad A");
}

TEST_CASE("Button prompts name the control of the device used last") {
    input::InputState state(input::InputMap::rpg_preset());
    CHECK(state.last_device() == Binding::Device::Keyboard);
    CHECK(state.prompt("interact") == "Z");
    state.begin_step();
    state.set(Binding::button(Button::South), true);
    CHECK(state.last_device() == Binding::Device::Gamepad);
    CHECK(state.prompt("interact") == "Pad A");
    CHECK(state.prompt("nothing").empty());
    state.begin_step();
    state.set(Binding::key(Key::Left), true);
    CHECK(state.prompt("cancel") == "X");
}
