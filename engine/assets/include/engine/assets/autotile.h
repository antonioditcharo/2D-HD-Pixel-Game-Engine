#pragma once

// Auto-tiles (plan/02 B15, C4): a terrain drawn as a block of 2 x 3 tiles, as RPG Maker's
// autotiles are, expanded into every shape it takes beside itself, so painting can pick
// the right one from the neighbours.
//
// The block, in tiles:
//
//   +---------+---------+
//   | preview | inner   |   preview: how the terrain looks in a palette
//   |         | corners |   inner corners: each quarter is a corner cut into the terrain
//   +---------+---------+
//   | a 2 x 2 patch of  |   the terrain as an island: outer corners, edges between them
//   | the terrain, with |   and the middle filled
//   | its border        |
//   +---------+---------+
//
// Each tile of every shape is put together from four quarter tiles of the block, picked by
// the neighbours on that quarter's sides and corner: an outer corner, an edge, an inner
// corner, or the middle. There are 47 shapes (the "blob" set).
//
// See docs/dev/scene2d.md (Auto-tiles).

#include "engine/assets/assets.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace engine::assets {

/// Tiles each terrain takes once expanded: the 47 shapes, and the preview last.
inline constexpr std::uint32_t autotile_shapes = 48;
/// Expanded terrains are laid out this many tiles wide (6 rows each).
inline constexpr std::uint32_t autotile_columns = 8;

/// Which neighbours of a cell hold the same terrain, one bit each.
enum Neighbour : std::uint8_t {
    north = 1,
    east = 2,
    south = 4,
    west = 8,
    north_east = 16,
    south_east = 32,
    south_west = 64,
    north_west = 128,
};

/// The shape (0 to 46) for a cell with `neighbours`. A corner neighbour counts only with
/// both sides beside it, so 256 masks come down to 47 shapes. 46 is the terrain all round.
[[nodiscard]] std::uint32_t autotile_shape(std::uint8_t neighbours);
/// The neighbours mask of each shape, for tools and tests.
[[nodiscard]] const std::array<std::uint8_t, 47>& autotile_masks();

/// Expands the terrains of `source` (blocks of 2 x 3 tiles of `tile_size` pixels, side by
/// side and in rows, counted row by row) into `autotile_shapes` tiles each, 8 tiles wide:
/// terrain k's shape s is tile k * 48 + s. Nothing, with `error`, for a size that is odd or
/// below 2, or a picture smaller than one block.
[[nodiscard]] std::optional<Image> expand_autotiles(const Image& source, std::uint32_t tile_size,
                                                    std::string& error);

} // namespace engine::assets
