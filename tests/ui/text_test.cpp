// Text in the built-in pixel font: metrics, UTF-8, wrapping, and exact pixels on the GPU.

#include "engine/ui/text.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/gpu.h"

namespace render = engine::render;
namespace ui = engine::ui;

TEST_CASE("Glyphs are as wide as their pixels, plus one pixel of space") {
    const ui::PixelFont& font = ui::PixelFont::builtin();
    CHECK(font.line_height() == 9);
    CHECK(font.advance(U'H') == 6); // 5 wide
    CHECK(font.advance(U'i') == 4); // 3 wide
    CHECK(font.advance(U'l') == 4);
    CHECK(font.advance(U'.') == 3); // 2 wide
    CHECK(font.advance(U' ') == 4);
    CHECK(font.advance(U'é') == font.advance(U'?')); // not in the font: '?'
    CHECK(font.glyph(U'g').h == 8.0F);               // a descender row
    CHECK(ui::measure(font, "Hi") == 9);             // 5 + 1 + 3, no trailing space
    CHECK(ui::measure(font, "") == 0);
    // The atlas holds every glyph side by side, white on transparent.
    CHECK(font.atlas_height() == 8);
    CHECK(font.atlas().size() == std::size_t{font.atlas_width()} * 8 * 4);
}

TEST_CASE("UTF-8 is decoded, and malformed bytes do not break anything") {
    CHECK(ui::decode_utf8("caf\xC3\xA9") == std::u32string{U'c', U'a', U'f', U'é'});
    CHECK(ui::decode_utf8("\xF0\x9F\x98\x80") == std::u32string{U'\U0001F600'});
    CHECK(ui::decode_utf8("a\xC3") == std::u32string{U'a', U'�'}); // cut short
    CHECK(ui::decode_utf8("\xFF!") == std::u32string{U'�', U'!'});
}

TEST_CASE("Text wraps at spaces, keeps words whole, and breaks only words too long") {
    const ui::PixelFont& font = ui::PixelFont::builtin();
    // "Welcome," is 37 pixels; with " to" it would be 50.
    const int width = ui::measure(font, "Welcome,");
    CHECK(ui::wrap(font, "Welcome, to the village", width) ==
          std::vector<std::string>{"Welcome,", "to the", "village"});
    CHECK(ui::wrap(font, "One\nTwo", 200) == std::vector<std::string>{"One", "Two"});
    CHECK(ui::wrap(font, "", 50) == std::vector<std::string>{""});
    // A word wider than the line is broken where it overflows.
    const std::vector<std::string> broken = ui::wrap(font, "MMMMMMMMMM", 20);
    REQUIRE(broken.size() > 1);
    for (const std::string& line : broken) {
        CHECK(ui::measure(font, line) <= 20);
    }
    std::string joined;
    for (const std::string& line : broken) {
        joined += line;
    }
    CHECK(joined == "MMMMMMMMMM");
    // Every line of a long text fits.
    for (const std::string& line : ui::wrap(
             font, "The road east leads home, past the mill and over the old stone bridge.", 120)) {
        CHECK(ui::measure(font, line) <= 120);
    }
}

TEST_CASE("Text draws pixel-exact") {
    render::Device* device = test_support::gpu();
    if (device == nullptr) {
        return;
    }
    const ui::PixelFont& font = ui::PixelFont::builtin();
    const auto atlas =
        device->create_texture(font.atlas_width(), font.atlas_height(), font.atlas());
    const auto target = device->create_render_target(16, 10);
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(atlas != nullptr);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    sprites->begin(*target, render::Color{0, 0, 0, 255});
    ui::draw_text(*sprites, *atlas, font, "Hi", 1.0F, 1.0F, render::Color{255, 200, 0, 255});
    REQUIRE(sprites->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    const auto lit = [&pixels](int x, int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * 16 + static_cast<std::size_t>(x)) * 4;
        return pixels.at(i) == 255 && pixels.at(i + 1) == 200 && pixels.at(i + 2) == 0;
    };
    // H: both sides on every row, the bar on its fourth row.
    for (int row = 0; row < 7; ++row) {
        CHECK(lit(1, 1 + row));
        CHECK(lit(5, 1 + row));
    }
    CHECK_FALSE(lit(3, 1));
    CHECK(lit(3, 4));
    // i starts 6 pixels on: its dot, a gap, then its stem and foot.
    CHECK(lit(8, 1));
    CHECK_FALSE(lit(8, 2));
    CHECK(lit(8, 4));
    CHECK(lit(7, 7));       // the foot's left end
    CHECK_FALSE(lit(6, 4)); // the space between letters
    CHECK_FALSE(lit(0, 0)); // nothing outside
    CHECK_FALSE(lit(1, 8)); // nothing below the baseline for H
}

