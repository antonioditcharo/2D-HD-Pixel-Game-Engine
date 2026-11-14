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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::ui {

class PixelFont {
public:
    /// The built-in font: printable ASCII; anything else shows as '?'.
    [[nodiscard]] static const PixelFont& builtin();
    /// The printable ASCII characters in order, from the space to '~': the characters a
    /// font strip has when none are named.
    [[nodiscard]] static std::string_view ascii_characters() noexcept;
    /// A font from a strip of letters (RGBA, `width` x `height`, rows top-down): the
    /// letters side by side, each separated from the next by a column of one colour, the
    /// colour of the top-left pixel (as Love2D's and Construct's image fonts are). Widths
    /// come from the strip, so letters may differ in width. `characters` (UTF-8) names the
    /// letters in order; empty is ascii_characters(). Letters are white where drawn (any
    /// pixel neither see-through nor the separator), so text colours tint them. Nothing,
    /// with `error`, if the strip and the characters do not match.
    [[nodiscard]] static std::optional<PixelFont>
    from_strip(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height,
               std::string_view characters, std::string& error);

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

    std::unordered_map<char32_t, Glyph> glyphs_;
    std::uint32_t atlas_width_ = 0;
    std::uint32_t atlas_height_ = 0;
    std::vector<std::uint8_t> atlas_;
    int line_height_ = 9;
    int height_ = 8;        ///< Of every glyph.
    int space_advance_ = 4; ///< The pen's move for a space with no glyph of its own.
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

/// Text with effects, as messages carry them (plan/02 G4): the words without their tags,
/// and how each byte of them is shown.
///
/// Tags: [wave]...[/wave] (letters bob up and down), [shake]...[/shake] (they tremble),
/// [color=gold]...[/color] (red, gold, green, blue, grey, white, or #rrggbb),
/// [speed=2]...[/speed] (typed that many times faster; below 1, slower), and [pause=0.5]
/// (typing waits that many seconds). "[[" is a "["; anything else in brackets is left as
/// written.
struct RichText {
    struct Style {
        render::Color color{255, 255, 255, 255};
        bool wave = false;
        bool shake = false;
        float speed = 1.0F;
    };
    std::string plain;         ///< The words, without tags.
    std::vector<Style> styles; ///< One per byte of `plain`.
    /// Seconds typing waits before the byte at each index.
    std::vector<std::pair<std::size_t, float>> pauses;
};

[[nodiscard]] RichText parse_rich(std::string_view text);

/// Queues the bytes `begin` to `end` of `rich.plain` (one line of it, as wrap() gave it) at
/// (x, y), each glyph in its style, `seconds` into its showing (for waves and shakes), and
/// only those before byte `shown` (the typewriter).
void draw_rich(render::SpriteRenderer& sprites, const render::Texture& atlas, const PixelFont& font,
               const RichText& rich, std::size_t begin, std::size_t end, float x, float y,
               double seconds, std::size_t shown);

/// Where each of `lines` (wrap() of `plain`) starts in `plain`: they are its bytes in order,
/// with the spaces wrap() drops at breaks skipped.
[[nodiscard]] std::vector<std::size_t> line_starts(std::string_view plain,
                                                   const std::vector<std::string>& lines);

} // namespace engine::ui
