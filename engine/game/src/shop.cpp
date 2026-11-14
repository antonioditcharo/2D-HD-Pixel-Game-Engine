#include "engine/game/shop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace engine::game {

const UiTheme& ShopScreen::theme() const noexcept {
    static const UiTheme standard;
    return theme_ != nullptr ? *theme_ : standard;
}

namespace {

constexpr std::array<const char*, 3> menu_choices{"Buy", "Sell", "Leave"};
/// Rows the Buy and Sell lists show at once; the list scrolls past them.
constexpr std::size_t list_rows = 8;

std::string trimmed(std::string_view text) {
    const std::size_t a = text.find_first_not_of(' ');
    if (a == std::string_view::npos) {
        return {};
    }
    return std::string(text.substr(a, text.find_last_not_of(' ') - a + 1));
}

std::int64_t whole(const Table* table, std::string_view row, std::string_view column) {
    const std::optional<double> value =
        table != nullptr ? table->number(row, column) : std::nullopt;
    return value && std::isfinite(*value) && *value > 0.0
               ? static_cast<std::int64_t>(std::min(std::floor(*value), 1e12))
               : 0;
}

std::string text_or(const Table* table, std::string_view row, std::string_view column,
                    std::string fallback) {
    std::optional<std::string> value = table != nullptr ? table->text(row, column) : std::nullopt;
    return value && !value->empty() ? std::move(*value) : std::move(fallback);
}

} // namespace

std::vector<Ware> shop_wares(std::string_view items, const Table* table,
                             std::vector<std::string>& missing) {
    std::vector<Ware> wares;
    std::string item;
    const std::string list = std::string(items) + ",";
    for (const char c : list) {
        if (c != ',') {
            item += c;
            continue;
        }
        const std::string id = trimmed(item);
        item.clear();
        if (id.empty() || std::ranges::any_of(wares, [&](const Ware& w) { return w.item == id; })) {
            continue;
        }
        Ware ware = sell_ware(id, table);
        ware.price = whole(table, id, "price");
        if (table == nullptr || table->row(id) == nullptr || ware.price <= 0) {
            missing.push_back(id);
            continue;
        }
        wares.push_back(std::move(ware));
    }
    return wares;
}

Ware sell_ware(const std::string& item, const Table* table) {
    Ware ware;
    ware.item = item;
    ware.name = text_or(table, item, "name", item);
    ware.description = text_or(table, item, "description", {});
    ware.price = whole(table, item, "price");
    // Its own sell price if it has one (0: the shop does not buy it), else half its price.
    const TableRow* row = table != nullptr ? table->row(item) : nullptr;
    bool own = false;
    if (row != nullptr) {
        const auto cell = row->cells.find("sell");
        own = cell != row->cells.end() && !cell->second.empty();
    }
    ware.sell = own ? whole(table, item, "sell") : ware.price / 2;
    return ware;
}

ShopScreen::ShopScreen(std::vector<Ware> wares, const Table* table)
    : wares_(std::move(wares)), table_(table) {}

std::vector<Ware> ShopScreen::sellable(const InventoryObject& inventory) const {
    std::vector<Ware> out;
    for (const auto& [item, count] : inventory.items()) {
        if (item == shop_money || count <= 0) {
            continue;
        }
        Ware ware = sell_ware(item, table_);
        if (ware.sell > 0) {
            out.push_back(std::move(ware));
        }
    }
    return out;
}

std::int64_t ShopScreen::most(const Ware& ware, const InventoryObject& inventory) const {
    if (selling_) {
        return inventory.count_of(ware.item);
    }
    const std::int64_t room = InventoryObject::most - inventory.count_of(ware.item);
    const std::int64_t affordable =
        ware.price > 0 ? inventory.count_of(shop_money) / ware.price : 0;
    return std::max<std::int64_t>(std::min({room, affordable, std::int64_t{99}}), 0);
}

void ShopScreen::describe(const InventoryObject& inventory) {
    note_.clear();
    const std::vector<Ware> list = page_ == Page::Sell ? sellable(inventory) : wares_;
    if ((page_ == Page::Buy || page_ == Page::Sell) && cursor_ < list.size()) {
        note_ = list[cursor_].description;
    }
}

