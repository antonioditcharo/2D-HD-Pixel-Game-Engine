#pragma once

// Battles in the game (plan/02 F2): the "party" logic keeps (who is in it, their HP and MP
// between battles, experience earned), the "battle" logic starts, and the battle screen the
// player fights on, laid out as classic RPGs lay it out: the enemies above with their HP,
// the party's status below, a command menu (Attack, Skill, Defend, Item, Flee), lists of
// skills, items and targets, and what happens told line by line.
//
// See docs/dev/battle.md.

#include "engine/foundation/reflect.h"
#include "engine/game/battle.h"
#include "engine/game/inventory.h"
#include "engine/game/tables.h"
#include "engine/game/ui_theme.h"
#include "engine/render/render.h"
#include "engine/ui/text.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::game {

class PartyObject {
public:
    /// Adds a member by their row of data/party; nothing if they are in it, or six are.
    void add_member(const std::string& id);
    void remove_member(const std::string& id);
    /// Everyone back to full HP and MP (at the next battle, from the table's maxima).
    void heal_all();
    [[nodiscard]] bool in_party(const std::string& id) const;
    [[nodiscard]] std::int64_t size() const noexcept {
        return static_cast<std::int64_t>(members_.size());
    }
    [[nodiscard]] std::int64_t xp() const noexcept { return xp_; }
    /// A member's HP now; -1 for full (never hurt since healed).
    [[nodiscard]] std::int64_t hp_of(const std::string& id) const;

    [[nodiscard]] const std::vector<std::string>& members() const noexcept { return members_; }
    /// HP and MP by member, as battles left them; members missing are at full.
    [[nodiscard]] std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>>&
    condition() noexcept {
        return condition_;
    }
    [[nodiscard]] const std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>>&
    condition() const noexcept {
        return condition_;
    }
    void add_xp(std::int64_t xp) noexcept { xp_ += std::max<std::int64_t>(xp, 0); }
    void set_xp(std::int64_t xp) noexcept { xp_ = std::max<std::int64_t>(xp, 0); }

    // ----- Levels -----
    /// Where data/levels and data/items are read from (the runtime's tables).
    void set_tables(const TablesObject* tables) noexcept { tables_ = tables; }
    /// Where equipment comes from and goes back to (the runtime's inventory).
    void set_inventory(InventoryObject* inventory) noexcept { inventory_ = inventory; }

    // ----- Equipment -----
    /// Puts `item` (a row of data/items with a slot) on a member of the party, from the
    /// inventory; what they wore in that slot goes back to it. Nothing if the member is not
    /// in the party, the player does not carry it, or it is not equipment.
    void equip(const std::string& id, const std::string& item);
    /// Takes off what a member wears in `slot` ("weapon"), back to the inventory.
    void unequip(const std::string& id, const std::string& slot);
    /// What a member wears in `slot`; empty for nothing.
    [[nodiscard]] std::string equipped(const std::string& id, const std::string& slot) const;
    /// Everything a member wears.
    [[nodiscard]] std::vector<std::string> equipment_of(const std::string& id) const;
    /// What everyone wears, by member and slot, for saves.
    using Equipment =
        std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>>;
    [[nodiscard]] Equipment& equipment() noexcept { return equipment_; }
    [[nodiscard]] const Equipment& equipment() const noexcept { return equipment_; }
    /// Every member's equipment, for battles.
    [[nodiscard]] std::map<std::string, std::vector<std::string>, std::less<>>
    all_equipment() const;
    /// Gives a member experience; reaching a level is remembered for take_level_ups().
    void give_xp(const std::string& id, std::int64_t xp);
    /// A member's experience, and their level from it.
    [[nodiscard]] std::int64_t xp_of(const std::string& id) const;
    [[nodiscard]] std::int64_t level_of(const std::string& id) const;
    /// Experience a member still needs for their next level; 0 at the top.
    [[nodiscard]] std::int64_t xp_to_next(const std::string& id) const;
    /// Every member's level, for battles.
    [[nodiscard]] std::map<std::string, std::int64_t, std::less<>> levels() const;
    /// Experience by member, for saves.
    [[nodiscard]] std::map<std::string, std::int64_t, std::less<>>& member_xp() noexcept {
        return member_xp_;
    }
    [[nodiscard]] const std::map<std::string, std::int64_t, std::less<>>&
    member_xp() const noexcept {
        return member_xp_;
    }
    struct LevelUp {
        std::string id;
        std::int64_t from = 1;
        std::int64_t to = 1;
    };
    /// Levels reached since the last call, to announce.
    [[nodiscard]] std::vector<LevelUp> take_level_ups() { return std::exchange(level_ups_, {}); }

