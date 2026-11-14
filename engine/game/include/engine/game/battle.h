#pragma once

// Turn-based battles (plan/02 F2), as classic RPGs fight them: the party against a few
// enemies; each round every party member picks a command (attack, a skill, defend, an
// item, flee), the enemies pick theirs, and everyone acts in order of speed. Fighters'
// numbers come from data tables (tables.h): the party's, the enemies', the skills' and the
// items'. The rules are here, with no drawing; the runtime shows them.
//
// See docs/dev/battle.md.

#include "engine/game/tables.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::game {

struct Fighter {
    std::string id;    ///< Its row in the party's or the enemies' table.
    std::string name;  ///< As shown.
    std::string image; ///< A picture of it, by path in assets/ (the table's "image").
    bool enemy = false;
    std::int64_t hp = 1;
    std::int64_t max_hp = 1;
    std::int64_t mp = 0;
    std::int64_t max_mp = 0;
    std::int64_t attack = 1;
    std::int64_t defense = 0;
    std::int64_t speed = 1;
    std::vector<std::string> skills; ///< Rows of the skills table it can use.
    std::int64_t level = 1;          ///< A party member's; enemies stay at 1.
    /// Status effects on it now (rows of data/states), with the rounds each has left.
    std::vector<std::pair<std::string, std::int64_t>> states;
    std::vector<std::string> resists; ///< States it cannot get (from what it wears).
    std::string inflicts;             ///< An enemy's: a state its attacks may give...
    std::int64_t inflict_chance = 0;  ///< ...this often, in percent.
    [[nodiscard]] bool has_state(std::string_view state) const noexcept {
        return std::any_of(states.begin(), states.end(),
                           [state](const auto& s) { return s.first == state; });
    }
    std::int64_t xp = 0; ///< An enemy's, given when it is beaten.
    std::int64_t gold = 0;
    bool defending = false;

    [[nodiscard]] bool alive() const noexcept { return hp > 0; }
};

struct BattleSkill {
    std::string id;
    std::string name;
    std::int64_t mp_cost = 0;
    std::int64_t power = 10;
    bool heals = false;             ///< Heals an ally, rather than hurting an enemy.
    bool all = false;               ///< Every enemy (or ally) at once.
    std::string inflicts;           ///< A state it may give (rows of data/states)...
    std::int64_t chance = 100;      ///< ...this often, in percent.
    std::vector<std::string> cures; ///< States it ends ("all" for every one).
};

/// A status effect: poison, sleep, a blessing (a row of data/states).
struct StatusEffect {
    std::string id;
    std::string name = "Poisoned"; ///< As in "Slime is poisoned!".
    std::int64_t turns = 3;        ///< Rounds it lasts.
    std::int64_t hp_per_turn = 0;  ///< At each round's end: below 0 hurts, above heals.
    bool skip_turn = false;        ///< Asleep, frozen: it cannot act.
    bool ends_on_hit = false;      ///< Damage ends it (sleep).
    std::int64_t attack = 0;       ///< Added to the fighter's numbers while it lasts.
    std::int64_t defense = 0;
    std::int64_t speed = 0;
};

struct BattleCommand {
    enum class Kind : std::uint8_t { Attack, Skill, Defend, Item, Flee };
    Kind kind = Kind::Attack;
    std::string what;       ///< The skill or item.
    std::size_t target = 0; ///< Which fighter it is aimed at.
};

enum class BattleOutcome : std::uint8_t { Fighting, Won, Lost, Fled };

/// Where the battle's numbers come from.
struct BattleData {
    const Table* party = nullptr;   ///< name, max_hp, max_mp, attack, defense, speed, skills
    const Table* enemies = nullptr; ///< name, max_hp, attack, defense, speed, xp, gold, skills
    const Table* skills = nullptr;  ///< name, mp_cost, power, heals, all
    const Table* items = nullptr;   ///< name, heal
    const Table* levels = nullptr;  ///< xp: the experience each level needs (optional)
    const Table* states = nullptr;  ///< name, turns, hp_per_turn, skip_turn, ends_on_hit
    /// Party members' levels, by row; those missing are at level 1.
    std::map<std::string, std::int64_t, std::less<>> member_levels;
    /// What party members wear, by row: rows of `items` with a slot.
    std::map<std::string, std::vector<std::string>, std::less<>> member_equipment;
};

/// The places equipment is worn, as data/items' `slot` column names them.
inline constexpr std::array<std::string_view, 5> equip_slots{"weapon", "shield", "head", "body",
                                                             "accessory"};
/// The slot `item` of data/items (`items`) is worn in; empty if it is not equipment.
[[nodiscard]] std::string slot_of(const Table* items, std::string_view item);

/// A fighter from the tables, at full HP and MP: an enemy as its row says; a party member
/// at their level (member_levels), with their growth and what they wear (member_equipment).
/// Nothing, with `error`, for a row the table lacks.
[[nodiscard]] std::optional<Fighter> make_fighter(const BattleData& data, const std::string& id,
                                                  bool enemy, std::string& error);

