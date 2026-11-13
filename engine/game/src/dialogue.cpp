#include "engine/game/dialogue.h"

#include "engine/game/lighting.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace engine::game {

namespace {

std::string trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return std::string(text.substr(first, last - first + 1));
}

} // namespace

void DialogueObject::say(const std::string& text) {
    queue_.push_back(Message{text, {}});
    said_.push_back(text);
}

void DialogueObject::ask(const std::string& question, const std::string& answers) {
    Message message{question, {}};
    std::size_t start = 0;
    while (start <= answers.size()) {
        const std::size_t bar = std::min(answers.find('|', start), answers.size());
        std::string answer = trimmed(std::string_view(answers).substr(start, bar - start));
        if (!answer.empty()) {
            message.choices.push_back(std::move(answer));
        }
        start = bar + 1;
    }
    queue_.push_back(std::move(message));
    said_.push_back(question);
}

void DialogueObject::say_as(const std::string& speaker, const std::string& text) {
    say(text);
    queue_.back().speaker = trimmed(speaker);
}

void DialogueObject::ask_as(const std::string& speaker, const std::string& question,
                            const std::string& answers) {
    ask(question, answers);
    queue_.back().speaker = trimmed(speaker);
}

const std::string& DialogueObject::current_speaker() const noexcept {
    static const std::string none;
    return queue_.empty() ? none : queue_.front().speaker;
}

void DialogueObject::start_conversation(const std::string& file) {
    start_conversation_at(file, {});
}

void DialogueObject::start_conversation_at(const std::string& file, const std::string& node) {
    request_ = ConversationRequest{trimmed(file), trimmed(node)};
}

std::optional<DialogueObject::ConversationRequest> DialogueObject::take_conversation_request() {
    std::optional<ConversationRequest> request = std::move(request_);
    request_.reset();
    return request;
}

void DialogueObject::set_flag(const std::string& name) {
    if (const std::string key = trimmed(name); !key.empty()) {
        flags_[key] = true;
    }
}

void DialogueObject::clear_flag(const std::string& name) {
    if (const std::string key = trimmed(name); !key.empty()) {
        flags_[key] = false;
    }
}

bool DialogueObject::flag(const std::string& name) const {
    const auto found = flags_.find(trimmed(name));
    return found != flags_.end() && found->second;
}

const std::vector<std::string>& DialogueObject::choices() const noexcept {
    static const std::vector<std::string> none;
    return queue_.empty() ? none : queue_.front().choices;
}

bool DialogueObject::close_current() {
    if (!queue_.empty()) {
        queue_.pop_front();
    }
    return queue_.empty();
}

bool DialogueObject::choose(std::size_t index) {
    if (queue_.empty() || index >= queue_.front().choices.size()) {
        return false;
    }
    answer_ = static_cast<std::int64_t>(index) + 1;
    answer_text_ = queue_.front().choices[index];
    return true;
}

void register_dialogue_types(reflect::Registry& registry) {
    auto dialogue = registry.add_type<DialogueObject>("dialogue")
                        .display_name("Dialogue")
                        .category("Story")
                        .tooltip("Words on screen: what characters say.");
    dialogue.action<&DialogueObject::say>("say", {"text"})
        .text("Say {text}")
        .tooltip("Shows a message box. The player reads it and presses Interact to go on.");
    dialogue.action<&DialogueObject::ask>("ask", {"question", "answers"})
        .text("Ask {question} with answers {answers}")
        .tooltip("Shows a question and answers to pick from, separated by | (Yes|No). Up "
                 "and Down choose, and Interact picks. Wait until the message closes, then "
                 "check the answer.");
    dialogue.action<&DialogueObject::say_as>("say_as", {"speaker", "text"})
        .text("{speaker} says {text}")
        .tooltip("Shows a message box with the speaker's name above it.");
    dialogue.action<&DialogueObject::start_conversation>("start_conversation", {"file"})
        .text("Start the conversation {file}")
        .tooltip("Plays a conversation made in the Story editor, such as talk/elder.dtalk: its "
                 "lines, the player's choices and what they lead to.");
    dialogue
        .action<&DialogueObject::start_conversation_at>("start_conversation_at", {"file", "node"})
        .text("Start the conversation {file} at {node}")
        .tooltip("Plays a conversation from one of its nodes rather than its start.");
    dialogue.action<&DialogueObject::set_flag>("set_flag", {"name"})
        .text("Set the flag {name}")
        .tooltip("Marks a fact of the story as true, such as met_the_elder. Conversations "
                 "test flags too.");
    dialogue.action<&DialogueObject::clear_flag>("clear_flag", {"name"})
        .text("Clear the flag {name}")
        .tooltip("Marks a fact of the story as no longer true.");
    dialogue.condition<&DialogueObject::flag>("flag", {"name"})
        .text("The flag {name} is set")
        .tooltip("Whether a fact of the story is true.");
    dialogue.event<std::string>("conversation_ended", {"name"})
        .text("When a conversation ends")
        .tooltip("Happens when a conversation reaches its end; {name} is its file.");
    dialogue.condition<&DialogueObject::is_open>("is_open")
        .text("A message is open")
        .tooltip("Whether a message box is on screen.");
    dialogue.getter<&DialogueObject::answer>("answer")
        .text("The answer chosen")
        .tooltip("The last answer picked, counted from 1 (the first answer is 1); 0 before "
                 "any.");
    dialogue.getter<&DialogueObject::answer_text>("answer_text")
        .text("The answer's text")
        .tooltip("The words of the last answer picked.");
    dialogue.event<std::int64_t>("answered", {"answer"})
        .text("When an answer is picked")
        .tooltip("Happens when the player picks an answer; {answer} counts from 1.");
    dialogue.event<>("closed")
        .text("When the message closes")
        .tooltip("Happens when the player closes the last message.");
}

void register_lighting_types(reflect::Registry& registry) {
    auto lighting = registry.add_type<LightingObject>("lighting")
                        .display_name("Lighting")
                        .category("Visuals")
                        .tooltip("How bright the scene is, in the Diorama style.");
    lighting.property<&LightingObject::ambient>("ambient")
        .range(0.0, 2.0)
        .step(0.05)
        .display_name("Ambient light")
        .tooltip("Light from everywhere: low for night and caves, so lights stand out.");
    lighting.property<&LightingObject::sun>("sun")
        .range(0.0, 2.0)
        .step(0.05)
        .display_name("Sunlight")
        .tooltip("How strong the sun (or the moon) is.");
    lighting.property<&LightingObject::sun_angle>("sun_angle")
        .range(0.0, 360.0)
        .step(5.0)
        .unit(reflect::Unit::Degrees)
        .display_name("Sun direction")
        .tooltip("Where the sun is, clockwise from north (up the map): 90 is east, 180 "
                 "south, 270 west. Shadows fall the other way.");
    lighting.property<&LightingObject::sun_height>("sun_height")
        .range(5.0, 90.0)
        .step(5.0)
        .unit(reflect::Unit::Degrees)
        .display_name("Sun height")
        .tooltip("How high the sun is above the horizon: low for long morning and evening "
                 "shadows, 90 for noon.");
}

} // namespace engine::game
