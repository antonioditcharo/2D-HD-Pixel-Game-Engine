#pragma once

// The renderer's base layer (ADR-004): a GPU device on the SDL3 GPU API, textures, render
// targets that can be read back, and a batched sprite renderer. SDL types stay inside the
// module.
//
// See docs/dev/rendering.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

class Device;

/// An 8-bit RGBA colour.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend constexpr bool operator==(Color, Color) = default;
};

/// A rectangle in pixels: x and y are the top-left corner.
struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

/// A point on a render target, in pixels.
struct Point {
    float x = 0.0F;
    float y = 0.0F;
};

struct DeviceOptions {
    /// Create the device without a display, for tests, tools and offline rendering. Such a
    /// device can still show windows with the Vulkan backend (through SDL's offscreen
    /// video driver), but not with Direct3D 12, which needs a real window.
    bool headless = false;
    /// Ask the driver for validation. Slower; for development.
    bool debug = false;
};

/// A texture with RGBA8 pixels that sprites can be drawn from.
class Texture {
public:
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;
    ~Texture();

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    /// True if some pixel is partly see-through (alpha between 0 and 255), such as glass
    /// or water painted that way. The world renderer blends such pictures (§7).
    [[nodiscard]] bool translucent() const noexcept { return translucent_; }

    struct Native;
    [[nodiscard]] const Native& native() const noexcept { return *native_; }

private:
    friend class Device;
    Texture(std::unique_ptr<Native> native, std::uint32_t width, std::uint32_t height);

    std::unique_ptr<Native> native_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool translucent_ = false;
};

/// An offscreen image that can be drawn into and read back.
class RenderTarget {
public:
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;
    ~RenderTarget();

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    /// Waits for all drawing to finish and returns the pixels, RGBA8, rows top to bottom.
    /// Empty if reading failed (see Device::last_error()).
    [[nodiscard]] std::vector<std::uint8_t> read_pixels();

    struct Native;
    [[nodiscard]] const Native& native() const noexcept { return *native_; }

private:
    friend class Device;
    RenderTarget(std::unique_ptr<Native> native, std::uint32_t width, std::uint32_t height);

    std::unique_ptr<Native> native_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

/// Where an image lands in a window when shown pixel-perfect (plan/04 §3): upscaled by the
/// largest whole number that fits, and centred. If even 1x does not fit, it is shown at 1x,
/// centred and cropped.
struct Placement {
    std::uint32_t scale = 1;
    std::int64_t x = 0; ///< The image's left edge in the window; negative when cropped.
    std::int64_t y = 0;
    std::uint64_t width = 0; ///< The image's size in the window: its size times scale.
    std::uint64_t height = 0;

    friend bool operator==(const Placement&, const Placement&) = default;
};

[[nodiscard]] Placement fit_integer(std::uint32_t image_width, std::uint32_t image_height,
                                    std::uint32_t window_width,
                                    std::uint32_t window_height) noexcept;

/// A window the game is shown in.
class Window {
public:
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;
    ~Window();

    /// The drawable size in pixels (it changes when the player resizes the window).
    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;

    /// Shows `image` pixel-perfect (see fit_integer()), with `letterbox` around it. Returns
    /// false, with the device's last_error() set, if presenting failed. A minimised window
    /// shows nothing and returns true.
    bool present(const RenderTarget& image, Color letterbox);

    struct Native;

private:
    friend class Device;
    explicit Window(std::unique_ptr<Native> native);

    std::unique_ptr<Native> native_;
};

/// The GPU. Every object it creates must be destroyed before it.
class Device {
public:
    /// Creates a device, or returns null and sets `error` if no supported GPU is available.
    [[nodiscard]] static std::unique_ptr<Device> create(const DeviceOptions& options,
                                                        std::string& error);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
    ~Device();

    /// "vulkan", "direct3d12" or "metal".
    [[nodiscard]] std::string driver_name() const;

    /// A texture from RGBA8 pixels, rows top to bottom. Null if the size is zero, the
    /// pixel count does not match, or the GPU refuses (see last_error()).
    [[nodiscard]] std::unique_ptr<Texture> create_texture(std::uint32_t width, std::uint32_t height,
                                                          std::span<const std::uint8_t> rgba);

    /// An offscreen target. Null if the size is zero or the GPU refuses.
    [[nodiscard]] std::unique_ptr<RenderTarget> create_render_target(std::uint32_t width,
                                                                     std::uint32_t height);

