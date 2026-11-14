#include "engine/game/battle_screen.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine::game {

const UiTheme& BattleScreen::theme() const noexcept {
    static const UiTheme standard;
    return theme_ != nullptr ? *theme_ : standard;
}

namespace {

constexpr std::size_t most_members = 6;
constexpr std::array<const char*, 5> commands{"Attack", "Skill", "Defend", "Item", "Flee"};

std::string trimmed(std::string_view text) {
    const std::size_t a = text.find_first_not_of(" \t");
    if (a == std::string_view::npos) {
        return {};
    }
    const std::size_t b = text.find_last_not_of(" \t");
    return std::string(text.substr(a, b - a + 1));
}

} // namespace

// ----- Party and battle objects -----

void PartyObject::add_member(const std::string& id) {
    const std::string key = trimmed(id);
    if (!key.empty() && !in_party(key) && members_.size() < most_members) {
        members_.push_back(key);
    }
}

void PartyObject::remove_member(const std::string& id) {
    const std::string key = trimmed(id);
    std::erase(members_, key);
    condition_.erase(key);
}

void PartyObject::heal_all() {
    condition_.clear();
}

bool PartyObject::in_party(const std::string& id) const {
    return std::find(members_.begin(), members_.end(), trimmed(id)) != members_.end();
}

std::int64_t PartyObject::hp_of(const std::string& id) const {
    const auto found = condition_.find(trimmed(id));
    return found == condition_.end() ? -1 : found->second.first;
}

void PartyObject::give_xp(const std::string& id, std::int64_t xp) {
    const std::string key = trimmed(id);
    if (key.empty() || xp <= 0) {
        return;
    }
    const std::int64_t before = level_of(key);
    std::int64_t& mine = member_xp_[key];
    mine = std::min<std::int64_t>(mine + xp, 999'999'999);
    if (const std::int64_t after = level_of(key); after > before) {
        level_ups_.push_back(LevelUp{key, before, after});
    }
}

std::int64_t PartyObject::xp_of(const std::string& id) const {
    const auto found = member_xp_.find(trimmed(id));
    return found == member_xp_.end() ? 0 : found->second;
}

std::int64_t PartyObject::level_of(const std::string& id) const {
    return level_for_xp(xp_of(id), tables_ != nullptr ? tables_->find("data/levels") : nullptr);
}

std::int64_t PartyObject::xp_to_next(const std::string& id) const {
    const Table* levels = tables_ != nullptr ? tables_->find("data/levels") : nullptr;
    const std::int64_t level = level_of(id);
    return level >= top_level(levels) ? 0 : xp_for_level(level + 1, levels) - xp_of(id);
}

std::map<std::string, std::int64_t, std::less<>> PartyObject::levels() const {
    std::map<std::string, std::int64_t, std::less<>> out;
    for (const auto& [id, xp] : member_xp_) {
        out[id] = level_of(id);
    }
    return out;
}

void PartyObject::equip(const std::string& id, const std::string& item) {
    const std::string member = trimmed(id);
    const std::string what = trimmed(item);
    const Table* items = tables_ != nullptr ? tables_->find("data/items") : nullptr;
    const std::string slot = slot_of(items, what);
    if (!in_party(member) || slot.empty() || inventory_ == nullptr || !inventory_->has(what)) {
        return;
    }
    inventory_->take(what, 1);
    std::string& worn = equipment_[member][slot];
    if (!worn.empty()) {
        inventory_->give(worn, 1);
    }
    worn = what;
}

void PartyObject::unequip(const std::string& id, const std::string& slot) {
    const auto member = equipment_.find(trimmed(id));
    if (member == equipment_.end()) {
        return;
    }
    const auto worn = member->second.find(trimmed(slot));
    if (worn == member->second.end()) {
        return;
    }
    if (inventory_ != nullptr && !worn->second.empty()) {
        inventory_->give(worn->second, 1);
    }
    member->second.erase(worn);
}

std::string PartyObject::equipped(const std::string& id, const std::string& slot) const {
    const auto member = equipment_.find(trimmed(id));
    if (member == equipment_.end()) {
        return {};
    }
    const auto worn = member->second.find(trimmed(slot));
    return worn == member->second.end() ? std::string() : worn->second;
}

