#include "engine/game/quests.h"

#include "engine/game/dialogue.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace engine::game {

namespace {

/// `name` without spaces around it.
std::string_view trimmed(std::string_view name) {
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = name.find_last_not_of(" \t");
    return name.substr(first, last - first + 1);
}

} // namespace

Quest* QuestObject::find(std::string_view name) {
    const auto found = quests_.find(trimmed(name));
    return found == quests_.end() ? nullptr : &found->second;
}

const Quest* QuestObject::find(std::string_view name) const {
    const auto found = quests_.find(trimmed(name));
    return found == quests_.end() ? nullptr : &found->second;
}

void QuestObject::start(const std::string& name, const std::string& title) {
    const std::string_view key = trimmed(name);
    if (key.empty() || find(key) != nullptr || quests_.size() >= most) {
        return;
    }
    Quest quest;
    const std::string_view shown = trimmed(title);
    quest.title = std::string(shown.empty() ? key : shown);
    quest.order = next_++;
    quests_.emplace(std::string(key), std::move(quest));
}

void QuestObject::set_objective(const std::string& name, const std::string& objective) {
    if (Quest* quest = find(name)) {
        quest->objective = std::string(trimmed(objective));
    }
}

void QuestObject::set_stage(const std::string& name, std::int64_t stage) {
    if (Quest* quest = find(name)) {
        quest->stage = std::max<std::int64_t>(stage, 0);
    }
}

void QuestObject::complete(const std::string& name) {
    if (Quest* quest = find(name); quest != nullptr && quest->state == QuestState::Active) {
        quest->state = QuestState::Done;
    }
}

void QuestObject::fail(const std::string& name) {
    if (Quest* quest = find(name); quest != nullptr && quest->state == QuestState::Active) {
        quest->state = QuestState::Failed;
    }
}

bool QuestObject::started(const std::string& name) const {
    return find(name) != nullptr;
}

bool QuestObject::active(const std::string& name) const {
    const Quest* quest = find(name);
    return quest != nullptr && quest->state == QuestState::Active;
}

bool QuestObject::done(const std::string& name) const {
    const Quest* quest = find(name);
    return quest != nullptr && quest->state == QuestState::Done;
}

bool QuestObject::failed(const std::string& name) const {
    const Quest* quest = find(name);
    return quest != nullptr && quest->state == QuestState::Failed;
}

std::int64_t QuestObject::stage_of(const std::string& name) const {
    const Quest* quest = find(name);
    return quest == nullptr ? 0 : quest->stage;
}

std::int64_t QuestObject::active_count() const {
    return static_cast<std::int64_t>(
        std::count_if(quests_.begin(), quests_.end(),
                      [](const auto& entry) { return entry.second.state == QuestState::Active; }));
}

std::string QuestObject::journal() const {
    std::vector<const Quest*> ordered;
    ordered.reserve(quests_.size());
    for (const auto& [name, quest] : quests_) {
        ordered.push_back(&quest);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Quest* a, const Quest* b) { return a->order < b->order; });
    std::string text;
    for (const Quest* quest : ordered) {
        if (quest->state == QuestState::Active) {
            text += (text.empty() ? "" : "\n") + quest->title +
                    (quest->objective.empty() ? std::string() : ": " + quest->objective);
        }
    }
    for (const Quest* quest : ordered) {
        if (quest->state != QuestState::Active) {
            text += (text.empty() ? "" : "\n") + quest->title +
                    (quest->state == QuestState::Done ? " (done)" : " (failed)");
        }
    }
    return text.empty() ? std::string("The journal is empty.") : "Journal\n" + text;
}

void QuestObject::show_journal() {
    if (dialogue_ != nullptr) {
        dialogue_->say(journal());
    }
}

void QuestObject::restore(std::string_view name, Quest quest) {
    const std::string_view key = trimmed(name);
    if (key.empty() || quests_.size() >= most) {
        return;
    }
    next_ = std::max(next_, quest.order + 1);
    quests_.insert_or_assign(std::string(key), std::move(quest));
}

void register_quest_types(reflect::Registry& registry) {
    auto quests =
        registry.add_type<QuestObject>("quests").display_name("Quests").category("Story").tooltip(
            "The story's tasks: started, moved through stages, completed or "
            "failed, and listed in the journal.");
    quests.action<&QuestObject::start>("start", {"name", "title"})
        .text("Start the quest {name}: {title}")
        .tooltip("Starts a quest by name, with the title the journal shows. Starting it again "
                 "changes nothing.");
    quests.action<&QuestObject::set_objective>("set_objective", {"name", "objective"})
        .text("Set the objective of {name} to {objective}")
        .tooltip("What the player is to do now, as the journal shows it.");
    quests.action<&QuestObject::set_stage>("set_stage", {"name", "stage"})
        .text("Move the quest {name} to stage {stage}")
        .tooltip("Where the quest has got to, for Rules to check: 0, 1, 2...")
        .param("stage")
        .range(0.0, 1000000.0);
    quests.action<&QuestObject::complete>("complete", {"name"})
        .text("Complete the quest {name}")
        .tooltip("The quest is done: the journal lists it as done.");
    quests.action<&QuestObject::fail>("fail", {"name"})
        .text("Fail the quest {name}")
        .tooltip("The quest can no longer be done: the journal lists it as failed.");
    quests.action<&QuestObject::show_journal>("show_journal")
        .text("Show the journal")
        .tooltip("A message listing the quests under way, with what to do, then those "
                 "finished.");
    quests.condition<&QuestObject::started>("started", {"name"})
        .text("The quest {name} has started")
        .tooltip("Whether it was ever started, finished or not.");
    quests.condition<&QuestObject::active>("active", {"name"})
        .text("The quest {name} is under way")
        .tooltip("Started, and neither completed nor failed.");
    quests.condition<&QuestObject::done>("done", {"name"})
        .text("The quest {name} is complete")
        .tooltip("Whether it was completed.");
    quests.condition<&QuestObject::failed>("failed", {"name"})
        .text("The quest {name} has failed")
        .tooltip("Whether it was failed.");
    quests.getter<&QuestObject::stage_of>("stage_of", {"name"})
        .text("The stage of {name}")
        .tooltip("Where the quest has got to; 0 if it has not started.");
    quests.getter<&QuestObject::active_count>("active_count")
        .text("The number of quests under way")
        .tooltip("How many quests are started and not finished.");
}

} // namespace engine::game
