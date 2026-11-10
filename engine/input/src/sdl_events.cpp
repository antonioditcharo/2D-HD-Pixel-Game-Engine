// Translation of SDL's input events into Sparxel's keys and buttons.

#include "engine/input/input.h"

#include <SDL3/SDL.h>

#include <array>
#include <optional>
#include <utility>

namespace engine::input {

namespace {

[[nodiscard]] std::optional<Key> key_from(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (code - SDL_SCANCODE_A));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        return static_cast<Key>(static_cast<int>(Key::Num1) + (code - SDL_SCANCODE_1));
    }
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (code - SDL_SCANCODE_F1));
    }
    constexpr std::array<std::pair<SDL_Scancode, Key>, 16> named{{
        {SDL_SCANCODE_0, Key::Num0},
        {SDL_SCANCODE_UP, Key::Up},
        {SDL_SCANCODE_DOWN, Key::Down},
        {SDL_SCANCODE_LEFT, Key::Left},
        {SDL_SCANCODE_RIGHT, Key::Right},
        {SDL_SCANCODE_SPACE, Key::Space},
        {SDL_SCANCODE_RETURN, Key::Enter},
        {SDL_SCANCODE_ESCAPE, Key::Escape},
        {SDL_SCANCODE_TAB, Key::Tab},
        {SDL_SCANCODE_BACKSPACE, Key::Backspace},
        {SDL_SCANCODE_LSHIFT, Key::LeftShift},
        {SDL_SCANCODE_RSHIFT, Key::RightShift},
        {SDL_SCANCODE_LCTRL, Key::LeftCtrl},
        {SDL_SCANCODE_RCTRL, Key::RightCtrl},
        {SDL_SCANCODE_LALT, Key::LeftAlt},
        {SDL_SCANCODE_RALT, Key::RightAlt},
    }};
    for (const auto& [sdl, key] : named) {
        if (sdl == code) {
            return key;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Button> button_from(int button) {
    constexpr std::array<std::pair<int, Button>, 14> buttons{{
        {SDL_GAMEPAD_BUTTON_SOUTH, Button::South},
        {SDL_GAMEPAD_BUTTON_EAST, Button::East},
        {SDL_GAMEPAD_BUTTON_WEST, Button::West},
        {SDL_GAMEPAD_BUTTON_NORTH, Button::North},
        {SDL_GAMEPAD_BUTTON_BACK, Button::Back},
        {SDL_GAMEPAD_BUTTON_START, Button::Start},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Button::LeftShoulder},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Button::RightShoulder},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, Button::DpadUp},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Button::DpadDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Button::DpadLeft},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Button::DpadRight},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, Button::LeftStick},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, Button::RightStick},
    }};
    for (const auto& [sdl, mapped] : buttons) {
        if (sdl == button) {
            return mapped;
        }
    }
    return std::nullopt;
}

} // namespace

bool pump_events(InputState& state, Recording* recording, std::uint64_t step) {
    static const bool gamepads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    (void)gamepads; // Without gamepad support, keyboards still work.
    const auto apply = [&](Binding binding, bool down) {
        state.set(binding, down);
        if (recording != nullptr) {
            recording->record(step, binding, down);
        }
    };
    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            keep_running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!event.key.repeat) {
                if (const std::optional<Key> key = key_from(event.key.scancode)) {
                    apply(Binding::key(*key), event.type == SDL_EVENT_KEY_DOWN);
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            SDL_OpenGamepad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (const std::optional<Button> button = button_from(event.gbutton.button)) {
                apply(Binding::button(*button), event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            }
            break;
        default:
            break;
        }
    }
    return keep_running;
}

} // namespace engine::input
