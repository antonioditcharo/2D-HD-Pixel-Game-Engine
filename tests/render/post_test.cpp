// Post-processing: neutral settings copy a frame exactly; tilt-shift blurs outside a sharp
// band; bloom spreads a glow; grading and the vignette do what they say. GPU tests need
// lavapipe or a real GPU (see render_test.cpp).

#include "engine/render/render.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "support/gpu.h"

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

/// A frame of `width` x `height` painted by `paint`, and a second target to process into.
struct Frame {
    Frame(render::Device& device, std::uint32_t width, std::uint32_t height,
          const std::function<Color(std::uint32_t, std::uint32_t)>& paint)
        : w(width), h(height) {
        std::vector<std::uint8_t> rgba;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const Color c = paint(x, y);
                rgba.insert(rgba.end(), {c.r, c.g, c.b, c.a});
            }
        }
        const auto image = device.create_texture(w, h, rgba);
        source = device.create_render_target(w, h);
        destination = device.create_render_target(w, h);
        const auto sprites = render::SpriteRenderer::create(device);
        post = render::PostProcessor::create(device);
        INFO(device.last_error());
        REQUIRE(image != nullptr);
        REQUIRE(source != nullptr);
        REQUIRE(destination != nullptr);
        REQUIRE(sprites != nullptr);
        REQUIRE(post != nullptr);
        sprites->begin(*source, Color{0, 0, 0, 255});
        sprites->draw(*image, Rect{0, 0, static_cast<float>(w), static_cast<float>(h)},
                      Rect{0, 0, static_cast<float>(w), static_cast<float>(h)});
        REQUIRE(sprites->end());
        original = source->read_pixels();
    }

    std::vector<std::uint8_t> apply(const render::Look& look) {
        REQUIRE(post->apply(*source, *destination, look));
        return destination->read_pixels();
    }

    std::uint32_t w;
    std::uint32_t h;
    std::unique_ptr<render::RenderTarget> source;
    std::unique_ptr<render::RenderTarget> destination;
    std::unique_ptr<render::PostProcessor> post;
    std::vector<std::uint8_t> original;
};

} // namespace

TEST_CASE("A Look is neutral until it changes something") {
    render::Look look;
    CHECK(look.neutral());
    look.tilt_shift = true;
    CHECK_FALSE(look.neutral());
    look.blur = 0.0F; // tilt-shift with no blur changes nothing
    CHECK(look.neutral());
    for (const auto& change : std::vector<std::function<void(render::Look&)>>{
             [](render::Look& l) { l.bloom = 0.5F; }, [](render::Look& l) { l.exposure = 1.1F; },
             [](render::Look& l) { l.contrast = 0.9F; },
             [](render::Look& l) { l.saturation = 1.2F; },
             [](render::Look& l) { l.vignette = 0.3F; }}) {
        render::Look changed;
        change(changed);
        CHECK_FALSE(changed.neutral());
    }
}

TEST_CASE("A neutral Look copies the frame exactly") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Frame frame(*device, 37, 23, [](std::uint32_t x, std::uint32_t y) {
        return Color{static_cast<std::uint8_t>(x * 7), static_cast<std::uint8_t>(y * 11),
                     static_cast<std::uint8_t>((x * y) % 256), 255};
    });
    CHECK(frame.apply(render::Look{}) == frame.original);
    // Tilt-shift on, but with no blur, is still an exact copy.
    render::Look no_blur;
    no_blur.tilt_shift = true;
    no_blur.blur = 0.0F;
    CHECK(frame.apply(no_blur) == frame.original);
}

