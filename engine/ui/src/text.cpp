#include "engine/ui/text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace engine::ui {

namespace {

/// The built-in font, code points 32 to 126, eight rows each, top-down. Each row's five
/// low bits are its pixels, the highest of them the leftmost. Rows 0 to 6 stand on the
/// baseline; row 7 holds descenders. Designed for Sparxel.
constexpr std::array<std::array<std::uint8_t, 8>, 95> builtin_glyphs{{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00}, // !
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00}, // #
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00}, // $
    {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13, 0x00}, // %
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, // &
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00}, // (
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00}, // )
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00}, // *
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}, // ,
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x00}, // /
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08, 0x00}, // ;
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00}, // <
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00}, // =
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00}, // >
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00}, // ?
    {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E, 0x00}, // @
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00}, // C
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00}, // M
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00}, // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A, 0x00}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00}, // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00}, // Z
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00}, // [
    {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01, 0x00}, // backslash
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00}, // ]
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00}, // _
    {0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00}, // a
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E, 0x00}, // b
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00}, // c
    {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F, 0x00}, // d
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00}, // e
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00}, // f
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, // g
    {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00}, // h
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00}, // i
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x12, 0x0C}, // j
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}, // k
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, // l
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00}, // m
    {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00}, // n
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00}, // o
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10}, // p
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01}, // q
    {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00}, // r
    {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00}, // s
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06, 0x00}, // t
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00}, // u
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, // v
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00}, // w
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, // x
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x0E}, // y
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00}, // z
    {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02, 0x00}, // {
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, // |
    {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 0x00}, // }
    {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00}, // ~
}};

constexpr int cell_width = 5;
constexpr int cell_height = 8;
constexpr int space_advance = 4;

} // namespace

PixelFont::PixelFont() {
    // Glyphs side by side in one row, each cropped to its pixels, a pixel apart.
    std::vector<std::array<int, 2>> extents; // first and last column of each glyph
    int total = 0;
    for (const auto& rows : builtin_glyphs) {
        int first = cell_width;
        int last = -1;
        for (const std::uint8_t row : rows) {
            for (int column = 0; column < cell_width; ++column) {
                if ((row >> (cell_width - 1 - column)) & 1U) {
                    first = std::min(first, column);
                    last = std::max(last, column);
                }
            }
        }
        extents.push_back({first, last});
        total += (last >= first ? last - first + 1 : 0) + 1;
    }
    atlas_width_ = static_cast<std::uint32_t>(total);
    atlas_height_ = cell_height;
    atlas_.assign(std::size_t{atlas_width_} * atlas_height_ * 4, 0);
    int x = 0;
    for (std::size_t i = 0; i < builtin_glyphs.size(); ++i) {
        const auto [first, last] = extents[i];
        Glyph glyph;
        glyph.x = x;
        glyph.width = last >= first ? last - first + 1 : 0;
        for (int row = 0; row < cell_height; ++row) {
            for (int column = 0; column < glyph.width; ++column) {
                const int source = first + column;
                if ((builtin_glyphs[i][static_cast<std::size_t>(row)] >>
                     (cell_width - 1 - source)) &
                    1U) {
                    const std::size_t at = ((static_cast<std::size_t>(row) * atlas_width_) +
                                            static_cast<std::size_t>(x + column)) *
                                           4;
                    atlas_[at] = 255;
                    atlas_[at + 1] = 255;
                    atlas_[at + 2] = 255;
                    atlas_[at + 3] = 255;
                }
            }
        }
        glyphs_.emplace(static_cast<char32_t>(32 + i), glyph);
        x += glyph.width + 1;
    }
    height_ = cell_height;
    space_advance_ = space_advance;
}

std::string_view PixelFont::ascii_characters() noexcept {
    return " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
           "abcdefghijklmnopqrstuvwxyz{|}~";
}

