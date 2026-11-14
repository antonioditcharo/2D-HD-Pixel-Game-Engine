#include "editor/core/story.h"

#include "engine/game/save.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;

namespace {

constexpr std::size_t max_undo = 256;

bool valid_id(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}

} // namespace

StoryEditor::StoryEditor(game::Conversation conversation)
    : conversation_(std::move(conversation)) {}

game::Conversation StoryEditor::blank() {
    game::Conversation c;
    c.start = "start";
    game::TalkNode first;
    first.id = "start";
    first.text = "Hello!";
    c.nodes.push_back(std::move(first));
    return c;
}

std::optional<game::Conversation> StoryEditor::load(const fs::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "The conversation cannot be opened.";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return game::parse_conversation(text, error);
}

bool StoryEditor::save(const game::Conversation& conversation, const fs::path& file,
                       std::string& error) {
    return game::write_file_atomically(file, game::conversation_to_text(conversation), error);
}

game::TalkNode* StoryEditor::find(std::string_view id) noexcept {
    for (game::TalkNode& node : conversation_.nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

void StoryEditor::record(std::string label, game::Conversation before, std::string merge_key) {
    if (!merge_key.empty() && merge_key == merge_key_ && !undo_.empty() && redo_.empty()) {
        undo_.back().after = conversation_;
        undo_.back().serial_after = next_serial_++;
        serial_ = undo_.back().serial_after;
        return;
    }
    Step step{std::move(label), std::move(before), conversation_, serial_, next_serial_++};
    serial_ = step.serial_after;
    undo_.push_back(std::move(step));
    if (undo_.size() > max_undo) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    merge_key_ = std::move(merge_key);
}

std::string StoryEditor::add_node(double x, double y, std::string_view speaker,
                                  std::string_view after) {
    game::Conversation before = conversation_;
    std::string id;
    for (int n = 1;; ++n) {
        id = "node_" + std::to_string(n);
        if (find(id) == nullptr) {
            break;
        }
    }
    game::TalkNode node;
    node.id = id;
    node.speaker = std::string(speaker);
    node.x = x;
    node.y = y;
    conversation_.nodes.push_back(std::move(node));
    if (game::TalkNode* previous = find(after); previous != nullptr) {
        previous->next = id;
    }
    record("Add " + id, std::move(before));
    return id;
}

bool StoryEditor::remove_node(std::string_view id, std::string& error) {
    if (find(id) == nullptr) {
        return false;
    }
    if (id == conversation_.start) {
        error = "The conversation starts here. Make another node the start first.";
        return false;
    }
    game::Conversation before = conversation_;
    std::erase_if(conversation_.nodes, [id](const game::TalkNode& n) { return n.id == id; });
    for (game::TalkNode& node : conversation_.nodes) {
        if (node.next == id) {
            node.next.clear();
        }
        for (game::TalkChoice& choice : node.choices) {
            if (choice.next == id) {
                choice.next.clear();
            }
        }
    }
    record("Delete " + std::string(id), std::move(before));
    return true;
}

bool StoryEditor::rename_node(std::string_view id, std::string_view name, std::string& error) {
    game::TalkNode* node = find(id);
    if (node == nullptr || id == name) {
        return false;
    }
    if (!valid_id(name)) {
        error = "A node's name is letters, digits and _ only, such as ask_about_cat.";
        return false;
    }
    if (find(name) != nullptr) {
        error = "There is a node called \"" + std::string(name) + "\" already.";
        return false;
    }
    game::Conversation before = conversation_;
    const std::string old(id);
    node->id = std::string(name);
    for (game::TalkNode& other : conversation_.nodes) {
        if (other.next == old) {
            other.next = node->id;
        }
        for (game::TalkChoice& choice : other.choices) {
            if (choice.next == old) {
                choice.next = node->id;
            }
        }
    }
    if (conversation_.start == old) {
        conversation_.start = node->id;
    }
    record("Rename " + old, std::move(before));
    return true;
}

bool StoryEditor::set_node(std::string_view id, const game::TalkNode& value, std::string& error) {
    game::TalkNode* node = find(id);
    if (node == nullptr) {
        return false;
    }
    const auto leads_nowhere = [this](const std::string& next) {
        return !next.empty() && find(next) == nullptr;
    };
    if (leads_nowhere(value.next) ||
        std::any_of(value.choices.begin(), value.choices.end(),
                    [&](const game::TalkChoice& c) { return leads_nowhere(c.next); })) {
        error = "That leads to a node the conversation does not have.";
        return false;
    }
    game::TalkNode next = value;
    next.id = node->id;
    next.x = node->x;
    next.y = node->y;
    if (next == *node) {
        return false;
    }
    game::Conversation before = conversation_;
    *node = std::move(next);
    record("Change " + node->id, std::move(before), "node:" + node->id);
    return true;
}

bool StoryEditor::move_node(std::string_view id, double x, double y) {
    game::TalkNode* node = find(id);
    if (node == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        (node->x == x && node->y == y)) {
        return false;
    }
    game::Conversation before = conversation_;
    node->x = x;
    node->y = y;
    record("Move " + node->id, std::move(before), "move:" + node->id);
    return true;
}

bool StoryEditor::set_start(std::string_view id, std::string& error) {
    if (find(id) == nullptr) {
        error = "There is no such node.";
        return false;
    }
    if (conversation_.start == id) {
        return false;
    }
    game::Conversation before = conversation_;
    conversation_.start = std::string(id);
    record("Start at " + conversation_.start, std::move(before));
    return true;
}

std::optional<std::size_t> StoryEditor::add_choice(std::string_view id, std::string& error) {
    game::TalkNode* node = find(id);
    if (node == nullptr) {
        error = "There is no such node.";
        return std::nullopt;
    }
    if (node->choices.size() >= 8) {
        error = "A node offers at most 8 choices.";
        return std::nullopt;
    }
    game::Conversation before = conversation_;
    node->choices.push_back(
        game::TalkChoice{"Choice " + std::to_string(node->choices.size() + 1), {}, {}});
    record("Add a choice to " + node->id, std::move(before));
    return node->choices.size() - 1;
}

bool StoryEditor::remove_choice(std::string_view id, std::size_t index) {
    game::TalkNode* node = find(id);
    if (node == nullptr || index >= node->choices.size()) {
        return false;
    }
    game::Conversation before = conversation_;
    node->choices.erase(node->choices.begin() + static_cast<std::ptrdiff_t>(index));
    record("Remove a choice from " + node->id, std::move(before));
    return true;
}

bool StoryEditor::add_speaker(std::string_view name, std::string_view color) {
    if (name.empty() || conversation_.speaker(name) != nullptr) {
        return false;
    }
    game::Conversation before = conversation_;
    conversation_.speakers.push_back(game::Speaker{std::string(name), std::string(color)});
    record("Add speaker " + std::string(name), std::move(before));
    return true;
}

bool StoryEditor::set_speaker(std::string_view name, const game::Speaker& value) {
    const auto found = std::find_if(conversation_.speakers.begin(), conversation_.speakers.end(),
                                    [name](const game::Speaker& s) { return s.name == name; });
    if (found == conversation_.speakers.end()) {
        return false;
    }
    game::Speaker next = value;
    next.name = found->name;
    next.pitch = std::isfinite(next.pitch) ? std::clamp(next.pitch, 0.25, 4.0) : 1.0;
    if (next == *found) {
        return false;
    }
    game::Conversation before = conversation_;
    *found = std::move(next);
    record("Change speaker " + found->name, std::move(before), "speaker:" + found->name);
    return true;
}

std::string StoryEditor::undo_text() const {
    return undo_.empty() ? std::string() : undo_.back().label;
}

std::string StoryEditor::redo_text() const {
    return redo_.empty() ? std::string() : redo_.back().label;
}

bool StoryEditor::undo() {
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    conversation_ = step.before;
    serial_ = step.serial_before;
    redo_.push_back(std::move(step));
    merge_key_.clear();
    return true;
}

bool StoryEditor::redo() {
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    conversation_ = step.after;
    serial_ = step.serial_after;
    undo_.push_back(std::move(step));
    merge_key_.clear();
    return true;
}

} // namespace editor
