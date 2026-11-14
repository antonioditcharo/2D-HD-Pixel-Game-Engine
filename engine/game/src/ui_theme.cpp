#include "engine/game/ui_theme.h"

#include "engine/game/hud.h"

#include <algorithm>

namespace engine::game {

const std::array<UiThemePreset, 5>& ui_theme_presets() {
    static const std::array<UiThemePreset, 5> presets{{
        {"midnight", "Midnight", UiTheme{}},
        {"parchment", "Parchment",
         UiTheme{{238, 222, 186, 245},
                 {110, 76, 42, 255},
                 {62, 40, 22, 255},
                 {140, 112, 80, 255},
                 {170, 60, 30, 255},
                 {200, 170, 120, 250}}},
        {"forest", "Forest",
         UiTheme{{16, 40, 28, 240},
                 {150, 200, 120, 255},
                 {235, 245, 225, 255},
                 {130, 160, 120, 255},
                 {250, 230, 120, 255},
                 {30, 70, 40, 245}}},
        {"crimson", "Crimson",
         UiTheme{{40, 8, 14, 240},
                 {220, 170, 90, 255},
                 {250, 235, 220, 255},
                 {170, 130, 120, 255},
                 {255, 200, 90, 255},
                 {90, 20, 30, 245}}},
        {"clean", "Clean",
         UiTheme{{245, 246, 250, 245},
                 {40, 44, 60, 255},
                 {30, 32, 44, 255},
                 {120, 124, 140, 255},
                 {40, 110, 220, 255},
                 {220, 226, 240, 250}}},
    }};
    return presets;
}

std::string color_to_hex(render::Color color) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "#";
    const auto put = [&out](std::uint8_t v) {
        out += digits[v >> 4U];
        out += digits[v & 0xFU];
    };
    put(color.r);
    put(color.g);
    put(color.b);
    if (color.a != 255) {
        put(color.a);
    }
    return out;
}

std::optional<render::Color> color_from_hex(std::string_view hex) {
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') {
        return std::nullopt;
    }
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    std::array<std::uint8_t, 4> parts{0, 0, 0, 255};
    for (std::size_t i = 0; i * 2 + 1 < hex.size(); ++i) {
        const int high = digit(hex[1 + (i * 2)]);
        const int low = digit(hex[2 + (i * 2)]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        parts.at(i) = static_cast<std::uint8_t>((high * 16) + low);
    }
    return render::Color{parts[0], parts[1], parts[2], parts[3]};
}

void draw_box(render::SpriteRenderer& sprites, const render::Texture& white, const UiTheme& ui,
              const BoxFrame* frame, render::Rect at, float opacity) {
    const float keep = std::clamp(opacity, 0.0F, 1.0F);
    const auto faded = [keep](render::Color c) {
        c.a = static_cast<std::uint8_t>(static_cast<float>(c.a) * keep);
        return c;
    };
    if (frame != nullptr && frame->texture != nullptr) {
        const auto tw = static_cast<float>(frame->texture->width());
        const auto th = static_cast<float>(frame->texture->height());
        for (const SlicePiece& piece : nine_slice(tw, th, at, frame->edge)) {
            sprites.draw(*frame->texture, piece.source, piece.destination,
                         faded(render::Color{255, 255, 255, 255}));
        }
        return;
    }
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    sprites.draw(white, one, at, faded(ui.border));
    sprites.draw(white, one,
                 render::Rect{at.x + 1.0F, at.y + 1.0F, std::max(0.0F, at.w - 2.0F),
                              std::max(0.0F, at.h - 2.0F)},
                 faded(ui.box));
}

} // namespace engine::game
