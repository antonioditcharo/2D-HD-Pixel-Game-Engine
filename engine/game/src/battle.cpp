#include "engine/game/battle.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

namespace engine::game {

namespace {

std::int64_t whole(const Table* table, const std::string& row, std::string_view column,
                   std::int64_t fallback) {
    if (table == nullptr) {
        return fallback;
    }
    const std::optional<double> v = table->number(row, column);
    return v ? static_cast<std::int64_t>(std::llround(*v)) : fallback;
}

std::string text(const Table* table, const std::string& row, std::string_view column,
                 const std::string& fallback) {
    if (table == nullptr) {
        return fallback;
    }
    const std::optional<std::string> v = table->text(row, column);
    return v && !v->empty() ? *v : fallback;
}

/// "fire, heal" or "fire|heal" as a list.
std::vector<std::string> list(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    for (const char c : text + ",") {
        if (c == ',' || c == '|') {
            const std::size_t a = item.find_first_not_of(' ');
            const std::size_t b = item.find_last_not_of(' ');
            if (a != std::string::npos) {
                out.push_back(item.substr(a, b - a + 1));
            }
            item.clear();
        } else {
            item += c;
        }
    }
    return out;
}

} // namespace

std::int64_t top_level(const Table* levels) {
    if (levels != nullptr && !levels->rows.empty()) {
        return std::min<std::int64_t>(static_cast<std::int64_t>(levels->rows.size()), 999);
    }
    return 99;
}

std::int64_t xp_for_level(std::int64_t level, const Table* levels) {
    level = std::clamp<std::int64_t>(level, 1, top_level(levels));
    if (level == 1) {
        return 0;
    }
    if (levels != nullptr && !levels->rows.empty()) {
        const std::string name = std::to_string(level);
        const std::string& row = levels->row(name) != nullptr
                                     ? name
                                     : levels->rows[static_cast<std::size_t>(level - 1)].id;
        return std::max<std::int64_t>(0, whole(levels, row, "xp", 5 * level * (level - 1)));
    }
    return 5 * level * (level - 1);
}

std::int64_t level_for_xp(std::int64_t xp, const Table* levels) {
    std::int64_t level = 1;
    const std::int64_t top = top_level(levels);
    while (level < top && xp >= xp_for_level(level + 1, levels)) {
        ++level;
    }
    return level;
}

std::vector<std::string> skills_at(const Table* party, const std::string& id, std::int64_t level) {
    std::vector<std::string> out;
    for (const std::string& entry : list(text(party, id, "skills", ""))) {
        const std::size_t at = entry.find('@');
        if (at == std::string::npos) {
            out.push_back(entry);
            continue;
        }
        std::int64_t from = 1;
        try {
            from = std::stoll(entry.substr(at + 1));
        } catch (const std::exception&) {
            from = 1;
        }
        std::string skill = entry.substr(0, at);
        while (!skill.empty() && skill.back() == ' ') {
            skill.pop_back();
        }
        if (level >= from && !skill.empty()) {
            out.push_back(std::move(skill));
        }
    }
    return out;
}

std::optional<Battle>
Battle::start(const BattleData& data, const std::vector<std::string>& party,
              const std::map<std::string, std::pair<std::int64_t, std::int64_t>, std::less<>>& now,
              const std::vector<std::string>& enemies, std::uint32_t seed, std::string& error) {
    if (data.party == nullptr || data.enemies == nullptr) {
        error = "A battle needs the tables data/party and data/enemies.";
        return std::nullopt;
    }
    if (party.empty() || enemies.empty()) {
        error = "A battle needs at least one party member and one enemy.";
        return std::nullopt;
    }
    Battle b;
    b.data_ = data;
    b.random_ = seed == 0 ? 1U : seed;
    const auto add = [&](const std::string& id, bool enemy) {
        const Table* table = enemy ? data.enemies : data.party;
        if (table->row(id) == nullptr) {
            error =
                std::string(enemy ? "data/enemies" : "data/party") + " has no row \"" + id + "\".";
            return false;
        }
        Fighter f;
        f.id = id;
        f.enemy = enemy;
        f.name = text(table, id, "name", id);
        f.image = text(table, id, "image", "");
        if (const auto level = data.member_levels.find(id);
            !enemy && level != data.member_levels.end()) {
            f.level = std::clamp<std::int64_t>(level->second, 1, top_level(data.levels));
        }
        // A party member grows by each "<stat>_growth" for every level past the first.
        const auto stat = [&](const char* column, std::int64_t fallback) {
            const std::int64_t base = whole(table, id, column, fallback);
            return enemy ? base
                         : base + ((f.level - 1) *
                                   whole(table, id, std::string(column) + "_growth", 0));
        };
        f.max_hp = std::max<std::int64_t>(1, stat("max_hp", 10));
        f.max_mp = std::max<std::int64_t>(0, stat("max_mp", 0));
        f.hp = f.max_hp;
        f.mp = f.max_mp;
        if (const auto known = now.find(id); !enemy && known != now.end()) {
            f.hp = std::clamp<std::int64_t>(known->second.first, 0, f.max_hp);
            f.mp = std::clamp<std::int64_t>(known->second.second, 0, f.max_mp);
        }
        f.attack = std::max<std::int64_t>(0, stat("attack", 5));
        f.defense = std::max<std::int64_t>(0, stat("defense", 0));
        f.speed = stat("speed", 5);
        f.skills = enemy ? list(text(table, id, "skills", "")) : skills_at(table, id, f.level);
        f.xp = enemy ? std::max<std::int64_t>(0, whole(table, id, "xp", 0)) : 0;
        f.gold = enemy ? std::max<std::int64_t>(0, whole(table, id, "gold", 0)) : 0;
        b.fighters_.push_back(std::move(f));
        return true;
    };
    for (const std::string& id : party) {
        if (!add(id, false)) {
            return std::nullopt;
        }
    }
    // Enemies of one kind are told apart: Slime A, Slime B.
    std::map<std::string, int> kinds;
    for (const std::string& id : enemies) {
        ++kinds[id];
    }
    std::map<std::string, int> counted;
    for (const std::string& id : enemies) {
        if (!add(id, true)) {
            return std::nullopt;
        }
        if (kinds[id] > 1) {
            b.fighters_.back().name += ' ';
            b.fighters_.back().name += static_cast<char>('A' + counted[id]++);
        }
    }
    if (data.skills != nullptr) {
        for (const TableRow& row : data.skills->rows) {
            BattleSkill s;
            s.id = row.id;
            s.name = text(data.skills, row.id, "name", row.id);
            s.mp_cost = std::max<std::int64_t>(0, whole(data.skills, row.id, "mp_cost", 0));
            s.power = whole(data.skills, row.id, "power", 10);
            s.heals = whole(data.skills, row.id, "heals", 0) != 0;
            s.all = whole(data.skills, row.id, "all", 0) != 0;
            b.skills_.push_back(std::move(s));
        }
    }
    if (std::none_of(b.fighters_.begin(), b.fighters_.end(),
                     [](const Fighter& f) { return !f.enemy && f.alive(); })) {
        error = "Every party member is down: there is no one to fight.";
        return std::nullopt;
    }
    std::string names;
    for (const Fighter& f : b.fighters_) {
        if (f.enemy) {
            names += (names.empty() ? "" : ", ") + f.name;
        }
    }
    b.messages_.push_back(names + (enemies.size() == 1 ? " appears!" : " appear!"));
    return b;
}

const BattleSkill* Battle::skill(std::string_view id) const noexcept {
    for (const BattleSkill& s : skills_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

std::optional<std::size_t> Battle::chooser() const noexcept {
    if (outcome_ != BattleOutcome::Fighting) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < fighters_.size(); ++i) {
        if (!fighters_[i].enemy && fighters_[i].alive() && !chosen_.contains(i)) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<const BattleSkill*> Battle::usable_skills() const {
    std::vector<const BattleSkill*> out;
    const std::optional<std::size_t> who = chooser();
    if (!who) {
        return out;
    }
    for (const std::string& id : fighters_[*who].skills) {
        const BattleSkill* s = skill(id);
        if (s != nullptr && s->mp_cost <= fighters_[*who].mp) {
            out.push_back(s);
        }
    }
    return out;
}

bool Battle::choose(const BattleCommand& command,
                    const std::map<std::string, std::int64_t, std::less<>>& items,
                    std::string& error) {
    const std::optional<std::size_t> who = chooser();
    if (!who) {
        error = "No one is choosing now.";
        return false;
    }
    const Fighter& me = fighters_[*who];
    const auto standing = [&](std::size_t target, bool enemy) {
        return target < fighters_.size() && fighters_[target].enemy == enemy &&
               fighters_[target].alive();
    };
    switch (command.kind) {
    case BattleCommand::Kind::Attack:
        if (!standing(command.target, true)) {
            error = "Choose an enemy still standing.";
            return false;
        }
        break;
    case BattleCommand::Kind::Skill: {
        const BattleSkill* s = skill(command.what);
        if (s == nullptr ||
            std::find(me.skills.begin(), me.skills.end(), command.what) == me.skills.end()) {
            error = me.name + " cannot use that.";
            return false;
        }
        if (s->mp_cost > me.mp) {
            error = me.name + " has too little MP for " + s->name + ".";
            return false;
        }
        const bool aimed_right =
            s->all || (s->heals ? standing(command.target, false) : standing(command.target, true));
        if (!aimed_right) {
            error = s->heals ? "Choose someone of the party still standing."
                             : "Choose an enemy still standing.";
            return false;
        }
        break;
    }
    case BattleCommand::Kind::Item: {
        const auto have = items.find(command.what);
        const auto reserved = reserved_items_.find(command.what);
        const std::int64_t left = (have == items.end() ? 0 : have->second) -
                                  (reserved == reserved_items_.end() ? 0 : reserved->second);
        if (left <= 0) {
            error = "The party has no " + command.what + " left.";
            return false;
        }
        if (command.target >= fighters_.size() || fighters_[command.target].enemy) {
            error = "Choose someone of the party.";
            return false;
        }
        ++reserved_items_[command.what];
        break;
    }
    case BattleCommand::Kind::Defend:
    case BattleCommand::Kind::Flee:
        break;
    }
    chosen_[*who] = command;
    if (!chooser()) {
        fight_round();
    }
    return true;
}

bool Battle::undo_choice() {
    if (chosen_.empty() || outcome_ != BattleOutcome::Fighting) {
        return false;
    }
    const auto last = std::prev(chosen_.end());
    if (last->second.kind == BattleCommand::Kind::Item) {
        if (--reserved_items_[last->second.what] <= 0) {
            reserved_items_.erase(last->second.what);
        }
    }
    chosen_.erase(last);
    return true;
}

std::uint32_t Battle::next_random() {
    random_ ^= random_ << 13U;
    random_ ^= random_ >> 17U;
    random_ ^= random_ << 5U;
    return random_;
}

std::int64_t Battle::base_damage(const Fighter& attacker, const Fighter& target,
                                 std::int64_t power) {
    std::int64_t damage = std::max<std::int64_t>(1, power + (attacker.attack * 2) - target.defense);
    if (target.defending) {
        damage = std::max<std::int64_t>(1, damage / 2);
    }
    return damage;
}

void Battle::fight_round() {
    // The enemies choose: each attacks someone of the party still standing.
    std::vector<std::size_t> party;
    for (std::size_t i = 0; i < fighters_.size(); ++i) {
        if (!fighters_[i].enemy && fighters_[i].alive()) {
            party.push_back(i);
        }
    }
    for (std::size_t i = 0; i < fighters_.size(); ++i) {
        if (fighters_[i].enemy && fighters_[i].alive() && !party.empty()) {
            chosen_[i] =
                BattleCommand{BattleCommand::Kind::Attack, {}, party[next_random() % party.size()]};
        }
    }
    // Defenders defend from the start of the round.
    for (const auto& [who, command] : chosen_) {
        fighters_[who].defending = command.kind == BattleCommand::Kind::Defend;
    }
    // Fastest first; on a tie, the party, then by place.
    std::vector<std::size_t> order;
    order.reserve(chosen_.size());
    for (const auto& [who, command] : chosen_) {
        order.push_back(who);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (fighters_[a].speed != fighters_[b].speed) {
            return fighters_[a].speed > fighters_[b].speed;
        }
        return !fighters_[a].enemy && fighters_[b].enemy;
    });
    for (const std::size_t who : order) {
        if (outcome_ != BattleOutcome::Fighting) {
            break;
        }
        if (fighters_[who].alive()) {
            act(who, chosen_[who]);
            check_end();
        }
    }
    for (Fighter& f : fighters_) {
        f.defending = false;
    }
    chosen_.clear();
    reserved_items_.clear();
}

void Battle::act(std::size_t who, const BattleCommand& command) {
    Fighter& me = fighters_[who];
    // A target that fell already this round: the next one of the same side still standing.
    const auto retarget = [&](std::size_t target) {
        if (target < fighters_.size() && fighters_[target].alive()) {
            return target;
        }
        const bool enemy = target < fighters_.size() ? fighters_[target].enemy : !me.enemy;
        for (std::size_t i = 0; i < fighters_.size(); ++i) {
            if (fighters_[i].enemy == enemy && fighters_[i].alive()) {
                return i;
            }
        }
        return target;
    };
    const auto hurt = [&](std::size_t target, std::int64_t power) {
        Fighter& t = fighters_[target];
        const std::int64_t base = base_damage(me, t, power);
        // A spread of a tenth either way, so fights are not all the same.
        const std::int64_t spread = std::max<std::int64_t>(1, base / 10);
        const std::int64_t damage = std::max<std::int64_t>(
            1, base - spread +
                   static_cast<std::int64_t>(next_random() %
                                             static_cast<std::uint32_t>(2 * spread + 1)));
        t.hp = std::max<std::int64_t>(0, t.hp - damage);
        messages_.push_back(t.name + " takes " + std::to_string(damage) + " damage.");
        if (!t.alive()) {
            messages_.push_back(t.name + (t.enemy ? " is beaten!" : " falls!"));
        }
    };
    const auto heal = [&](std::size_t target, std::int64_t amount) {
        Fighter& t = fighters_[target];
        const std::int64_t before = t.hp;
        t.hp = std::min(t.max_hp, t.hp + std::max<std::int64_t>(amount, 0));
        messages_.push_back(t.name + " recovers " + std::to_string(t.hp - before) + " HP.");
    };
    switch (command.kind) {
    case BattleCommand::Kind::Attack: {
        const std::size_t target = retarget(command.target);
        messages_.push_back(me.name + " attacks!");
        if (target < fighters_.size() && fighters_[target].alive()) {
            hurt(target, 0);
        }
        break;
    }
    case BattleCommand::Kind::Skill: {
        const BattleSkill* s = skill(command.what);
        if (s == nullptr || s->mp_cost > me.mp) {
            messages_.push_back(me.name + " cannot do that now.");
            break;
        }
        me.mp -= s->mp_cost;
        messages_.push_back(me.name + " uses " + s->name + "!");
        for (std::size_t i = 0; i < fighters_.size(); ++i) {
            const bool aimed = s->all ? (fighters_[i].enemy == (s->heals ? me.enemy : !me.enemy) &&
                                         fighters_[i].alive())
                                      : i == (s->heals ? command.target : retarget(command.target));
            if (!aimed) {
                continue;
            }
            if (s->heals) {
                if (fighters_[i].alive()) {
                    heal(i, s->power);
                }
            } else if (fighters_[i].alive()) {
                hurt(i, s->power);
            }
        }
        break;
    }
    case BattleCommand::Kind::Defend:
        messages_.push_back(me.name + " defends.");
        break;
    case BattleCommand::Kind::Item: {
        const std::string name = text(data_.items, command.what, "name", command.what);
        messages_.push_back(me.name + " uses " + name + ".");
        ++used_items_[command.what];
        if (command.target < fighters_.size() && fighters_[command.target].alive()) {
            heal(command.target, whole(data_.items, command.what, "heal", 0));
        }
        break;
    }
    case BattleCommand::Kind::Flee:
        // Faster parties get away more often: half the time, more when faster.
        if (next_random() % 100U <
            50U + static_cast<std::uint32_t>(std::clamp<std::int64_t>(me.speed * 2, 0, 45))) {
            messages_.push_back("The party gets away!");
            outcome_ = BattleOutcome::Fled;
        } else {
            messages_.push_back(me.name + " tries to run, but cannot get away!");
        }
        break;
    }
}

void Battle::check_end() {
    if (outcome_ != BattleOutcome::Fighting) {
        return;
    }
    const bool party_up = std::any_of(fighters_.begin(), fighters_.end(),
                                      [](const Fighter& f) { return !f.enemy && f.alive(); });
    const bool enemies_up = std::any_of(fighters_.begin(), fighters_.end(),
                                        [](const Fighter& f) { return f.enemy && f.alive(); });
    if (!enemies_up) {
        outcome_ = BattleOutcome::Won;
        messages_.push_back("Victory! " + std::to_string(xp()) + " experience and " +
                            std::to_string(gold()) + " gold.");
    } else if (!party_up) {
        outcome_ = BattleOutcome::Lost;
        messages_.push_back("The party has fallen...");
    }
}

std::vector<std::string> Battle::take_messages() {
    return std::exchange(messages_, {});
}

std::map<std::string, std::int64_t, std::less<>> Battle::take_used_items() {
    return std::exchange(used_items_, {});
}

std::int64_t Battle::xp() const noexcept {
    std::int64_t total = 0;
    for (const Fighter& f : fighters_) {
        total += f.enemy && !f.alive() ? f.xp : 0;
    }
    return total;
}

std::int64_t Battle::gold() const noexcept {
    std::int64_t total = 0;
    for (const Fighter& f : fighters_) {
        total += f.enemy && !f.alive() ? f.gold : 0;
    }
    return total;
}

} // namespace engine::game
