// Golden-pixel tests for the renderer. They need a GPU; lavapipe (Mesa's software Vulkan)
// is enough. Without one they are skipped with a message, unless the environment variable
// SPARXEL_REQUIRE_GPU=1 is set (as Linux CI does), which turns a missing GPU into a failure.

#include "engine/render/render.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "support/gpu.h"

namespace engine::render {
// Lets failed checks print colours.
std::ostream& operator<<(std::ostream& out, Color c) {
    return out << "(" << int{c.r} << ", " << int{c.g} << ", " << int{c.b} << ", " << int{c.a}
               << ")";
}
} // namespace engine::render

namespace render = engine::render;
using render::Color;
using render::Rect;

namespace {

using test_support::gpu;

Color pixel(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t x,
            std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * width + x) * 4;
    return Color{pixels.at(i), pixels.at(i + 1), pixels.at(i + 2), pixels.at(i + 3)};
}

/// A 2 x 2 texture: red, green / blue, white.
std::unique_ptr<render::Texture> checker(render::Device& device) {
    const std::vector<std::uint8_t> rgba{
        255, 0, 0,   255, 0,   255, 0,   255, //
        0,   0, 255, 255, 255, 255, 255, 255,
    };
    return device.create_texture(2, 2, rgba);
}

} // namespace

TEST_CASE("A device starts headless and clears a target") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    MESSAGE("GPU driver: " << device->driver_name());
    const std::unique_ptr<render::RenderTarget> target = device->create_render_target(8, 4);
    REQUIRE(target != nullptr);
    const std::unique_ptr<render::SpriteRenderer> sprites = render::SpriteRenderer::create(*device);
    REQUIRE(sprites != nullptr);
    sprites->begin(*target, Color{10, 20, 30, 255});
    REQUIRE(sprites->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    REQUIRE(pixels.size() == 8 * 4 * 4);
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            CHECK(pixel(pixels, 8, x, y) == Color{10, 20, 30, 255});
        }
    }
    CHECK(sprites->last_stats().draw_calls == 0);
}

