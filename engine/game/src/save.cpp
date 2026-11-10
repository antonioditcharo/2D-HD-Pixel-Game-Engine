#include "engine/game/save.h"

#include <array>
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

void register_save_types(reflect::Registry& registry) {
    auto game = registry.add_type<SaveObject>("game").display_name("Game").category("Game").tooltip(
        "Saving and loading the player's progress.");
    game.action<&SaveObject::save>("save", {"slot"})
        .text("Save the game in slot {slot}")
        .tooltip("Writes the player's progress to a save slot, from 1 to 99.");
    game.action<&SaveObject::load>("load", {"slot"})
        .text("Load the game from slot {slot}")
        .tooltip("Goes back to the progress saved in a slot.");
    game.condition<&SaveObject::has_save>("has_save", {"slot"})
        .text("Slot {slot} has a save")
        .tooltip("Whether anything was saved in a slot.");
}

} // namespace engine::game
