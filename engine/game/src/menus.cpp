#include "engine/game/menus.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::game {

namespace {

constexpr std::array<const char*, 3> title_choices{"New game", "Continue", "Quit"};
constexpr std::array<const char*, 7> pause_choices{"Resume", "Items", "Quests", "Party",
                                                   "Save",   "Load",  "Quit"};

} // namespace

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
        out.assign(title_choices.begin(), title_choices.end());
        break;
    case Page::Pause:
        out.assign(pause_choices.begin(), pause_choices.end());
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

GameMenu::Result GameMenu::press(std::string_view action, const SaveObject& saves) {
    note_.clear();
    const std::size_t count = choices(saves).size();
    if (action == "move_up" && count > 0) {
        cursor_ = (cursor_ + count - 1) % count;
        return {};
    }
    if (action == "move_down" && count > 0) {
        cursor_ = (cursor_ + 1) % count;
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
        case Page::Save:
            cursor_ = static_cast<std::size_t>(page_) - static_cast<std::size_t>(Page::Pause);
            page_ = Page::Pause;
            return {};
        case Page::Load:
            // From the title, back to it; from the pause menu, back to that.
            cursor_ = from_title_ ? 1 : 5;
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
            page_ = Page::Save;
            cursor_ = 0;
            return {};
        case 5:
            page_ = Page::Load;
            cursor_ = 0;
            return {};
        default:
            return {Result::Kind::Quit, 0};
        }
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
                write("  " + pause[k], pad + pad, pad + pad + (line * static_cast<float>(k)),
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
                lines.push_back(member +
                                (hp < 0 ? std::string("  HP full") : "  HP " + std::to_string(hp)));
            }
            if (lines.empty()) {
                lines.emplace_back("No one has joined the party.");
            }
            lines.push_back("Experience: " + std::to_string(party.xp()));
            break;
        default:
            break;
        }
        if (page_ == Page::Save || page_ == Page::Load) {
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