std::vector<std::string> PartyObject::equipment_of(const std::string& id) const {
    std::vector<std::string> out;
    for (const std::string_view slot : equip_slots) {
        if (std::string worn = equipped(id, std::string(slot)); !worn.empty()) {
            out.push_back(std::move(worn));
        }
    }
    return out;
}

std::map<std::string, std::vector<std::string>, std::less<>> PartyObject::all_equipment() const {
    std::map<std::string, std::vector<std::string>, std::less<>> out;
    for (const auto& [member, worn] : equipment_) {
        out[member] = equipment_of(member);
    }
    return out;
}

void BattleObject::start(const std::string& enemies) {
    request_ = enemies;
}

std::optional<std::string> BattleObject::take_request() {
    return std::exchange(request_, std::nullopt);
}

void register_battle_types(reflect::Registry& registry) {
    auto party =
        registry.add_type<PartyObject>("party").display_name("Party").category("Story").tooltip(
            "Who fights alongside the player: rows of data/party.");
    party.action<&PartyObject::add_member>("add_member", {"name"})
        .text("{name} joins the party")
        .tooltip("Adds someone by their row of data/party (at most six).");
    party.action<&PartyObject::remove_member>("remove_member", {"name"})
        .text("{name} leaves the party")
        .tooltip("Takes someone out of the party.");
    party.action<&PartyObject::heal_all>("heal_all")
        .text("Heal the party")
        .tooltip("Everyone back to full HP and MP, as at an inn.");
    party.condition<&PartyObject::in_party>("in_party", {"name"})
        .text("{name} is in the party")
        .tooltip("Whether someone is in the party.");
    party.getter<&PartyObject::size>("size")
        .text("The number in the party")
        .tooltip("How many are in the party.");
    party.getter<&PartyObject::xp>("xp")
        .text("The party's experience")
        .tooltip("All the experience won in battles.");
    party.action<&PartyObject::give_xp>("give_xp", {"name", "xp"})
        .text("Give {name} {xp} experience")
        .tooltip("Experience for one member, as a battle gives it; enough reaches a new "
                 "level, with its stats and skills.")
        .param("xp")
        .range(1.0, 999999999.0)
        .default_value(reflect::Value{std::int64_t{10}});
    party.getter<&PartyObject::level_of>("level_of", {"name"})
        .text("The level of {name}")
        .tooltip("From their experience: data/levels, or 10, 30, 60, 100... for levels 2, "
                 "3, 4, 5...");
    party.getter<&PartyObject::xp_of>("xp_of", {"name"})
        .text("The experience of {name}")
        .tooltip("All the experience they have.");
    party.getter<&PartyObject::xp_to_next>("xp_to_next", {"name"})
        .text("The experience {name} needs for the next level")
        .tooltip("0 at the highest level.");
    party.action<&PartyObject::equip>("equip", {"name", "item"})
        .text("Equip {item} on {name}")
        .tooltip("Takes it from what the player carries (a row of data/items with a slot: "
                 "weapon, shield, head, body or accessory); what was worn there goes back.");
    party.action<&PartyObject::unequip>("unequip", {"name", "slot"})
        .text("Take off the {slot} of {name}")
        .tooltip("Back to what the player carries. Slots: weapon, shield, head, body, "
                 "accessory.");
    party.getter<&PartyObject::equipped>("equipped", {"name", "slot"})
        .text("The {slot} {name} wears")
        .tooltip("The item's name; empty for nothing.");
    party.event<std::string>("level_up", {"name"})
        .text("When {name} reaches a new level")
        .tooltip("After a battle won, or experience given.");
    auto battle =
        registry.add_type<BattleObject>("battle").display_name("Battle").category("Story").tooltip(
            "Turn-based battles, with numbers from the data tables.");
    battle.action<&BattleObject::start>("start", {"enemies"})
        .text("Start a battle against {enemies}")
        .tooltip("Enemies by their rows of data/enemies, separated by commas: slime, slime, "
                 "bat. The party fights them on the battle screen.");
    battle.action<&BattleObject::set_background>("set_background", {"picture"})
        .text("Show {picture} behind battles")
        .tooltip("A picture behind every battle from now on (a cave, a field), by its path in "
                 "assets/; empty for the world. Encounter Areas and Enemies on the Map can "
                 "choose their own.");
    battle.condition<&BattleObject::fighting>("fighting")
        .text("A battle is on")
        .tooltip("Whether the battle screen shows.");
    battle.event<>("won").text("When the party wins").tooltip("Every enemy is beaten.");
    battle.event<>("lost").text("When the party loses").tooltip("Every party member is down.");
    battle.event<>("fled").text("When the party gets away").tooltip("Flee worked.");
}

