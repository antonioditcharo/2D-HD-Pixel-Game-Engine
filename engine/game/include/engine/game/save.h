#pragma once

// Saved games (plan/03 §10): the state of a running game written to a file and read back.
// A save holds the world (every entity and component, as a scene) and every logic asset's
// variables. Files are written atomically, so a crash or power cut while saving never
// leaves a broken save behind.
//
// See docs/dev/game.md §8.

#include "engine/foundation/reflect.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::game {

inline constexpr std::string_view save_format = "sparxel.save";
inline constexpr int save_version = 1;
/// Slots are numbered from 1 to this.
inline constexpr std::int64_t max_save_slot = 99;

/// The file of save slot `slot` in `folder`, for example "slot-07.sparxel-save". Empty for a
/// slot outside 1 to max_save_slot.
[[nodiscard]] std::filesystem::path save_slot_file(const std::filesystem::path& folder,
                                                   std::int64_t slot);

/// Writes `text` to `file`: first to a temporary file beside it, which then replaces `file`
/// in one step. Creates missing folders. Returns false, with `error`, if anything fails;
/// `file` is then unchanged.
[[nodiscard]] bool write_file_atomically(const std::filesystem::path& file, std::string_view text,
                                         std::string& error);

/// The "game" object logic uses to save and load: "Save the game in slot [n]", "Load slot
/// [n]" and the condition "Slot [n] has a save". Saving and loading happen at the end of
/// the step, after logic has run, so a Rule never sees a half-loaded world.
class SaveObject {
public:
    using Folder = std::function<std::filesystem::path()>;
    SaveObject() = default;
    /// `folder` gives the save folder when it is first needed (finding it may create it).
    explicit SaveObject(Folder folder) : folder_(std::move(folder)) {}

    void save(std::int64_t slot) { requests_.push_back(Request{false, slot}); }
    void load(std::int64_t slot) { requests_.push_back(Request{true, slot}); }
    [[nodiscard]] bool has_save(std::int64_t slot) const;

    struct Request {
        bool load = false;
        std::int64_t slot = 0;
    };
    /// The requests made since the last call, oldest first.
    [[nodiscard]] std::vector<Request> take_requests() { return std::exchange(requests_, {}); }
    /// The save folder; empty if there is none.
    [[nodiscard]] std::filesystem::path folder() const;

private:
    Folder folder_;
    mutable std::filesystem::path resolved_;
    mutable bool has_resolved_ = false;
    std::vector<Request> requests_;
};

/// Registers SaveObject as "game".
void register_save_types(reflect::Registry& registry);

} // namespace engine::game
