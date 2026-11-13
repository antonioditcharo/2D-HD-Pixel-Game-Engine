#include "engine/game/menus.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::game {

namespace {

constexpr std::array<const char*, 3> title_choices{"New game", "Continue", "Quit"};
constexpr std::array<const char*, 9> pause_choices{"Resume",  "Items", "Quests", "Party", "Equip",
                                                   "Options", "Save",  "Load",   "Quit"};

/// Text speeds the Options page cycles through (-1: the game's own).
constexpr std::array<double, 5> text_speeds{-1.0, 0.0, 20.0, 40.0, 80.0};

std::string percent(double v) {
    return std::to_string(static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 100.0))) + "%";
}

/// "move_up" as players read it: "Move up".
std::string action_label(std::string label) {
    std::ranges::replace(label, '_', ' ');
    if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') {
        label[0] = static_cast<char>(label[0] - 'a' + 'A');
    }
    return label;
}

std::string speed_name(double speed) {
    if (speed < 0.0) {
        return "The game's own";
    }
    if (speed == 0.0) {
        return "Instant";
    }
    return speed <= 20.0 ? "Slow" : speed <= 40.0 ? "Normal" : "Fast";
}

} // namespace

std::vector<std::string> menu_words() {
    std::vector<std::string> out(title_choices.begin(), title_choices.end());
    out.insert(out.end(), pause_choices.begin(), pause_choices.end());
    return out;
}

void GameMenu::open_title() {
    page_ = Page::Title;
    from_title_ = true;
    cursor_ = 0;
    note_.clear();
}

void GameMenu::open_pause() {
    page_ = Page::Pause;
    from_title_ = false;
    cursor_ = 0;
    note_.clear();
}

std::vector<std::string> GameMenu::choices(const SaveObject& saves) const {
    std::vector<std::string> out;
    switch (page_) {
    case Page::Title:
        for (const char* choice : title_choices) {
            out.push_back(tr(choice));
        }
        break;
    case Page::Pause:
        for (const char* choice : pause_choices) {
            out.push_back(tr(choice));
        }
        break;
    case Page::Equip:
        out = equip_choices();
        break;
    case Page::Options:
        out.push_back("Music  " + percent(options_.music));
        out.push_back("Sounds  " + percent(options_.sounds));
        out.push_back("Text speed  " + speed_name(options_.text_speed));
        out.push_back("Language  " + (options_.language.empty() ? std::string("The game's own")
                                                                : options_.language));
        for (const auto& [action, keys] : options_.keys) {
            out.push_back(action_label(action) + "  " +
                          (waiting_ == action ? std::string("press a key...") : keys));
        }
        break;
    case Page::Save:
    case Page::Load:
        for (std::int64_t slot = 1; slot <= menu_slots; ++slot) {
            out.push_back("Slot " + std::to_string(slot) +
                          (saves.has_save(slot) ? std::string() : std::string("  (empty)")));
        }
        break;
    default:
        break;
    }
    return out;
}

GameMenu::Result GameMenu::capture(input::Binding binding) {
    if (waiting_.empty()) {
        return {};
    }
    Result result{Result::Kind::Rebind, 0, waiting_, binding};
    waiting_.clear();
    return result;
}

