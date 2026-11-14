#include "engine/game/conversation.h"

#include "engine/game/dialogue.h"
#include "engine/serialize/json.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <utility>

namespace engine::game {

namespace {

using serialize::Json;

constexpr std::array<std::string_view, 4> condition_ids{"has_item", "flag", "quest_active",
                                                        "quest_done"};
constexpr std::array<std::string_view, 6> effect_ids{"give_item",  "take_item",   "set_flag",
                                                     "clear_flag", "start_quest", "complete_quest"};

template <std::size_t N>
std::optional<std::size_t> index_of(const std::array<std::string_view, N>& ids,
                                    std::string_view id) {
    for (std::size_t i = 0; i < N; ++i) {
        if (ids.at(i) == id) {
            return i;
        }
    }
    return std::nullopt;
}

std::string text_at(const Json& json, const char* key) {
    return json.contains(key) && json[key].is_string() ? json[key].get<std::string>()
                                                       : std::string();
}

std::vector<TalkCondition> read_conditions(const Json& json) {
    std::vector<TalkCondition> out;
    if (!json.contains("if") || !json["if"].is_array()) {
        return out;
    }
    for (const Json& c : json["if"]) {
        if (!c.is_object()) {
            continue;
        }
        const std::optional<std::size_t> kind = index_of(condition_ids, text_at(c, "test"));
        if (!kind) {
            continue;
        }
        out.push_back(TalkCondition{static_cast<TalkCondition::Kind>(*kind), text_at(c, "name"),
                                    c.value("not", false)});
    }
    return out;
}

Json write_conditions(const std::vector<TalkCondition>& conditions) {
    Json out = Json::array();
    for (const TalkCondition& c : conditions) {
        Json one{{"test", condition_ids.at(static_cast<std::size_t>(c.kind))}, {"name", c.name}};
        if (c.negate) {
            one["not"] = true;
        }
        out.push_back(std::move(one));
    }
    return out;
}

std::int64_t whole(std::string_view text, std::int64_t fallback) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : fallback;
}

} // namespace