    /// A resizable window that this device presents to. Null if it cannot be created,
    /// including on a headless device whose backend is not Vulkan (see DeviceOptions).
    [[nodiscard]] std::unique_ptr<Window> create_window(std::string_view title, std::uint32_t width,
                                                        std::uint32_t height);

    /// The last error reported by the GPU layer, in its own words.
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

    struct Native;
    [[nodiscard]] Native& native() noexcept { return *native_; }

private:
    explicit Device(std::unique_ptr<Native> native);

    std::unique_ptr<Native> native_;
    std::string last_error_;

    friend class RenderTarget;
    friend class SpriteRenderer;
    friend class WorldRenderer;
    friend class PostProcessor;
    friend class Window;
};

/// Draws textured, tinted rectangles in batches.
///
/// Coordinates are pixels of the target: (0, 0) is the top-left corner and y grows
/// downwards. Sprites are drawn in the order given, later ones on top, with alpha blending
/// and nearest-neighbour sampling, so pixel art stays crisp.
class SpriteRenderer {
public:
    /// Returns null and sets the device's last_error() if the pipeline cannot be created.
    [[nodiscard]] static std::unique_ptr<SpriteRenderer> create(Device& device);

    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;
    SpriteRenderer(SpriteRenderer&&) = delete;
    SpriteRenderer& operator=(SpriteRenderer&&) = delete;
    ~SpriteRenderer();

    /// Starts a frame into `target`, cleared to `clear`.
    void begin(RenderTarget& target, Color clear);
    /// Starts drawing over what `target` already holds, for interfaces and text on top of
    /// a frame.
    void begin_over(RenderTarget& target);
    /// How sprites queued from now on are blended (begin() sets Normal again).
    enum class Blend : std::uint8_t {
        Normal, ///< Over what is there, by the sprite's alpha.
        /// Brightens what is there by the sprite's colour: what is there times one plus
        /// it, as a lamp lights a wall, so the picture's own detail and colours show.
        /// Black changes nothing; white doubles it. Alpha is not used.
        Light,
    };
    void set_blend(Blend blend);
    /// Queues a sprite: the `source` pixels of `texture`, drawn to `destination`, multiplied
    /// by `tint`. Does nothing outside begin() and end().
    void draw(const Texture& texture, Rect source, Rect destination,
              Color tint = {255, 255, 255, 255});
    /// Queues a sprite drawn into any four-sided shape, for pictures turned, scaled or
    /// flipped: `corners` are where the source's top-left, top-right, bottom-right and
    /// bottom-left go.
    void draw(const Texture& texture, Rect source, const std::array<Point, 4>& corners,
              Color tint = {255, 255, 255, 255});
    /// Sends the frame to the GPU. Returns false, with the device's last_error() set, if it
    /// could not.
    bool end();

    struct Stats {
        std::size_t sprites = 0;
        std::size_t draw_calls = 0; ///< One per run of sprites sharing a texture.
    };
    /// Counts for the last frame that ended.
    [[nodiscard]] Stats last_stats() const noexcept { return stats_; }

    struct Native;

private:
    SpriteRenderer(Device& device, std::unique_ptr<Native> native);

    Device& device_;
    std::unique_ptr<Native> native_;
    Stats stats_;
};

// ---------------------------------------------------------------------------------------
// 3D: the Diorama style (plan/04 §4)
// ---------------------------------------------------------------------------------------

/// A point or direction in world units. The world is right-handed with y up.
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend constexpr bool operator==(Vec3, Vec3) = default;
};

/// A 4 x 4 matrix stored row by row, applied to column vectors: clip = m * (x, y, z, 1).
/// Clip space is the GPU's: x and y from -1 to 1 with y up, depth from 0 (near) to 1 (far).
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    /// A perspective projection looking down -z, with a vertical field of view in radians.
    [[nodiscard]] static Mat4 perspective(float fov_y, float aspect, float near, float far);
    /// An orthographic projection of the box from (left, bottom, -near) to (right, top, -far).
    [[nodiscard]] static Mat4 orthographic(float left, float right, float bottom, float top,
                                           float near, float far);
    /// A camera at `eye` looking at `target`, with `up` roughly up.
    [[nodiscard]] static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up);

    /// m * (p, 1): clip coordinates x, y, z, w.
    [[nodiscard]] std::array<float, 4> transform(Vec3 p) const noexcept;

    friend Mat4 operator*(const Mat4& a, const Mat4& b) noexcept;
    friend bool operator==(const Mat4&, const Mat4&) = default;
};

