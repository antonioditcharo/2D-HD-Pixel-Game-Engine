#include "engine/input/input.h"

#include <algorithm>
#include <utility>

namespace engine::input {

// ---------------------------------------------------------------------------------------
// InputMap
// ---------------------------------------------------------------------------------------

bool InputMap::bind(std::string_view action, Binding binding) {
    if (!reflect::is_valid_id(action)) {
        return false;
    }
    auto found = actions_.find(action);
    if (found == actions_.end()) {
        found = actions_.emplace(std::string(action), std::set<Binding>{}).first;
    }
    return found->second.insert(binding).second;
}

bool InputMap::unbind(std::string_view action, Binding binding) {
    const auto found = actions_.find(action);
    if (found == actions_.end() || found->second.erase(binding) == 0) {
        return false;
    }
    if (found->second.empty()) {
        actions_.erase(found);
    }
    return true;
}

bool InputMap::has_action(std::string_view action) const {
    return actions_.find(action) != actions_.end();
}

std::vector<std::string> InputMap::actions() const {
    std::vector<std::string> names;
    names.reserve(actions_.size());
    for (const auto& [name, bindings] : actions_) {
        names.push_back(name);
    }
    return names;
}

std::vector<Binding> InputMap::bindings(std::string_view action) const {
    const auto found = actions_.find(action);
    return found == actions_.end()
               ? std::vector<Binding>{}
               : std::vector<Binding>(found->second.begin(), found->second.end());
}

std::vector<std::string> InputMap::actions_for(Binding binding) const {
    std::vector<std::string> names;
    for (const auto& [name, bindings] : actions_) {
        if (bindings.contains(binding)) {
            names.push_back(name);
        }
    }
    return names;
}

InputMap InputMap::rpg_preset() {
    InputMap map;
    const auto both = [&](std::string_view action, std::initializer_list<Key> keys,
                          std::initializer_list<Button> buttons) {
        for (const Key key : keys) {
            map.bind(action, Binding::key(key));
        }
        for (const Button button : buttons) {
            map.bind(action, Binding::button(button));
        }
    };
    both("move_up", {Key::Up, Key::W}, {Button::DpadUp});
    both("move_down", {Key::Down, Key::S}, {Button::DpadDown});
    both("move_left", {Key::Left, Key::A}, {Button::DpadLeft});
    both("move_right", {Key::Right, Key::D}, {Button::DpadRight});
    both("interact", {Key::Z, Key::Enter, Key::Space}, {Button::South});
    both("cancel", {Key::X, Key::Backspace}, {Button::East});
    both("menu", {Key::Escape}, {Button::Start});
    return map;
}

// ---------------------------------------------------------------------------------------
// InputState
// ---------------------------------------------------------------------------------------

InputState::InputState(InputMap map) : map_(std::move(map)) {}

void InputState::begin_step() {
    pressed_.clear();
    released_.clear();
}

bool InputState::held_by_controls(std::string_view action) const {
    for (const Binding binding : map_.bindings(action)) {
        if (down_.contains(binding)) {
            return true;
        }
    }
    return false;
}

void InputState::set(Binding binding, bool down) {
    if (down == down_.contains(binding)) {
        return; // a repeat
    }
    const std::vector<std::string> actions = map_.actions_for(binding);
    std::vector<bool> before;
    before.reserve(actions.size());
    for (const std::string& action : actions) {
        before.push_back(held_by_controls(action));
    }
    if (down) {
        down_.insert(binding);
    } else {
        down_.erase(binding);
    }
    // An action changes only when its first control goes down or its last one goes up.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const bool after = held_by_controls(actions[i]);
        if (!before[i] && after) {
            pressed_.insert(actions[i]);
        } else if (before[i] && !after) {
            released_.insert(actions[i]);
        }
    }
}

bool InputState::held(std::string_view action) const {
    return held_by_controls(action);
}

bool InputState::pressed(std::string_view action) const {
    return pressed_.find(action) != pressed_.end();
}

bool InputState::released(std::string_view action) const {
    return released_.find(action) != released_.end();
}

std::vector<std::string> InputState::pressed_actions() const {
    return {pressed_.begin(), pressed_.end()};
}

void InputState::set_map(InputMap map) {
    map_ = std::move(map);
}

// ---------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------

void Recording::record(std::uint64_t step, Binding binding, bool down) {
    changes_.push_back(Change{step, binding, down});
}

void Recording::replay(std::uint64_t step, InputState& state) const {
    const auto first =
        std::lower_bound(changes_.begin(), changes_.end(), step,
                         [](const Change& change, std::uint64_t s) { return change.step < s; });
    for (auto it = first; it != changes_.end() && it->step == step; ++it) {
        state.set(it->binding, it->down);
    }
}

// ---------------------------------------------------------------------------------------
// The logic-facing object
// ---------------------------------------------------------------------------------------

bool InputObject::is_held(const std::string& action) const {
    return state_ != nullptr && state_->held(action);
}

bool InputObject::was_pressed(const std::string& action) const {
    return state_ != nullptr && state_->pressed(action);
}

bool InputObject::was_released(const std::string& action) const {
    return state_ != nullptr && state_->released(action);
}

void register_types(reflect::Registry& registry) {
    auto input =
        registry.add_type<InputObject>("input").display_name("Input").category("Input").tooltip(
            "The player's buttons and keys, as named actions such as Interact.");
    input.condition<&InputObject::is_held>("is_held", {"action"})
        .text("{action} is held")
        .tooltip("True while any button bound to the action is held down.");
    input.condition<&InputObject::was_pressed>("was_pressed", {"action"})
        .text("{action} was pressed")
        .tooltip("True in the step the action was pressed.");
    input.condition<&InputObject::was_released>("was_released", {"action"})
        .text("{action} was released")
        .tooltip("True in the step the action was let go.");
    input.event<std::string>("pressed", {"action"})
        .text("When {action} is pressed")
        .tooltip("Happens once each time an action is pressed.");
}

} // namespace engine::input
