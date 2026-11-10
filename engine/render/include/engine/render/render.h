#pragma once

// The renderer's base layer (ADR-004): a GPU device on the SDL3 GPU API, textures, render
// targets that can be read back, and a batched sprite renderer. SDL types stay inside the
// module.
//
// See docs/dev/rendering.md.

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

    struct Native;
    [[nodiscard]] const Native& native() const noexcept { return *native_; }

private:
    friend class Device;
    Texture(std::unique_ptr<Native> native, std::uint32_t width, std::uint32_t height);

    std::unique_ptr<Native> native_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
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
    /// Queues a sprite: the `source` pixels of `texture`, drawn to `destination`, multiplied
    /// by `tint`. Does nothing outside begin() and end().
    void draw(const Texture& texture, Rect source, Rect destination,
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

} // namespace engine::render
