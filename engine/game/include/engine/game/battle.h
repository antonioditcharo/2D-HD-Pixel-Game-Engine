#pragma once

// Turn-based battles (plan/02 F2), as classic RPGs fight them: the party against a few
// enemies; each round every party member picks a command (attack, a skill, defend, an
// item, flee), the enemies pick theirs, and everyone acts in order of speed. Fighters'
// numbers come from data tables (tables.h): the party's, the enemies', the skills' and the
// items'. The rules are here, with no drawing; the runtime shows them.
//
// See docs/dev/battle.md.

#include "engine/game/tables.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
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
    std::int64_t xp = 0;             ///< An enemy's, given when it is beaten.
    std::int64_t gold = 0;
    bool defending = false;

    [[nodiscard]] bool alive() const noexcept { return hp > 0; }
};

struct BattleSkill {
    std::string id;
    std::string name;
    std::int64_t mp_cost = 0;
    std::int64_t power = 10;
    bool heals = false; ///< Heals an ally, rather than hurting an enemy.
    bool all = false;   ///< Every enemy (or ally) at once.
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
};

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
    /// Whether `name` is a row of the items table: something usable in battle.
    [[nodiscard]] bool is_item(std::string_view name) const noexcept {
        return data_.items != nullptr && data_.items->row(name) != nullptr;
    }
    /// The damage `attacker` does to `target` with `power`, before the random spread: power
    /// plus attack times two, minus defence, at least 1; halved if the target defends.
    [[nodiscard]] static std::int64_t base_damage(const Fighter& attacker, const Fighter& target,
                                                  std::int64_t power);

private:
    void fight_round();
    void act(std::size_t who, const BattleCommand& command);
    [[nodiscard]] std::uint32_t next_random();
    void check_end();

    BattleData data_;
    std::vector<Fighter> fighters_;
    std::vector<BattleSkill> skills_;
    std::map<std::size_t, BattleCommand> chosen_;
    std::vector<std::string> messages_;
    std::map<std::string, std::int64_t, std::less<>> used_items_;
    std::map<std::string, std::int64_t, std::less<>> reserved_items_;
    BattleOutcome outcome_ = BattleOutcome::Fighting;
    std::uint32_t random_ = 1;
};

} // namespace engine::game
