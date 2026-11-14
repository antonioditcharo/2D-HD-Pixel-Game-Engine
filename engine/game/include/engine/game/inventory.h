#pragma once

// The "inventory" object logic uses for what the player carries: keys, coins, potions,
// each by name with how many. Rules give and take them and check for them ("If the
// player has a key"); saves keep them.
//
// See docs/dev/game.md §5.

#include "engine/foundation/reflect.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace engine::game {

class InventoryObject {
public:
    /// The most of one thing the player can carry.
    static constexpr std::int64_t most = 999'999;

    /// Adds `count` of `item` (its name, spaces around it dropped), up to `most`. Nothing
    /// for an empty name or a count below 1.
    void give(const std::string& item, std::int64_t count);
    /// Takes away up to `count` of `item`; at none left, the player no longer has it.
    /// Nothing for a count below 1.
    void take(const std::string& item, std::int64_t count);
    /// Whether the player carries at least one `item`.
    [[nodiscard]] bool has(const std::string& item) const;
    /// Whether the player carries at least `count` of `item` (1 for a count below 1).
    [[nodiscard]] bool has_at_least(const std::string& item, std::int64_t count) const;
    /// How many of `item` the player carries; 0 for none.
    [[nodiscard]] std::int64_t count_of(const std::string& item) const;
    /// How many different things the player carries.
    [[nodiscard]] std::int64_t kinds() const noexcept;

    /// Everything carried, by name, for saves and the editor.
    [[nodiscard]] const std::map<std::string, std::int64_t, std::less<>>& items() const noexcept {
        return items_;
    }
    /// Sets how many of `item` there are (0 or less removes it), up to `most`.
    void set(std::string_view item, std::int64_t count);
    /// Empties it: when a save is loaded.
    void clear() noexcept { items_.clear(); }

    /// Opens a shop selling `items` (rows of data/items, separated by commas: "potion,
    /// ether"), for gold. The runtime takes the request.
    void open_shop(const std::string& items) { shop_request_ = items; }
    [[nodiscard]] std::optional<std::string> take_shop_request() {
        return std::exchange(shop_request_, std::nullopt);
    }
    /// Whether the player is in a shop.
    [[nodiscard]] bool shopping() const noexcept { return shopping_; }
    void set_shopping(bool on) noexcept { shopping_ = on; }

private:
    std::map<std::string, std::int64_t, std::less<>> items_;
    std::optional<std::string> shop_request_;
    bool shopping_ = false;
};

/// Registers InventoryObject as "inventory".
void register_inventory_types(reflect::Registry& registry);

} // namespace engine::game
