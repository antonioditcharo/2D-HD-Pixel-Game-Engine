#include "engine/game/dialogue.h"

#include "engine/game/lighting.h"
#include "engine/game/localization.h"

#include <algorithm>
#include <array>
#include <cmath>
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

std::string DialogueObject::shown(const std::string& text) const {
    return translations_ != nullptr ? translations_->translate(text) : text;
}

void DialogueObject::say(const std::string& text) {
    queue_.push_back(Message{shown(text), {}});
    said_.push_back(text);
}

void DialogueObject::ask(const std::string& question, const std::string& answers) {
    Message message{shown(question), {}};
    std::size_t start = 0;
    while (start <= answers.size()) {
        const std::size_t bar = std::min(answers.find('|', start), answers.size());
        std::string answer = trimmed(std::string_view(answers).substr(start, bar - start));
        if (!answer.empty()) {
            message.choices.push_back(shown(answer));
            message.written.push_back(std::move(answer));
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

void DialogueObject::set_look(SpeakerLook look) {
    if (!queue_.empty()) {
        queue_.back().look = std::move(look);
    }
}

const SpeakerLook& DialogueObject::current_look() const noexcept {
    static const SpeakerLook none;
    return queue_.empty() ? none : queue_.front().look;
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
    answer_text_ = index < queue_.front().written.size() ? queue_.front().written[index]
                                                         : queue_.front().choices[index];
    return true;
}

void LightingObject::set_weather(const std::string& kind, double strength) {
    const bool known = kind == "rain" || kind == "snow" || kind == "fog" || kind == "clear";
    weather = known ? kind : std::string("clear");
    weather_strength =
        weather == "clear" || !std::isfinite(strength) ? 0.0 : std::clamp(strength, 0.0, 1.0);
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

LightingObject daylight(double hours) {
    struct Key {
        double hour, ambient, sun, r, g, b;
    };
    static constexpr std::array<Key, 8> keys{{
        {0.0, 0.22, 0.18, 0.55, 0.65, 1.00}, // night
        {4.5, 0.22, 0.18, 0.55, 0.65, 1.00},
        {6.0, 0.42, 0.30, 1.10, 0.86, 0.72}, // dawn
        {9.0, 0.55, 0.45, 1.00, 0.98, 0.95},
        {12.0, 0.60, 0.50, 1.00, 1.00, 1.00}, // noon
        {16.0, 0.55, 0.45, 1.02, 0.97, 0.90},
        {18.0, 0.42, 0.30, 1.15, 0.80, 0.65}, // dusk
        {20.0, 0.22, 0.18, 0.55, 0.65, 1.00},
    }};
    double t = std::isfinite(hours) ? std::fmod(hours, 24.0) : 12.0;
    if (t < 0.0) {
        t += 24.0;
    }
    LightingObject light;
    // Between the keys either side (after the last, towards midnight's).
    Key a = keys.back();
    Key b{24.0,           keys.front().ambient, keys.front().sun,
          keys.front().r, keys.front().g,       keys.front().b};
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t >= keys[i].hour && t < keys[i + 1].hour) {
            a = keys[i];
            b = keys[i + 1];
        }
    }
    const double f = b.hour > a.hour ? (t - a.hour) / (b.hour - a.hour) : 0.0;
    const auto mix = [f](double x, double y) { return x + ((y - x) * f); };
    light.ambient = mix(a.ambient, b.ambient);
    light.sun = mix(a.sun, b.sun);
    light.tint_r = mix(a.r, b.r);
    light.tint_g = mix(a.g, b.g);
    light.tint_b = mix(a.b, b.b);
    constexpr double pi = 3.14159265358979323846;
    if (t >= 6.0 && t <= 18.0) {
        light.sun_angle = 90.0 + ((t - 6.0) / 12.0 * 180.0);
        light.sun_height = std::max(5.0, 70.0 * std::sin((t - 6.0) / 12.0 * pi));
    } else {
        light.sun_angle = 180.0; // the moon, in the south
        light.sun_height = 35.0;
    }
    light.time_of_day = t;
    light.follows_time = true;
    return light;
}

void advance_day(LightingObject& lighting, double seconds) {
    if (std::isfinite(lighting.day_minutes) && lighting.day_minutes > 0.0 &&
        std::isfinite(seconds)) {
        lighting.time_of_day += seconds * 24.0 / (lighting.day_minutes * 60.0);
        lighting.time_of_day = std::fmod(lighting.time_of_day, 24.0);
        if (lighting.time_of_day < 0.0) {
            lighting.time_of_day += 24.0;
        }
    }
    if (lighting.follows_time) {
        const LightingObject now = daylight(lighting.time_of_day);
        lighting.ambient = now.ambient;
        lighting.sun = now.sun;
        lighting.sun_angle = now.sun_angle;
        lighting.sun_height = now.sun_height;
        lighting.tint_r = now.tint_r;
        lighting.tint_g = now.tint_g;
        lighting.tint_b = now.tint_b;
    }
}

void LightingObject::set_time(double hours) {
    if (std::isfinite(hours)) {
        time_of_day = std::fmod(std::fmod(hours, 24.0) + 24.0, 24.0);
        follows_time = true;
        advance_day(*this, 0.0);
    }
}

void LightingObject::set_tint(double red, double green, double blue) {
    tint_r = std::clamp(std::isfinite(red) ? red : 1.0, 0.0, 2.0);
    tint_g = std::clamp(std::isfinite(green) ? green : 1.0, 0.0, 2.0);
    tint_b = std::clamp(std::isfinite(blue) ? blue : 1.0, 0.0, 2.0);
    follows_time = false;
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
    lighting.property<&LightingObject::time_of_day>("time_of_day")
        .range(0.0, 24.0)
        .step(0.25)
        .display_name("Time of day")
        .tooltip("The hour, from 0 (midnight) to 24.");
    lighting.property<&LightingObject::day_minutes>("day_minutes")
        .range(0.0, 1440.0)
        .step(1.0)
        .display_name("Day length")
        .tooltip("How many real minutes a game day lasts; 0 stops the clock.");
    lighting.property<&LightingObject::follows_time>("follows_time")
        .display_name("Light follows the time")
        .tooltip("The sun, the light and its colour follow the time of day.");
    lighting.property<&LightingObject::tint_r>("tint_r")
        .range(0.0, 2.0)
        .step(0.05)
        .display_name("Tint red")
        .tooltip("The light's red, 1 unchanged.");
    lighting.property<&LightingObject::tint_g>("tint_g")
        .range(0.0, 2.0)
        .step(0.05)
        .display_name("Tint green")
        .tooltip("The light's green, 1 unchanged.");
    lighting.property<&LightingObject::tint_b>("tint_b")
        .range(0.0, 2.0)
        .step(0.05)
        .display_name("Tint blue")
        .tooltip("The light's blue, 1 unchanged.");
    lighting.action<&LightingObject::set_time>("set_time", {"hours"})
        .text("Set the time of day to {hours}")
        .tooltip("Jumps the clock to an hour (6 is dawn, 12 noon, 18 dusk, 22 night), and the "
                 "light follows it.")
        .param("hours")
        .range(0.0, 24.0);
    lighting.action<&LightingObject::set_weather>("set_weather", {"kind", "strength"})
        .text("Set the weather to {kind}, {strength} strong")
        .tooltip("rain, snow, fog or clear; strength from 0 to 1. It eases in over a few "
                 "seconds, over the whole screen.")
        .param("strength")
        .range(0.0, 1.0)
        .default_value(reflect::Value{0.7});
    lighting.condition<&LightingObject::raining>("raining")
        .text("It is raining")
        .tooltip("Whether the weather is rain.");
    lighting.action<&LightingObject::set_tint>("set_tint", {"red", "green", "blue"})
        .text("Tint the light {red}, {green}, {blue}")
        .tooltip("Colours the light, each channel 0 to 2 (1 unchanged): a red alarm, a green "
                 "swamp. The light stops following the time.");
}

} // namespace engine::game