std::optional<PixelFont> PixelFont::from_strip(const std::vector<std::uint8_t>& rgba,
                                               std::uint32_t width, std::uint32_t height,
                                               std::string_view characters, std::string& error) {
    if (width == 0 || height == 0 || rgba.size() != std::size_t{width} * height * 4) {
        error = "The font's picture is empty.";
        return std::nullopt;
    }
    const std::u32string letters =
        decode_utf8(characters.empty() ? ascii_characters() : characters);
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t at = ((std::size_t{y} * width) + x) * 4;
        return std::array<std::uint8_t, 4>{rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
    };
    const std::array<std::uint8_t, 4> separator = pixel(0, 0);
    // The runs of columns between separator columns: the letters.
    std::vector<std::array<std::uint32_t, 2>> runs; // first column, width
    std::uint32_t start = 0;
    bool inside = false;
    for (std::uint32_t x = 0; x <= width; ++x) {
        bool apart = x == width;
        if (!apart) {
            apart = true;
            for (std::uint32_t y = 0; y < height && apart; ++y) {
                apart = pixel(x, y) == separator;
            }
        }
        if (!apart && !inside) {
            start = x;
            inside = true;
        } else if (apart && inside) {
            runs.push_back({start, x - start});
            inside = false;
        }
    }
    if (runs.size() != letters.size()) {
        error = "The font's picture has " + std::to_string(runs.size()) + " letters, but " +
                std::to_string(letters.size()) +
                " characters are named. Letters are separated by columns of the top-left "
                "pixel's colour.";
        return std::nullopt;
    }
    PixelFont font;
    font.glyphs_.clear();
    std::uint32_t total = 0;
    for (const auto& run : runs) {
        total += run[1] + 1;
    }
    font.atlas_width_ = total;
    font.atlas_height_ = height;
    font.atlas_.assign(std::size_t{total} * height * 4, 0);
    std::uint32_t x = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto [first, glyph_width] = runs[i];
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t column = 0; column < glyph_width; ++column) {
                const std::array<std::uint8_t, 4> p = pixel(first + column, y);
                if (p[3] == 0 || p == separator) {
                    continue;
                }
                const std::size_t at = ((std::size_t{y} * total) + x + column) * 4;
                font.atlas_[at] = 255;
                font.atlas_[at + 1] = 255;
                font.atlas_[at + 2] = 255;
                font.atlas_[at + 3] = p[3];
            }
        }
        font.glyphs_[letters[i]] = Glyph{static_cast<int>(x), static_cast<int>(glyph_width)};
        x += glyph_width + 1;
    }
    font.height_ = static_cast<int>(height);
    font.line_height_ = static_cast<int>(height) + 1;
    font.space_advance_ = std::max(2, static_cast<int>(height) / 2);
    return font;
}

const PixelFont& PixelFont::builtin() {
    static const PixelFont font;
    return font;
}

const PixelFont::Glyph& PixelFont::find(char32_t c) const noexcept {
    static const Glyph none{};
    if (const auto found = glyphs_.find(c); found != glyphs_.end()) {
        return found->second;
    }
    // A letter the font lacks: its question mark (or nothing, if it has none).
    const auto unknown = glyphs_.find(U'?');
    return unknown != glyphs_.end() ? unknown->second : none;
}

int PixelFont::advance(char32_t c) const noexcept {
    if (c == U' ' && !glyphs_.contains(U' ')) {
        return space_advance_;
    }
    return c == U' ' ? std::max(find(c).width + 1, space_advance_) : find(c).width + 1;
}

render::Rect PixelFont::glyph(char32_t c) const noexcept {
    const Glyph& g = find(c);
    return render::Rect{static_cast<float>(g.x), 0.0F, static_cast<float>(g.width),
                        static_cast<float>(height_)};
}

std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        int length = 0;
        char32_t value = 0;
        if (byte < 0x80) {
            length = 1;
            value = byte;
        } else if ((byte & 0xE0U) == 0xC0) {
            length = 2;
            value = byte & 0x1FU;
        } else if ((byte & 0xF0U) == 0xE0) {
            length = 3;
            value = byte & 0x0FU;
        } else if ((byte & 0xF8U) == 0xF0) {
            length = 4;
            value = byte & 0x07U;
        }
        bool valid = length > 0 && i + static_cast<std::size_t>(length) <= text.size();
        for (int k = 1; valid && k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            valid = (next & 0xC0U) == 0x80;
            value = (value << 6U) | (next & 0x3FU);
        }
        if (!valid) {
            out.push_back(U'\uFFFD');
            ++i;
            continue;
        }
        out.push_back(value);
        i += static_cast<std::size_t>(length);
    }
    return out;
}

