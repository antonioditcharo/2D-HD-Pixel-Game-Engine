#pragma once

// Ready-made menus (plan/02 F1, K3), as RPG Maker games come with them: a title screen
// (New game, Continue, Quit) and a pause menu on the Menu key (Resume, Items, Quests, Party,
// Save, Load, Quit), with pages for what the player carries, the journal, the party's
// status and the save slots. A project turns each on in its settings.
//
// See docs/dev/menus.md.

#include "engine/game/battle_screen.h"
#include "engine/game/inventory.h"
#include "engine/game/localization.h"
#include "engine/game/quests.h"
#include "engine/game/save.h"
#include "engine/input/input.h"
#include "engine/render/render.h"
#include "engine/ui/text.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// The words of the title screen and the pause menu, to translate.
[[nodiscard]] std::vector<std::string> menu_words();

/// The save slots the menus offer.
inline constexpr std::int64_t menu_slots = 3;

class GameMenu {
public:
    enum class Page : std::uint8_t {
        None,
        Title,
        Pause,
        Items,
        Quests,
        Party,
        Equip,
        Options,
        Save,
        Load
    };
    /// What the player chose on the Options page.
    struct Options {
        double music = 1.0;  ///< 0 to 1.
        double sounds = 1.0; ///< 0 to 1.
        /// Letters a second messages are typed at; -1 keeps the game's own, 0 shows them
        /// whole.
        double text_speed = -1.0;
        /// A language of data/translations; empty keeps the game's own.
        std::string language;
        /// Each action the player can rebind, with the names of its keys now.
        std::vector<std::pair<std::string, std::string>> keys;
    };
    /// The Options page's first key line: Music, Sounds, Text speed and Language come first.
    static constexpr std::size_t first_key_line = 4;
    /// What a choice asks the game to do.
    struct Result {
        enum class Kind : std::uint8_t { None, NewGame, Load, Save, Quit, Resume, Options, Rebind };
        Kind kind = Kind::None;
        std::int64_t slot = 0;
        std::string action;       ///< Rebind: the action...
        input::Binding binding{}; ///< ...and its new key.
    };

    /// What the Equip page works on: the party (changed), the tables for names and numbers,
    /// and what the player carries. Set once by the runtime.
    void attach(PartyObject* party, const TablesObject* tables,
                const InventoryObject* inventory) noexcept {
        party_ = party;
        tables_ = tables;
        inventory_ = inventory;
    }
    /// Where the menus' words are translated from, and the languages offered; null for none.
    void set_translations(const Translations* translations) noexcept {
        translations_ = translations;
    }
    /// The Equip page's step: choosing a member, one of their slots, or what to wear there.
    enum class EquipStep : std::uint8_t { Member, Slot, Item };
    [[nodiscard]] EquipStep equip_step() const noexcept { return equip_step_; }

    void open_title();
    void open_pause();
    void close() noexcept { page_ = Page::None; }
    [[nodiscard]] bool open() const noexcept { return page_ != Page::None; }
    [[nodiscard]] Page page() const noexcept { return page_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    /// The choices of the page showing (empty for pages that only show something).
    [[nodiscard]] std::vector<std::string> choices(const SaveObject& saves) const;

    /// A pressed action ("move_up", "move_down", "interact", "cancel", "menu").
    Result press(std::string_view action, const SaveObject& saves);
    /// A line to show for a moment, such as "Saved in slot 2."
    void say(std::string line) { note_ = std::move(line); }
    [[nodiscard]] Options& options() noexcept { return options_; }
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    /// Whether the Options page waits for a key to bind.
    [[nodiscard]] bool waiting_for_key() const noexcept { return !waiting_.empty(); }
    /// The key pressed while waiting: the action waited for gets it.
    Result capture(input::Binding binding);

    /// Draws the page over `surface`. `title` is the game's name.
    void draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
              const render::Texture& white, const render::Texture& atlas, const ui::PixelFont& font,
              const std::string& title, const SaveObject& saves, const InventoryObject& inventory,
              const QuestObject& quests, const PartyObject& party) const;

private:
    Page page_ = Page::None;
    bool from_title_ = false; ///< Loading from the title screen, not the pause menu.
    std::size_t cursor_ = 0;
    std::string note_;
    Options options_;
    std::string waiting_; ///< The action waiting for a key.

    // The Equip page.
    [[nodiscard]] std::vector<std::string> equip_choices() const;
    /// What the chosen slot can take: "" (nothing) and the items carried for it.
    [[nodiscard]] std::vector<std::string> equip_candidates() const;
    [[nodiscard]] BattleData equip_data() const;
    [[nodiscard]] std::string member_name(const std::string& id) const;
    EquipStep equip_step_ = EquipStep::Member;
    std::size_t equip_member_ = 0;
    std::size_t equip_slot_ = 0;
    /// `words` translated, if there are translations.
    [[nodiscard]] std::string tr(std::string_view words) const;
    const Translations* translations_ = nullptr;
    PartyObject* party_ = nullptr;
    const TablesObject* tables_ = nullptr;
    const InventoryObject* inventory_ = nullptr;
};

} // namespace engine::game
