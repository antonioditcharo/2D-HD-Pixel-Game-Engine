#include "engine/game/menus.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::game {

const UiTheme& GameMenu::theme() const noexcept {
    static const UiTheme standard;
    return theme_ != nullptr ? *theme_ : standard;
}

namespace {

constexpr std::array<const char*, 3> title_choices{"New game", "Continue", "Quit"};
constexpr std::array<const char*, 10> pause_choices{
    "Resume", "Items", "Skills", "Quests", "Party", "Equip", "Options", "Save", "Load", "Quit"};

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
    open_seconds_ = 0.0;
    page_ = Page::Title;
    from_title_ = true;
    cursor_ = 0;
    note_.clear();
}

void GameMenu::open_save() {
    open_seconds_ = 0.0;
    page_ = Page::Save;
    from_title_ = false;
    at_save_point_ = true;
    cursor_ = 0;
    note_.clear();
}

void GameMenu::open_pause() {
    at_save_point_ = false;
    open_seconds_ = 0.0;
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
            // Credits, before Quit, when the game has some.
            if (std::string_view(choice) == "Quit" && !credits_.empty()) {
                out.push_back(tr("Credits"));
            }
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
    case Page::Items:
    case Page::Skills:
        out = use_choices();
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
    if ((action == "cancel" || action == "menu") &&
        ((page_ == Page::Items && use_step_ == UseStep::Target) ||
         (page_ == Page::Skills && use_step_ != UseStep::User))) {
        if (use_step_ == UseStep::Target) {
            use_step_ = UseStep::List;
            const std::vector<std::string> list = usable();
            const auto at = std::find(list.begin(), list.end(), use_what_);
            cursor_ = at == list.end() ? 0 : static_cast<std::size_t>(at - list.begin());
        } else {
            use_step_ = UseStep::User;
            cursor_ = use_user_;
        }
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
        case Page::Skills:
        case Page::Quests:
        case Page::Party:
        case Page::Equip:
        case Page::Options:
        case Page::Save:
            if (page_ == Page::Save && at_save_point_) {
                page_ = Page::None;
                at_save_point_ = false;
                return {Result::Kind::Resume, 0};
            }
            cursor_ = static_cast<std::size_t>(page_) - static_cast<std::size_t>(Page::Pause);
            page_ = Page::Pause;
            return {};
        case Page::Credits:
            page_ = Page::Title;
            cursor_ = 2;
            return {};
        case Page::Load:
            // From the title, back to it; from the pause menu, back to that.
            cursor_ = from_title_ ? 1 : 8;
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
        if (cursor_ == 2 && !credits_.empty()) {
            page_ = Page::Credits;
            credits_seconds_ = 0.0;
            return {};
        }
        return {Result::Kind::Quit, 0};
    case Page::Credits:
        page_ = Page::Title; // Interact ends them, as Cancel does
        cursor_ = 2;
        return {};
    case Page::Pause: {
        // The page each choice opens, in pause_choices' order; Resume and Quit close it.
        constexpr std::array<Page, 10> opens{Page::None,  Page::Items, Page::Skills,  Page::Quests,
                                             Page::Party, Page::Equip, Page::Options, Page::Save,
                                             Page::Load,  Page::None};
        if (cursor_ == 0) {
            page_ = Page::None;
            return {Result::Kind::Resume, 0};
        }
        if (cursor_ + 1 >= opens.size()) {
            return {Result::Kind::Quit, 0};
        }
        page_ = opens.at(cursor_);
        cursor_ = 0;
        equip_step_ = EquipStep::Member;
        equip_member_ = 0;
        use_step_ = page_ == Page::Skills ? UseStep::User : UseStep::List;
        use_user_ = 0;
        return {};
    }
    case Page::Items:
    case Page::Skills: {
        if (party_ == nullptr || party_->members().empty()) {
            return {};
        }
        if (use_step_ == UseStep::User) {
            use_user_ = std::min(cursor_, party_->members().size() - 1);
            use_step_ = UseStep::List;
            cursor_ = 0;
        } else if (use_step_ == UseStep::List) {
            const std::vector<std::string> list = usable();
            if (cursor_ >= list.size()) {
                return {};
            }
            use_what_ = list[cursor_];
            use_step_ = UseStep::Target;
            cursor_ = 0;
        } else if (cursor_ < party_->members().size()) {
            use_on(party_->members()[cursor_]);
        }
        return {};
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

std::optional<Fighter> GameMenu::member_now(const std::string& id) const {
    std::string error;
    std::optional<Fighter> f = make_fighter(equip_data(), id, false, error);
    if (f && party_ != nullptr) {
        if (const auto known = party_->condition().find(id); known != party_->condition().end()) {
            f->hp = std::clamp<std::int64_t>(known->second.first, 0, f->max_hp);
            f->mp = std::clamp<std::int64_t>(known->second.second, 0, f->max_mp);
        }
    }
    return f;
}

std::vector<std::string> GameMenu::usable() const {
    std::vector<std::string> out;
    const Table* items = tables_ != nullptr ? tables_->find("data/items") : nullptr;
    if (page_ == Page::Items) {
        for (const auto& [item, count] : inventory_ != nullptr
                                             ? inventory_->items()
                                             : std::map<std::string, std::int64_t, std::less<>>{}) {
            const std::optional<double> heal =
                items != nullptr ? items->number(item, "heal") : std::nullopt;
            if (count > 0 && heal && *heal > 0.0 && slot_of(items, item).empty()) {
                out.push_back(item);
            }
        }
        return out;
    }
    // Skills: the user's that heal.
    const Table* skills = tables_ != nullptr ? tables_->find("data/skills") : nullptr;
    if (party_ == nullptr || skills == nullptr || use_user_ >= party_->members().size()) {
        return out;
    }
    const std::string& user = party_->members()[use_user_];
    for (const std::string& skill :
         skills_at(tables_->find("data/party"), user, party_->level_of(user))) {
        const std::optional<double> heals = skills->number(skill, "heals");
        const std::optional<double> power = skills->number(skill, "power");
        if (heals && *heals != 0.0 && (!power || *power > 0.0)) {
            out.push_back(skill);
        }
    }
    return out;
}

std::vector<std::string> GameMenu::use_choices() const {
    std::vector<std::string> out;
    if (party_ == nullptr) {
        return out;
    }
    const auto named = [this](const char* table, const std::string& row) {
        const Table* t = tables_ != nullptr ? tables_->find(table) : nullptr;
        const std::optional<std::string> name = t != nullptr ? t->text(row, "name") : std::nullopt;
        return name && !name->empty() ? *name : row;
    };
    const Table* skills = tables_ != nullptr ? tables_->find("data/skills") : nullptr;
    switch (use_step_) {
    case UseStep::User:
    case UseStep::Target:
        for (const std::string& member : party_->members()) {
            const std::optional<Fighter> f = member_now(member);
            std::string line = member_name(member);
            if (f) {
                line += "  HP " + std::to_string(f->hp) + "/" + std::to_string(f->max_hp);
                if (f->max_mp > 0) {
                    line += "  MP " + std::to_string(f->mp) + "/" + std::to_string(f->max_mp);
                }
            }
            out.push_back(line);
        }
        break;
    case UseStep::List:
        for (const std::string& what : usable()) {
            if (page_ == Page::Items) {
                out.push_back(named("data/items", what) + "  x" +
                              std::to_string(inventory_->count_of(what)));
            } else {
                const std::optional<double> cost =
                    skills != nullptr ? skills->number(what, "mp_cost") : std::nullopt;
                out.push_back(named("data/skills", what) + "  " +
                              std::to_string(cost ? static_cast<std::int64_t>(*cost) : 0) + " MP");
            }
        }
        if (out.empty()) {
            out.emplace_back(page_ == Page::Items ? "Nothing to use." : "No healing skills.");
        }
        break;
    }
    return out;
}

void GameMenu::use_on(const std::string& target) {
    std::optional<Fighter> f = member_now(target);
    if (!f || party_ == nullptr || inventory_ == nullptr) {
        return;
    }
    if (f->hp <= 0) {
        note_ = member_name(target) + " is down: only an inn helps.";
        return;
    }
    std::int64_t amount = 0;
    if (page_ == Page::Items) {
        const Table* items = tables_->find("data/items");
        const std::optional<double> heal =
            items != nullptr ? items->number(use_what_, "heal") : std::nullopt;
        if (!inventory_->has(use_what_) || !heal) {
            use_step_ = UseStep::List;
            return;
        }
        amount = static_cast<std::int64_t>(*heal);
        inventory_->take(use_what_, 1);
    } else {
        const Table* skills = tables_->find("data/skills");
        const std::string& user = party_->members().at(use_user_);
        std::optional<Fighter> caster = user == target ? f : member_now(user);
        const std::optional<double> cost =
            skills != nullptr ? skills->number(use_what_, "mp_cost") : std::nullopt;
        const std::optional<double> power =
            skills != nullptr ? skills->number(use_what_, "power") : std::nullopt;
        const auto mp = static_cast<std::int64_t>(cost ? *cost : 0.0);
        if (!caster || caster->mp < mp) {
            note_ = member_name(user) + " has too little MP.";
            return;
        }
        caster->mp -= mp;
        party_->condition()[user] = {caster->hp, caster->mp};
        if (user == target) {
            f->mp = caster->mp;
        }
        amount = static_cast<std::int64_t>(power ? *power : 10.0);
    }
    const std::int64_t before = f->hp;
    f->hp = std::min(f->max_hp, f->hp + std::max<std::int64_t>(amount, 0));
    party_->condition()[target] = {f->hp, f->mp};
    note_ = member_name(target) + " recovers " + std::to_string(f->hp - before) + " HP.";
    if (page_ == Page::Items && !inventory_->has(use_what_)) {
        use_step_ = UseStep::List;
        cursor_ = 0;
    }
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
                    const InventoryObject& /*inventory: the Items page reads it as attached*/,
                    const QuestObject& quests, const PartyObject& party) const {
    if (page_ == Page::None) {
        return;
    }
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    const auto w = static_cast<float>(surface.width());
    const auto h = static_cast<float>(surface.height());
    const auto line = static_cast<float>(font.line_height());
    constexpr float pad = 5.0F;
    const UiTheme& ui = theme();
    const render::Color text = ui.text;
    const render::Color dim = ui.dim;
    const render::Color gold = ui.highlight;
    // Opening, the menu fades in over an eighth of a second.
    const auto appear = static_cast<float>(std::clamp(open_seconds_ / 0.125, 0.0, 1.0));
    const auto fade = [appear](render::Color c) {
        c.a = static_cast<std::uint8_t>(static_cast<float>(c.a) * appear);
        return c;
    };
    const auto box = [&](float x, float y, float bw, float bh) {
        draw_box(sprites, white, ui, frame_, render::Rect{x, y, bw, bh}, appear);
    };
    const auto write = [&](const std::string& words, float x, float y, render::Color colour) {
        ui::draw_text(sprites, atlas, font, words, x, y, fade(colour));
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
        fade(render::Color{0, 0, 10, static_cast<std::uint8_t>(page_ == Page::Title ? 120 : 150)}));
    const std::vector<std::string> items = choices(saves);
    if (page_ == Page::Credits) {
        // The credits roll up the screen, centred, then start again.
        constexpr double speed = 20.0; // pixels a second
        const float total = line * static_cast<float>(credits_.size() + 1);
        const float travel = h + total;
        const auto offset =
            static_cast<float>(std::fmod(credits_seconds_ * speed, static_cast<double>(travel)));
        for (std::size_t k = 0; k < credits_.size(); ++k) {
            const float y = std::floor(h - offset + (line * static_cast<float>(k)));
            if (y < -line || y > h) {
                continue;
            }
            const auto tw = static_cast<float>(ui::measure(font, credits_[k]));
            write(credits_[k], std::floor((w - tw) / 2.0F), y, k == 0 ? gold : text);
        }
    } else if (page_ == Page::Title || (page_ == Page::Load && from_title_)) {
        // The game's name, in the upper third, and the choices below it, centred.
        // Twice the size while it fits, as titles are.
        const int big = static_cast<float>(ui::measure(font, title) * 2) <= w - 8.0F ? 2 : 1;
        const float tw = static_cast<float>(ui::measure(font, title) * big);
        ui::draw_text(sprites, atlas, font, title, std::floor((w - tw) / 2.0F),
                      std::floor(h * 0.25F), fade(gold), big);
        const float bw = 80.0F;
        list(items, std::floor((w - bw) / 2.0F), std::floor(h * 0.5F), bw);
    } else {
        // The pause menu down the left; the page chosen beside it.
        const float menu_width = std::min(70.0F, w * 0.3F);
        const bool on_pause = page_ == Page::Pause;
        if (on_pause) {
            list(items, pad, pad, menu_width);
        } else if (!at_save_point_) {
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
            // Each member's status: level, HP and MP now, their numbers with what they
            // wear, and the experience to the next level.
            for (const std::string& member : party.members()) {
                const std::int64_t next = party.xp_to_next(member);
                const std::optional<Fighter> f = member_now(member);
                if (!f) {
                    const std::int64_t hp = party.hp_of(member);
                    lines.push_back(
                        member + "  Lv " + std::to_string(party.level_of(member)) +
                        (hp < 0 ? std::string("  HP full") : "  HP " + std::to_string(hp)));
                    continue;
                }
                lines.push_back(member_name(member) + "  Lv " + std::to_string(f->level) + "  HP " +
                                std::to_string(f->hp) + "/" + std::to_string(f->max_hp) +
                                (f->max_mp > 0 ? "  MP " + std::to_string(f->mp) + "/" +
                                                     std::to_string(f->max_mp)
                                               : std::string()));
                lines.push_back("   Atk " + std::to_string(f->attack) + "  Def " +
                                std::to_string(f->defense) + "  Spd " + std::to_string(f->speed) +
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
        if (page_ == Page::Items || page_ == Page::Skills) {
            const float bh = list(items, x, pad, bw);
            const char* hint = use_step_ == UseStep::Target ? "On whom?"
                               : use_step_ == UseStep::User ? "Who uses a skill?"
                                                            : nullptr;
            if (hint != nullptr) {
                write(tr(hint), x + pad, pad + bh + 2, dim);
            }
        } else if (page_ == Page::Equip) {
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
