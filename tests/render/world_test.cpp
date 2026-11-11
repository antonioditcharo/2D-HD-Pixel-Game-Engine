// The 3D world renderer: matrices on the CPU, then exact pixels on the GPU for depth,
// alpha cutout, colour, fog and perspective. GPU tests need lavapipe or a real GPU (see
// render_test.cpp).

#include "engine/render/render.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include "support/gpu.h"

namespace render = engine::render;
using render::Color;
using render::Mat4;
using render::Rect;
using render::Vec3;

namespace {

using test_support::gpu;

Color pixel(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t x,
            std::uint32_t y) {
    const std::size_t i = (std::size_t{y} * width + x) * 4;
    return Color{pixels.at(i), pixels.at(i + 1), pixels.at(i + 2), pixels.at(i + 3)};
}

bool near_colour(Color a, Color b, int tolerance) {
    return std::abs(int{a.r} - int{b.r}) <= tolerance &&
           std::abs(int{a.g} - int{b.g}) <= tolerance && std::abs(int{a.b} - int{b.b}) <= tolerance;
}

std::unique_ptr<render::Texture> solid(render::Device& device, Color c) {
    const std::vector<std::uint8_t> rgba{c.r, c.g, c.b, c.a};
    return device.create_texture(1, 1, rgba);
}

/// A quad facing +z (towards an eye on the +z side), from (x0, y0) to (x1, y1) at depth z.
std::array<Vec3, 4> facing(float x0, float y0, float x1, float y1, float z) {
    return {Vec3{x0, y1, z}, Vec3{x1, y1, z}, Vec3{x1, y0, z}, Vec3{x0, y0, z}};
}

std::array<float, 3> clip_to_ndc(const std::array<float, 4>& clip) {
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}

const Color red{255, 0, 0, 255};
const Color blue{0, 0, 255, 255};
const Color green{0, 255, 0, 255};
const Color background{10, 20, 30, 255};

} // namespace