GameMenu::Result GameMenu::press(std::string_view action, const SaveObject& saves) {
    note_.clear();
    if (!waiting_.empty()) {
        return {}; // the key pressed is taken by capture()
    }
    // Options: Left and Right change the value the cursor is on.
    if (page_ == Page::Options && (action == "move_left" || action == "move_right") &&
        cursor_ < first_key_line) {
        const double step = action == "move_right" ? 0.1 : -0.1;
        if (cursor_ == 0) {
            options_.music =
                std::clamp(std::round((options_.music + step) * 10.0) / 10.0, 0.0, 1.0);
        } else if (cursor_ == 1) {
            options_.sounds =
                std::clamp(std::round((options_.sounds + step) * 10.0) / 10.0, 0.0, 1.0);
        } else if (cursor_ == 3) {
            // The game's own, then each language of data/translations, round.
            std::vector<std::string> languages{std::string()};
            if (translations_ != nullptr) {
                languages.insert(languages.end(), translations_->languages().begin(),
                                 translations_->languages().end());
            }
            const auto now = std::find(languages.begin(), languages.end(), options_.language);
            std::size_t at =
                now == languages.end() ? 0 : static_cast<std::size_t>(now - languages.begin());
            at = (at + (action == "move_right" ? 1 : languages.size() - 1)) % languages.size();
            options_.language = languages[at];
        } else {
            std::size_t at = 0;
            for (std::size_t i = 0; i < text_speeds.size(); ++i) {
                if (text_speeds.at(i) == options_.text_speed) {
                    at = i;
                }
            }
            at = (at + (action == "move_right" ? 1 : text_speeds.size() - 1)) % text_speeds.size();
            options_.text_speed = text_speeds.at(at);
        }
        return {Result::Kind::Options, 0};
    }
    const std::size_t count = choices(saves).size();
    if (action == "move_up" && count > 0) {
        cursor_ = (cursor_ + count - 1) % count;
        return {};
    }
    if (action == "move_down" && count > 0) {
        cursor_ = (cursor_ + 1) % count;
        return {};
    }
    if ((action == "cancel" || action == "menu") && page_ == Page::Equip &&
        equip_step_ != EquipStep::Member) {
        if (equip_step_ == EquipStep::Item) {
            equip_step_ = EquipStep::Slot;
            cursor_ = equip_slot_;
        } else {
            equip_step_ = EquipStep::Member;
            cursor_ = equip_member_;
        }
        return {};
    }
    if (action == "cancel" || action == "menu") {
        switch (page_) {
        case Page::Pause:
            page_ = Page::None;
            return {Result::Kind::Resume, 0};
        case Page::Items:
        case Page::Quests:
        case Page::Party:
        case Page::Equip:
        case Page::Options:
        case Page::Save:
            cursor_ = static_cast<std::size_t>(page_) - static_cast<std::size_t>(Page::Pause);
            page_ = Page::Pause;
            return {};
        case Page::Load:
            // From the title, back to it; from the pause menu, back to that.
            cursor_ = from_title_ ? 1 : 7;
            page_ = from_title_ ? Page::Title : Page::Pause;
            return {};
        default:
            return {};
        }
    }
    if (action != "interact") {
        return {};
    }
    switch (page_) {
    case Page::Title:
        if (cursor_ == 0) {
            page_ = Page::None;
            return {Result::Kind::NewGame, 0};
        }
        if (cursor_ == 1) {
            page_ = Page::Load;
            cursor_ = 0;
            return {};
        }
        return {Result::Kind::Quit, 0};
    case Page::Pause:
        switch (cursor_) {
        case 0:
            page_ = Page::None;
            return {Result::Kind::Resume, 0};
        case 1:
            page_ = Page::Items;
            return {};
        case 2:
            page_ = Page::Quests;
            return {};
        case 3:
            page_ = Page::Party;
            return {};
        case 4:
            page_ = Page::Equip;
            equip_step_ = EquipStep::Member;
            equip_member_ = 0;
            cursor_ = 0;
            return {};
        case 5:
            page_ = Page::Options;
            cursor_ = 0;
            return {};
        case 6:
            page_ = Page::Save;
            cursor_ = 0;
            return {};
        case 7:
            page_ = Page::Load;
            cursor_ = 0;
            return {};
        default:
            return {Result::Kind::Quit, 0};
        }
    case Page::Equip: {
        if (party_ == nullptr || party_->members().empty()) {
            return {};
        }
        if (equip_step_ == EquipStep::Member) {
            equip_member_ = std::min(cursor_, party_->members().size() - 1);
            equip_step_ = EquipStep::Slot;
            cursor_ = 0;
        } else if (equip_step_ == EquipStep::Slot) {
            equip_slot_ = std::min(cursor_, equip_slots.size() - 1);
            equip_step_ = EquipStep::Item;
            cursor_ = 0;
        } else {
            const std::vector<std::string> candidates = equip_candidates();
            const std::string& member = party_->members().at(equip_member_);
            const std::string slot(equip_slots.at(equip_slot_));
            if (cursor_ < candidates.size()) {
                if (candidates[cursor_].empty()) {
                    party_->unequip(member, slot);
                } else {
                    party_->equip(member, candidates[cursor_]);
                }
            }
            equip_step_ = EquipStep::Slot;
            cursor_ = equip_slot_;
        }
        return {};
    }
    case Page::Options:
        // On a key: wait for the next key pressed, to bind to it.
        if (cursor_ >= first_key_line && cursor_ - first_key_line < options_.keys.size()) {
            waiting_ = options_.keys[cursor_ - first_key_line].first;
        }
        return {};
    case Page::Save:
        return {Result::Kind::Save, static_cast<std::int64_t>(cursor_) + 1};
    case Page::Load: {
        const auto slot = static_cast<std::int64_t>(cursor_) + 1;
        if (!saves.has_save(slot)) {
            note_ = "Slot " + std::to_string(slot) + " is empty.";
            return {};
        }
        page_ = Page::None;
        return {Result::Kind::Load, slot};
    }
    default:
        return {};
    }
}