TEST_CASE("Tilt-shift keeps a band sharp and blurs above and below it") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    // Black and white columns, one pixel each: blur turns them grey.
    Frame frame(*device, 32, 32, [](std::uint32_t x, std::uint32_t) {
        return x % 2 == 0 ? Color{0, 0, 0, 255} : Color{255, 255, 255, 255};
    });
    render::Look look;
    look.tilt_shift = true;
    look.focus_center = 0.5F;
    look.focus_height = 0.25F; // rows 12 to 19 are sharp (centres within 0.125 of the middle)
    look.blur = 4.0F;
    look.blur_ramp = 0.1F;
    const std::vector<std::uint8_t> pixels = frame.apply(look);
    for (std::uint32_t y = 12; y < 20; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            CHECK(pixel(pixels, 32, x, y) == pixel(frame.original, 32, x, y));
        }
    }
    // Far from the band, black and white columns both come out mid-grey.
    for (const std::uint32_t y : {0U, 31U}) {
        for (const std::uint32_t x : {10U, 11U}) {
            const Color c = pixel(pixels, 32, x, y);
            CAPTURE(x);
            CAPTURE(y);
            CHECK(std::abs(int{c.r} - 128) < 20);
            CHECK(c.r == c.g);
            CHECK(c.g == c.b);
        }
    }
    // Just outside the band, blurred a little: still darker and lighter, but not 0 and 255.
    const Color dark = pixel(pixels, 32, 10, 10);
    const Color light = pixel(pixels, 32, 11, 10);
    CHECK(dark.r > 0);
    CHECK(light.r < 255);
    CHECK(dark.r < light.r);
}

TEST_CASE("Bloom spreads a glow around bright pixels only") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    // A white dot, and a dim grey one below the threshold.
    Frame frame(*device, 32, 32, [](std::uint32_t x, std::uint32_t y) {
        if (x == 8 && y == 8) {
            return Color{255, 255, 255, 255};
        }
        if (x == 24 && y == 24) {
            return Color{100, 100, 100, 255};
        }
        return Color{0, 0, 0, 255};
    });
    render::Look look;
    look.bloom = 1.0F;
    look.bloom_threshold = 0.5F;
    look.bloom_radius = 4.0F;
    const std::vector<std::uint8_t> pixels = frame.apply(look);
    CHECK(pixel(pixels, 32, 8, 8) == Color{255, 255, 255, 255});
    const Color near = pixel(pixels, 32, 10, 8); // 2 pixels away
    CHECK(near.r > 0);
    CHECK(near.r == near.g);
    CHECK(pixel(pixels, 32, 9, 9).r > pixel(pixels, 32, 11, 11).r); // fading out
    CHECK(pixel(pixels, 32, 20, 8) == Color{0, 0, 0, 255});         // beyond its reach
    // The dim dot is below the threshold: no glow, itself unchanged.
    CHECK(pixel(pixels, 32, 25, 24) == Color{0, 0, 0, 255});
    CHECK(pixel(pixels, 32, 24, 24) == Color{100, 100, 100, 255});
}

TEST_CASE("Grading changes exposure, contrast and saturation, and the vignette the corners") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    Frame frame(*device, 16, 16,
                [](std::uint32_t, std::uint32_t) { return Color{64, 32, 160, 255}; });
    const auto near = [](Color a, Color b) {
        return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1;
    };
    render::Look look;
    look.exposure = 1.5F;
    CHECK(near(pixel(frame.apply(look), 16, 5, 5), Color{96, 48, 240, 255}));
    look = render::Look{};
    look.contrast = 0.0F; // everything mid-grey
    CHECK(near(pixel(frame.apply(look), 16, 5, 5), Color{128, 128, 128, 255}));
    look = render::Look{};
    look.saturation = 0.0F; // the luma: 0.2126 * 64 + 0.7152 * 32 + 0.0722 * 160 = 48.0
    CHECK(near(pixel(frame.apply(look), 16, 5, 5), Color{48, 48, 48, 255}));
    look = render::Look{};
    look.vignette = 1.0F;
    const std::vector<std::uint8_t> pixels = frame.apply(look);
    const Color corner = pixel(pixels, 16, 0, 0);
    const Color middle = pixel(pixels, 16, 8, 8);
    CHECK(corner.b < middle.b);
    CHECK(std::abs(middle.b - 160) <= 1); // the centre keeps its colour
    // Out-of-range values are clamped, not used: exposure 100 is 4.
    look = render::Look{};
    look.exposure = 100.0F;
    CHECK(near(pixel(frame.apply(look), 16, 5, 5), Color{255, 128, 255, 255}));
}

TEST_CASE("Post-processing needs a second target of the same size") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto post = render::PostProcessor::create(*device);
    const auto a = device->create_render_target(8, 8);
    const auto b = device->create_render_target(8, 4);
    REQUIRE(post != nullptr);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK_FALSE(post->apply(*a, *a, render::Look{}));
    CHECK_FALSE(post->apply(*a, *b, render::Look{}));
    CHECK(device->last_error() == "Post-processing needs another target of the same size.");
}
