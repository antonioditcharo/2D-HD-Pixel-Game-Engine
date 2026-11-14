#pragma once

// The Story editor's model (plan/06 §3.6): a conversation (engine/game/conversation.h)
// being edited, with undo. Every edit is one step, labelled in plain language; typing into
// one field, or dragging one node, merges into one step until end_merge(). No user
// interface: tested on its own.
//
// See docs/dev/story.md.

#include "engine/game/conversation.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class StoryEditor {
public:
    explicit StoryEditor(engine::game::Conversation conversation);

    /// A new conversation: one node, "start", said by nobody yet.
    [[nodiscard]] static engine::game::Conversation blank();
    /// Reads a ".dtalk" file. Nothing, with `error`, if it cannot.
    [[nodiscard]] static std::optional<engine::game::Conversation>
    load(const std::filesystem::path& file, std::string& error);
    /// Writes a ".dtalk" file, atomically. False, with `error`, if it cannot.
    static bool save(const engine::game::Conversation& conversation,
                     const std::filesystem::path& file, std::string& error);

    [[nodiscard]] const engine::game::Conversation& conversation() const noexcept {
        return conversation_;
    }
    [[nodiscard]] const engine::game::TalkNode* node(std::string_view id) const noexcept {
        return conversation_.find(id);
    }

    /// Adds a node at (x, y) in the graph, called "node_1" (or the next free number), said
    /// by `speaker`. With `after`, that node leads to it (its "next"). Returns its id.
    std::string add_node(double x, double y, std::string_view speaker = {},
                         std::string_view after = {});
    /// Deletes a node; what led to it now ends there instead. The start node stays.
    bool remove_node(std::string_view id, std::string& error);
    /// Renames a node, and everything that leads to it. Refused for an empty name, a name
    /// taken, or one with characters other than letters, digits and _.
    bool rename_node(std::string_view id, std::string_view name, std::string& error);
    /// Changes a node's words, speaker, next node, conditions, effects and choices (its id
    /// and place stay). Typing merges into one step.
    bool set_node(std::string_view id, const engine::game::TalkNode& value, std::string& error);
    /// Moves a node in the graph; a drag merges into one step.
    bool move_node(std::string_view id, double x, double y);
    bool set_start(std::string_view id, std::string& error);
    /// Adds a choice to a node, leading nowhere yet; returns its index.
    std::optional<std::size_t> add_choice(std::string_view id, std::string& error);
    bool remove_choice(std::string_view id, std::size_t index);
    /// Adds a speaker (unless one has the name) with a name plate colour.
    bool add_speaker(std::string_view name, std::string_view color = "#ffd27a");
    /// Changes how a speaker looks and sounds (plate colour, portrait, voice, pitch); the
    /// name stays. Typing merges into one step.
    bool set_speaker(std::string_view name, const engine::game::Speaker& value);

    // ----- Undo -----
    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::string undo_text() const;
    [[nodiscard]] std::string redo_text() const;
    bool undo();
    bool redo();
    void end_merge() noexcept { merge_key_.clear(); }
    [[nodiscard]] bool modified() const noexcept { return serial_ != saved_; }
    void mark_saved() noexcept { saved_ = serial_; }

private:
    struct Step {
        std::string label;
        engine::game::Conversation before;
        engine::game::Conversation after;
        std::uint64_t serial_before = 0;
        std::uint64_t serial_after = 0;
    };
    [[nodiscard]] engine::game::TalkNode* find(std::string_view id) noexcept;
    void record(std::string label, engine::game::Conversation before, std::string merge_key = {});

    engine::game::Conversation conversation_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::string merge_key_;
    std::uint64_t serial_ = 0;
    std::uint64_t next_serial_ = 1;
    std::uint64_t saved_ = 0;
};

} // namespace editor