TEST_CASE("Sprites land on exact pixels, the right way up, with nearest sampling") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto sprites = render::SpriteRenderer::create(*device);
    const auto texture = checker(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    REQUIRE(texture != nullptr);

    // The whole 2 x 2 texture scaled to 8 x 8 at (2, 4): each texel becomes a 4 x 4 block.
    sprites->begin(*target, Color{0, 0, 0, 255});
    sprites->draw(*texture, Rect{0, 0, 2, 2}, Rect{2, 4, 8, 8});
    REQUIRE(sprites->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    REQUIRE(pixels.size() == 16 * 16 * 4);

    const Color black{0, 0, 0, 255};
    const Color red{255, 0, 0, 255};
    const Color green{0, 255, 0, 255};
    const Color blue{0, 0, 255, 255};
    const Color white{255, 255, 255, 255};
    // Top-left texel (red) is at the top-left: y grows downwards.
    CHECK(pixel(pixels, 16, 2, 4) == red);
    CHECK(pixel(pixels, 16, 5, 7) == red);
    CHECK(pixel(pixels, 16, 6, 4) == green);
    CHECK(pixel(pixels, 16, 9, 7) == green);
    CHECK(pixel(pixels, 16, 2, 8) == blue);
    CHECK(pixel(pixels, 16, 5, 11) == blue);
    CHECK(pixel(pixels, 16, 9, 11) == white);
    // Exactly 8 x 8: the pixels just outside are untouched.
    CHECK(pixel(pixels, 16, 1, 4) == black);
    CHECK(pixel(pixels, 16, 10, 4) == black);
    CHECK(pixel(pixels, 16, 2, 3) == black);
    CHECK(pixel(pixels, 16, 2, 12) == black);
    // Every covered pixel is one of the four texel colours: no filtering blur.
    for (std::uint32_t y = 4; y < 12; ++y) {
        for (std::uint32_t x = 2; x < 10; ++x) {
            const Color c = pixel(pixels, 16, x, y);
            CHECK((c == red || c == green || c == blue || c == white));
        }
    }
}

TEST_CASE("Source rectangles, tints, alpha blending and draw order") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    const auto texture = checker(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    REQUIRE(texture != nullptr);

    sprites->begin(*target, Color{0, 0, 0, 255});
    // Only the white texel, tinted orange, filling the target.
    sprites->draw(*texture, Rect{1, 1, 1, 1}, Rect{0, 0, 8, 8}, Color{255, 128, 0, 255});
    // Then the red texel at half opacity over the left half: drawn later, so on top.
    sprites->draw(*texture, Rect{0, 0, 1, 1}, Rect{0, 0, 4, 8}, Color{255, 255, 255, 128});
    REQUIRE(sprites->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    REQUIRE(pixels.size() == 8 * 8 * 4);

    CHECK(pixel(pixels, 8, 6, 3) == Color{255, 128, 0, 255});
    // Half red over orange: green 128 * (1 - 128/255) = 63.75. Allow one step of rounding.
    const Color blended = pixel(pixels, 8, 1, 3);
    CHECK(blended.r == 255);
    CHECK(blended.g >= 63);
    CHECK(blended.g <= 64);
    CHECK(blended.b == 0);
}

TEST_CASE("begin_over() draws over what the target holds") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(8, 8);
    const auto sprites = render::SpriteRenderer::create(*device);
    const auto texture = checker(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);
    REQUIRE(texture != nullptr);
    sprites->begin(*target, Color{10, 20, 30, 255});
    sprites->draw(*texture, Rect{0, 0, 1, 1}, Rect{0, 0, 4, 4}); // red, top-left
    REQUIRE(sprites->end());
    sprites->begin_over(*target);
    sprites->draw(*texture, Rect{0, 1, 1, 1}, Rect{4, 4, 4, 4}); // blue, bottom-right
    REQUIRE(sprites->end());
    std::vector<std::uint8_t> pixels = target->read_pixels();
    CHECK(pixel(pixels, 8, 1, 1) == Color{255, 0, 0, 255});  // kept
    CHECK(pixel(pixels, 8, 6, 1) == Color{10, 20, 30, 255}); // kept
    CHECK(pixel(pixels, 8, 6, 6) == Color{0, 0, 255, 255});  // drawn over
    // And begin() after it clears again.
    sprites->begin(*target, Color{0, 0, 0, 255});
    REQUIRE(sprites->end());
    pixels = target->read_pixels();
    CHECK(pixel(pixels, 8, 1, 1) == Color{0, 0, 0, 255});
}

TEST_CASE("Sprites sharing a texture are batched into one draw call") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(64, 64);
    const auto sprites = render::SpriteRenderer::create(*device);
    const auto a = checker(*device);
    const auto b = checker(*device);
    REQUIRE(target != nullptr);
    REQUIRE(sprites != nullptr);

    sprites->begin(*target, Color{});
    for (int i = 0; i < 1000; ++i) {
        const int column = i % 60;
        const int row = i / 60;
        sprites->draw(*a, Rect{0, 0, 2, 2},
                      Rect{static_cast<float>(column), static_cast<float>(row), 2, 2});
    }
    REQUIRE(sprites->end());
    CHECK(sprites->last_stats().sprites == 1000);
    CHECK(sprites->last_stats().draw_calls == 1);

    // Alternating textures need a draw call per run, to keep the order.
    sprites->begin(*target, Color{});
    sprites->draw(*a, Rect{0, 0, 2, 2}, Rect{0, 0, 2, 2});
    sprites->draw(*a, Rect{0, 0, 2, 2}, Rect{2, 0, 2, 2});
    sprites->draw(*b, Rect{0, 0, 2, 2}, Rect{4, 0, 2, 2});
    sprites->draw(*a, Rect{0, 0, 2, 2}, Rect{6, 0, 2, 2});
    REQUIRE(sprites->end());
    CHECK(sprites->last_stats().draw_calls == 3);

    // The renderer keeps working across frames of different sizes.
    sprites->begin(*target, Color{1, 2, 3, 255});
    sprites->draw(*b, Rect{1, 1, 1, 1}, Rect{0, 0, 1, 1});
    REQUIRE(sprites->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    CHECK(pixel(pixels, 64, 0, 0) == Color{255, 255, 255, 255});
    CHECK(pixel(pixels, 64, 1, 0) == Color{1, 2, 3, 255});
}

TEST_CASE("Invalid requests are refused with a message") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    CHECK(device->create_render_target(0, 4) == nullptr);
    CHECK_FALSE(device->last_error().empty());
    const std::vector<std::uint8_t> too_few(12, 0);
    CHECK(device->create_texture(2, 2, too_few) == nullptr);
    CHECK(device->last_error() ==
          "A texture needs a size above zero and exactly width x height x 4 bytes of pixels.");
    const auto sprites = render::SpriteRenderer::create(*device);
    REQUIRE(sprites != nullptr);
    CHECK_FALSE(sprites->end()); // end() without begin()
}

TEST_CASE("Images are placed in windows pixel-perfect") {
    // 320 x 180 in 1920 x 1080: exactly 6x, no letterbox.
    CHECK(render::fit_integer(320, 180, 1920, 1080) == render::Placement{6, 0, 0, 1920, 1080});
    // In 1366 x 768: 4x (1280 x 720), centred.
    CHECK(render::fit_integer(320, 180, 1366, 768) == render::Placement{4, 43, 24, 1280, 720});
    // Smaller than the image: 1x, centred and cropped.
    CHECK(render::fit_integer(320, 180, 200, 100) == render::Placement{1, -60, -40, 320, 180});
    // A tall window: the width decides.
    CHECK(render::fit_integer(320, 180, 700, 2000).scale == 2);
}

TEST_CASE("A window presents a target") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    if (device->driver_name() != "vulkan") {
        // The shared test device is headless, which can show windows only with Vulkan.
        CHECK(device->create_window("Sparxel test", 100, 60) == nullptr);
        CHECK(device->last_error() ==
              "A headless device can show windows only with Vulkan; this one uses " +
                  device->driver_name() + ".");
        MESSAGE("Presenting to a window is tested with Vulkan only.");
        return;
    }
    const auto window = device->create_window("Sparxel test", 100, 60);
    INFO(device->last_error());
    REQUIRE(window != nullptr);
    CHECK(window->width() == 100);
    CHECK(window->height() == 60);
    const auto small = device->create_render_target(32, 18);
    const auto large = device->create_render_target(400, 300);
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(window->present(*small, render::Color{0, 0, 0, 255}));
        CHECK(window->present(*large, render::Color{0, 0, 0, 255})); // cropped
    }
    CHECK(device->create_window("No size", 0, 10) == nullptr);
}