// ----- The screen -----

std::string BattleScreen::states_of(const Fighter& f) const {
    std::string out;
    for (const auto& [id, left] : f.states) {
        const StatusEffect* e = battle_.state(id);
        out += (out.empty() ? "" : " ") + (e != nullptr ? e->name : id);
    }
    return out;
}

BattleScreen::BattleScreen(Battle battle) : battle_(std::move(battle)) {
    pull_messages();
}

void BattleScreen::pull_messages() {
    for (std::string& line : battle_.take_messages()) {
        messages_.push_back(std::move(line));
    }
}

bool BattleScreen::finished() const noexcept {
    return messages_.empty() && battle_.outcome() != BattleOutcome::Fighting;
}

const std::string* BattleScreen::message() const noexcept {
    if (!problem_.empty()) {
        return &problem_;
    }
    return messages_.empty() ? nullptr : &messages_.front();
}

std::vector<std::size_t> BattleScreen::targets() const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < battle_.fighters().size(); ++i) {
        const Fighter& f = battle_.fighters()[i];
        if (f.enemy != target_party_ && f.alive()) {
            out.push_back(i);
        }
    }
    return out;
}

std::optional<std::size_t> BattleScreen::target() const {
    if (mode_ != Mode::Target) {
        return std::nullopt;
    }
    const std::vector<std::size_t> all = targets();
    return cursor_ < all.size() ? std::optional<std::size_t>(all[cursor_]) : std::nullopt;
}

std::vector<std::string> BattleScreen::menu(const InventoryObject& inventory) const {
    std::vector<std::string> out;
    switch (mode_) {
    case Mode::Command:
        out.assign(commands.begin(), commands.end());
        break;
    case Mode::Skill:
        for (const BattleSkill* s : battle_.usable_skills()) {
            out.push_back(s->name + "  " + std::to_string(s->mp_cost) + " MP");
        }
        break;
    case Mode::Item:
        if (battle_.fighters().empty()) {
            break;
        }
        for (const auto& [item, count] : inventory.items()) {
            if (battle_.is_item(item)) {
                out.push_back(item + "  x" + std::to_string(count));
            }
        }
        break;
    case Mode::Target:
        for (const std::size_t i : targets()) {
            out.push_back(battle_.fighters()[i].name);
        }
        break;
    }
    return out;
}