/// The highest level: data/levels' rows, else 99.
[[nodiscard]] std::int64_t top_level(const Table* levels);
/// All the experience needed to reach `level` (0 for level 1): data/levels' `xp` on the row
/// named by the level ("3"), else on the level's row in order; without the table,
/// 5 x level x (level - 1): 10 for level 2, 30 for 3, 60 for 4.
[[nodiscard]] std::int64_t xp_for_level(std::int64_t level, const Table* levels);
/// The level `xp` experience reaches.
[[nodiscard]] std::int64_t level_for_xp(std::int64_t xp, const Table* levels);
/// The skills a party member of data/party (`party`) knows at `level`: its `skills` column,
/// where "fire@3" is learned at level 3.
[[nodiscard]] std::vector<std::string> skills_at(const Table* party, const std::string& id,
                                                 std::int64_t level);

class Battle {
public:
    /// The party members (rows of the party table, with their HP and MP now, if known) against
    /// the enemies (rows of the enemies table; the same row may come twice). Nothing, with
    /// `error`, for a member or enemy the tables lack, or none of either.
    [[nodiscard]] static std::optional<Battle>
    start(const BattleData& data, const std::vector<std::string>& party,
          const std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>>& now,
          const std::vector<std::string>& enemies, std::uint32_t seed, std::string& error);

    [[nodiscard]] const std::vector<Fighter>& fighters() const noexcept { return fighters_; }
    [[nodiscard]] BattleOutcome outcome() const noexcept { return outcome_; }
    /// The party member choosing a command now (an index into fighters()); nothing once the
    /// battle is over.
    [[nodiscard]] std::optional<std::size_t> chooser() const noexcept;
    /// The skills the chooser can use now (enough MP).
    [[nodiscard]] std::vector<const BattleSkill*> usable_skills() const;
    /// The chooser's command. Once every party member still standing has one, the round is
    /// fought. Refused, with `error`, for a skill unknown, without MP, an item the party lacks
    /// (`has_item`), or a target that is not standing on the right side.
    bool choose(const BattleCommand& command,
                const std::map<std::string, std::int64_t, std::less<>>& items, std::string& error);
    /// Takes back the last command chosen this round.
    bool undo_choice();
    /// What happened since the last call, line by line ("Slime takes 7 damage.").
    [[nodiscard]] std::vector<std::string> take_messages();
    /// Items used up this battle, by name, to take from the inventory.
    [[nodiscard]] std::map<std::string, std::int64_t, std::less<>> take_used_items();
    /// Experience and gold the beaten enemies give.
    [[nodiscard]] std::int64_t xp() const noexcept;
    [[nodiscard]] std::int64_t gold() const noexcept;
    [[nodiscard]] const BattleSkill* skill(std::string_view id) const noexcept;
    [[nodiscard]] const StatusEffect* state(std::string_view id) const noexcept;
    /// Whether a fighter cannot act now (asleep).
    [[nodiscard]] bool held(const Fighter& f) const noexcept;
    /// Whether `name` is a row of the items table, and not equipment: usable in battle.
    [[nodiscard]] bool is_item(std::string_view name) const {
        return data_.items != nullptr && data_.items->row(name) != nullptr &&
               slot_of(data_.items, name).empty();
    }
    /// The damage `attacker` does to `target` with `power`, before the random spread: power
    /// plus attack times two, minus defence, at least 1; halved if the target defends.
    [[nodiscard]] static std::int64_t base_damage(const Fighter& attacker, const Fighter& target,
                                                  std::int64_t power);

private:
    void fight_round();
    /// Fights rounds while no one of the party can choose (all asleep).
    void advance();
    void act(std::size_t who, const BattleCommand& command);
    /// Gives `target` a state (unless it resists, or the chance fails); renewed if it has it.
    void inflict(std::size_t target, const std::string& state, std::int64_t chance);
    /// Ends states on `target`: those named, or every one for "all".
    void cure(std::size_t target, const std::vector<std::string>& states);
    void end_state(Fighter& f, std::size_t index);
    /// The round's end: states hurt or heal, and run out.
    void tick_states();
    [[nodiscard]] std::uint32_t next_random();
    void check_end();

    BattleData data_;
    std::vector<Fighter> fighters_;
    std::vector<BattleSkill> skills_;
    std::vector<StatusEffect> states_;
    std::map<std::size_t, BattleCommand> chosen_;
    std::vector<std::string> messages_;
    std::map<std::string, std::int64_t, std::less<>> used_items_;
    std::map<std::string, std::int64_t, std::less<>> reserved_items_;
    BattleOutcome outcome_ = BattleOutcome::Fighting;
    std::uint32_t random_ = 1;
};

} // namespace engine::game
