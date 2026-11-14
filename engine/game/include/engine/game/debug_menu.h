#pragma once

// The debug menu (plan/02 N4): while testing a game from the editor, F1 opens a menu that
// changes the game's state without code, to reach any part of it quickly: give items,
// set story flags, finish quests, go anywhere, heal the party. Exported games never have it.
//
// See docs/dev/player.md (Testing from the editor).

#include "engine/game/battle_screen.h"
#include "engine/game/dialogue.h"
#include "engine/game/inventory.h"
#include "engine/game/quests.h"
#include "engine/game/tables.h"
#include "engine/render/render.h"
#include "engine/ui/text.h"
#include "engine/world/world.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// What the debug menu changes; set once by the runtime.
struct DebugTargets {
    InventoryObject* inventory = nullptr;
    DialogueObject* dialogue = nullptr; ///< For the story's flags.
    QuestObject* quests = nullptr;
    PartyObject* party = nullptr;
    world::World* world = nullptr;
    const TablesObject* tables = nullptr; ///< data/items, for things to give.
};

class DebugMenu {
public:
    enum class Page : std::uint8_t { None, Main, Items, StoryFlags, Quests, GoTo };

    void attach(const DebugTargets& targets) noexcept { targets_ = targets; }
    void open();
    void close() noexcept { page_ = Page::None; }
    [[nodiscard]] bool is_open() const noexcept { return page_ != Page::None; }
    [[nodiscard]] Page page() const noexcept { return page_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    /// What the page showing lists.
    [[nodiscard]] std::vector<std::string> lines() const;
    /// What just happened ("Gave 1 potion."); empty for nothing.
    [[nodiscard]] const std::string& note() const noexcept { return note_; }

    /// A pressed action: Up and Down choose, Interact does, Left and Right take away and
    /// give items, Cancel goes back.
    void press(std::string_view action);

    void draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
              const render::Texture& white, const render::Texture& atlas,
              const ui::PixelFont& font) const;

private:
    [[nodiscard]] std::vector<std::string> item_ids() const;
    [[nodiscard]] std::vector<std::string> flag_names() const;
    [[nodiscard]] std::vector<std::string> quest_names() const;
    [[nodiscard]] std::vector<world::Entity> places() const;
    void go_to(world::Entity target);

    DebugTargets targets_;
    Page page_ = Page::None;
    std::size_t cursor_ = 0;
    std::string note_;
};

} // namespace engine::game
