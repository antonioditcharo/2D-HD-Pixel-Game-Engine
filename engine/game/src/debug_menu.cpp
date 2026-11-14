#include "engine/game/debug_menu.h"

#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace engine::game {

namespace {

constexpr std::array<const char*, 6> main_lines{"Give items", "Story flags",    "Quests",
                                                "Go to...",   "Heal the party", "Close"};
/// Lines shown at once; the list scrolls past them.
constexpr std::size_t shown_lines = 12;

} // namespace

void DebugMenu::open() {
    page_ = Page::Main;
    cursor_ = 0;
    note_.clear();
}

std::vector<std::string> DebugMenu::item_ids() const {
    std::set<std::string, std::less<>> ids;
    const Table* items = targets_.tables != nullptr ? targets_.tables->find("data/items") : nullptr;
    if (items != nullptr) {
        for (const TableRow& row : items->rows) {
            ids.insert(row.id);
        }
    }
    if (targets_.inventory != nullptr) {
        for (const auto& [item, count] : targets_.inventory->items()) {
            ids.insert(item);
        }
    }
    ids.insert("gold");
    return {ids.begin(), ids.end()};
}

std::vector<std::string> DebugMenu::flag_names() const {
    std::vector<std::string> out;
    if (targets_.dialogue != nullptr) {
        for (const auto& [flag, on] : targets_.dialogue->flags()) {
            out.push_back(flag);
        }
    }
    return out;
}

std::vector<std::string> DebugMenu::quest_names() const {
    std::vector<std::string> out;
    if (targets_.quests != nullptr) {
        for (const auto& [name, quest] : targets_.quests->quests()) {
            out.push_back(name);
        }
    }
    return out;
}

std::vector<world::Entity> DebugMenu::places() const {
    std::vector<world::Entity> out;
    if (targets_.world == nullptr) {
        return out;
    }
    const world::World& w = *targets_.world;
    const reflect::TypeInfo* type = w.registry().find_type<scene2d::Transform2D>();
    if (type == nullptr) {
        return out;
    }
    w.each(*type, [&](world::Entity e, reflect::ObjectRef) {
        if (!w.name(e).empty() && w.get<TopDownMover>(e) == nullptr) {
            out.push_back(e);
        }
    });
    std::sort(out.begin(), out.end(),
              [&w](world::Entity a, world::Entity b) { return w.name(a) < w.name(b); });
    return out;
}

std::vector<std::string> DebugMenu::lines() const {
    std::vector<std::string> out;
    switch (page_) {
    case Page::None:
        break;
    case Page::Main:
        out.assign(main_lines.begin(), main_lines.end());
        break;
    case Page::Items:
        for (const std::string& item : item_ids()) {
            out.push_back(item + "  x" +
                          std::to_string(targets_.inventory != nullptr
                                             ? targets_.inventory->count_of(item)
                                             : 0));
        }
        break;
    case Page::StoryFlags:
        for (const std::string& flag : flag_names()) {
            out.push_back(flag + "  " +
                          (targets_.dialogue->flag(flag) ? std::string("on") : std::string("off")));
        }
        if (out.empty()) {
            out.emplace_back("No flags yet.");
        }
        break;
    case Page::Quests:
        for (const std::string& name : quest_names()) {
            const QuestState state = targets_.quests->quests().at(name).state;
            out.push_back(name + "  " +
                          (state == QuestState::Done     ? "done"
                           : state == QuestState::Failed ? "failed"
                                                         : "under way"));
        }
        if (out.empty()) {
            out.emplace_back("No quests yet.");
        }
        break;
    case Page::GoTo:
        for (const world::Entity e : places()) {
            out.emplace_back(targets_.world->name(e));
        }
        break;
    }
    return out;
}

void DebugMenu::go_to(world::Entity target) {
    world::World* w = targets_.world;
    const auto* there = w->get<scene2d::Transform2D>(target);
    if (there == nullptr) {
        return;
    }
    const reflect::TypeInfo* mover = w->registry().find_type<TopDownMover>();
    world::Entity player;
    if (mover != nullptr) {
        w->each(*mover, [&](world::Entity e, reflect::ObjectRef) {
            const Walker* walker = w->get<Walker>(e);
            if (!player.is_valid() && (walker == nullptr || !walker->enabled)) {
                player = e;
            }
        });
    }
    auto* feet = player.is_valid() ? w->get<scene2d::Transform2D>(player) : nullptr;
    if (feet == nullptr) {
        note_ = "There is no player to move.";
        return;
    }
    // Just in front of it (below), so the player does not stand inside it.
    feet->x = there->x;
    feet->y = there->y + 1.0;
    note_ = "Went to " + std::string(w->name(target)) + ".";
}

