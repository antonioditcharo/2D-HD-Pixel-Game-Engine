#pragma once

// Input as named actions (plan/02 U1): games ask "is Interact pressed?", never "is the Z
// key down?". Keys and gamepad buttons are bound to actions in an InputMap; InputState
// follows which actions are held, and which were pressed or released this step. Input can
// be recorded per fixed step and replayed exactly (plan/03 §7, N6).
//
// See docs/dev/input.md.

#include "engine/foundation/reflect.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace engine::input {

/// Keyboard keys, by position on a US layout (so WASD stays WASD on other layouts).
enum class Key : std::uint8_t {
    Unknown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Up,
    Down,
    Left,
    Right,
    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

/// Gamepad buttons, by position: South is A on Xbox and Cross on PlayStation.
enum class Button : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Start,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftStick,
    RightStick,
};

/// One physical control.
struct Binding {
    enum class Device : std::uint8_t { Keyboard, Gamepad };
    Device device = Device::Keyboard;
    std::uint16_t code = 0; ///< A Key or a Button.

    [[nodiscard]] static constexpr Binding key(Key k) noexcept {
        return Binding{Device::Keyboard, static_cast<std::uint16_t>(k)};
    }
    [[nodiscard]] static constexpr Binding button(Button b) noexcept {
        return Binding{Device::Gamepad, static_cast<std::uint16_t>(b)};
    }
    friend constexpr auto operator<=>(Binding, Binding) = default;
};

/// Which controls trigger which actions. An action may have many bindings, and a binding
/// may trigger several actions.
class InputMap {
public:
    /// Binds `binding` to `action`. Returns false if `action` is not a valid id
    /// (lowercase letters, digits and underscores), or the pair already exists.
    bool bind(std::string_view action, Binding binding);
    /// Removes a binding from an action. Returns false if it was not there.
    bool unbind(std::string_view action, Binding binding);

    [[nodiscard]] bool has_action(std::string_view action) const;
    /// Every action, sorted.
    [[nodiscard]] std::vector<std::string> actions() const;
    /// The bindings of an action, sorted; empty if it has none.
    [[nodiscard]] std::vector<Binding> bindings(std::string_view action) const;
    /// The actions a binding triggers, sorted.
    [[nodiscard]] std::vector<std::string> actions_for(Binding binding) const;

    /// The RPG preset: move_up, move_down, move_left, move_right (arrows, WASD, d-pad),
    /// interact (Z, Enter, Space, South), cancel (X, Backspace, East), menu (Escape, Start).
    [[nodiscard]] static InputMap rpg_preset();

private:
    std::map<std::string, std::set<Binding>, std::less<>> actions_;
};

/// The state of every action, updated once per fixed step.
class InputState {
public:
    explicit InputState(InputMap map);

    /// Starts a step: forgets which actions were pressed or released in the previous one.
    /// Call it before feeding the step's input.
    void begin_step();
    /// A control went down or up. Repeats of the same state are ignored.
    void set(Binding binding, bool down);

    /// Held now, by any of its bindings.
    [[nodiscard]] bool held(std::string_view action) const;
    /// Went from not held to held during this step.
    [[nodiscard]] bool pressed(std::string_view action) const;
    /// Went from held to not held during this step.
    [[nodiscard]] bool released(std::string_view action) const;
    /// Actions pressed during this step, sorted.
    [[nodiscard]] std::vector<std::string> pressed_actions() const;

    [[nodiscard]] const InputMap& map() const noexcept { return map_; }
    /// Replaces the bindings (for rebinding). Controls held now stay held.
    void set_map(InputMap map);
    /// Controls that went down during this step, in order: for "press a key" while
    /// rebinding.
    /// The kind of control pressed last: keyboard or gamepad, for showing which button
    /// to press ("Z" or "Pad A").
    [[nodiscard]] Binding::Device last_device() const noexcept { return last_device_; }
    /// The name of the first control of `action` on the device pressed last ("Z", "Pad A");
    /// empty if the action has none there.
    [[nodiscard]] std::string prompt(std::string_view action) const;
    [[nodiscard]] const std::vector<Binding>& pressed_bindings() const noexcept {
        return newly_down_;
    }

private:
    [[nodiscard]] bool held_by_controls(std::string_view action) const;

    InputMap map_;
    std::set<Binding> down_;
    std::vector<Binding> newly_down_;
    Binding::Device last_device_ = Binding::Device::Keyboard;
    std::set<std::string, std::less<>> pressed_;
    std::set<std::string, std::less<>> released_;
};

/// A control's name, as players read it: "Z", "Enter", "Up", "Pad A", "Pad Start".
[[nodiscard]] std::string binding_name(Binding binding);

/// Input recorded per fixed step, for deterministic replays and tests.
class Recording {
public:
    struct Change {
        std::uint64_t step = 0;
        Binding binding;
        bool down = false;

        friend bool operator==(const Change&, const Change&) = default;
    };

    void record(std::uint64_t step, Binding binding, bool down);
    /// Feeds the changes recorded for `step` into `state`. Call it after begin_step().
    void replay(std::uint64_t step, InputState& state) const;
    [[nodiscard]] const std::vector<Change>& changes() const noexcept { return changes_; }

private:
    std::vector<Change> changes_; ///< In the order recorded; steps never go backwards.
};

/// The input as no-code logic sees it: conditions "Action [x] is held" and "Action [x] was
/// pressed". Bind an instance to the logic VM as an object, for example "input".
class InputObject {
public:
    InputObject() = default;
    explicit InputObject(const InputState* state) : state_(state) {}

    [[nodiscard]] bool is_held(const std::string& action) const;
    [[nodiscard]] bool was_pressed(const std::string& action) const;
    [[nodiscard]] bool was_released(const std::string& action) const;

private:
    const InputState* state_{nullptr};
};

/// Registers InputObject as "input", with the conditions above and the event
/// "pressed" (carrying the action) for Rules such as "When Interact is pressed".
void register_types(reflect::Registry& registry);

/// Reads the operating system's input events through SDL and feeds them into `state`.
/// Returns false when the user asked to quit (closing the window). Starts SDL's gamepad
/// subsystem the first time, and opens gamepads as they are connected. When `recording` is
/// given, every change is also recorded for `step`.
bool pump_events(InputState& state, Recording* recording = nullptr, std::uint64_t step = 0);

} // namespace engine::input