void BattleScreen::press(std::string_view action, const InventoryObject& inventory) {
    // Reading: Interact (or Cancel) goes on.
    if (!problem_.empty()) {
        if (action == "interact" || action == "cancel") {
            problem_.clear();
        }
        return;
    }
    if (!messages_.empty()) {
        if (action == "interact" || action == "cancel") {
            messages_.erase(messages_.begin());
        }
        return;
    }
    if (battle_.outcome() != BattleOutcome::Fighting) {
        return;
    }
    const std::size_t count = menu(inventory).size();
    if (action == "move_up" && count > 0) {
        cursor_ = (cursor_ + count - 1) % count;
        return;
    }
    if (action == "move_down" && count > 0) {
        cursor_ = (cursor_ + 1) % count;
        return;
    }
    if (action == "cancel") {
        if (mode_ != Mode::Command) {
            mode_ = Mode::Command;
        } else {
            static_cast<void>(battle_.undo_choice()); // back to the member before
        }
        cursor_ = 0;
        return;
    }
    if (action != "interact" || count == 0) {
        return;
    }
    const auto commit = [&](const BattleCommand& command) {
        std::string error;
        if (!battle_.choose(command, inventory.items(), error)) {
            problem_ = error;
        }
        mode_ = Mode::Command;
        cursor_ = 0;
        pull_messages();
    };
    switch (mode_) {
    case Mode::Command:
        switch (cursor_) {
        case 0:
            pending_ = BattleCommand{BattleCommand::Kind::Attack, {}, 0};
            target_party_ = false;
            mode_ = Mode::Target;
            cursor_ = 0;
            break;
        case 1:
            mode_ = Mode::Skill;
            cursor_ = 0;
            break;
        case 2:
            commit(BattleCommand{BattleCommand::Kind::Defend, {}, 0});
            break;
        case 3:
            mode_ = Mode::Item;
            cursor_ = 0;
            break;
        default:
            commit(BattleCommand{BattleCommand::Kind::Flee, {}, 0});
            break;
        }
        break;
    case Mode::Skill: {
        const std::vector<const BattleSkill*> skills = battle_.usable_skills();
        const BattleSkill* s = skills.at(cursor_);
        pending_ = BattleCommand{BattleCommand::Kind::Skill, s->id, 0};
        if (s->all) {
            commit(pending_);
        } else {
            target_party_ = s->heals;
            mode_ = Mode::Target;
            cursor_ = 0;
        }
        break;
    }
    case Mode::Item: {
        std::vector<std::string> usable;
        for (const auto& [item, held] : inventory.items()) {
            static_cast<void>(held);
            if (battle_.is_item(item)) {
                usable.push_back(item);
            }
        }
        pending_ = BattleCommand{BattleCommand::Kind::Item, usable.at(cursor_), 0};
        target_party_ = true;
        mode_ = Mode::Target;
        cursor_ = 0;
        break;
    }
    case Mode::Target: {
        const std::optional<std::size_t> chosen = target();
        if (chosen) {
            pending_.target = *chosen;
            commit(pending_);
        }
        break;
    }
    }
}