    void clear() noexcept {
        members_.clear();
        condition_.clear();
        member_xp_.clear();
        level_ups_.clear();
        equipment_.clear();
        xp_ = 0;
    }

private:
    std::vector<std::string> members_;
    std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>> condition_;
    std::int64_t xp_ = 0;
    std::map<std::string, std::int64_t, std::less<>> member_xp_;
    std::vector<LevelUp> level_ups_;
    Equipment equipment_;
    const TablesObject* tables_ = nullptr;
    InventoryObject* inventory_ = nullptr;
};

class BattleObject {
public:
    /// Starts a battle against enemies by their rows of data/enemies, separated by commas
    /// ("slime, slime, bat"). The runtime takes the request.
    void start(const std::string& enemies);
    [[nodiscard]] bool fighting() const noexcept { return fighting_; }
    /// The picture behind battles from now on (a path in assets/); empty for the world.
    void set_background(const std::string& picture) { background_ = picture; }
    [[nodiscard]] const std::string& background() const noexcept { return background_; }
    [[nodiscard]] std::optional<std::string> take_request();
    void set_fighting(bool on) noexcept { fighting_ = on; }

private:
    std::optional<std::string> request_;
    bool fighting_ = false;
    std::string background_;
};

/// Registers PartyObject as "party" and BattleObject as "battle".
void register_battle_types(reflect::Registry& registry);

/// The battle the player is fighting: its menus, and the messages still to read.
class BattleScreen {
public:
    explicit BattleScreen(Battle battle);
    /// The colours it is drawn with (the project's); null for the standard ones.
    void set_theme(const UiTheme* theme) noexcept { theme_ = theme; }
    /// The picture its boxes are drawn with (null, or one without a picture: the look's
    /// colours). Kept by the caller.
    void set_frame(const BoxFrame* frame) noexcept { frame_ = frame; }
    /// With a picture behind the battle, the screen dims it less than it dims the world.
    void set_backdrop(bool picture) noexcept { backdrop_ = picture; }

    /// A pressed action ("move_up", "move_down", "interact", "cancel"), with what the
    /// player carries for the item list.
    void press(std::string_view action, const InventoryObject& inventory);
    /// True once the battle is over and its last message read.
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] const Battle& battle() const noexcept { return battle_; }
    [[nodiscard]] Battle& battle() noexcept { return battle_; }
    /// The message being read, if any.
    [[nodiscard]] const std::string* message() const noexcept;
    /// What the menu offers now, and which is chosen.
    [[nodiscard]] std::vector<std::string> menu(const InventoryObject& inventory) const;
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    /// The fighter a target cursor points at, if choosing a target.
    [[nodiscard]] std::optional<std::size_t> target() const;

    /// Draws the screen over `surface` (`white`: one white pixel; `atlas` and `font`: text).
    void draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
              const render::Texture& white, const render::Texture& atlas, const ui::PixelFont& font,
              const InventoryObject& inventory,
              const std::function<const render::Texture*(const std::string&)>& picture = {}) const;

private:
    enum class Mode : std::uint8_t { Command, Skill, Item, Target };
    /// The names of a fighter's states: "Poisoned Asleep".
    [[nodiscard]] std::string states_of(const Fighter& f) const;
    void pull_messages();
    [[nodiscard]] std::vector<std::size_t> targets() const;

    Battle battle_;
    Mode mode_ = Mode::Command;
    std::size_t cursor_ = 0;
    BattleCommand pending_;
    bool target_party_ = false;
    std::vector<std::string> messages_;
    std::string problem_;
    bool backdrop_ = false;
    [[nodiscard]] const UiTheme& theme() const noexcept;
    const UiTheme* theme_ = nullptr;
    const BoxFrame* frame_ = nullptr;
};

} // namespace engine::game
