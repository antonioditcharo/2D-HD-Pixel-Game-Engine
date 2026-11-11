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

#include <array>
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
    /// In the Diorama style, how tall each tile stands, in tiles: 0 is flat ground; above,
    /// every tile is a block with the tile's picture on its top and sides.
    double height = 0.0;

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

/// "Light": a point light for the Diorama style, such as a torch, a lamp or a glowing
/// window. It lights the ground, walls and characters around it, fading smoothly to
/// nothing at its radius. Lights add to the scene's ambient and sun light, so they show
/// best at dusk and night.
struct Light {
    std::uint8_t color_r = 255; ///< A warm torchlight by default.
    std::uint8_t color_g = 180;
    std::uint8_t color_b = 110;
    double intensity = 1.0; ///< 0 to 8.
    double radius = 4.0;    ///< In tiles, 0 to 32.
    double height = 1.0;    ///< Above the ground, in tiles, 0 to 16.
    bool enabled = true;
};

/// Registers Transform2D ("transform_2d"), Sprite ("sprite"), SpriteAnimation
/// ("sprite_animation"), Tilemap ("tilemap") and Light ("light") with their editor
/// metadata.
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

/// True if the boxes share more than an edge (or a rounding error of one).
[[nodiscard]] bool overlaps(const Box& a, const Box& b) noexcept;

/// Like sweep(), against boxes instead of tiles: how far `box` can move by `distance`
/// along `axis` before it would enter one of `obstacles`. Obstacles it already overlaps do
/// not block.
[[nodiscard]] double sweep_boxes(const Box& box, Axis axis, double distance,
                                 const std::vector<Box>& obstacles);

/// How far `box` can move by `distance` along `axis` before it would enter a tile of a
/// solid Tilemap in `world`, visible or not: `distance` itself if nothing is in the way,
/// else the distance to the first such tile's edge (0 when touching it). Tiles the box
/// already overlaps do not block, so a character placed inside a wall can walk out.
/// `ppu` sizes tilemaps whose tile_size is 0.
[[nodiscard]] double sweep(const world::World& world, const Box& box, Axis axis, double distance,
                           std::uint32_t ppu);

/// The area the tilemaps of `world` cover, in units: the smallest box around every
/// tilemap's grid (visible or not, solid or not). Nothing if no tilemap has tiles. `ppu`
/// sizes tilemaps whose tile_size is 0.
[[nodiscard]] std::optional<Box> map_bounds(const world::World& world, std::uint32_t ppu);

/// Where a camera looking at (x, y) must look so that its view stays inside `bounds`.
/// `view` is what it shows around the point, relative to it (left and top negative; an
/// infinite edge is never limited). On each axis the view moves in as little as it can; if
/// it is larger than `bounds`, it is centred on them. Returns {x, y}.
[[nodiscard]] std::array<double, 2> clamp_view(double x, double y, const Box& view,
                                               const Box& bounds) noexcept;

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

// ---------------------------------------------------------------------------------------
// The Diorama style (plan/04 §4): the same entities in 3D
// ---------------------------------------------------------------------------------------

/// The camera of the Diorama style. A map position (x, y) is the world point (x, 0, y): the
/// ground is the x-z plane, up is +y. The camera orbits the point it looks at.
struct DioramaCamera {
    double x = 0.0; ///< The map point looked at, in tiles.
    double y = 0.0;
    /// How far the camera looks down, in degrees: 90 is straight down. Kept from 5 to 85.
    double pitch = 35.0;
    /// Turning around the point, in degrees: 0 looks towards smaller map y ("north"), as
    /// the 2D view does.
    double yaw = 0.0;
    double distance = 12.0; ///< From the point, in tiles.
    double fov = 30.0;      ///< Vertical field of view, in degrees: narrow is the HD-2D look.
    std::uint32_t ppu = 16; ///< Pixels per tile, to size sprites and tiles.
};

/// The light of a diorama: every face is lit by `ambient` plus `diffuse` times how much it
/// faces the sun.
struct DioramaLight {
    render::Vec3 sun{-0.4F, 1.0F, 0.6F}; ///< Towards the sun; need not be normalised.
    float ambient = 0.55F;
    float diffuse = 0.45F;
    /// How dark the soft shadow under each sprite is, 0 (none) to 1. Shadows lie on the
    /// top of the highest tile under the sprite's feet; with no tile there, none is drawn.
    float sprite_shadow = 0.35F;
};

/// The top of the highest visible tile under map point (x, y), in tiles above the
/// ground: a block's height, or about 0 for flat maps. Nothing if no tile is there.
[[nodiscard]] std::optional<float> ground_top(const world::World& world, double x, double y,
                                              std::uint32_t ppu);

/// How bright a face with `normal` is under `light`, from 0 to 1 (and above, if the light
/// adds up to more).
[[nodiscard]] float brightness(render::Vec3 normal, const DioramaLight& light) noexcept;

/// The camera's view-projection for a target of `aspect` (width / height), and its eye.
[[nodiscard]] render::Mat4 diorama_view_projection(const DioramaCamera& camera, float aspect,
                                                   render::Vec3* eye = nullptr);

/// A ray from the eye through a point of a Diorama camera's image.
struct Ray {
    render::Vec3 origin;
    render::Vec3 direction; ///< Of length 1.
};

/// The ray from `camera`'s eye through the point (`ndc_x`, `ndc_y`) of its image, where
/// x runs from -1 (left) to 1 (right) and y from -1 (bottom) to 1 (top), for a target of
/// `aspect`: what is under the cursor, for picking and dragging in the editor.
[[nodiscard]] Ray diorama_ray(const DioramaCamera& camera, float aspect, double ndc_x,
                              double ndc_y) noexcept;

/// Where `ray` meets the ground plane at `height` tiles up, as a map point (x, y); nothing
/// if it runs parallel to it or away from it.
[[nodiscard]] std::optional<std::array<double, 2>> ground_hit(const Ray& ray,
                                                              double height = 0.0) noexcept;

/// The ground a Diorama camera shows around the point it looks at, relative to that point,
/// in tiles, for a target of `aspect` (width / height). The top and bottom are where the
/// screen's top and bottom edges meet the ground, so perspective shows more ground above
/// the point than below; the top is infinite when the screen's top edge looks above the
/// horizon. The sides are measured at the point, so a little more ground shows at the far
/// corners. Infinite all round for a turned camera (yaw not 0), which is never limited.
[[nodiscard]] Box diorama_view_extent(const DioramaCamera& camera, double aspect) noexcept;

/// The point lights draw_diorama() uses: every enabled Light with a Position, at its
/// height above the ground, nearest to the map point (x, y) first (ties by GUID), at most
/// render::max_point_lights of them.
[[nodiscard]] std::vector<render::PointLight> diorama_lights(const world::World& world, double x,
                                                             double y);

/// Draws the world in the Diorama style: tilemaps as ground and blocks, sprites as
/// upright billboards facing the camera, ordered by depth, lit and fogged. Missing images
/// and invalid tiles are counted as in draw_world(); `drawn` counts quads.
DrawStats draw_diorama(const world::World& world, TextureCache& textures,
                       render::WorldRenderer& renderer, render::RenderTarget& target,
                       const DioramaCamera& camera, render::Color clear,
                       const DioramaLight& light = {}, render::Fog fog = {});

} // namespace engine::scene2d
