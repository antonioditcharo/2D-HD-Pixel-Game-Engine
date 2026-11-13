#include "engine/game/inventory.h"

#include <algorithm>

namespace engine::game {

namespace {

/// `name` without spaces around it.
std::string_view trimmed(std::string_view name) {
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = name.find_last_not_of(" \t");
    return name.substr(first, last - first + 1);
}

} // namespace

void InventoryObject::give(const std::string& item, std::int64_t count) {
    if (count < 1) {
        return;
    }
    set(item, std::min(count_of(item), most) + std::min(count, most));
}

void InventoryObject::take(const std::string& item, std::int64_t count) {
    if (count < 1) {
        return;
    }
    set(item, count_of(item) - std::min(count, most));
}

bool InventoryObject::has(const std::string& item) const {
    return count_of(item) > 0;
}

bool InventoryObject::has_at_least(const std::string& item, std::int64_t count) const {
    return count_of(item) >= std::max<std::int64_t>(count, 1);
}

std::int64_t InventoryObject::count_of(const std::string& item) const {
    const auto found = items_.find(trimmed(item));
    return found == items_.end() ? 0 : found->second;
}

std::int64_t InventoryObject::kinds() const noexcept {
    return static_cast<std::int64_t>(items_.size());
}

void InventoryObject::set(std::string_view item, std::int64_t count) {
    const std::string_view name = trimmed(item);
    if (name.empty()) {
        return;
    }
    if (count <= 0) {
        if (const auto found = items_.find(name); found != items_.end()) {
            items_.erase(found);
        }
        return;
    }
    items_.insert_or_assign(std::string(name), std::min(count, most));
}

void register_inventory_types(reflect::Registry& registry) {
    auto inventory = registry.add_type<InventoryObject>("inventory")
                         .display_name("Inventory")
                         .category("Story")
                         .tooltip("What the player carries: keys, coins, potions, by name.");
    inventory.action<&InventoryObject::give>("give", {"item", "count"})
        .text("Give {count} {item}")
        .tooltip("Adds things to what the player carries, by name: Give 1 key.")
        .param("count")
        .range(1.0, static_cast<double>(InventoryObject::most))
        .default_value(reflect::Value{std::int64_t{1}});
    inventory.action<&InventoryObject::take>("take", {"item", "count"})
        .text("Take {count} {item}")
        .tooltip("Takes things away from what the player carries; never below none.")
        .param("count")
        .range(1.0, static_cast<double>(InventoryObject::most))
        .default_value(reflect::Value{std::int64_t{1}});
    inventory.condition<&InventoryObject::has>("has", {"item"})
        .text("The player has {item}")
        .tooltip("Whether the player carries at least one of it.");
    inventory.condition<&InventoryObject::has_at_least>("has_at_least", {"item", "count"})
        .text("The player has at least {count} {item}")
        .tooltip("Whether the player carries that many of it, or more.")
        .param("count")
        .range(1.0, static_cast<double>(InventoryObject::most))
        .default_value(reflect::Value{std::int64_t{1}});
    inventory.getter<&InventoryObject::count_of>("count_of", {"item"})
        .text("The number of {item}")
        .tooltip("How many of it the player carries; 0 for none.");
    inventory.getter<&InventoryObject::kinds>("kinds")
        .text("The number of different things carried")
        .tooltip("How many different things the player carries.");
    inventory.action<&InventoryObject::open_shop>("open_shop", {"items"})
        .text("Open a shop selling {items}")
        .tooltip("The shop screen: the player buys these rows of data/items (separated by "
                 "commas: potion, ether) at their price, and sells what they carry, for gold.");
    inventory.condition<&InventoryObject::shopping>("shopping")
        .text("The player is in a shop")
        .tooltip("Whether the shop screen is open.");
    inventory.event<>("shop_closed")
        .text("When the player leaves a shop")
        .tooltip("The shop screen closed.");
}

} // namespace engine::game
