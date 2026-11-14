#pragma once

// On-screen displays without code (plan/02 K2): words and bars fixed to a place on the
// screen, not the map, such as the gold carried, the hero's HP, a score or a countdown.
// Their words name values in braces, filled each frame: {gold}, {hp:hero}, {var:score}.
//
// See docs/dev/ui.md (Screen Text and Screen Bar).

#include "engine/foundation/reflect.h"
#include "engine/world/world.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace engine::game {

/// Where on the screen something is fixed.
enum class ScreenAnchor : std::uint8_t {
    TopLeft,
    Top,
    TopRight,
    Left,
    Middle,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

/// "Screen Text": words fixed to the screen, with values in braces.
struct ScreenText {
    std::string text = "Gold: {gold}";
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0; ///< Pixels in from the anchor's sides.
    double y = 4.0;
    std::int64_t size = 1; ///< 1 or 2: twice as large.
    bool visible = true;
};

/// "Screen Bar": a bar filled as far as a value is towards its most, as HP is.
struct ScreenBar {
    std::string value = "{hp:hero}";    ///< What fills it...
    std::string most = "{max_hp:hero}"; ///< ...out of this.
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0;
    double y = 14.0;
    double width = 48.0; ///< Pixels.
    double height = 4.0;
    std::string color = "#dc4646";
    bool visible = true;
};

/// `text` with each {name} replaced by `value(name)`; a doubled brace ({{) stays one.
[[nodiscard]] std::string fill_values(std::string_view text,
                                      const std::function<std::string(std::string_view)>& value);

/// Registers the components and the "screen_anchor" choice list.
void register_hud(reflect::Registry& registry);
/// Makes them usable as components of `world`.
bool register_hud_components(world::World& world);

} // namespace engine::game
