#include "engine/assets/autotile.h"

#include <algorithm>
#include <cstddef>

namespace engine::assets {

namespace {

/// A corner counts only with both of its sides.
std::uint8_t reduced(std::uint8_t m) {
    std::uint8_t out = m & (north | east | south | west);
    const auto keep = [&](std::uint8_t corner, std::uint8_t a, std::uint8_t b) {
        if ((m & corner) != 0 && (m & a) != 0 && (m & b) != 0) {
            out = static_cast<std::uint8_t>(out | corner);
        }
    };
    keep(north_east, north, east);
    keep(south_east, south, east);
    keep(south_west, south, west);
    keep(north_west, north, west);
    return out;
}

struct Tables {
    std::array<std::uint8_t, 47> masks{};
    std::array<std::uint8_t, 256> shape{};
};

const Tables& tables() {
    static const Tables built = [] {
        Tables t;
        std::array<bool, 256> seen{};
        std::size_t count = 0;
        // Shapes in order of their masks, so the terrain all round (255) comes last.
        for (unsigned m = 0; m < 256; ++m) {
            const std::uint8_t r = reduced(static_cast<std::uint8_t>(m));
            if (r == m && !seen.at(r)) {
                seen.at(r) = true;
                t.masks.at(count++) = r;
            }
        }
        for (unsigned m = 0; m < 256; ++m) {
            const std::uint8_t r = reduced(static_cast<std::uint8_t>(m));
            const auto at = std::find(t.masks.begin(), t.masks.end(), r);
            t.shape.at(m) = static_cast<std::uint8_t>(at - t.masks.begin());
        }
        return t;
    }();
    return built;
}

} // namespace

std::uint32_t autotile_shape(std::uint8_t neighbours) {
    return tables().shape.at(neighbours);
}

const std::array<std::uint8_t, 47>& autotile_masks() {
    return tables().masks;
}

std::optional<Image> expand_autotiles(const Image& source, std::uint32_t tile_size,
                                      std::string& error) {
    if (tile_size < 2 || tile_size % 2 != 0) {
        error = "Auto-tiles need tiles an even number of pixels wide.";
        return std::nullopt;
    }
    const std::uint32_t blocks_across = source.width / (2 * tile_size);
    const std::uint32_t blocks_down = source.height / (3 * tile_size);
    if (blocks_across == 0 || blocks_down == 0 ||
        source.rgba.size() < std::size_t{source.width} * source.height * 4) {
        error = "The picture is smaller than one auto-tile block (2 x 3 tiles of " +
                std::to_string(tile_size) + " pixels).";
        return std::nullopt;
    }
    const std::uint32_t terrains = blocks_across * blocks_down;
    const std::uint32_t half = tile_size / 2;
    const std::uint32_t rows_each = autotile_shapes / autotile_columns;
    Image out;
    out.width = autotile_columns * tile_size;
    out.height = terrains * rows_each * tile_size;
    out.rgba.assign(std::size_t{out.width} * out.height * 4, 0);

    // Copies the quarter at (qx, qy) of a block, in quarters, to (x, y) in pixels.
    const auto copy = [&](std::uint32_t bx, std::uint32_t by, std::uint32_t qx, std::uint32_t qy,
                          std::uint32_t x, std::uint32_t y) {
        for (std::uint32_t row = 0; row < half; ++row) {
            const std::size_t from =
                ((std::size_t{by} + (std::size_t{qy} * half) + row) * source.width + bx +
                 (std::size_t{qx} * half)) *
                4;
            const std::size_t to = ((std::size_t{y} + row) * out.width + x) * 4;
            std::copy_n(source.rgba.begin() + static_cast<std::ptrdiff_t>(from), half * 4,
                        out.rgba.begin() + static_cast<std::ptrdiff_t>(to));
        }
    };

    for (std::uint32_t k = 0; k < terrains; ++k) {
        const std::uint32_t bx = (k % blocks_across) * 2 * tile_size;
        const std::uint32_t by = (k / blocks_across) * 3 * tile_size;
        for (std::uint32_t s = 0; s < autotile_shapes; ++s) {
            const std::uint32_t tile = (k * autotile_shapes) + s;
            const std::uint32_t x = (tile % autotile_columns) * tile_size;
            const std::uint32_t y = (tile / autotile_columns) * tile_size;
            if (s == autotile_shapes - 1) {
                // The preview, as drawn.
                for (std::uint32_t q = 0; q < 4; ++q) {
                    copy(bx, by, q % 2, q / 2, x + ((q % 2) * half), y + ((q / 2) * half));
                }
                continue;
            }
            const std::uint8_t m = autotile_masks().at(s);
            // Each quarter: left or right (h), top or bottom (v).
            for (std::uint32_t q = 0; q < 4; ++q) {
                const bool left = q % 2 == 0;
                const bool top = q / 2 == 0;
                const bool side = (m & (left ? west : east)) != 0;
                const bool updown = (m & (top ? north : south)) != 0;
                const std::uint8_t corner =
                    top ? (left ? north_west : north_east) : (left ? south_west : south_east);
                const bool diagonal = (m & corner) != 0;
                const std::uint32_t outer_x = left ? 0 : 3;
                const std::uint32_t outer_y = top ? 2 : 5;
                const std::uint32_t inner_x = left ? 2 : 1;
                const std::uint32_t inner_y = top ? 4 : 3;
                std::uint32_t qx = outer_x;
                std::uint32_t qy = outer_y;
                if (side && updown) {
                    if (diagonal) {
                        qx = inner_x; // the middle
                        qy = inner_y;
                    } else {
                        qx = left ? 2 : 3; // a corner cut in, from the inner-corner tile
                        qy = top ? 0 : 1;
                    }
                } else if (side) {
                    qx = inner_x; // the edge along the top or bottom
                } else if (updown) {
                    qy = inner_y; // the edge down the left or right
                }
                copy(bx, by, qx, qy, x + (left ? 0 : half), y + (top ? 0 : half));
            }
        }
    }
    return out;
}

} // namespace engine::assets
