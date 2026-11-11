#pragma once

// The "dialogue" object logic uses to show words on screen: "Say [text]" opens a message
// box at the bottom of the screen. Interact turns its pages and closes it, and the player
// stands still while it is open. When the last message closes, "When the message closes"
// Rules run. "Ask [question] with answers [a|b]" also lists answers to pick from.
//
// See docs/dev/ui.md §3.

#include "engine/foundation/reflect.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace engine::game {

class DialogueObject {
public:
    /// Queues a message; it shows once those before it are closed.
    void say(const std::string& text);
    /// Queues a question with answers separated by "|" ("Yes|No"); spaces around each are
    /// dropped, and so are empty ones. With no answers left, it is an ordinary message.
    void ask(const std::string& question, const std::string& answers);
    [[nodiscard]] bool is_open() const noexcept { return !queue_.empty(); }

    /// The message showing, or null.
    [[nodiscard]] const std::string* current() const noexcept {
        return queue_.empty() ? nullptr : &queue_.front().text;
    }
    /// The answers of the message showing; empty if it is not a question.
    [[nodiscard]] const std::vector<std::string>& choices() const noexcept;
    /// Closes the message showing. Returns true if that was the last.
    bool close_current();
    /// Records `index` (from 0) of the showing question's answers as the answer. Returns
    /// false, changing nothing, if no question shows or there is no such answer.
    bool choose(std::size_t index);
    /// The last answer chosen, counted from 1 (0 before any), and its text.
    [[nodiscard]] std::int64_t answer() const noexcept { return answer_; }
    [[nodiscard]] std::string answer_text() const { return answer_text_; }

    /// Closes every message without telling Rules: when a save is loaded.
    void clear() noexcept { queue_.clear(); }
    /// Every message said or asked, oldest first, for logs and tests.
    [[nodiscard]] const std::vector<std::string>& said() const noexcept { return said_; }

private:
    struct Message {
        std::string text;
        std::vector<std::string> choices;
    };
    std::deque<Message> queue_;
    std::vector<std::string> said_;
    std::int64_t answer_ = 0;
    std::string answer_text_;
};

/// Registers DialogueObject as "dialogue".
void register_dialogue_types(reflect::Registry& registry);

} // namespace engine::game