std::string GameMenu::tr(std::string_view words) const {
    return translations_ != nullptr ? translations_->translate(words) : std::string(words);
}

BattleData GameMenu::equip_data() const {
    BattleData data;
    if (tables_ != nullptr) {
        data.party = tables_->find("data/party");
        data.items = tables_->find("data/items");
        data.levels = tables_->find("data/levels");
    }
    if (party_ != nullptr) {
        data.member_levels = party_->levels();
        data.member_equipment = party_->all_equipment();
    }
    return data;
}

std::string GameMenu::member_name(const std::string& id) const {
    const Table* table = tables_ != nullptr ? tables_->find("data/party") : nullptr;
    const std::optional<std::string> name =
        table != nullptr ? table->text(id, "name") : std::nullopt;
    return name && !name->empty() ? *name : id;
}

std::vector<std::string> GameMenu::equip_candidates() const {
    std::vector<std::string> out{std::string()};
    const Table* items = tables_ != nullptr ? tables_->find("data/items") : nullptr;
    if (inventory_ == nullptr || equip_slot_ >= equip_slots.size()) {
        return out;
    }
    for (const auto& [item, count] : inventory_->items()) {
        if (count > 0 && slot_of(items, item) == equip_slots.at(equip_slot_)) {
            out.push_back(item);
        }
    }
    return out;
}

std::vector<std::string> GameMenu::equip_choices() const {
    std::vector<std::string> out;
    if (party_ == nullptr) {
        return out;
    }
    const Table* items = tables_ != nullptr ? tables_->find("data/items") : nullptr;
    const auto item_name = [items](const std::string& item) {
        const std::optional<std::string> name =
            items != nullptr ? items->text(item, "name") : std::nullopt;
        return name && !name->empty() ? *name : item;
    };
    switch (equip_step_) {
    case EquipStep::Member:
        for (const std::string& member : party_->members()) {
            out.push_back(member_name(member) + "  Lv " + std::to_string(party_->level_of(member)));
        }
        break;
    case EquipStep::Slot: {
        const std::string& member = party_->members().at(equip_member_);
        for (const std::string_view slot : equip_slots) {
            const std::string worn = party_->equipped(member, std::string(slot));
            std::string label(slot);
            label[0] = static_cast<char>(label[0] - 'a' + 'A');
            out.push_back(label + "  " + (worn.empty() ? std::string("-") : item_name(worn)));
        }
        break;
    }
    case EquipStep::Item:
        for (const std::string& item : equip_candidates()) {
            out.push_back(item.empty() ? std::string("(nothing)")
                                       : item_name(item) + "  x" +
                                             std::to_string(inventory_->count_of(item)));
        }
        break;
    }
    return out;
}