TEST_CASE("Rich text: tags become styles, pauses and speeds; the rest is kept as written") {
    const ui::RichText rich = ui::parse_rich(
        "A [wave]big[/wave] [color=gold]prize[/color]![pause=0.5] [speed=2]go[/speed] "
        "[[x] [nope] [color=puce]c");
    CHECK(rich.plain == "A big prize! go [x] [nope] [color=puce]c");
    REQUIRE(rich.styles.size() == rich.plain.size());
    CHECK_FALSE(rich.styles[0].wave);
    CHECK(rich.styles[2].wave); // b
    CHECK_FALSE(rich.styles[5].wave);
    CHECK(rich.styles[6].color.g == 210);  // p, in gold
    CHECK(rich.styles[11].color.g == 255); // !, white again
    REQUIRE(rich.pauses.size() == 1);
    CHECK(rich.pauses[0].first == 12); // before the space after "!"
    CHECK(rich.pauses[0].second == doctest::Approx(0.5F));
    CHECK(rich.styles[13].speed == doctest::Approx(2.0F)); // g
    CHECK(rich.styles[16].speed == doctest::Approx(1.0F));
    const ui::RichText hex = ui::parse_rich("[color=#102030]a");
    CHECK(hex.styles[0].color.r == 16);
    CHECK(hex.styles[0].color.b == 48);
    // Where wrapped lines start in the words.
    const std::string words = "one two three four";
    const std::vector<std::string> lines{"one two", "three", "four"};
    CHECK(ui::line_starts(words, lines) == std::vector<std::size_t>{0, 8, 14});
}

TEST_CASE("Fonts from a strip of letters: widths found, white where drawn, mismatches told") {
    // "A" 2 wide and "Bé" 3 wide (a UTF-8 letter), 4 high, between magenta columns.
    constexpr std::uint32_t width = 9;
    constexpr std::uint32_t height = 4;
    const std::array<std::uint8_t, 4> magenta{255, 0, 255, 255};
    const std::array<std::uint8_t, 4> ink{40, 40, 40, 255};
    const std::array<std::uint8_t, 4> clear{0, 0, 0, 0};
    // Columns: separator, A, A, separator, é, é, é, separator, separator.
    const std::array<int, width> column{0, 1, 1, 0, 2, 2, 2, 0, 0};
    std::vector<std::uint8_t> rgba;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const int kind = column.at(x);
            // A is solid; é has a see-through top row.
            const auto& p = kind == 0 ? magenta : (kind == 2 && y == 0) ? clear : ink;
            rgba.insert(rgba.end(), p.begin(), p.end());
        }
    }
    std::string error;
    const std::optional<ui::PixelFont> font =
        ui::PixelFont::from_strip(rgba, width, height, "A\xC3\xA9", error);
    REQUIRE(font.has_value());
    if (!font) {
        return;
    }
    CHECK(font->line_height() == 5);
    CHECK(font->advance(U'A') == 3);
    CHECK(font->advance(U'\u00e9') == 4);
    CHECK(font->advance(U' ') == 2); // no space letter: half the height
    CHECK(font->glyph(U'A').h == 4.0F);
    CHECK(ui::measure(*font, "A\xC3\xA9") == 6); // no gap after the last
    // White where drawn, so text colours tint it; the see-through row stays so.
    const ui::PixelFont& f = *font;
    const auto alpha = [&f](std::uint32_t x, std::uint32_t y) {
        return f.atlas().at(((std::size_t{y} * f.atlas_width()) + x) * 4 + 3);
    };
    CHECK(f.atlas().at(0) == 255);
    CHECK(alpha(0, 0) == 255);
    CHECK(alpha(3, 0) == 0);
    CHECK(alpha(3, 1) == 255);
    // A letter it lacks shows as nothing (it has no question mark).
    CHECK(font->advance(U'Z') == 1);

    // Two letters drawn, three named: told plainly.
    CHECK_FALSE(ui::PixelFont::from_strip(rgba, width, height, "ABC", error).has_value());
    CHECK(error.find("has 2 letters, but 3") != std::string::npos);
    CHECK_FALSE(ui::PixelFont::from_strip({}, 0, 0, "", error).has_value());
    CHECK(ui::PixelFont::ascii_characters().size() == 95);
}