void ShopScreen::press(std::string_view action, InventoryObject& inventory) {
    if (closed_) {
        return;
    }
    const bool up = action == "move_up";
    const bool down = action == "move_down";
    const bool back = action == "cancel" || action == "menu";
    const bool pick = action == "interact";
    switch (page_) {
    case Page::Menu:
        if (up || down) {
            cursor_ = (cursor_ + (up ? menu_choices.size() - 1 : 1)) % menu_choices.size();
        } else if (back || (pick && cursor_ == 2)) {
            closed_ = true;
        } else if (pick) {
            page_ = cursor_ == 0 ? Page::Buy : Page::Sell;
            selling_ = cursor_ == 1;
            cursor_ = 0;
            describe(inventory);
        }
        return;
    case Page::Buy:
    case Page::Sell: {
        const std::vector<Ware> list = page_ == Page::Sell ? sellable(inventory) : wares_;
        if ((up || down) && !list.empty()) {
            cursor_ = (cursor_ + (up ? list.size() - 1 : 1)) % list.size();
            describe(inventory);
        } else if (back) {
            cursor_ = page_ == Page::Sell ? 1 : 0;
            page_ = Page::Menu;
            note_.clear();
        } else if (pick && cursor_ < list.size()) {
            if (most(list[cursor_], inventory) < 1) {
                note_ = selling_ ? "There is none to sell." : "Not enough gold.";
                return;
            }
            chosen_ = cursor_;
            quantity_ = 1;
            page_ = Page::Quantity;
        }
        return;
    }
    case Page::Quantity: {
        const std::vector<Ware> list = selling_ ? sellable(inventory) : wares_;
        if (chosen_ >= list.size()) {
            page_ = selling_ ? Page::Sell : Page::Buy;
            return;
        }
        const Ware& ware = list[chosen_];
        const std::int64_t top = std::max<std::int64_t>(most(ware, inventory), 1);
        if (action == "move_right" || up) {
            const std::int64_t step = up ? 10 : 1;
            quantity_ = quantity_ == top ? 1 : std::min(quantity_ + step, top);
        } else if (action == "move_left" || down) {
            const std::int64_t step = down ? 10 : 1;
            quantity_ = quantity_ == 1 ? top : std::max<std::int64_t>(quantity_ - step, 1);
        } else if (back) {
            page_ = selling_ ? Page::Sell : Page::Buy;
            cursor_ = chosen_;
            describe(inventory);
        } else if (pick) {
            const std::int64_t count = std::min(quantity_, most(ware, inventory));
            if (count < 1) {
                return;
            }
            if (selling_) {
                inventory.take(ware.item, count);
                inventory.give(std::string(shop_money), ware.sell * count);
                note_ = "Sold " + std::to_string(count) + " " + ware.name + ".";
            } else {
                inventory.take(std::string(shop_money), ware.price * count);
                inventory.give(ware.item, count);
                note_ = "Bought " + std::to_string(count) + " " + ware.name + ".";
            }
            page_ = selling_ ? Page::Sell : Page::Buy;
            const std::size_t left = selling_ ? sellable(inventory).size() : wares_.size();
            cursor_ = left == 0 ? 0 : std::min(chosen_, left - 1);
        }
        return;
    }
    }
}

std::vector<std::string> ShopScreen::lines(const InventoryObject& inventory) const {
    std::vector<std::string> out;
    const bool selling = page_ == Page::Sell || (page_ == Page::Quantity && selling_);
    for (const Ware& ware : selling ? sellable(inventory) : wares_) {
        out.push_back(ware.name + "|" + std::to_string(selling ? ware.sell : ware.price));
    }
    return out;
}

