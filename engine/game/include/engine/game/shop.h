#pragma once

// Shops (plan/02 F1, K3): logic opens one selling some of data/items, and the player buys
// and sells for gold on a shop screen laid out as RPGs lay it out: Buy, Sell and Leave, a
// list of wares with their prices, how many the player carries, and a quantity to pick.
//
// See docs/dev/shop.md.

#include "engine/game/inventory.h"
#include "engine/game/tables.h"
#include "engine/game/ui_theme.h"
#include "engine/render/render.h"
#include "engine/ui/text.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// What the player pays with: the inventory's "gold", which battles give.
inline constexpr const char* shop_money = "gold";

/// Something a shop sells or buys back.
struct Ware {
    std::string item;        ///< Its name in the inventory (its row of data/items).
    std::string name;        ///< As shown: the row's `name`, else the item.
    std::string description; ///< The row's `description`; may be empty.
    std::int64_t price = 0;  ///< To buy one.
    std::int64_t sell = 0;   ///< Paid for one sold; 0: the shop does not buy it.

    friend bool operator==(const Ware&, const Ware&) = default;
};

/// The wares a shop selling `items` (rows of data/items, separated by commas: "potion,
/// ether") offers: each with a price (its `price` column) above 0. Items not in the table,
/// or without a price, are left out and named in `missing`.
[[nodiscard]] std::vector<Ware> shop_wares(std::string_view items, const Table* table,
                                           std::vector<std::string>& missing);
/// What the shop would pay for `item`: its `sell` cell if it has one, else half its `price`
/// (rounded down); 0 if the shop does not buy it.
[[nodiscard]] Ware sell_ware(const std::string& item, const Table* table);

/// The shop the player is in: its pages, the quantity being picked, and what was bought.
class ShopScreen {
public:
    enum class Page : std::uint8_t { Menu, Buy, Sell, Quantity };

    /// A shop selling `wares`, with data/items (`table`) for what it buys back.
    ShopScreen(std::vector<Ware> wares, const Table* table);

    /// A pressed action ("move_up", "move_down", "move_left", "move_right", "interact",
    /// "cancel", "menu"). Buying and selling change `inventory`.
    void press(std::string_view action, InventoryObject& inventory);
    /// The colours it is drawn with (the project's); null for the standard ones.
    void set_theme(const UiTheme* theme) noexcept { theme_ = theme; }
    /// The picture its boxes are drawn with (null, or one without a picture: the look's
    /// colours). Kept by the caller.
    void set_frame(const BoxFrame* frame) noexcept { frame_ = frame; }
    /// True once the player leaves.
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    [[nodiscard]] Page page() const noexcept { return page_; }
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }
    [[nodiscard]] std::int64_t quantity() const noexcept { return quantity_; }
    [[nodiscard]] const std::vector<Ware>& wares() const noexcept { return wares_; }
    /// What the Sell page lists: what the player carries that the shop buys.
    [[nodiscard]] std::vector<Ware> sellable(const InventoryObject& inventory) const;
    /// The line under the lists: the ware's description, or what just happened.
    [[nodiscard]] const std::string& note() const noexcept { return note_; }

    /// Draws the screen over `surface` (`white`: one white pixel; `atlas` and `font`: text).
    void draw(render::SpriteRenderer& sprites, render::RenderTarget& surface,
              const render::Texture& white, const render::Texture& atlas, const ui::PixelFont& font,
              const InventoryObject& inventory) const;

private:
    /// The most of `ware` the player can buy or sell now.
    [[nodiscard]] std::int64_t most(const Ware& ware, const InventoryObject& inventory) const;
    [[nodiscard]] std::vector<std::string> lines(const InventoryObject& inventory) const;
    void describe(const InventoryObject& inventory);

    std::vector<Ware> wares_;
    const Table* table_ = nullptr;
    Page page_ = Page::Menu;
    bool selling_ = false;   ///< Whether Quantity is for selling.
    std::size_t cursor_ = 0; ///< On the page showing.
    std::size_t chosen_ = 0; ///< The ware the quantity is for.
    std::int64_t quantity_ = 1;
    std::string note_;
    bool closed_ = false;
    [[nodiscard]] const UiTheme& theme() const noexcept;
    const UiTheme* theme_ = nullptr;
    const BoxFrame* frame_ = nullptr;
};

} // namespace engine::game
