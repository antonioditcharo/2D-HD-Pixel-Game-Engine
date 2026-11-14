#include "engine/game/save.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace engine::game {

namespace fs = std::filesystem;

fs::path save_slot_file(const fs::path& folder, std::int64_t slot) {
    if (slot < 1 || slot > max_save_slot) {
        return {};
    }
    std::array<char, 32> name{};
    std::snprintf(name.data(), name.size(), "slot-%02lld.sparxel-save",
                  static_cast<long long>(slot));
    return folder / name.data();
}

bool write_file_atomically(const fs::path& file, std::string_view text, std::string& error) {
    std::error_code ec;
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
        if (ec) {
            error = "The folder " + file.parent_path().generic_string() +
                    " cannot be created: " + ec.message();
            return false;
        }
    }
    fs::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            error = "The file " + temporary.generic_string() + " cannot be written.";
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, file, ec);
    if (ec) {
        error = "The file " + file.generic_string() + " cannot be replaced: " + ec.message();
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

fs::path SaveObject::folder() const {
    if (!has_resolved_) {
        resolved_ = folder_ ? folder_() : fs::path{};
        has_resolved_ = true;
    }
    return resolved_;
}

bool SaveObject::has_save(std::int64_t slot) const {
    const fs::path base = folder();
    if (base.empty()) {
        return false;
    }
    const fs::path file = save_slot_file(base, slot);
    std::error_code ec;
    return !file.empty() && fs::is_regular_file(file, ec);
}

void SaveObject::start_timer(const std::string& name, double seconds) {
    if (!name.empty() && std::isfinite(seconds)) {
        timers_[name] = std::max(seconds, 0.0);
    }
}

double SaveObject::time_left(const std::string& name) const {
    const auto found = timers_.find(name);
    return found == timers_.end() ? 0.0 : found->second;
}

std::vector<std::string> SaveObject::tick_timers(double seconds) {
    std::vector<std::string> ended;
    for (auto it = timers_.begin(); it != timers_.end();) {
        it->second -= seconds;
        if (it->second <= 1e-9) {
            ended.push_back(it->first);
            it = timers_.erase(it);
        } else {
            ++it;
        }
    }
    return ended;
}

void SaveObject::unlock(const std::string& name) {
    if (!name.empty() && achievements_.insert(name).second) {
        newly_.push_back(name);
    }
}

void register_save_types(reflect::Registry& registry) {
    auto game = registry.add_type<SaveObject>("game").display_name("Game").category("Game").tooltip(
        "Saving and loading the player's progress.");
    game.action<&SaveObject::save>("save", {"slot"})
        .text("Save the game in slot {slot}")
        .tooltip("Writes the player's progress to a save slot, from 1 to 99.")
        .param("slot")
        .range(1.0, 99.0);
    game.action<&SaveObject::load>("load", {"slot"})
        .text("Load the game from slot {slot}")
        .tooltip("Goes back to the progress saved in a slot.")
        .param("slot")
        .range(1.0, 99.0);
    game.condition<&SaveObject::has_save>("has_save", {"slot"})
        .text("Slot {slot} has a save")
        .tooltip("Whether anything was saved in a slot.")
        .param("slot")
        .range(1.0, 99.0);
    game.action<&SaveObject::go_to_scene>("go_to_scene", {"scene"})
        .text("Go to scene {scene}")
        .tooltip("Leaves this scene for another, such as maps/cave.dscene, when this step of "
                 "the game ends. Variables and waiting Sequences carry on.");
    game.action<&SaveObject::go_to_scene_at>("go_to_scene_at", {"scene", "where"})
        .text("Go to scene {scene}, arriving at {where}")
        .tooltip("Leaves this scene for another, and puts the player where something of "
                 "that name is in it: a door, a stair.");
    game.action<&SaveObject::create>("create", {"prefab", "where"})
        .text("Create {prefab} at {where}")
        .tooltip("A copy of a prefab (prefabs/coin.dprefab) where something of that name is: a "
                 "chest, the player. Copies are named after the prefab, numbered after the "
                 "first: coin, coin_2.");
    game.action<&SaveObject::create_at>("create_at", {"prefab", "x", "y"})
        .text("Create {prefab} at {x}, {y}")
        .tooltip("A copy of a prefab at a place on the map, in tiles.");
    game.action<&SaveObject::remove>("remove", {"name"})
        .text("Remove {name}")
        .tooltip("Takes something out of the scene, with what is under it: a coin picked "
                 "up, an enemy beaten.");
    game.action<&SaveObject::start_timer>("start_timer", {"name", "seconds"})
        .text("Start the timer {name} for {seconds} seconds")
        .tooltip("Counts down while the game plays; when it ends, Rules on When a timer ends "
                 "run. Starting it again starts it over.")
        .param("seconds")
        .range(0.0, 86400.0)
        .default_value(reflect::Value{5.0});
    game.action<&SaveObject::stop_timer>("stop_timer", {"name"})
        .text("Stop the timer {name}")
        .tooltip("It ends without telling Rules.");
    game.condition<&SaveObject::timer_running>("timer_running", {"name"})
        .text("The timer {name} is running")
        .tooltip("Started, and not yet ended or stopped.");
    game.getter<&SaveObject::time_left>("time_left", {"name"})
        .text("The seconds left on the timer {name}")
        .tooltip("0 for a timer not running.");
    game.event<std::string>("timer_ended", {"name"})
        .text("When a timer ends ({name})")
        .tooltip("A timer started by logic counted down to 0.");
    game.action<&SaveObject::open_save_menu>("open_save_menu")
        .text("Open the save menu")
        .tooltip("The save slots, as at a save point: a crystal the player talks to. "
                 "Cancel goes back to the game.");
    game.action<&SaveObject::show_minimap>("show_minimap")
        .text("Show the minimap")
        .tooltip("The small map of the tiles around the player, top right.");
    game.action<&SaveObject::hide_minimap>("hide_minimap")
        .text("Hide the minimap")
        .tooltip("In a cut scene, or where the player should not see the way.");
    game.action<&SaveObject::send_message>("send_message", {"name"})
        .text("Send the message {name}")
        .tooltip("Every Rule on When a message is sent hears it, with its name: one part of the "
                 "game telling others, as door_opened.");
    game.event<std::string>("message", {"name"})
        .text("When a message is sent ({name})")
        .tooltip("Logic sent a message with Send the message.");
    game.action<&SaveObject::unlock>("unlock", {"name"})
        .text("Unlock the achievement {name}")
        .tooltip("Kept for the player across every save, and announced once at the top of "
                 "the screen. Names and descriptions come from data/achievements.");
    game.condition<&SaveObject::unlocked>("unlocked", {"name"})
        .text("The achievement {name} is unlocked")
        .tooltip("Whether the player has unlocked it, in this game or another.");
    game.getter<&SaveObject::achievement_count>("achievement_count")
        .text("The number of achievements unlocked")
        .tooltip("How many the player has unlocked.");
    game.getter<&SaveObject::last_created>("last_created")
        .text("The name of the last thing created")
        .tooltip("Such as coin_3: to remember it, or to remove it later.");
    game.condition<&SaveObject::in_scene>("in_scene", {"scene"})
        .text("The scene is {scene}")
        .tooltip("Whether the scene being played is this one.");
    game.condition<&SaveObject::is_tagged>("is_tagged", {"object", "tag"})
        .text("{object} is tagged {tag}")
        .tooltip("Whether an object (such as the event's [who]) has the tag: \"[who] is "
                 "tagged enemy\". No for an object without it, or no such object.");
}

} // namespace engine::game