/// Distance fog: from `start` to `end` units from the eye, colours fade to `color`.
struct Fog {
    Color color{0, 0, 0, 255};
    float start = 0.0F;
    float end = 0.0F; ///< At or below `start`: no fog.
};

/// A light shining in every direction from a point, fading smoothly to nothing at its
/// radius: a torch, a lamp, a glowing window.
struct PointLight {
    Vec3 position;
    float radius = 4.0F; ///< In world units; at or below 0 it lights nothing.
    /// Red, green and blue from 0 to 1, times the light's strength (may exceed 1).
    std::array<float, 3> color{1.0F, 1.0F, 1.0F};
};

/// The most point lights one frame can have; the renderer keeps the first this many.
inline constexpr std::size_t max_point_lights = 16;

/// How a quad takes point lights. Their light is added to the quad's colour, times
/// `albedo`, where they reach its front.
struct Surface {
    Vec3 normal;       ///< Which way the quad faces. Zero: point lights do not reach it.
    float wrap = 0.0F; ///< 0: lit only from the front (Lambert); 1: from all round, softly
                       ///< (half-Lambert), for billboards that should not go dark from behind.
    std::array<float, 3> albedo{1.0F, 1.0F, 1.0F}; ///< Usually the tint.
    /// Whether it casts the sun's shadow (WorldRenderer::set_sun_shadow()). Billboards
    /// usually do not: seen from the sun they are edge-on cards.
    bool casts_shadow = true;
};

/// The sun's shadows: what stands between a surface and the sun darkens the sun's part of
/// its light, as a shadow map (depth seen from the sun) tells.
struct SunShadow {
    /// The sun's view: an orthographic projection looking along its rays, covering what
    /// casts and receives shadows.
    Mat4 view_projection;
    Vec3 towards_sun{0.0F, 1.0F, 0.0F}; ///< Unit.
    float darkness = 0.0F;              ///< 0 (none) to 1.
    /// How far in front of the stored depth a surface still counts as lit, in the sun
    /// view's depth units (0 to 1), against shadowing itself.
    float bias = 0.001F;
};

/// The shadow map's size, in texels per side.
inline constexpr std::uint32_t sun_shadow_size = 2048;

/// How much of a point light reaches a point `distance` away, before the angle: 1 at the
/// light, falling smoothly to 0 at `radius`: (1 - (d / r)^2)^2.
[[nodiscard]] float light_falloff(float distance, float radius) noexcept;

/// Draws textured quads in 3D with a depth buffer: the ground, walls and billboarded
/// sprites of a diorama.
///
/// Texels with alpha below one half are cut out (neither drawn nor hiding what is behind),
/// so pixel art keeps hard edges and needs no sorting: the depth buffer orders everything.
/// Each quad's colour multiplies its texels; that is where lighting and tint go.
class WorldRenderer {
public:
    /// Returns null and sets the device's last_error() if the pipeline cannot be created.
    [[nodiscard]] static std::unique_ptr<WorldRenderer> create(Device& device);

    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    WorldRenderer(WorldRenderer&&) = delete;
    WorldRenderer& operator=(WorldRenderer&&) = delete;
    ~WorldRenderer();

    /// Starts a frame into `target`, cleared to `clear`, seen through `view_projection`
    /// from `eye` (for fog), lit by up to max_point_lights of `lights` (the first ones).
    void begin(RenderTarget& target, Color clear, const Mat4& view_projection, Vec3 eye,
               Fog fog = {}, std::span<const PointLight> lights = {});
    /// Queues a quad showing the `source` pixels of `texture`. The corners are the world
    /// positions of the source's top-left, top-right, bottom-right and bottom-left.
    /// `color` multiplies the texels (ambient and sun light times tint); point lights add
    /// to it as `surface` says, per pixel.
    ///
    /// A quad whose colour's alpha is below 1, or whose texture is translucent(), is
    /// see-through: drawn after everything else, blended over what is behind it, farthest
    /// first. Otherwise texels are either drawn or cut out (alpha below a half).
    void draw(const Texture& texture, const std::array<Vec3, 4>& corners, Rect source,
              std::array<float, 4> color = {1.0F, 1.0F, 1.0F, 1.0F}, const Surface& surface = {});
    /// Queues a soft round shadow over the quad with these corners (as draw() takes
    /// them): what is already there is darkened by up to `darkness` (0 to 1) in the
    /// middle, fading out to the rim. Shadows are drawn after every quad, and only where
    /// something nearer is not (they are depth-tested, not depth-written).
    void draw_shadow(const std::array<Vec3, 4>& corners, float darkness);
    /// Queues a shadow shaped like the `source` pixels of `shape`: where they are opaque,
    /// what is already drawn is darkened. The corners are where the source's top-left,
    /// top-right, bottom-right and bottom-left go, as draw() takes them, and `darkness`
    /// (0 to 1) is how dark the shadow is at each, blended between them; a character's
    /// silhouette cast on the ground by the sun, darkest at its feet.
    void draw_shadow(const Texture& shape, const std::array<Vec3, 4>& corners, Rect source,
                     const std::array<float, 4>& darkness);
    /// Turns on the sun's shadows for this frame (after begin(), which turns them off):
    /// every opaque quad that casts_shadow is drawn into a shadow map from the sun first,
    /// and every surface facing the sun is darkened where the map says it is hidden from
    /// it. Darkness 0 turns them off.
    void set_sun_shadow(const SunShadow& shadow);
    /// Sends the frame to the GPU. Returns false, with the device's last_error() set, if it
    /// could not.
    bool end();

