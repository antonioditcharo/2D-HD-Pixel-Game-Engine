#pragma once

// Undo and redo by snapshots, for the editor's windows that edit one value (a table, a
// conversation): each step keeps the value before and after it, labelled in plain language.
// Changes to one thing in a row (typing into one cell) merge into one step until
// end_merge(). Knows whether the value changed since it was saved.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace editor {

template <typename T>
class History {
public:
    explicit History(T value) : value_(std::move(value)) {}

    [[nodiscard]] const T& value() const noexcept { return value_; }

    /// Changes the value by `change` (a function taking T&) as one step labelled `label`.
    /// Returns false, recording nothing, if `change` returns false or changed nothing.
    template <typename Change>
    bool apply(std::string label, Change&& change, std::string merge_key = {}) {
        T before = value_;
        if (!change(value_) || value_ == before) {
            value_ = std::move(before);
            return false;
        }
        if (!merge_key.empty() && merge_key == merge_key_ && !undo_.empty() && redo_.empty()) {
            undo_.back().after = value_;
            undo_.back().serial_after = next_serial_++;
            serial_ = undo_.back().serial_after;
            return true;
        }
        undo_.push_back(Step{std::move(label), std::move(before), value_, serial_, next_serial_++});
        serial_ = undo_.back().serial_after;
        if (undo_.size() > most) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
        merge_key_ = std::move(merge_key);
        return true;
    }

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::string undo_text() const {
        return undo_.empty() ? std::string() : undo_.back().label;
    }
    [[nodiscard]] std::string redo_text() const {
        return redo_.empty() ? std::string() : redo_.back().label;
    }
    bool undo() {
        if (undo_.empty()) {
            return false;
        }
        Step step = std::move(undo_.back());
        undo_.pop_back();
        value_ = step.before;
        serial_ = step.serial_before;
        redo_.push_back(std::move(step));
        merge_key_.clear();
        return true;
    }
    bool redo() {
        if (redo_.empty()) {
            return false;
        }
        Step step = std::move(redo_.back());
        redo_.pop_back();
        value_ = step.after;
        serial_ = step.serial_after;
        undo_.push_back(std::move(step));
        merge_key_.clear();
        return true;
    }
    void end_merge() noexcept { merge_key_.clear(); }
    [[nodiscard]] bool modified() const noexcept { return serial_ != saved_; }
    void mark_saved() noexcept { saved_ = serial_; }

    /// The most steps kept; older ones are forgotten.
    static constexpr std::size_t most = 256;

private:
    struct Step {
        std::string label;
        T before;
        T after;
        std::uint64_t serial_before = 0;
        std::uint64_t serial_after = 0;
    };
    T value_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::string merge_key_;
    std::uint64_t serial_ = 0;
    std::uint64_t next_serial_ = 1;
    std::uint64_t saved_ = 0;
};

} // namespace editor