TEST_CASE("Projection matrices map the view volume to the GPU's clip space") {
    const Mat4 perspective = Mat4::perspective(std::numbers::pi_v<float> / 2.0F, 2.0F, 1.0F, 10.0F);
    auto ndc = clip_to_ndc(perspective.transform(Vec3{0, 0, -1}));
    CHECK(ndc[2] == doctest::Approx(0.0)); // the near plane is depth 0
    ndc = clip_to_ndc(perspective.transform(Vec3{0, 0, -10}));
    CHECK(ndc[2] == doctest::Approx(1.0)); // the far plane is depth 1
    // 90 degrees vertically: at distance 4, y = 4 is the top edge; x is halved by aspect 2.
    ndc = clip_to_ndc(perspective.transform(Vec3{8, 4, -4}));
    CHECK(ndc[0] == doctest::Approx(1.0));
    CHECK(ndc[1] == doctest::Approx(1.0));

    const Mat4 ortho = Mat4::orthographic(-8, 8, -4, 4, 1, 21);
    ndc = clip_to_ndc(ortho.transform(Vec3{8, -4, -1}));
    CHECK(ndc[0] == doctest::Approx(1.0));
    CHECK(ndc[1] == doctest::Approx(-1.0));
    CHECK(ndc[2] == doctest::Approx(0.0));
    CHECK(clip_to_ndc(ortho.transform(Vec3{0, 0, -21}))[2] == doctest::Approx(1.0));

    // A camera at (0, 0, 10) looking at the origin: the origin is 10 in front of it.
    const Mat4 view = Mat4::look_at(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const auto origin = view.transform(Vec3{0, 0, 0});
    CHECK(origin[0] == doctest::Approx(0.0));
    CHECK(origin[2] == doctest::Approx(-10.0));
    // A camera above, looking down at the origin from (0, 10, 10): up stays up on screen.
    const Mat4 tilted = Mat4::look_at(Vec3{0, 10, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    CHECK(tilted.transform(Vec3{0, 1, 0})[1] > 0.0F);
    CHECK(tilted.transform(Vec3{1, 0, 0})[0] == doctest::Approx(1.0)); // x stays to the right

    CHECK(Mat4{} * view == view);
    CHECK(view * Mat4{} == view);
}

TEST_CASE("Depth orders quads, whatever order they are drawn in") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto world = render::WorldRenderer::create(*device);
    INFO(device->last_error());
    REQUIRE(target != nullptr);
    REQUIRE(world != nullptr);
    const auto red_texture = solid(*device, red);
    const auto blue_texture = solid(*device, blue);
    // Orthographic, 1 unit per pixel: x and y from -8 to 8, the camera at z = 10.
    const Mat4 camera = Mat4::orthographic(-8, 8, -8, 8, 1, 20) *
                        Mat4::look_at(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Rect whole{0, 0, 1, 1};

    world->begin(*target, background, camera, Vec3{0, 0, 10});
    // Blue is nearer (z = 1) but drawn first; red behind (z = 0) must not cover it.
    world->draw(*blue_texture, facing(0, 0, 8, 8, 1), whole);
    world->draw(*red_texture, facing(-4, -4, 4, 4, 0), whole);
    REQUIRE(world->end());
    CHECK(world->last_stats().quads == 2);
    CHECK(world->last_stats().draw_calls == 2);
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    // Red covers pixels 4 to 11 both ways; blue covers columns 8 to 15, rows 0 to 7.
    CHECK(pixel(pixels, 16, 4, 11) == red);
    CHECK(pixel(pixels, 16, 7, 4) == red);
    CHECK(pixel(pixels, 16, 8, 4) == blue); // the overlap: blue is nearer
    CHECK(pixel(pixels, 16, 11, 7) == blue);
    CHECK(pixel(pixels, 16, 15, 0) == blue);
    CHECK(pixel(pixels, 16, 3, 4) == background);
    CHECK(pixel(pixels, 16, 4, 12) == background);
    CHECK(pixel(pixels, 16, 8, 8) == red); // below blue
}

TEST_CASE("Transparent texels are cut out, colours multiply, and fog fades with distance") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto world = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(world != nullptr);
    const auto red_texture = solid(*device, red);
    // Two texels: opaque green, then fully transparent.
    const std::vector<std::uint8_t> holed{0, 255, 0, 255, 255, 255, 255, 0};
    const auto holed_texture = device->create_texture(2, 1, holed);
    REQUIRE(holed_texture != nullptr);
    const Mat4 camera = Mat4::orthographic(-8, 8, -8, 8, 1, 20) *
                        Mat4::look_at(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Vec3 eye{0, 0, 10};

    // The holed quad is nearest; through its transparent half the red quad shows.
    world->begin(*target, background, camera, eye);
    world->draw(*holed_texture, facing(-8, -8, 0, 8, 2), Rect{0, 0, 2, 1});
    world->draw(*red_texture, facing(-4, -4, 4, 4, 0), Rect{0, 0, 1, 1});
    REQUIRE(world->end());
    const std::vector<std::uint8_t> pixels = target->read_pixels();
    CHECK(pixel(pixels, 16, 2, 6) == green);      // the opaque half
    CHECK(pixel(pixels, 16, 5, 6) == red);        // through the hole
    CHECK(pixel(pixels, 16, 5, 1) == background); // through the hole, nothing behind
    CHECK(pixel(pixels, 16, 9, 6) == red);

    // A colour of one half halves the texels.
    world->begin(*target, background, camera, eye);
    world->draw(*red_texture, facing(-4, -4, 4, 4, 0), Rect{0, 0, 1, 1}, {0.5F, 0.5F, 0.5F, 1.0F});
    REQUIRE(world->end());
    CHECK(near_colour(pixel(target->read_pixels(), 16, 8, 8), Color{128, 0, 0, 255}, 1));

    // Fog: 10 units away, fog from 0 to 20 is half way; from 0 to 1, complete; from 100
    // on, none.
    const Color white_fog{255, 255, 255, 255};
    const auto fogged = [&](float start, float end) {
        world->begin(*target, background, camera, eye, render::Fog{white_fog, start, end});
        world->draw(*red_texture, facing(-1, -1, 1, 1, 0), Rect{0, 0, 1, 1});
        REQUIRE(world->end());
        return pixel(target->read_pixels(), 16, 8, 8);
    };
    CHECK(near_colour(fogged(0.0F, 20.0F), Color{255, 128, 128, 255}, 2));
    CHECK(fogged(0.0F, 1.0F) == Color{255, 255, 255, 255});
    CHECK(fogged(100.0F, 200.0F) == red);
    CHECK(fogged(5.0F, 5.0F) == red); // an empty range is no fog
}

TEST_CASE("Perspective makes far things smaller") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto world = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(world != nullptr);
    const auto red_texture = solid(*device, red);
    // 90 degrees: a 4-unit quad 4 units away fills half the view, 8 units away a quarter.
    const Mat4 camera = Mat4::perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 0.1F, 100.0F) *
                        Mat4::look_at(Vec3{0, 0, 0}, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    const auto draw_at = [&](float distance) {
        world->begin(*target, background, camera, Vec3{});
        world->draw(*red_texture, facing(-2, -2, 2, 2, -distance), Rect{0, 0, 1, 1});
        REQUIRE(world->end());
        return target->read_pixels();
    };
    std::vector<std::uint8_t> pixels = draw_at(4.0F); // pixels 4 to 11
    CHECK(pixel(pixels, 16, 4, 4) == red);
    CHECK(pixel(pixels, 16, 11, 11) == red);
    CHECK(pixel(pixels, 16, 3, 8) == background);
    CHECK(pixel(pixels, 16, 12, 8) == background);
    pixels = draw_at(8.0F); // pixels 6 to 9
    CHECK(pixel(pixels, 16, 6, 6) == red);
    CHECK(pixel(pixels, 16, 9, 9) == red);
    CHECK(pixel(pixels, 16, 5, 8) == background);
    CHECK(pixel(pixels, 16, 10, 8) == background);
}

TEST_CASE("Point lights fall off with distance and light only what faces them") {
    CHECK(render::light_falloff(0.0F, 4.0F) == 1.0F);
    CHECK(render::light_falloff(2.0F, 4.0F) == doctest::Approx(0.5625)); // (1 - 1/4)^2
    CHECK(render::light_falloff(4.0F, 4.0F) == 0.0F);
    CHECK(render::light_falloff(9.0F, 4.0F) == 0.0F);
    CHECK(render::light_falloff(1.0F, 0.0F) == 0.0F);
    CHECK(render::light_falloff(std::nanf(""), 4.0F) == 0.0F);

    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto world = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(world != nullptr);
    const auto white = solid(*device, Color{255, 255, 255, 255});
    const Mat4 camera = Mat4::orthographic(-8, 8, -8, 8, 1, 20) *
                        Mat4::look_at(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Vec3 eye{0, 0, 10};
    const std::array<float, 4> dark{0.0F, 0.0F, 0.0F, 1.0F};
    // A red light 2 units in front of the middle of a wall facing it, reaching 4 units.
    const std::array<render::PointLight, 1> red_light{
        render::PointLight{Vec3{0.5F, 0.5F, 2.0F}, 4.0F, {1.0F, 0.0F, 0.0F}}};
    const auto draw = [&](const render::Surface& surface, std::span<const render::PointLight> l) {
        world->begin(*target, background, camera, eye, render::Fog{}, l);
        world->draw(*white, facing(-8, -8, 8, 8, 0), Rect{0, 0, 1, 1}, dark, surface);
        REQUIRE(world->end());
        return target->read_pixels();
    };
    render::Surface wall;
    wall.normal = Vec3{0, 0, 1};
    const std::vector<std::uint8_t> pixels = draw(wall, red_light);
    // Pixel (8, 7) covers x 0..1, y 0..1, so its centre is right under the light: 2 away,
    // facing it squarely: 0.5625 of full red.
    CHECK(near_colour(pixel(pixels, 16, 8, 7), Color{143, 0, 0, 255}, 2));
    // Further along the wall: weaker, then nothing beyond the radius (3.5 units away
    // along the wall is 4.03 from the light).
    const Color off_centre = pixel(pixels, 16, 10, 7); // centre x = 2.5: 2.83 away
    CHECK(off_centre.r > 20);
    CHECK(off_centre.r < 143);
    CHECK(pixel(pixels, 16, 12, 7) == Color{0, 0, 0, 255}); // x = 4.5: 4.47 away
    CHECK(pixel(pixels, 16, 0, 0) == Color{0, 0, 0, 255});

    // Facing away, with no wrap: unlit. With wrap 1 (billboards): half of facing it.
    wall.normal = Vec3{0, 0, -1};
    CHECK(pixel(draw(wall, red_light), 16, 8, 7) == Color{0, 0, 0, 255});
    wall.wrap = 1.0F;
    CHECK(pixel(draw(wall, red_light), 16, 8, 7) == Color{0, 0, 0, 255}); // dot = -1: (−1+1)/2 = 0
    wall.normal = Vec3{1, 0, 0}; // side-on: dot 0, so half with wrap 1
    CHECK(near_colour(pixel(draw(wall, red_light), 16, 8, 7), Color{72, 0, 0, 255}, 2));

    // The albedo multiplies the light; no normal means no point light at all.
    wall = render::Surface{};
    wall.normal = Vec3{0, 0, 1};
    wall.albedo = {0.5F, 1.0F, 1.0F};
    CHECK(near_colour(pixel(draw(wall, red_light), 16, 8, 7), Color{72, 0, 0, 255}, 2));
    CHECK(pixel(draw(render::Surface{}, red_light), 16, 8, 7) == Color{0, 0, 0, 255});

    // Lights add up, and with the quad's own colour; broken lights are skipped.
    const std::array<render::PointLight, 3> two{
        render::PointLight{Vec3{0.5F, 0.5F, 2.0F}, 4.0F, {1.0F, 0.0F, 0.0F}},
        render::PointLight{Vec3{0.5F, 0.5F, 2.0F}, 4.0F, {0.0F, 0.0F, 1.0F}},
        render::PointLight{Vec3{0.5F, 0.5F, std::nanf("")}, 4.0F, {0.0F, 1.0F, 0.0F}}};
    wall = render::Surface{};
    wall.normal = Vec3{0, 0, 1};
    CHECK(near_colour(pixel(draw(wall, two), 16, 8, 7), Color{143, 0, 143, 255}, 2));

    // More than max_point_lights: only the first ones count.
    std::vector<render::PointLight> many(
        render::max_point_lights + 4,
        render::PointLight{Vec3{0.5F, 0.5F, 2.0F}, 4.0F, {0.01F, 0.0F, 0.0F}});
    const Color sum = pixel(draw(wall, many), 16, 8, 7);
    // 16 lights of 0.01 at 0.5625: 0.09 of 255 = 23; all 20 would be 29.
    CHECK(std::abs(int{sum.r} - 23) <= 1);
}

TEST_CASE("Shadows darken what is drawn, softly, and hide behind nearer things") {
    render::Device* device = gpu();
    if (device == nullptr) {
        return;
    }
    const auto target = device->create_render_target(16, 16);
    const auto world = render::WorldRenderer::create(*device);
    REQUIRE(target != nullptr);
    REQUIRE(world != nullptr);
    const auto white = solid(*device, Color{255, 255, 255, 255});
    const auto blue_texture = solid(*device, blue);
    const Mat4 camera = Mat4::orthographic(-8, 8, -8, 8, 1, 20) *
                        Mat4::look_at(Vec3{0, 0, 10}, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Vec3 eye{0, 0, 10};
    // A white wall filling the view; a shadow of darkness 0.5 over its middle 8 x 8.
    world->begin(*target, background, camera, eye);
    world->draw(*white, facing(-8, -8, 8, 8, 0), Rect{0, 0, 1, 1});
    world->draw_shadow(facing(-4, -4, 4, 4, 0.01F), 0.5F);
    REQUIRE(world->end());
    CHECK(world->last_stats().shadows == 1);
    std::vector<std::uint8_t> pixels = target->read_pixels();
    // The middle of the blob is fully dark: white times 0.5.
    CHECK(near_colour(pixel(pixels, 16, 8, 8), Color{128, 128, 128, 255}, 1));
    CHECK(near_colour(pixel(pixels, 16, 7, 7), Color{128, 128, 128, 255}, 1));
    // Its rim fades: lighter than the middle, darker than the wall.
    const Color rim = pixel(pixels, 16, 4, 8);
    CHECK(rim.r > 128);
    CHECK(rim.r < 255);
    // Beyond its square, and in its square's corners beyond the circle, the wall is white.
    CHECK(pixel(pixels, 16, 2, 8) == Color{255, 255, 255, 255});
    CHECK(pixel(pixels, 16, 4, 4) == Color{255, 255, 255, 255});

    // A nearer quad hides the shadow; darkness 0 draws nothing.
    world->begin(*target, background, camera, eye);
    world->draw(*white, facing(-8, -8, 8, 8, 0), Rect{0, 0, 1, 1});
    world->draw(*blue_texture, facing(0, 0, 8, 8, 1), Rect{0, 0, 1, 1});
    world->draw_shadow(facing(-4, -4, 4, 4, 0.01F), 1.0F);
    world->draw_shadow(facing(-8, -8, 8, 8, 0.01F), 0.0F);
    REQUIRE(world->end());
    CHECK(world->last_stats().shadows == 1);
    pixels = target->read_pixels();
    CHECK(pixel(pixels, 16, 9, 6) == blue);                              // nearer: unshadowed
    CHECK(near_colour(pixel(pixels, 16, 6, 9), Color{0, 0, 0, 255}, 1)); // darkness 1
    CHECK(pixel(pixels, 16, 0, 15) == Color{255, 255, 255, 255});
}