void DebugMenu::press(std::string_view action) {
    if (page_ == Page::None) {
        return;
    }
    const std::size_t count = std::max<std::size_t>(lines().size(), 1);
    if (action == "move_up" || action == "move_down") {
        cursor_ = (cursor_ + (action == "move_up" ? count - 1 : 1)) % count;
        return;
    }
    if (action == "cancel" || action == "menu") {
        if (page_ == Page::Main) {
            close();
        } else {
            cursor_ = static_cast<std::size_t>(page_) - static_cast<std::size_t>(Page::Items);
            page_ = Page::Main;
        }
        return;
    }
    note_.clear();
    if (page_ == Page::Items && targets_.inventory != nullptr &&
        (action == "move_left" || action == "move_right" || action == "interact")) {
        const std::vector<std::string> items = item_ids();
        if (cursor_ < items.size()) {
            if (action == "move_left") {
                targets_.inventory->take(items[cursor_], 1);
            } else {
                targets_.inventory->give(items[cursor_], items[cursor_] == "gold" ? 100 : 1);
            }
        }
        return;
    }
    if (action != "interact") {
        return;
    }
    switch (page_) {
    case Page::Main:
        if (cursor_ == 4) {
            if (targets_.party != nullptr) {
                targets_.party->heal_all();
            }
            note_ = "The party is healed.";
        } else if (cursor_ == 5) {
            close();
        } else {
            page_ = static_cast<Page>(static_cast<std::size_t>(Page::Items) + cursor_);
            cursor_ = 0;
        }
        break;
    case Page::StoryFlags: {
        const std::vector<std::string> flags = flag_names();
        if (cursor_ < flags.size()) {
            if (targets_.dialogue->flag(flags[cursor_])) {
                targets_.dialogue->clear_flag(flags[cursor_]);
            } else {
                targets_.dialogue->set_flag(flags[cursor_]);
            }
        }
        break;
    }
    case Page::Quests: {
        const std::vector<std::string> quests = quest_names();
        if (cursor_ < quests.size()) {
            targets_.quests->complete(quests[cursor_]);
        }
        break;
    }
    case Page::GoTo: {
        const std::vector<world::Entity> there = places();
        if (cursor_ < there.size()) {
            go_to(there[cursor_]);
            close();
        }
        break;
    }
    default:
        break;
    }
}

void DebugMenu::draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
                     const render::Texture& white, const render::Texture& atlas,
                     const ui::PixelFont& font) const {
    if (page_ == Page::None) {
        return;
    }
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    const auto w = static_cast<float>(surface.width());
    const auto line = static_cast<float>(font.line_height());
    constexpr float pad = 4.0F;
    const std::vector<std::string> all = lines();
    const std::size_t first = cursor_ >= shown_lines ? cursor_ - shown_lines + 1 : 0;
    const std::size_t count = std::min(shown_lines, all.size() - std::min(first, all.size()));
    const float bw = std::min(w - (2 * pad), 160.0F);
    const float bh = (line * static_cast<float>(count + 2)) + (2 * pad);
    const float x = w - bw - pad;
    sprites.begin_over(surface);
    // A magenta border: plainly not part of the game.
    sprites.draw(white, one, render::Rect{x, pad, bw, bh}, render::Color{220, 60, 220, 255});
    sprites.draw(white, one, render::Rect{x + 1, pad + 1, bw - 2, bh - 2},
                 render::Color{10, 10, 20, 235});
    ui::draw_text(sprites, atlas, font, "Debug (F1)", x + pad, pad + pad,
                  render::Color{220, 60, 220, 255});
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t i = first + k;
        const bool here = i == cursor_;
        ui::draw_text(sprites, atlas, font, (here ? "> " : "  ") + all[i], x + pad,
                      pad + pad + (line * static_cast<float>(k + 1)),
                      here ? render::Color{255, 210, 122, 255} : render::Color{255, 255, 255, 255});
    }
    const std::string hint = !note_.empty()         ? note_
                             : page_ == Page::Items ? std::string("Right/Interact: +1, Left: -1")
                                                    : std::string("Interact: do, Cancel: back");
    ui::draw_text(sprites, atlas, font, hint, x + pad,
                  pad + pad + (line * static_cast<float>(count + 1)),
                  render::Color{150, 155, 170, 255});
    static_cast<void>(sprites.end());
}

} // namespace engine::game