void GameMenu::draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
                    const render::Texture& white, const render::Texture& atlas,
                    const ui::PixelFont& font, const std::string& title, const SaveObject& saves,
                    const InventoryObject& inventory, const QuestObject& quests,
                    const PartyObject& party) const {
    if (page_ == Page::None) {
        return;
    }
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    const auto w = static_cast<float>(surface.width());
    const auto h = static_cast<float>(surface.height());
    const auto line = static_cast<float>(font.line_height());
    constexpr float pad = 5.0F;
    const render::Color text{255, 255, 255, 255};
    const render::Color dim{150, 155, 170, 255};
    const render::Color gold{255, 210, 122, 255};
    const auto box = [&](float x, float y, float bw, float bh) {
        sprites.draw(white, one, render::Rect{x, y, bw, bh}, render::Color{230, 232, 240, 255});
        sprites.draw(white, one, render::Rect{x + 1, y + 1, bw - 2, bh - 2},
                     render::Color{18, 20, 40, 240});
    };
    const auto write = [&](const std::string& words, float x, float y, render::Color colour) {
        ui::draw_text(sprites, atlas, font, words, x, y, colour);
    };
    const auto list = [&](const std::vector<std::string>& items, float x, float y, float bw) {
        const float bh =
            (line * static_cast<float>(std::max<std::size_t>(items.size(), 1))) + (2 * pad);
        box(x, y, bw, bh);
        for (std::size_t k = 0; k < items.size(); ++k) {
            write((k == cursor_ ? "> " : "  ") + items[k], x + pad,
                  y + pad + (line * static_cast<float>(k)), k == cursor_ ? gold : text);
        }
        return bh;
    };
    sprites.begin_over(surface);
    sprites.draw(
        white, one, render::Rect{0, 0, w, h},
        render::Color{0, 0, 10, static_cast<std::uint8_t>(page_ == Page::Title ? 120 : 150)});
    const std::vector<std::string> items = choices(saves);
    if (page_ == Page::Title || (page_ == Page::Load && from_title_)) {
        // The game's name, in the upper third, and the choices below it, centred.
        // Twice the size while it fits, as titles are.
        const int big = static_cast<float>(ui::measure(font, title) * 2) <= w - 8.0F ? 2 : 1;
        const float tw = static_cast<float>(ui::measure(font, title) * big);
        ui::draw_text(sprites, atlas, font, title, std::floor((w - tw) / 2.0F),
                      std::floor(h * 0.25F), gold, big);
        const float bw = 80.0F;
        list(items, std::floor((w - bw) / 2.0F), std::floor(h * 0.5F), bw);
    } else {
        // The pause menu down the left; the page chosen beside it.
        const float menu_width = std::min(70.0F, w * 0.3F);
        const bool on_pause = page_ == Page::Pause;
        if (on_pause) {
            list(items, pad, pad, menu_width);
        } else {
            std::vector<std::string> pause(pause_choices.begin(), pause_choices.end());
            const float bh = (line * static_cast<float>(pause.size())) + (2 * pad);
            box(pad, pad, menu_width, bh);
            const std::size_t current =
                static_cast<std::size_t>(page_) - static_cast<std::size_t>(Page::Pause);
            for (std::size_t k = 0; k < pause.size(); ++k) {
                write("  " + tr(pause[k]), pad + pad, pad + pad + (line * static_cast<float>(k)),
                      k == current ? gold : dim);
            }
        }
        const float x = menu_width + (2 * pad);
        const float bw = w - x - pad;
        std::vector<std::string> lines;
        switch (page_) {
        case Page::Items:
            for (const auto& [item, count] : inventory.items()) {
                lines.push_back(item + "  x" + std::to_string(count));
            }
            if (lines.empty()) {
                lines.emplace_back("Nothing carried.");
            }
            break;
        case Page::Quests: {
            const std::string journal = quests.journal();
            std::size_t start = 0;
            while (start <= journal.size()) {
                const std::size_t end = std::min(journal.find('\n', start), journal.size());
                for (std::string& wrapped : ui::wrap(font, journal.substr(start, end - start),
                                                     static_cast<int>(bw - (2 * pad)))) {
                    lines.push_back(std::move(wrapped));
                }
                start = end + 1;
            }
            break;
        }
        case Page::Party:
            for (const std::string& member : party.members()) {
                const std::int64_t hp = party.hp_of(member);
                const std::int64_t next = party.xp_to_next(member);
                lines.push_back(member + "  Lv " + std::to_string(party.level_of(member)) +
                                (hp < 0 ? std::string("  HP full") : "  HP " + std::to_string(hp)) +
                                (next > 0 ? "  next " + std::to_string(next) : std::string()));
            }
            if (lines.empty()) {
                lines.emplace_back("No one has joined the party.");
            }
            lines.push_back("Experience: " + std::to_string(party.xp()));
            break;
        default:
            break;
        }
        if (page_ == Page::Equip) {
            const std::vector<std::string> empty{"No one has joined the party."};
            const float bh = list(items.empty() ? empty : items, x, pad, bw);
            // The member's numbers now, and with the piece the cursor is on.
            if (party_ != nullptr && equip_step_ != EquipStep::Member &&
                equip_member_ < party_->members().size()) {
                const std::string& member = party_->members().at(equip_member_);
                BattleData data = equip_data();
                std::string error;
                const std::optional<Fighter> now = make_fighter(data, member, false, error);
                std::optional<Fighter> then;
                if (equip_step_ == EquipStep::Item) {
                    const std::vector<std::string> candidates = equip_candidates();
                    const std::string slot(equip_slots.at(equip_slot_));
                    std::vector<std::string> worn;
                    for (const std::string_view other : equip_slots) {
                        const std::string piece =
                            other == slot ? (cursor_ < candidates.size() ? candidates[cursor_]
                                                                         : std::string())
                                          : party_->equipped(member, std::string(other));
                        if (!piece.empty()) {
                            worn.push_back(piece);
                        }
                    }
                    data.member_equipment[member] = worn;
                    then = make_fighter(data, member, false, error);
                }
                if (now) {
                    const std::array<std::pair<const char*, std::int64_t Fighter::*>, 5> numbers{
                        {{"Max HP", &Fighter::max_hp},
                         {"Max MP", &Fighter::max_mp},
                         {"Attack", &Fighter::attack},
                         {"Defense", &Fighter::defense},
                         {"Speed", &Fighter::speed}}};
                    const float ny = pad + bh + pad;
                    box(x, ny, bw, (line * 5.0F) + (2 * pad));
                    for (std::size_t k = 0; k < numbers.size(); ++k) {
                        const auto& [label, field] = numbers.at(k);
                        const std::int64_t before = (*now).*field;
                        std::string words = std::string(label) + "  " + std::to_string(before);
                        render::Color colour = text;
                        if (then) {
                            const std::int64_t after = (*then).*field;
                            words += " > " + std::to_string(after);
                            colour = after > before ? gold : after < before ? dim : text;
                        }
                        write(words, x + pad, ny + pad + (line * static_cast<float>(k)), colour);
                    }
                }
            }
        } else if (page_ == Page::Options) {
            const float bh = list(items, x, pad, bw);
            write(waiting_.empty() ? "Left/Right: change. Interact: new key."
                                   : "Press the new key.",
                  x + pad, pad + bh + 2, dim);
        } else if (page_ == Page::Save || page_ == Page::Load) {
            const float bh = list(items, x, pad, bw);
            write(page_ == Page::Save ? "Save in which slot?" : "Load which slot?", x + pad,
                  pad + bh + 2, dim);
        } else if (!lines.empty()) {
            const float bh = (line * static_cast<float>(lines.size())) + (2 * pad);
            box(x, pad, bw, std::min(bh, h - (2 * pad)));
            for (std::size_t k = 0;
                 k < lines.size() && pad + pad + (line * static_cast<float>(k + 1)) < h; ++k) {
                write(lines[k], x + pad, pad + pad + (line * static_cast<float>(k)), text);
            }
        }
    }
    if (!note_.empty()) {
        const float nw = static_cast<float>(ui::measure(font, note_)) + (2 * pad);
        box(std::floor((w - nw) / 2.0F), h - line - (3 * pad), nw, line + (2 * pad));
        write(note_, std::floor((w - nw) / 2.0F) + pad, h - line - (2 * pad), text);
    }
    static_cast<void>(sprites.end());
}

} // namespace engine::game
