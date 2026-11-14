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

/// A picture every box is drawn with instead of the look's flat colours: messages,
/// menus, shops and battles (plan/02 A11, K1).
struct BoxFrame {
    const render::Texture* texture = nullptr; ///< None: the look's colours.
    float edge = 3.0F;                        ///< 9-slice: the pixels at each edge kept as drawn.
};

/// A box at `at`: `frame`'s picture stretched with crisp corners if it has one (see
/// nine_slice() in hud.h), else the look's border round its box colour. `opacity` (0 to 1)
/// fades it, as boxes opening do.
void draw_box(render::SpriteRenderer& sprites, const render::Texture& white, const UiTheme& ui,
              const BoxFrame* frame, render::Rect at, float opacity = 1.0F);

/// "#rrggbb", or "#rrggbbaa" when not opaque.
[[nodiscard]] std::string color_to_hex(render::Color color);
/// "#rrggbb" or "#rrggbbaa"; nothing for anything else.
[[nodiscard]] std::optional<render::Color> color_from_hex(std::string_view hex);

} // namespace engine::game