const TalkNode* Conversation::find(std::string_view id) const noexcept {
    for (const TalkNode& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

const Speaker* Conversation::speaker(std::string_view name) const noexcept {
    for (const Speaker& s : speakers) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

std::optional<Conversation> parse_conversation(std::string_view text, std::string& error) {
    std::vector<serialize::Issue> issues;
    const std::optional<Json> parsed = serialize::parse(text, issues);
    if (!parsed || !parsed->is_object() || text_at(*parsed, "format") != conversation_format) {
        error = "This is not a conversation (\"sparxel.conversation\").";
        return std::nullopt;
    }
    const Json& json = *parsed;
    if (!json.contains("version") || !json["version"].is_number_integer() ||
        json["version"].get<int>() > conversation_version) {
        error = "This conversation was saved by a newer version of Sparxel.";
        return std::nullopt;
    }
    Conversation c;
    c.start = text_at(json, "start");
    if (json.contains("speakers") && json["speakers"].is_array()) {
        for (const Json& s : json["speakers"]) {
            if (s.is_object() && !text_at(s, "name").empty()) {
                Speaker speaker{text_at(s, "name")};
                if (const std::string colour = text_at(s, "color"); !colour.empty()) {
                    speaker.color = colour;
                }
                speaker.portrait = text_at(s, "portrait");
                speaker.voice = text_at(s, "voice");
                if (s.contains("pitch") && s["pitch"].is_number()) {
                    speaker.pitch = std::clamp(s["pitch"].get<double>(), 0.25, 4.0);
                }
                c.speakers.push_back(std::move(speaker));
            }
        }
    }
    if (json.contains("nodes") && json["nodes"].is_array()) {
        for (const Json& n : json["nodes"]) {
            if (!n.is_object() || c.nodes.size() >= most_talk_nodes) {
                continue;
            }
            TalkNode node;
            node.id = text_at(n, "id");
            node.speaker = text_at(n, "speaker");
            node.text = text_at(n, "text");
            node.next = text_at(n, "next");
            node.portrait = text_at(n, "portrait");
            node.conditions = read_conditions(n);
            node.x = n.contains("x") && n["x"].is_number() ? n["x"].get<double>() : 0.0;
            node.y = n.contains("y") && n["y"].is_number() ? n["y"].get<double>() : 0.0;
            if (n.contains("choices") && n["choices"].is_array()) {
                for (const Json& ch : n["choices"]) {
                    if (ch.is_object()) {
                        node.choices.push_back(TalkChoice{text_at(ch, "text"), text_at(ch, "next"),
                                                          read_conditions(ch)});
                    }
                }
            }
            if (n.contains("do") && n["do"].is_array()) {
                for (const Json& e : n["do"]) {
                    if (!e.is_object()) {
                        continue;
                    }
                    if (const auto kind = index_of(effect_ids, text_at(e, "effect"))) {
                        node.effects.push_back(TalkEffect{static_cast<TalkEffect::Kind>(*kind),
                                                          text_at(e, "name"), text_at(e, "value")});
                    }
                }
            }
            if (!node.id.empty()) {
                c.nodes.push_back(std::move(node));
            }
        }
    }
    return c;
}

std::string conversation_to_text(const Conversation& conversation) {
    Json json = Json::object();
    json["format"] = conversation_format;
    json["version"] = conversation_version;
    json["start"] = conversation.start;
    Json speakers = Json::array();
    for (const Speaker& s : conversation.speakers) {
        Json speaker{{"name", s.name}, {"color", s.color}};
        if (!s.portrait.empty()) {
            speaker["portrait"] = s.portrait;
        }
        if (!s.voice.empty()) {
            speaker["voice"] = s.voice;
        }
        if (s.pitch != 1.0) {
            speaker["pitch"] = s.pitch;
        }
        speakers.push_back(std::move(speaker));
    }
    json["speakers"] = std::move(speakers);
    Json nodes = Json::array();
    for (const TalkNode& node : conversation.nodes) {
        Json n{{"id", node.id}, {"text", node.text}, {"x", node.x}, {"y", node.y}};
        if (!node.speaker.empty()) {
            n["speaker"] = node.speaker;
        }
        if (!node.next.empty()) {
            n["next"] = node.next;
        }
        if (!node.portrait.empty()) {
            n["portrait"] = node.portrait;
        }
        if (!node.conditions.empty()) {
            n["if"] = write_conditions(node.conditions);
        }
        if (!node.choices.empty()) {
            Json choices = Json::array();
            for (const TalkChoice& choice : node.choices) {
                Json ch{{"text", choice.text}, {"next", choice.next}};
                if (!choice.conditions.empty()) {
                    ch["if"] = write_conditions(choice.conditions);
                }
                choices.push_back(std::move(ch));
            }
            n["choices"] = std::move(choices);
        }
        if (!node.effects.empty()) {
            Json effects = Json::array();
            for (const TalkEffect& e : node.effects) {
                effects.push_back({{"effect", effect_ids.at(static_cast<std::size_t>(e.kind))},
                                   {"name", e.name},
                                   {"value", e.value}});
            }
            n["do"] = std::move(effects);
        }
        nodes.push_back(std::move(n));
    }
    json["nodes"] = std::move(nodes);
    return serialize::to_text(json);
}

std::vector<std::string> check_conversation(const Conversation& c) {
    std::vector<std::string> problems;
    std::set<std::string, std::less<>> ids;
    for (const TalkNode& node : c.nodes) {
        if (!ids.insert(node.id).second) {
            problems.push_back("Two nodes are called \"" + node.id + "\".");
        }
    }
    if (c.find(c.start) == nullptr) {
        problems.push_back("It starts at \"" + c.start + "\", which is not one of its nodes.");
    }
    std::set<std::string, std::less<>> reached;
    for (const TalkNode& node : c.nodes) {
        const auto leads = [&](const std::string& next, const std::string& what) {
            if (next.empty()) {
                return;
            }
            reached.insert(next);
            if (c.find(next) == nullptr) {
                std::string problem = "\"" + node.id + "\"";
                problem += what;
                problem += " leads to \"";
                problem += next;
                problem += "\", which is not one of its nodes.";
                problems.push_back(std::move(problem));
            }
        };
        leads(node.next, "");
        for (const TalkChoice& choice : node.choices) {
            leads(choice.next, ", choice \"" + choice.text + "\",");
        }
    }
    for (const TalkNode& node : c.nodes) {
        if (node.id != c.start && !reached.contains(node.id)) {
            problems.push_back("Nothing leads to \"" + node.id + "\".");
        }
    }
    return problems;
}

bool holds(const TalkCondition& condition, const InventoryObject& inventory,
           const QuestObject& quests, const Flags& flags) {
    bool result = false;
    switch (condition.kind) {
    case TalkCondition::Kind::HasItem:
        result = inventory.has(condition.name);
        break;
    case TalkCondition::Kind::Flag: {
        const auto found = flags.find(condition.name);
        result = found != flags.end() && found->second;
        break;
    }
    case TalkCondition::Kind::QuestActive:
        result = quests.active(condition.name);
        break;
    case TalkCondition::Kind::QuestDone:
        result = quests.done(condition.name);
        break;
    }
    return condition.negate ? !result : result;
}

void ConversationPlayer::start(Conversation conversation, std::string name, std::string_view node) {
    conversation_ = std::move(conversation);
    name_ = std::move(name);
    at_ = node.empty() ? conversation_.start : std::string(node);
    shown_ = false;
    playing_ = true;
    offered_.clear();
}

std::string ConversationPlayer::update(DialogueObject& dialogue, InventoryObject& inventory,
                                       QuestObject& quests, Flags& flags) {
    if (!playing_ || dialogue.is_open()) {
        return {};
    }
    const auto all_hold = [&](const std::vector<TalkCondition>& conditions) {
        return std::all_of(conditions.begin(), conditions.end(), [&](const TalkCondition& c) {
            return holds(c, inventory, quests, flags);
        });
    };
    if (shown_) {
        // On from the node shown: by the answer picked, or to the node after it.
        const TalkNode* shown = conversation_.find(at_);
        if (!offered_.empty()) {
            const auto answer =
                static_cast<std::size_t>(std::max<std::int64_t>(dialogue.answer(), 1));
            at_ = answer <= offered_.size() ? offered_[answer - 1] : std::string();
        } else {
            at_ = shown != nullptr ? shown->next : std::string();
        }
        shown_ = false;
        offered_.clear();
    }
    // The next node whose conditions hold; a guard against nodes that lead round in a ring.
    const TalkNode* node = nullptr;
    for (int hops = 0; hops < 256 && !at_.empty(); ++hops) {
        node = conversation_.find(at_);
        if (node == nullptr || all_hold(node->conditions)) {
            break;
        }
        at_ = node->next;
        node = nullptr;
    }
    if (node == nullptr) {
        playing_ = false;
        return name_;
    }
    for (const TalkEffect& effect : node->effects) {
        switch (effect.kind) {
        case TalkEffect::Kind::GiveItem:
            inventory.give(effect.name, std::max<std::int64_t>(whole(effect.value, 1), 1));
            break;
        case TalkEffect::Kind::TakeItem:
            inventory.take(effect.name, std::max<std::int64_t>(whole(effect.value, 1), 1));
            break;
        case TalkEffect::Kind::SetFlag:
            flags[effect.name] = true;
            break;
        case TalkEffect::Kind::ClearFlag:
            flags[effect.name] = false;
            break;
        case TalkEffect::Kind::StartQuest:
            quests.start(effect.name, effect.value);
            break;
        case TalkEffect::Kind::CompleteQuest:
            quests.complete(effect.name);
            break;
        }
    }
    std::string answers;
    for (const TalkChoice& choice : node->choices) {
        if (all_hold(choice.conditions) && !choice.text.empty()) {
            std::string shown = choice.text;
            std::replace(shown.begin(), shown.end(), '|', '/'); // "|" separates answers
            answers += (answers.empty() ? "" : "|") + shown;
            offered_.push_back(choice.next);
        }
    }
    if (offered_.empty()) {
        dialogue.say_as(node->speaker, node->text);
    } else {
        dialogue.ask_as(node->speaker, node->text, answers);
    }
    // How the speaker looks and sounds, with this line's own picture if it has one.
    if (const Speaker* speaker = conversation_.speaker(node->speaker)) {
        dialogue.set_look(SpeakerLook{node->portrait.empty() ? speaker->portrait : node->portrait,
                                      speaker->voice, speaker->pitch, speaker->color});
    } else if (!node->portrait.empty()) {
        dialogue.set_look(SpeakerLook{node->portrait, {}, 1.0, {}});
    }
    shown_ = true;
    return {};
}

} // namespace engine::game
