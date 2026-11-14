#pragma once

// The look of a game's menus and messages (plan/02 K1, themes): the colours every ready-made
// box is drawn with, so games do not all look alike. Projects choose a preset or their own
// colours in the Project view.
//
// See docs/dev/menus.md (The look of menus and messages).

#include "engine/render/render.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace engine::game {

struct UiTheme {
    render::Color box{18, 20, 40, 240};          ///< Inside every box.
    render::Color border{230, 232, 240, 255};    ///< Their edges.
    render::Color text{255, 255, 255, 255};      ///< Words.
    render::Color dim{150, 155, 170, 255};       ///< Hints, and what cannot be chosen.
    render::Color highlight{255, 210, 122, 255}; ///< The choice the cursor is on, titles.
    render::Color plate{40, 30, 60, 245};        ///< Speakers' name plates.

    friend bool operator==(const UiTheme&, const UiTheme&) = default;
};

struct UiThemePreset {
    const char* id;
    const char* name;
    UiTheme theme;
};

/// Ready-made looks: Midnight (the default), Parchment, Forest, Crimson, Clean.
[[nodiscard]] const std::array<UiThemePreset, 5>& ui_theme_presets();

/// "#rrggbb", or "#rrggbbaa" when not opaque.
[[nodiscard]] std::string color_to_hex(render::Color color);
/// "#rrggbb" or "#rrggbbaa"; nothing for anything else.
[[nodiscard]] std::optional<render::Color> color_from_hex(std::string_view hex);

} // namespace engine::game
