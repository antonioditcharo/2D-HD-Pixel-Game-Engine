#pragma once

// Conversations (plan/02 G1, G3): what characters say to each other, as a graph of nodes in
// a ".dtalk" file, as dialogue tools for games lay it out. Each node is a line one speaker
// says, then either the node after it or a few choices for the player, each leading on.
// Nodes and choices can depend on what the player carries, the story's flags and its
// quests, and change them as they are reached. Logic starts one with "Start the
// conversation"; the game plays it through the dialogue object's message boxes, the
// speaker's name above each.
//
// See docs/dev/story.md.

#include "engine/game/inventory.h"
#include "engine/game/quests.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

class DialogueObject;

/// A test a node or a choice can depend on. With `negate`, the opposite.
struct TalkCondition {
    enum class Kind : std::uint8_t { HasItem, Flag, QuestActive, QuestDone };
    Kind kind = Kind::HasItem;
    std::string name; ///< The item, flag or quest.
    bool negate = false;

    friend bool operator==(const TalkCondition&, const TalkCondition&) = default;
};

/// What reaching a node does.
struct TalkEffect {
    enum class Kind : std::uint8_t {
        GiveItem,
        TakeItem,
        SetFlag,
        ClearFlag,
        StartQuest,
        CompleteQuest
    };
    Kind kind = Kind::GiveItem;
    std::string name;  ///< The item, flag or quest.
    std::string value; ///< How many items (a whole number), or a quest's title.

    friend bool operator==(const TalkEffect&, const TalkEffect&) = default;
};

struct TalkChoice {
    std::string text;
    std::string next;                      ///< The node it leads to; empty ends the conversation.
    std::vector<TalkCondition> conditions; ///< All must hold for it to be offered.

    friend bool operator==(const TalkChoice&, const TalkChoice&) = default;
};

struct TalkNode {
    std::string id;      ///< Unique in its conversation: "start", "ask_about_cat".
    std::string speaker; ///< Who says it; empty for words without a speaker.
    std::string text;
    std::string next; ///< Without choices: the node after; empty ends.
    std::vector<TalkChoice> choices;
    /// All must hold for the node to be said; otherwise the conversation goes on to `next`.
    std::vector<TalkCondition> conditions;
    std::vector<TalkEffect> effects; ///< Done as the node is said.
    /// Another picture of the speaker for this line (a mood: "portraits/elder_glad.png");
    /// empty shows the speaker's own.
    std::string portrait;
    double x = 0.0; ///< Where the editor's graph shows it.
    double y = 0.0;

    friend bool operator==(const TalkNode&, const TalkNode&) = default;
};

struct Speaker {
    std::string name;
    std::string color = "#ffd27a"; ///< The name plate's colour, "#rrggbb".
    std::string portrait; ///< A picture in assets/ shown beside their words; empty for none.
    std::string voice;    ///< A sound in assets/ played as their words are typed (blips).
    double pitch = 1.0;   ///< The voice's pitch, 0.25 to 4.

    friend bool operator==(const Speaker&, const Speaker&) = default;
};

struct Conversation {
    std::string start = "start"; ///< The node it begins with.
    std::vector<Speaker> speakers;
    std::vector<TalkNode> nodes;

    [[nodiscard]] const TalkNode* find(std::string_view id) const noexcept;
    [[nodiscard]] const Speaker* speaker(std::string_view name) const noexcept;

    friend bool operator==(const Conversation&, const Conversation&) = default;
};

/// The ".dtalk" format name and version.
inline constexpr std::string_view conversation_format = "sparxel.conversation";
inline constexpr int conversation_version = 1;
/// The most nodes a conversation has.
inline constexpr std::size_t most_talk_nodes = 2000;

/// Reads ".dtalk" text. Nothing, with `error` in plain language, if it is not one.
[[nodiscard]] std::optional<Conversation> parse_conversation(std::string_view text,
                                                             std::string& error);
/// The ".dtalk" text of a conversation; the same conversation is always the same text.
[[nodiscard]] std::string conversation_to_text(const Conversation& conversation);
/// What is wrong with a conversation, in plain language: a start or a "next" naming a node
/// that is not there, two nodes with one id, a node no way leads to. Empty if nothing.
[[nodiscard]] std::vector<std::string> check_conversation(const Conversation& conversation);

/// The story's flags: yes/no facts conversations set and test ("met_the_elder").
using Flags = std::map<std::string, bool, std::less<>>;

/// Plays a conversation through the dialogue object's message boxes, one node at a time.
class ConversationPlayer {
public:
    /// Starts `conversation` at `node` (empty: its start). Replaces one playing.
    void start(Conversation conversation, std::string name, std::string_view node = {});
    /// Called each step: once no message is open, goes on to the next node (by the answer
    /// picked, after a question) and shows it, doing its effects. Returns the name of a
    /// conversation that just ended, to tell Rules; empty otherwise.
    std::string update(DialogueObject& dialogue, InventoryObject& inventory, QuestObject& quests,
                       Flags& flags);
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    /// Stops without telling Rules: when a save is loaded.
    void stop() noexcept { playing_ = false; }

private:
    Conversation conversation_;
    std::string name_;
    std::string at_;     ///< The node to show next, or showing.
    bool shown_ = false; ///< Whether `at_` is showing.
    bool playing_ = false;
    std::vector<std::string> offered_; ///< The nodes the choices shown lead to.
};

/// Whether `condition` holds.
[[nodiscard]] bool holds(const TalkCondition& condition, const InventoryObject& inventory,
                         const QuestObject& quests, const Flags& flags);

} // namespace engine::game
