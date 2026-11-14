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
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace engine::game {

/// How a speaker looks and sounds in the message box; empty parts are not shown or played.
struct SpeakerLook {
    std::string portrait; ///< A picture in assets/.
    std::string voice;    ///< A sound in assets/, blipped as words are typed.
    double pitch = 1.0;
    std::string color; ///< The name plate's colour, "#rrggbb"; empty for the usual gold.

    [[nodiscard]] bool empty() const noexcept {
        return portrait.empty() && voice.empty() && color.empty();
    }
    friend bool operator==(const SpeakerLook&, const SpeakerLook&) = default;
};

class Translations;

class DialogueObject {
public:
    /// Where messages, questions and answers are translated from as they are queued
    /// (localization.h); null for none.
    void set_translations(const Translations* translations) noexcept {
        translations_ = translations;
    }
    /// Queues a message; it shows once those before it are closed.
    void say(const std::string& text);
    /// Queues a question with answers separated by "|" ("Yes|No"); spaces around each are
    /// dropped, and so are empty ones. With no answers left, it is an ordinary message.
    void ask(const std::string& question, const std::string& answers);
    /// The same, with the name of who says it, shown on a plate above the message.
    void say_as(const std::string& speaker, const std::string& text);
    void ask_as(const std::string& speaker, const std::string& question,
                const std::string& answers);
    [[nodiscard]] bool is_open() const noexcept { return !queue_.empty(); }
    /// Who says the message showing; empty for none.
    [[nodiscard]] const std::string& current_speaker() const noexcept;
    /// Gives the message queued last a look (its speaker's portrait, voice, colour).
    void set_look(SpeakerLook look);
    /// The look of the message showing; empty for none.
    [[nodiscard]] const SpeakerLook& current_look() const noexcept;

    /// Plays the conversation in `file` (its path in assets/, such as "talk/elder.dtalk")
    /// from its start (or, with `node`, from that node). The runtime takes the request.
    void start_conversation(const std::string& file);
    void start_conversation_at(const std::string& file, const std::string& node);
    struct ConversationRequest {
        std::string file;
        std::string node;
    };
    /// The conversation logic asked for since the last call, if any.
    [[nodiscard]] std::optional<ConversationRequest> take_conversation_request();

    /// The story's flags: yes/no facts that conversations and Rules set and test.
    void set_flag(const std::string& name);
    void clear_flag(const std::string& name);
    [[nodiscard]] bool flag(const std::string& name) const;
    [[nodiscard]] std::map<std::string, bool, std::less<>>& flags() noexcept { return flags_; }
    [[nodiscard]] const std::map<std::string, bool, std::less<>>& flags() const noexcept {
        return flags_;
    }

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
    void clear() noexcept {
        queue_.clear();
        request_.reset();
    }
    /// Every message said or asked, oldest first, for logs and tests.
    [[nodiscard]] const std::vector<std::string>& said() const noexcept { return said_; }

private:
    struct Message {
        std::string text;                 ///< As shown: translated.
        std::vector<std::string> choices; ///< As shown: translated.
        std::vector<std::string> written; ///< The answers as written, for answer_text().
        std::string speaker;
        SpeakerLook look;
    };
    /// `text` translated, if there are translations.
    [[nodiscard]] std::string shown(const std::string& text) const;
    const Translations* translations_ = nullptr;
    std::optional<ConversationRequest> request_;
    std::map<std::string, bool, std::less<>> flags_;
    std::deque<Message> queue_;
    std::vector<std::string> said_;
    std::int64_t answer_ = 0;
    std::string answer_text_;
};

/// Registers DialogueObject as "dialogue".
void register_dialogue_types(reflect::Registry& registry);

} // namespace engine::game
