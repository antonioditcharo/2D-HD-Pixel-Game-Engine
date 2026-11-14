#pragma once

// The "quests" object logic uses for the story's tasks (plan/02 G5): a quest starts with a
// title, moves through stages with an objective the player reads ("Find the lost cat"),
// and is completed or failed. The journal lists them in a message box; saves keep them.
//
// See docs/dev/game.md §5.

#include "engine/foundation/reflect.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace engine::game {

class DialogueObject;

enum class QuestState : std::uint8_t { Active, Done, Failed };

struct Quest {
    std::string title;
    std::string objective; ///< What to do now, as the journal shows it.
    std::int64_t stage = 0;
    QuestState state = QuestState::Active;
    std::uint64_t order = 0; ///< When it started, for the journal's order.
};

class QuestObject {
public:
    /// The most quests a game keeps.
    static constexpr std::size_t most = 1000;

    /// Starts the quest `name` (spaces around it dropped) as `title` (the name if empty),
    /// at stage 0. A quest already started is left as it is.
    void start(const std::string& name, const std::string& title);
    /// What the player is to do now in `name`, as the journal shows it. Nothing for a quest
    /// not started.
    void set_objective(const std::string& name, const std::string& objective);
    /// Moves `name` to `stage` (0 or more). Nothing for a quest not started.
    void set_stage(const std::string& name, std::int64_t stage);
    /// Completes or fails a quest under way. Nothing otherwise.
    void complete(const std::string& name);
    void fail(const std::string& name);
    /// Shows the journal in a message box: the quests under way with their objectives,
    /// then those finished, each in the order it started.
    void show_journal();

    [[nodiscard]] bool started(const std::string& name) const;
    [[nodiscard]] bool active(const std::string& name) const;
    [[nodiscard]] bool done(const std::string& name) const;
    [[nodiscard]] bool failed(const std::string& name) const;
    /// The stage `name` is at; 0 for a quest not started.
    [[nodiscard]] std::int64_t stage_of(const std::string& name) const;
    [[nodiscard]] std::int64_t active_count() const;
    /// The journal's text, as show_journal() shows it.
    [[nodiscard]] std::string journal() const;

    /// Where the journal is shown; set by the runtime.
    void set_dialogue(DialogueObject* dialogue) noexcept { dialogue_ = dialogue; }
    /// Every quest, by name, for saves.
    [[nodiscard]] const std::map<std::string, Quest, std::less<>>& quests() const noexcept {
        return quests_;
    }
    /// Puts a quest back as saved.
    void restore(std::string_view name, Quest quest);
    void clear() noexcept {
        quests_.clear();
        next_ = 0;
    }

private:
    [[nodiscard]] Quest* find(std::string_view name);
    [[nodiscard]] const Quest* find(std::string_view name) const;

    std::map<std::string, Quest, std::less<>> quests_;
    std::uint64_t next_ = 0;
    DialogueObject* dialogue_ = nullptr;
};

/// Registers QuestObject as "quests".
void register_quest_types(reflect::Registry& registry);

} // namespace engine::game
