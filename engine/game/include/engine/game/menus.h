#pragma once

// Ready-made menus (plan/02 F1, K3), as RPG Maker games come with them: a title screen
// (New game, Continue, Quit) and a pause menu on the Menu key (Resume, Items, Quests, Party,
// Save, Load, Quit), with pages for what the player carries, the journal, the party's
// status and the save slots. A project turns each on in its settings.
//
// See docs/dev/menus.md.

#include "engine/game/battle_screen.h"
#include "engine/game/inventory.h"
#include "engine/game/quests.h"
#include "engine/game/save.h"
#include "engine/render/render.h"
#include "engine/ui/text.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::game {

/// The save slots the menus offer.
inline constexpr std::int64_t menu_slots = 3;

class GameMenu {
public:
    enum class Page : std::uint8_t { None, Title, Pause, Items, Quests, Party, Save, Load };
    /// What a choice asks the game to do.
    struct Result {
        enum class Kind : std::uint8_t { None, NewGame, Load, Save, Quit, Resume };
        Kind kind = Kind::None;
        std::int64_t slot = 0;
    };

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
};

} // namespace engine::game