int measure(const PixelFont& font, std::string_view text) {
    int width = 0;
    for (const char32_t c : decode_utf8(text)) {
        width += font.advance(c);
    }
    return width > 0 ? width - 1 : 0; // no space after the last glyph
}

std::vector<std::string> wrap(const PixelFont& font, std::string_view text, int width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        const std::string_view paragraph = text.substr(start, end - start);
        std::string line;
        std::size_t at = 0;
        while (at < paragraph.size()) {
            const std::size_t space = std::min(paragraph.find(' ', at), paragraph.size());
            const std::string_view word = paragraph.substr(at, space - at);
            std::string candidate =
                line.empty() ? std::string(word) : line + " " + std::string(word);
            if (measure(font, candidate) <= width || line.empty()) {
                if (measure(font, candidate) > width) {
                    // A word too wide for any line: break it where it overflows.
                    std::string piece;
                    for (const char c : word) {
                        if (!piece.empty() && measure(font, piece + c) > width &&
                            (static_cast<unsigned char>(c) & 0xC0U) != 0x80) {
                            lines.push_back(piece);
                            piece.clear();
                        }
                        piece += c;
                    }
                    candidate = piece;
                }
                line = candidate;
                at = space + 1;
            } else {
                lines.push_back(line);
                line.clear();
            }
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

void draw_text(render::SpriteRenderer& sprites, const render::Texture& atlas, const PixelFont& font,
               std::string_view text, float x, float y, render::Color color, int scale) {
    const auto times = static_cast<float>(std::max(scale, 1));
    float pen = x;
    for (const char32_t c : decode_utf8(text)) {
        if (c != U' ') {
            const render::Rect source = font.glyph(c);
            sprites.draw(atlas, source, render::Rect{pen, y, source.w * times, source.h * times},
                         color);
        }
        pen += static_cast<float>(font.advance(c)) * times;
    }
}

namespace {

std::optional<render::Color> named_colour(std::string_view name) {
    struct Named {
        std::string_view name;
        render::Color colour;
    };
    static constexpr std::array<Named, 6> named{{{"red", {240, 90, 90, 255}},
                                                 {"gold", {255, 210, 122, 255}},
                                                 {"green", {130, 220, 120, 255}},
                                                 {"blue", {120, 170, 255, 255}},
                                                 {"grey", {160, 160, 170, 255}},
                                                 {"white", {255, 255, 255, 255}}}};
    for (const Named& n : named) {
        if (n.name == name) {
            return n.colour;
        }
    }
    if (name.size() == 7 && name.front() == '#') {
        const auto hex = [&](std::size_t i) -> int {
            int v = 0;
            for (std::size_t k = i; k < i + 2; ++k) {
                const char c = name[k];
                v *= 16;
                if (c >= '0' && c <= '9') {
                    v += c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    v += c - 'a' + 10;
                } else if (c >= 'A' && c <= 'F') {
                    v += c - 'A' + 10;
                } else {
                    return -1;
                }
            }
            return v;
        };
        const int r = hex(1);
        const int g = hex(3);
        const int b = hex(5);
        if (r >= 0 && g >= 0 && b >= 0) {
            return render::Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                 static_cast<std::uint8_t>(b), 255};
        }
    }
    return std::nullopt;
}

std::optional<float> number_of(std::string_view text) {
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

RichText parse_rich(std::string_view text) {
    RichText out;
    std::vector<RichText::Style> stack{RichText::Style{}};
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[' && i + 1 < text.size() && text[i + 1] == '[') {
            out.plain += '[';
            out.styles.push_back(stack.back());
            i += 2;
            continue;
        }
        if (text[i] == '[') {
            const std::size_t close = text.find(']', i);
            if (close != std::string_view::npos) {
                const std::string_view tag = text.substr(i + 1, close - i - 1);
                const std::size_t eq = tag.find('=');
                const std::string_view name = tag.substr(0, eq);
                const std::string_view value =
                    eq == std::string_view::npos ? std::string_view{} : tag.substr(eq + 1);
                bool known = true;
                RichText::Style next = stack.back();
                if (name == "wave") {
                    next.wave = true;
                    stack.push_back(next);
                } else if (name == "shake") {
                    next.shake = true;
                    stack.push_back(next);
                } else if (name == "color" || name == "colour") {
                    if (const std::optional<render::Color> c = named_colour(value)) {
                        next.color = *c;
                        stack.push_back(next);
                    } else {
                        known = false;
                    }
                } else if (name == "speed") {
                    if (const std::optional<float> v = number_of(value); v && *v > 0.0F) {
                        next.speed = std::clamp(*v, 0.05F, 20.0F);
                        stack.push_back(next);
                    } else {
                        known = false;
                    }
                } else if (name == "pause") {
                    if (const std::optional<float> v = number_of(value); v && *v >= 0.0F) {
                        out.pauses.emplace_back(out.plain.size(), std::min(*v, 10.0F));
                    } else {
                        known = false;
                    }
                } else if (!name.empty() && name.front() == '/' &&
                           (name == "/wave" || name == "/shake" || name == "/color" ||
                            name == "/colour" || name == "/speed")) {
                    if (stack.size() > 1) {
                        stack.pop_back();
                    }
                } else {
                    known = false;
                }
                if (known) {
                    i = close + 1;
                    continue;
                }
            }
        }
        out.plain += text[i];
        out.styles.push_back(stack.back());
        ++i;
    }
    return out;
}

std::vector<std::size_t> line_starts(std::string_view plain,
                                     const std::vector<std::string>& lines) {
    // Lines are pieces of the text in order, with the spaces and line breaks between them
    // dropped: each is found from where the one before ended.
    std::vector<std::size_t> out;
    std::size_t at = 0;
    for (const std::string& line : lines) {
        std::size_t found = line.empty() ? at : plain.find(line, at);
        if (found == std::string_view::npos) {
            found = at;
        }
        out.push_back(found);
        at = std::min(plain.size(), found + line.size());
    }
    return out;
}

void draw_rich(render::SpriteRenderer& sprites, const render::Texture& atlas, const PixelFont& font,
               const RichText& rich, std::size_t begin, std::size_t end, float x, float y,
               double seconds, std::size_t shown) {
    float pen = x;
    std::size_t at = begin;
    end = std::min(end, rich.plain.size());
    while (at < end && at < shown) {
        // One UTF-8 character.
        std::size_t length = 1;
        const auto lead = static_cast<unsigned char>(rich.plain[at]);
        if (lead >= 0xF0U) {
            length = 4;
        } else if (lead >= 0xE0U) {
            length = 3;
        } else if (lead >= 0xC0U) {
            length = 2;
        }
        const std::u32string code = decode_utf8(std::string_view(rich.plain).substr(at, length));
        const char32_t c = code.empty() ? U'?' : code.front();
        const RichText::Style& style = rich.styles[at];
        float dy = 0.0F;
        float dx = 0.0F;
        if (style.wave) {
            dy = std::round(1.5F * static_cast<float>(std::sin((seconds * 8.0) +
                                                               (static_cast<double>(at) * 0.7))));
        }
        if (style.shake) {
            const auto tick = static_cast<std::uint64_t>(seconds * 30.0);
            const std::uint64_t h = (tick * 0x9E3779B97F4A7C15ULL) ^ (at * 0xC2B2AE3D27D4EB4FULL);
            dx = static_cast<float>(static_cast<int>(h % 3U) - 1);
            dy += static_cast<float>(static_cast<int>((h >> 8U) % 3U) - 1);
        }
        if (c != U' ') {
            const render::Rect source = font.glyph(c);
            sprites.draw(atlas, source, render::Rect{pen + dx, y + dy, source.w, source.h},
                         style.color);
        }
        pen += static_cast<float>(font.advance(c));
        at += length;
    }
}

} // namespace engine::ui