void BattleScreen::draw(
    render::SpriteRenderer& sprites, render::RenderTarget& surface, const render::Texture& white,
    const render::Texture& atlas, const ui::PixelFont& font, const InventoryObject& inventory,
    const std::function<const render::Texture*(const std::string&)>& picture) const {
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    const auto w = static_cast<float>(surface.width());
    const auto h = static_cast<float>(surface.height());
    const auto line = static_cast<float>(font.line_height());
    constexpr float pad = 4.0F;
    const UiTheme& ui = theme();
    const render::Color text = ui.text;
    const render::Color dim = ui.dim;
    const render::Color gold = ui.highlight;
    const auto box = [&](float x, float y, float bw, float bh) {
        draw_box(sprites, white, ui, frame_, render::Rect{x, y, bw, bh});
    };
    const auto bar = [&](float x, float y, float bw, std::int64_t now, std::int64_t most,
                         render::Color colour) {
        sprites.draw(white, one, render::Rect{x, y, bw, 2}, render::Color{60, 60, 70, 255});
        const float filled =
            most > 0 ? bw * static_cast<float>(now) / static_cast<float>(most) : 0.0F;
        sprites.draw(white, one, render::Rect{x, y, filled, 2}, colour);
    };
    const auto write = [&](const std::string& words, float x, float y, render::Color colour) {
        ui::draw_text(sprites, atlas, font, words, x, y, colour);
    };
    sprites.begin_over(surface);
    // The world dims behind the battle; a battle background only a little.
    sprites.draw(white, one, render::Rect{0, 0, w, h},
                 render::Color{0, 0, 10, static_cast<std::uint8_t>(backdrop_ ? 50 : 150)});

    // The enemies, side by side across the top half, each with its HP.
    const std::vector<Fighter>& all = battle_.fighters();
    std::vector<std::size_t> enemies;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].enemy) {
            enemies.push_back(i);
        }
    }
    const std::optional<std::size_t> aimed = target();
    if (!enemies.empty()) {
        const float slot = w / static_cast<float>(enemies.size());
        for (std::size_t k = 0; k < enemies.size(); ++k) {
            const Fighter& f = all[enemies[k]];
            const float x = (slot * static_cast<float>(k)) + pad;
            float y = h * 0.18F;
            // Its picture, if it has one, standing above its name: whole pixels, scaled up
            // while it fits.
            const render::Texture* drawn =
                picture && !f.image.empty() && f.alive() ? picture(f.image) : nullptr;
            if (drawn != nullptr && drawn->width() > 0 && drawn->height() > 0) {
                const float room_w = slot - (2 * pad);
                const float room_h = h * 0.45F;
                float scale = 1.0F;
                while ((scale + 1.0F) * static_cast<float>(drawn->width()) <= room_w &&
                       (scale + 1.0F) * static_cast<float>(drawn->height()) <= room_h &&
                       scale < 4.0F) {
                    scale += 1.0F;
                }
                const float pw = static_cast<float>(drawn->width()) * scale;
                const float ph = static_cast<float>(drawn->height()) * scale;
                sprites.draw(*drawn,
                             render::Rect{0, 0, static_cast<float>(drawn->width()),
                                          static_cast<float>(drawn->height())},
                             render::Rect{std::floor(x + ((room_w - pw) / 2.0F)), 6.0F, pw, ph});
                y = std::max(y, 6.0F + ph + 2.0F);
            }
            const render::Color colour = !f.alive() ? dim : (aimed == enemies[k] ? gold : text);
            write((aimed == enemies[k] ? "> " : "") + f.name, x, y, colour);
            // Its states, under its HP bar, in purple.
            if (f.alive() && !f.states.empty()) {
                write(states_of(f), x, y + line + 4, render::Color{200, 150, 255, 255});
            }
            if (f.alive()) {
                bar(x, y + line + 1, std::min(slot - (2 * pad), 48.0F), f.hp, f.max_hp,
                    render::Color{220, 70, 70, 255});
            }
        }
    }

    // The party's status, bottom left; the command menu (or a list), bottom right.
    std::vector<std::size_t> party;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!all[i].enemy) {
            party.push_back(i);
        }
    }
    const float status_height =
        (line * static_cast<float>(std::max<std::size_t>(party.size(), 1))) + (2 * pad) + 4;
    const float menu_width = std::min(w * 0.38F, 110.0F);
    const float top = h - status_height;
    box(0, top, w - menu_width, status_height);
    const std::optional<std::size_t> choosing = battle_.chooser();
    for (std::size_t k = 0; k < party.size(); ++k) {
        const Fighter& f = all[party[k]];
        const float y = top + pad + (line * static_cast<float>(k));
        const render::Color colour =
            !f.alive() ? dim : (choosing == party[k] || aimed == party[k] ? gold : text);
        const std::string status = ((choosing == party[k] || aimed == party[k]) ? "> " : "") +
                                   f.name + "  HP " + std::to_string(f.hp) + "/" +
                                   std::to_string(f.max_hp) +
                                   (f.max_mp > 0 ? "  MP " + std::to_string(f.mp) : std::string());
        write(status, pad, y, colour);
        if (f.alive() && !f.states.empty()) {
            write(states_of(f), pad + static_cast<float>(ui::measure(font, status + "  ")), y,
                  render::Color{200, 150, 255, 255});
        }
    }
    const std::string* reading = message();
    if (reading != nullptr) {
        // What happened, in the menu's place, read with Interact.
        box(w - menu_width, top, menu_width, status_height);
        const std::vector<std::string> lines =
            ui::wrap(font, *reading, static_cast<int>(menu_width - (2 * pad)));
        for (std::size_t k = 0;
             k < lines.size() && static_cast<float>(k) * line < status_height - (2 * pad); ++k) {
            write(lines[k], w - menu_width + pad, top + pad + (line * static_cast<float>(k)), text);
        }
    } else if (battle_.outcome() == BattleOutcome::Fighting) {
        const std::vector<std::string> items = menu(inventory);
        const float menu_height = std::max(
            status_height,
            (line * static_cast<float>(std::max<std::size_t>(items.size(), 1))) + (2 * pad));
        box(w - menu_width, h - menu_height, menu_width, menu_height);
        for (std::size_t k = 0; k < items.size(); ++k) {
            write((k == cursor_ ? "> " : "  ") + items[k], w - menu_width + pad,
                  h - menu_height + pad + (line * static_cast<float>(k)),
                  k == cursor_ ? gold : text);
        }
        if (items.empty()) {
            write("(none)", w - menu_width + pad, h - menu_height + pad, dim);
        }
    }
    static_cast<void>(sprites.end());
}

} // namespace engine::game
