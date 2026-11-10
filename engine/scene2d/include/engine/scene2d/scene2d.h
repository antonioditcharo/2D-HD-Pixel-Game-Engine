#pragma once

// Classic 2D scenes (plan/04 §3): entities with a 2D transform and a sprite or a tilemap,
// drawn through a pixel-snapped orthographic camera, and sprites animated from sprite
// sheets. This is the first place where the world, the asset database and the renderer
// work together.
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
#include <optional>
#include <string>
#include <string_view>
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
    /// For a sprite sheet: the size of one frame in pixels. 0 uses the whole image. Frames
    /// are numbered from 0, left to right, then top to bottom; numbers past the last frame
    /// wrap around.
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::uint32_t frame = 0;
};

/// "Animation": plays a run of a sprite sheet's frames on the entity's Sprite.
struct SpriteAnimation {
    std::uint32_t first_frame = 0;
    std::uint32_t frame_count = 1;
    double frames_per_second = 8.0;
    bool loop = true;
    bool playing = true;
    double elapsed = 0.0; ///< Seconds since it started playing (hidden; saved with scenes).

    /// Starts again from the first frame.
    void play() {
        elapsed = 0.0;
        playing = true;
    }
    /// Stops on the current frame.
    void stop() { playing = false; }
};

/// A tilemap's tiles, parsed. Cells hold tile numbers, or -1 for none.
struct TileGrid {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::int32_t> cells; ///< Row by row.

    [[nodiscard]] std::int32_t at(std::uint32_t x, std::uint32_t y) const noexcept {
        return x < width && y < height ? cells[(std::size_t{y} * width) + x] : -1;
    }

    friend bool operator==(const TileGrid&, const TileGrid&) = default;
};

/// The largest tilemap side, in tiles.
inline constexpr std::uint32_t max_tilemap_side = 4096;

/// Parses tilemap text: one line per row, cells separated by commas, each a tile number
/// or empty (or -1) for no tile. Short rows are filled with empty cells. Returns nothing,
/// with `error` in plain language, for a cell that is not a whole number from -1 up, or a
/// map wider or taller than max_tilemap_side.
[[nodiscard]] std::optional<TileGrid> parse_tiles(std::string_view text, std::string& error);
/// The text form of `grid`, which parse_tiles() reads back unchanged.
[[nodiscard]] std::string format_tiles(const TileGrid& grid);

/// "Tilemap": a grid of tiles from one tileset image, with its top-left corner at the
/// entity's position. Tiles are numbered like sprite sheet frames. They are stored as text
/// (see parse_tiles()) so scene files stay readable and diff well.
struct Tilemap {
    std::string tileset;         ///< The GUID of a PNG asset.
    std::uint32_t tile_size = 0; ///< In pixels, square. 0 uses the camera's PPU.
    std::string tiles;
    bool visible = true;
    /// Every tile of this map blocks movement (see sweep()). Paint walls on their own map,
    /// hidden if the art is on another one.
    bool solid = false;

    /// The parsed tiles, kept until `tiles` changes. Null, with `error` set, if the text
    /// is not valid.
    [[nodiscard]] const TileGrid* grid(std::string& error) const;

private:
    // A cache only: not reflected, so never saved.
    mutable std::string parsed_text_;
    mutable std::optional<TileGrid> parsed_;
    mutable std::string parse_error_;
    mutable bool parsed_once_ = false;
};

/// Registers Transform2D ("transform_2d"), Sprite ("sprite"), SpriteAnimation
/// ("sprite_animation") and Tilemap ("tilemap") with their editor metadata.
void register_types(reflect::Registry& registry);
/// Makes them all usable as components of `world`. register_types() must have run first.
bool register_components(world::World& world);

/// Advances every playing SpriteAnimation by `seconds` and sets its Sprite's frame. Call
/// it once per fixed step. Returns the entities whose animation reached its end this time
/// (animations that do not loop), so their "finished" event can be sent.
std::vector<world::Entity> animate(world::World& world, double seconds);

/// An axis-aligned box in world units, such as a character's feet. Edges are half-open:
/// a box whose right edge is exactly on a tile's left edge does not overlap it.
struct Box {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

enum class Axis : std::uint8_t { X, Y };

/// How far `box` can move by `distance` along `axis` before it would enter a tile of a
/// solid Tilemap in `world`, visible or not: `distance` itself if nothing is in the way,
/// else the distance to the first such tile's edge (0 when touching it). Tiles the box
/// already overlaps do not block, so a character placed inside a wall can walk out.
/// `ppu` sizes tilemaps whose tile_size is 0.
[[nodiscard]] double sweep(const world::World& world, const Box& box, Axis axis, double distance,
                           std::uint32_t ppu);

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
    std::size_t drawn = 0;   ///< Sprites and tiles drawn.
    std::size_t missing = 0; ///< Sprites and tilemaps whose image or tiles could not be used.
};

/// Draws every visible entity that has a Transform2D and a Sprite or a Tilemap into
/// `target`, cleared to `clear`, as seen by `camera`. Positions are snapped to whole
/// pixels, so pixel art never shimmers. Only the tiles in view are drawn.
DrawStats draw_world(const world::World& world, TextureCache& textures,
                     render::SpriteRenderer& sprites, render::RenderTarget& target,
                     const Camera2D& camera, render::Color clear);

} // namespace engine::scene2d