    struct Stats {
        std::size_t quads = 0;
        std::size_t translucent = 0; ///< Of `quads`, those blended.
        std::size_t shadows = 0;
        std::size_t sun_casters = 0; ///< Quads drawn into the shadow map; 0 without one.
        /// One per run of quads sharing a texture, and one per run of shadows sharing a shape.
        std::size_t draw_calls = 0;
    };
    [[nodiscard]] Stats last_stats() const noexcept { return stats_; }

    struct Native;

private:
    WorldRenderer(Device& device, std::unique_ptr<Native> native);

    Device& device_;
    std::unique_ptr<Native> native_;
    Stats stats_;
};

// ---------------------------------------------------------------------------------------
// Post-processing: the Look (plan/04 §8)
// ---------------------------------------------------------------------------------------

/// How a finished frame is processed: tilt-shift depth of field (the "miniature diorama"
/// look), bloom, colour grading and a vignette. The defaults change nothing.
struct Look {
    /// Blur above and below a sharp band across the screen, as a tilt-shift lens does.
    bool tilt_shift = false;
    float focus_center = 0.55F;    ///< Where the sharp band is: 0 is the top, 1 the bottom.
    float focus_height = 0.35F;    ///< How much of the screen's height is sharp, 0 to 1.
    float blur = 3.0F;             ///< The strongest blur, in pixels, 0 to 12.
    float blur_ramp = 0.25F;       ///< Over how much of the height the blur grows, 0 to 1.
    float bloom = 0.0F;            ///< How much bright parts glow, 0 (none) to 2.
    float bloom_threshold = 0.75F; ///< From how bright (0 to 1) a pixel glows.
    float bloom_radius = 6.0F;     ///< How far the glow spreads, in pixels, 0 to 16.
    float exposure = 1.0F;         ///< Brightness, 0 to 4.
    float contrast = 1.0F;         ///< 0 to 2.
    float saturation = 1.0F;       ///< 0 (grey) to 2.
    float vignette = 0.0F;         ///< How much the corners darken, 0 to 1.
    /// The light's colour: each channel multiplied, 0 to 2 (1 changes nothing). Warm at dusk,
    /// blue at night.
    float tint_r = 1.0F;
    float tint_g = 1.0F;
    float tint_b = 1.0F;

    /// True if processing would change nothing, so it can be skipped.
    [[nodiscard]] bool neutral() const noexcept;
    friend bool operator==(const Look&, const Look&) = default;
};

/// Applies a Look to a finished frame, in two full-screen passes on the GPU.
class PostProcessor {
public:
    /// Returns null and sets the device's last_error() if the pipelines cannot be created.
    [[nodiscard]] static std::unique_ptr<PostProcessor> create(Device& device);

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    PostProcessor(PostProcessor&&) = delete;
    PostProcessor& operator=(PostProcessor&&) = delete;
    ~PostProcessor();

    /// Writes `source` processed by `look` into `destination`, which must be another
    /// target of the same size. Values out of range are clamped. Returns false, with the
    /// device's last_error() set, if it could not.
    bool apply(const RenderTarget& source, RenderTarget& destination, const Look& look);

    struct Native;

private:
    PostProcessor(Device& device, std::unique_ptr<Native> native);

    Device& device_;
    std::unique_ptr<Native> native_;
};

} // namespace engine::render
