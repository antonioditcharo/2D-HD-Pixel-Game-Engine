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
#include <map>
#include <optional>
#include <set>
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
/// [n]" and the condition "Slot [n] has a save"; and to move between scenes: "Go to scene
/// [scene]". Saving, loading and changing scene happen at the end of the step, after
/// logic has run, so a Rule never sees a half-loaded world.
class SaveObject {
public:
    using Folder = std::function<std::filesystem::path()>;
    SaveObject() = default;
    /// `folder` gives the save folder when it is first needed (finding it may create it).
    explicit SaveObject(Folder folder) : folder_(std::move(folder)) {}

    void save(std::int64_t slot) { requests_.push_back(Request{false, slot}); }
    void load(std::int64_t slot) { requests_.push_back(Request{true, slot}); }
    [[nodiscard]] bool has_save(std::int64_t slot) const;

    /// "Go to scene [scene]": the scene (its path in assets/, or its GUID) replaces the
    /// one being played at the end of the step. With `where`, the player arrives where
    /// the entity of that name is in the new scene.
    void go_to_scene(const std::string& scene) { scene_request_ = SceneRequest{scene, {}}; }
    void go_to_scene_at(const std::string& scene, const std::string& where) {
        scene_request_ = SceneRequest{scene, where};
    }
    /// "The scene is [scene]": whether `scene` (path or GUID) is the one being played.
    [[nodiscard]] bool in_scene(const std::string& scene) const {
        return !scene.empty() && (scene == scene_path_ || scene == scene_guid_);
    }
    struct SceneRequest {
        std::string scene;
        std::string where;
    };
    /// The scene asked for since the last call (the last one asked for wins), if any.
    [[nodiscard]] std::optional<SceneRequest> take_scene_request() {
        return std::exchange(scene_request_, std::nullopt);
    }
    /// Told by the runtime which scene is being played: its path in assets/ and its GUID.
    void set_scene(std::string path, std::string guid) {
        scene_path_ = std::move(path);
        scene_guid_ = std::move(guid);
    }
    [[nodiscard]] const std::string& scene() const noexcept { return scene_path_; }

    struct Request {
        bool load = false;
        std::int64_t slot = 0;
    };
    /// The requests made since the last call, oldest first.
    [[nodiscard]] std::vector<Request> take_requests() { return std::exchange(requests_, {}); }

    // ----- Making and removing things while playing (plan/02 E9) -----
    /// Makes a copy of a prefab (".dprefab", by its path in assets/) where something of
    /// that name is; the runtime takes the request.
    void create(const std::string& prefab, const std::string& where) {
        spawns_.push_back(Spawn{prefab, where, 0.0, 0.0, false});
    }
    /// The same at (x, y), in tiles.
    void create_at(const std::string& prefab, double x, double y) {
        spawns_.push_back(Spawn{prefab, {}, x, y, false});
    }
    /// Removes the entity of that name (or GUID), and what is under it.
    void remove(const std::string& name) { spawns_.push_back(Spawn{{}, name, 0.0, 0.0, true}); }
    struct Spawn {
        std::string prefab;
        std::string where; ///< The entity to create at, or to remove.
        double x = 0.0;
        double y = 0.0;
        bool remove = false;
    };
    [[nodiscard]] std::vector<Spawn> take_spawns() { return std::exchange(spawns_, {}); }
    // ----- Timers and messages (plan/02 E9) -----
    /// Starts (or starts again) a timer counting down `seconds`; when it ends, Rules on
    /// "timer_ended" hear its name.
    void start_timer(const std::string& name, double seconds);
    void stop_timer(const std::string& name) { timers_.erase(name); }
    [[nodiscard]] bool timer_running(const std::string& name) const {
        return timers_.contains(name);
    }
    /// Seconds a timer still has; 0 for one not running.
    [[nodiscard]] double time_left(const std::string& name) const;
    /// Counts every timer down by `seconds`; returns the names of those that ended.
    std::vector<std::string> tick_timers(double seconds);
    [[nodiscard]] std::map<std::string, double, std::less<>>& timers() noexcept { return timers_; }
    [[nodiscard]] const std::map<std::string, double, std::less<>>& timers() const noexcept {
        return timers_;
    }
    /// Sends a message every Rule on "message" hears, with its name, at the end of the step:
    /// one part of the game telling others ("door_opened").
    void send_message(const std::string& name) { messages_.push_back(name); }
    [[nodiscard]] std::vector<std::string> take_messages() { return std::exchange(messages_, {}); }

    // ----- Achievements (plan/02 F1) -----
    /// Unlocks an achievement (a row of data/achievements, or any name): kept for the player
    /// across every save, and announced once.
    void unlock(const std::string& name);
    [[nodiscard]] bool unlocked(const std::string& name) const {
        return achievements_.contains(name);
    }
    [[nodiscard]] std::int64_t achievement_count() const noexcept {
        return static_cast<std::int64_t>(achievements_.size());
    }
    [[nodiscard]] const std::set<std::string, std::less<>>& achievements() const noexcept {
        return achievements_;
    }
    /// Those unlocked before (read from the player's file at start): not announced again.
    void set_achievements(std::set<std::string, std::less<>> names) {
        achievements_ = std::move(names);
    }
    /// Unlocked since the last call, to announce and keep.
    [[nodiscard]] std::vector<std::string> take_unlocked() { return std::exchange(newly_, {}); }

    /// Opens the save slots, as at a save point; the runtime takes the request.
    void open_save_menu() { save_menu_ = true; }
    [[nodiscard]] bool take_save_menu() { return std::exchange(save_menu_, false); }

    /// The minimap shown or hidden by logic; nothing keeps the project's setting.
    void show_minimap() { minimap_ = true; }
    void hide_minimap() { minimap_ = false; }
    [[nodiscard]] std::optional<bool> minimap() const noexcept { return minimap_; }

    /// The name of the last copy made, for Rules to act on it.
    [[nodiscard]] std::string last_created() const { return last_created_; }
    /// Whether the object named `object` (a name or GUID, as an event's who) has the tag
    /// `tag`; false for no such object, or none tagged so. The runtime answers it.
    [[nodiscard]] bool is_tagged(const std::string& object, const std::string& tag) const {
        return tag_check_ && tag_check_(object, tag);
    }
    using TagCheck = std::function<bool(std::string_view object, std::string_view tag)>;
    void set_tag_check(TagCheck check) { tag_check_ = std::move(check); }
    void set_last_created(std::string name) { last_created_ = std::move(name); }
    /// The save folder; empty if there is none.
    [[nodiscard]] std::filesystem::path folder() const;

private:
    Folder folder_;
    mutable std::filesystem::path resolved_;
    mutable bool has_resolved_ = false;
    std::vector<Request> requests_;
    std::vector<Spawn> spawns_;
    std::map<std::string, double, std::less<>> timers_;
    std::vector<std::string> messages_;
    std::optional<bool> minimap_;
    bool save_menu_ = false;
    std::set<std::string, std::less<>> achievements_;
    std::vector<std::string> newly_;
    std::string last_created_;
    std::optional<SceneRequest> scene_request_;
    std::string scene_path_;
    std::string scene_guid_;
    TagCheck tag_check_;
};

/// Registers SaveObject as "game".
void register_save_types(reflect::Registry& registry);

} // namespace engine::game
