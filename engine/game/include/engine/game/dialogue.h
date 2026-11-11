#pragma once

// The "dialogue" object logic uses to show words on screen: "Say [text]" opens a message
// box at the bottom of the screen. Interact turns its pages and closes it, and the player
// stands still while it is open. When the last message closes, "When the message closes"
// Rules run.
//
// See docs/dev/ui.md §3.

#include "engine/foundation/reflect.h"

#include <deque>
#include <string>
#include <vector>

namespace engine::game {

class DialogueObject {
public:
    /// Queues a message; it shows once those before it are closed.
    void say(const std::string& text) {
        queue_.push_back(text);
        said_.push_back(text);
    }
    [[nodiscard]] bool is_open() const noexcept { return !queue_.empty(); }

    /// The message showing, or null.
    [[nodiscard]] const std::string* current() const noexcept {
        return queue_.empty() ? nullptr : &queue_.front();
    }
    /// Closes the message showing. Returns true if that was the last.
    bool close_current() {
        if (!queue_.empty()) {
            queue_.pop_front();
        }
        return queue_.empty();
    }
    /// Closes every message without telling Rules: when a save is loaded.
    void clear() noexcept { queue_.clear(); }
    /// Every message said, oldest first, for logs and tests.
    [[nodiscard]] const std::vector<std::string>& said() const noexcept { return said_; }

private:
    std::deque<std::string> queue_;
    std::vector<std::string> said_;
};

/// Registers DialogueObject as "dialogue".
void register_dialogue_types(reflect::Registry& registry);

} // namespace engine::game
