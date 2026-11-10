#pragma once

// Classic 2D scenes (plan/04 §3): entities with a 2D transform and a sprite, drawn through
// a pixel-snapped orthographic camera. This is the first place where the world, the asset
// database and the renderer work together.
//
// See docs/dev/scene2d.md.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/render/render.h"
#include "engine/world/world.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::scene2d {

/// Where an entity is, in world units: one unit is one tile of `ppu` pixels (plan/04 §2).
struct Transform2D {
    double x = 0.0;
    double y = 0.0; ///< Grows downwards, as on screen.
    /// Draw order between groups: higher layers are drawn on top. Within a layer, lower
    /// on screen (larger y) is drawn on top.
    std::int32_t layer = 0;
};

/// The image an entity shows.
struct Sprite {
    std::string image; ///< The GUID of a PNG asset, as 32 hexadecimal digits.
    /// The point of the image placed at the transform's position, from 0 (left, top) to 1
    /// (right, bottom). The default, bottom centre, puts a character's feet on its
    /// position, which is what y-sorting needs.
    double pivot_x = 0.5;
    double pivot_y = 1.0;
    std::uint8_t tint_r = 255;
    std::uint8_t tint_g = 255;
    std::uint8_t tint_b = 255;
    std::uint8_t tint_a = 255;
    bool visible = true;
};

/// Registers Transform2D ("transform_2d") and Sprite ("sprite") with their editor
/// metadata.
void register_types(reflect::Registry& registry);
/// Makes both usable as components of `world`. register_types() must have run first.
bool register_components(world::World& world);

/// A pixel-perfect orthographic camera.
struct Camera2D {
    double x = 0.0; ///< The world position at the centre of the target, in units.
    double y = 0.0;
    std::uint32_t ppu = 16;  ///< Pixels per unit, the project setting (8 to 64).
    std::uint32_t scale = 1; ///< Whole-number upscale of every pixel (1 = none).
};

/// GPU textures for image assets, loaded on first use.
class TextureCache {
public:
    TextureCache(render::Device& device, const assets::AssetDatabase& assets);

    /// The texture for an image asset, or null if it is unknown, not an image, or cannot
    /// be decoded or uploaded (see last_error()).
    const render::Texture* get(Guid image);

    /// Forgets textures whose assets were modified or removed, so the next get() loads
    /// them again. (A moved asset keeps its GUID and content, so its texture stays.) Call
    /// it with every result of AssetDatabase::refresh() to hot-reload images.
    void apply(const assets::AssetChanges& changes);

    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    render::Device& device_;
    const assets::AssetDatabase& assets_;
    std::unordered_map<Guid, std::unique_ptr<render::Texture>, GuidHash> textures_;
    std::string last_error_;
};

struct DrawStats {
    std::size_t drawn = 0;
    std::size_t missing = 0; ///< Sprites whose image could not be loaded.
};

/// Draws every visible entity that has both a Transform2D and a Sprite into `target`,
/// cleared to `clear`, as seen by `camera`. Positions are snapped to whole pixels, so
/// pixel art never shimmers.
DrawStats draw_world(const world::World& world, TextureCache& textures,
                     render::SpriteRenderer& sprites, render::RenderTarget& target,
                     const Camera2D& camera, render::Color clear);

} // namespace engine::scene2d
