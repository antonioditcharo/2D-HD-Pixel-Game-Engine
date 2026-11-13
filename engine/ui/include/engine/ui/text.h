#pragma once

// Text in pixel fonts (ADR-010: bitmap pixel fonts are first-class). The engine has a
// built-in 5 x 8 font, so every game can show text without a font asset: its glyphs are
// drawn in this module's source, proportional (each as wide as its widest pixel), with a
// descender row for g, j, p, q and y.
//
// See docs/dev/ui.md.

#include "engine/render/render.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

class PixelFont {
public:
    /// The built-in font: printable ASCII; anything else shows as '?'.
    [[nodiscard]] static const PixelFont& builtin();

    /// How far the pen moves after `c`, in pixels (its width plus one pixel of space).
    [[nodiscard]] int advance(char32_t c) const noexcept;
    /// From one line's top to the next one's.
    [[nodiscard]] int line_height() const noexcept { return line_height_; }
    /// Where `c` is in the atlas image.
    [[nodiscard]] render::Rect glyph(char32_t c) const noexcept;

    /// The atlas: white glyphs on transparent, RGBA, rows top-down. Tint it to colour text.
    [[nodiscard]] std::uint32_t atlas_width() const noexcept { return atlas_width_; }
    [[nodiscard]] std::uint32_t atlas_height() const noexcept { return atlas_height_; }
    [[nodiscard]] const std::vector<std::uint8_t>& atlas() const noexcept { return atlas_; }

    PixelFont();

private:
    struct Glyph {
        int x = 0; ///< In the atlas.
        int width = 0;
    };
    [[nodiscard]] const Glyph& find(char32_t c) const noexcept;

    std::vector<Glyph> glyphs_; ///< For code points 32 to 126.
    std::uint32_t atlas_width_ = 0;
    std::uint32_t atlas_height_ = 0;
    std::vector<std::uint8_t> atlas_;
    int line_height_ = 9;
};

/// The code points of UTF-8 `text`; a malformed byte becomes U+FFFD (shown as '?').
[[nodiscard]] std::u32string decode_utf8(std::string_view text);

/// How wide `text` is on one line, in pixels.
[[nodiscard]] int measure(const PixelFont& font, std::string_view text);

/// `text` broken into lines no wider than `width` pixels: at spaces, keeping words whole,
/// and inside a word only when the word alone is too wide. Line breaks in the text are
/// kept.
[[nodiscard]] std::vector<std::string> wrap(const PixelFont& font, std::string_view text,
                                            int width);

/// Queues `text` on one line with its top-left at (x, y), in `color`. `atlas` is the font's
/// atlas uploaded with Device::create_texture().
/// `scale` draws each pixel of the font as a square of that many pixels (titles).
void draw_text(render::SpriteRenderer& sprites, const render::Texture& atlas, const PixelFont& font,
               std::string_view text, float x, float y, render::Color color, int scale = 1);

} // namespace engine::ui