void ShopScreen::draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
                      const render::Texture& white, const render::Texture& atlas,
                      const ui::PixelFont& font, const InventoryObject& inventory) const {
    if (closed_) {
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
    const auto box = [&](float x, float y, float bw, float bh) {
        draw_box(sprites, white, ui, frame_, render::Rect{x, y, bw, bh});
    };
    const auto write = [&](const std::string& words, float x, float y, render::Color colour) {
        ui::draw_text(sprites, atlas, font, words, x, y, colour);
    };
    const auto right = [&](const std::string& words, float x, float y, render::Color colour) {
        write(words, x - static_cast<float>(ui::measure(font, words)), y, colour);
    };

    sprites.begin_over(surface);
    sprites.draw(white, one, render::Rect{0, 0, w, h}, render::Color{0, 0, 10, 130});

    // Top: the commands, and the gold carried.
    const float top_h = line + (2 * pad);
    box(pad, pad, w - (2 * pad), top_h);
    float x = pad * 2;
    for (std::size_t k = 0; k < menu_choices.size(); ++k) {
        const bool here =
            page_ == Page::Menu ? k == cursor_ : (k == 0 && !selling_) || (k == 1 && selling_);
        const std::string label =
            std::string(here && page_ == Page::Menu ? "> " : "  ") + menu_choices.at(k);
        write(label, x, pad * 2, here ? gold : (page_ == Page::Menu ? text : dim));
        x += static_cast<float>(ui::measure(font, label)) + (pad * 3);
    }
    right(std::to_string(inventory.count_of(shop_money)) + " gold", w - (pad * 2), pad * 2, gold);

    // The list of wares (or what can be sold), with prices and how many are carried.
    const float list_y = top_h + (2 * pad);
    const float note_h = (line * 2) + (2 * pad);
    const float list_h = std::max(h - list_y - note_h - (2 * pad), line + (2 * pad));
    box(pad, list_y, w - (2 * pad), list_h);
    if (page_ != Page::Menu) {
        const std::vector<std::string> rows = lines(inventory);
        const std::size_t shown_rows = std::max<std::size_t>(
            1, std::min(list_rows, static_cast<std::size_t>((list_h - (2 * pad)) / line)));
        const std::size_t at = page_ == Page::Quantity ? chosen_ : cursor_;
        const std::size_t first = at >= shown_rows ? at - shown_rows + 1 : 0;
        if (rows.empty()) {
            write(selling_ ? "Nothing the shop buys." : "Nothing for sale.", pad * 2, list_y + pad,
                  dim);
        }
        const std::vector<Ware> list = selling_ ? sellable(inventory) : wares_;
        for (std::size_t k = first; k < rows.size() && k < first + shown_rows; ++k) {
            const std::size_t bar = rows[k].rfind('|');
            const float y = list_y + pad + (line * static_cast<float>(k - first));
            const bool here = k == at;
            const bool can = most(list[k], inventory) > 0;
            const render::Color colour = here ? gold : (can ? text : dim);
            write((here ? "> " : "  ") + rows[k].substr(0, bar), pad * 2, y, colour);
            right("x" + std::to_string(inventory.count_of(list[k].item)), w * 0.62F, y, dim);
            right(rows[k].substr(bar + 1) + " G", w - (pad * 2), y, colour);
        }
    }

    // Bottom: the description, what just happened, or the quantity being picked.
    const float note_y = h - pad - note_h;
    box(pad, note_y, w - (2 * pad), note_h);
    if (page_ == Page::Quantity) {
        const std::vector<Ware> list = selling_ ? sellable(inventory) : wares_;
        if (chosen_ < list.size()) {
            const Ware& ware = list[chosen_];
            const std::int64_t each = selling_ ? ware.sell : ware.price;
            write(std::string(selling_ ? "Sell " : "Buy ") + ware.name + "  < " +
                      std::to_string(quantity_) + " >",
                  pad * 2, note_y + pad, gold);
            right(std::string(selling_ ? "+" : "-") + std::to_string(each * quantity_) + " G",
                  w - (pad * 2), note_y + pad, gold);
            write("Left/Right: 1, Up/Down: 10. Interact: done.", pad * 2, note_y + pad + line, dim);
        }
    } else if (page_ == Page::Menu) {
        write(note_.empty() ? std::string("Welcome!") : note_, pad * 2, note_y + pad, text);
    } else {
        write(note_, pad * 2, note_y + pad, text);
    }
    static_cast<void>(sprites.end());
}

} // namespace engine::game
