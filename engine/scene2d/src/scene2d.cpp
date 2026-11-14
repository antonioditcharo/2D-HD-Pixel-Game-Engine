#include "engine/scene2d/scene2d.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace engine::scene2d {

void register_types(reflect::Registry& registry) {
    using reflect::property_flag::advanced;
    auto transform = registry.add_type<Transform2D>("transform_2d")
                         .display_name("Position")
                         .category("Basics")
                         .tooltip("Where the object is in the map.");
    transform.property<&Transform2D::x>("x")
        .unit(reflect::Unit::Tiles)
        .display_name("Across")
        .tooltip("Distance from the left of the map.");
    transform.property<&Transform2D::y>("y")
        .unit(reflect::Unit::Tiles)
        .display_name("Down")
        .tooltip("Distance from the top of the map.");
    transform.property<&Transform2D::layer>("layer").range(-100, 100).flags(advanced).tooltip(
        "Higher layers are drawn on top. Within a layer, lower objects are drawn on top.");

    auto sprite =
        registry.add_type<Sprite>("sprite").display_name("Sprite").category("Visuals").tooltip(
            "Shows a picture at the object's position.");
    sprite.property<&Sprite::image>("image").tooltip("The picture to show.");
    sprite.property<&Sprite::recolor>("recolor")
        .display_name("Recolour")
        .tooltip("Colours of the picture drawn as others, as #from>#to pairs: the same "
                 "picture as a red slime and a green one. The Recolour button picks them.");
    sprite.property<&Sprite::visible>("visible").tooltip("Hide the picture without removing it.");
    sprite.property<&Sprite::pivot_x>("pivot_x").range(0.0, 1.0).step(0.5).flags(advanced).tooltip(
        "Which point of the picture sits on the position: 0 is the left edge, 1 the right.");
    sprite.property<&Sprite::pivot_y>("pivot_y").range(0.0, 1.0).step(0.5).flags(advanced).tooltip(
        "Which point of the picture sits on the position: 0 is the top edge, 1 the bottom.");
    sprite.property<&Sprite::tint_r>("tint_r").range(0, 255).flags(advanced).display_name(
        "Tint red");
    sprite.property<&Sprite::tint_g>("tint_g").range(0, 255).flags(advanced).display_name(
        "Tint green");
    sprite.property<&Sprite::tint_b>("tint_b").range(0, 255).flags(advanced).display_name(
        "Tint blue");
    sprite.property<&Sprite::tint_a>("tint_a").range(0, 255).flags(advanced).display_name(
        "Opacity");
    sprite.property<&Sprite::frame_width>("frame_width")
        .range(0, 4096)
        .unit(reflect::Unit::Pixels)
        .flags(advanced)
        .display_name("Frame width")
        .tooltip("For a sprite sheet: the width of one frame in pixels. 0 uses the whole "
                 "picture.");
    sprite.property<&Sprite::frame_height>("frame_height")
        .range(0, 4096)
        .unit(reflect::Unit::Pixels)
        .flags(advanced)
        .display_name("Frame height")
        .tooltip("For a sprite sheet: the height of one frame in pixels. 0 uses the whole "
                 "picture.");
    sprite.property<&Sprite::frame>("frame").range(0, 65535).flags(advanced).tooltip(
        "Which frame of the sprite sheet to show, counting from 0, left to right, then top "
        "to bottom.");
    sprite.property<&Sprite::rotation>("rotation")
        .range(-360.0, 360.0)
        .step(15.0)
        .unit(reflect::Unit::Degrees)
        .tooltip("Turns the picture about its pivot, clockwise.");
    sprite.property<&Sprite::scale_x>("scale_x")
        .range(-16.0, 16.0)
        .step(0.5)
        .display_name("Scale across")
        .tooltip("Stretches the picture across: 2 is twice as wide. Below 0 it is flipped.");
    sprite.property<&Sprite::scale_y>("scale_y")
        .range(-16.0, 16.0)
        .step(0.5)
        .display_name("Scale down")
        .tooltip("Stretches the picture up and down: 2 is twice as tall. Below 0 it is "
                 "upside down.");

    auto animation = registry.add_type<SpriteAnimation>("sprite_animation")
                         .display_name("Animation")
                         .category("Visuals")
                         .tooltip("Plays frames of a sprite sheet one after another.");
    animation.property<&SpriteAnimation::tag>("tag").display_name("Tag").tooltip(
        "A tag of the sprite's sheet to play, as named in Sprite Studio (walk, idle): "
        "its frames, each for its own time, in its direction. Empty: the frames "
        "below, by number.");
    animation.property<&SpriteAnimation::first_frame>("first_frame")
        .range(0, 65535)
        .display_name("First frame")
        .tooltip("The frame the animation starts on.");
    animation.property<&SpriteAnimation::frame_count>("frame_count")
        .range(1, 1024)
        .display_name("Frames")
        .tooltip("How many frames the animation has, from the first frame on.");
    animation.property<&SpriteAnimation::frames_per_second>("frames_per_second")
        .range(0.0, 60.0)
        .step(1.0)
        .display_name("Speed")
        .tooltip("Frames per second.");
    animation.property<&SpriteAnimation::loop>("loop").tooltip(
        "Start again after the last frame. Otherwise it stops there and says it finished.");
    animation.property<&SpriteAnimation::playing>("playing").tooltip("Whether it is playing.");
    animation.property<&SpriteAnimation::elapsed>("elapsed")
        .unit(reflect::Unit::Seconds)
        .flags(reflect::property_flag::hidden)
        .tooltip("How long it has been playing.");
    animation.action<&SpriteAnimation::play>("play")
        .text("Play the animation from the start")
        .tooltip("Starts the animation again from its first frame.");
    animation.action<&SpriteAnimation::play_tag>("play_tag", {"name"})
        .text("Play the animation {name}")
        .tooltip("Plays a tag of the sprite's sheet from its start, such as walk or idle.");
    animation.action<&SpriteAnimation::stop>("stop")
        .text("Stop the animation")
        .tooltip("Stops on the frame it is showing.");
    animation.event<>("finished")
        .text("When the animation finishes")
        .tooltip("Happens when an animation that does not loop reaches its last frame.");

    auto tilemap =
        registry.add_type<Tilemap>("tilemap").display_name("Tilemap").category("Visuals").tooltip(
            "A grid of tiles painted from a tileset picture.");
    tilemap.property<&Tilemap::tileset>("tileset").tooltip("The picture the tiles come from.");
    tilemap.property<&Tilemap::tile_size>("tile_size")
        .range(0, 256)
        .unit(reflect::Unit::Pixels)
        .flags(advanced)
        .display_name("Tile size")
        .tooltip("The size of one tile in pixels. 0 uses the project's pixels per tile.");
    tilemap.property<&Tilemap::tiles>("tiles")
        .flags(reflect::property_flag::hidden)
        .tooltip("The painted tiles.");
    tilemap.property<&Tilemap::visible>("visible").tooltip("Hide the tiles without removing "
                                                           "them.");
    tilemap.property<&Tilemap::height>("height")
        .range(0.0, 16.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("In the Diorama style, how tall each tile stands: 0 is flat ground.");
    tilemap.property<&Tilemap::heights>("heights")
        .flags(reflect::property_flag::advanced)
        .display_name("Tile heights")
        .tooltip("Each tile's own height in the Diorama style, painted with the Raise and "
                 "Lower brushes; empty cells stand at Height.");
    tilemap.property<&Tilemap::solid>("solid").tooltip(
        "Every tile of this map blocks movement. Paint walls on their own map.");
    tilemap.property<&Tilemap::shapes>("shapes")
        .display_name("Tile shapes")
        .flags(advanced)
        .tooltip("On a solid map, tiles that block only part of their square: 12 top, 13 "
                 "bottom, 7 none (full, none, top, bottom, left or right). Others block it all.");
    tilemap.property<&Tilemap::animation_frames>("animation_frames")
        .range(1, 64)
        .display_name("Animation frames")
        .tooltip("Animated tiles: each tile shows itself, then the next tiles of the tileset "
                 "in turn. 1 keeps them still.");
    tilemap.property<&Tilemap::animation_fps>("animation_fps")
        .range(0.0, 60.0)
        .step(1.0)
        .display_name("Animation speed")
        .tooltip("Frames a second of animated tiles.");
    tilemap.property<&Tilemap::elapsed>("elapsed")
        .unit(reflect::Unit::Seconds)
        .flags(reflect::property_flag::hidden)
        .tooltip("How long the tiles have been animating.");
    tilemap.property<&Tilemap::footsteps>("footsteps")
        .display_name("Footsteps")
        .tooltip("The sound walking on these tiles makes (grass, stone, wood); empty to keep "
                 "the walker's own.");
    tilemap.property<&Tilemap::autotile>("autotile")
        .display_name("Auto-tile")
        .tooltip("Painting picks each tile's edges and corners from its neighbours. For a "
                 "tileset made with Make auto-tiles in the Assets.");
    tilemap.property<&Tilemap::opacity>("opacity").range(0.0, 1.0).step(0.05).tooltip(
        "How solid the tiles look: lower for water over a riverbed, or glass, so "
        "what is behind shows through.");

    auto light =
        registry.add_type<Light>("light").display_name("Light").category("Visuals").tooltip(
            "Light from a point, such as a torch or a lamp, in the Diorama style.");
    light.property<&Light::color_r>("color_r").range(0, 255).display_name("Red");
    light.property<&Light::color_g>("color_g").range(0, 255).display_name("Green");
    light.property<&Light::color_b>("color_b").range(0, 255).display_name("Blue");
    light.property<&Light::intensity>("intensity")
        .range(0.0, 8.0)
        .step(0.1)
        .tooltip("How strong the light is: 1 is ordinary, more is brighter.");
    light.property<&Light::radius>("radius")
        .range(0.0, 32.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How far the light reaches; it fades to nothing there.");
    light.property<&Light::height>("height")
        .range(0.0, 16.0)
        .step(0.5)
        .unit(reflect::Unit::Tiles)
        .tooltip("How high above the ground the light is.");
    light.property<&Light::enabled>("enabled").tooltip("Turn the light off without removing it.");
}

bool register_components(world::World& world) {
    const bool transform = world.register_component<Transform2D>();
    const bool sprite = world.register_component<Sprite>();
    const bool animation = world.register_component<SpriteAnimation>();
    const bool tilemap = world.register_component<Tilemap>();
    const bool light = world.register_component<Light>();
    return transform && sprite && animation && tilemap && light;
}

// ---------------------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::optional<TileGrid> parse_tiles(std::string_view text, std::string& error) {
    TileGrid grid;
    if (text.empty()) {
        return grid;
    }
    std::vector<std::vector<std::int32_t>> rows;
    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const std::size_t line_end = std::min(text.find('\n', line_start), text.size());
        const std::string_view line = text.substr(line_start, line_end - line_start);
        if (rows.size() == max_tilemap_side) {
            error = "A tilemap can have at most " + std::to_string(max_tilemap_side) + " rows.";
            return std::nullopt;
        }
        std::vector<std::int32_t>& row = rows.emplace_back();
        std::size_t cell_start = 0;
        while (cell_start <= line.size()) {
            const std::size_t cell_end = std::min(line.find(',', cell_start), line.size());
            const std::string_view cell = trim(line.substr(cell_start, cell_end - cell_start));
            if (row.size() == max_tilemap_side) {
                error =
                    "A tilemap can have at most " + std::to_string(max_tilemap_side) + " columns.";
                return std::nullopt;
            }
            std::int64_t value = -1;
            if (!cell.empty()) {
                const auto [end, result] =
                    std::from_chars(cell.data(), cell.data() + cell.size(), value);
                if (result != std::errc{} || end != cell.data() + cell.size() || value < -1 ||
                    value > std::numeric_limits<std::int32_t>::max()) {
                    error = "Row " + std::to_string(rows.size()) + ", column " +
                            std::to_string(row.size() + 1) + " is '" + std::string(cell) +
                            "', which is not a tile number (a whole number from -1 up).";
                    return std::nullopt;
                }
            }
            row.push_back(static_cast<std::int32_t>(value));
            cell_start = cell_end + 1;
        }
        line_start = line_end + 1;
    }
    std::size_t width = 0;
    for (const std::vector<std::int32_t>& row : rows) {
        width = std::max(width, row.size());
    }
    grid.width = static_cast<std::uint32_t>(width);
    grid.height = static_cast<std::uint32_t>(rows.size());
    grid.cells.assign(width * rows.size(), -1);
    for (std::size_t y = 0; y < rows.size(); ++y) {
        std::copy(rows[y].begin(), rows[y].end(),
                  grid.cells.begin() + static_cast<std::ptrdiff_t>(y * width));
    }
    return grid;
}

std::string format_tiles(const TileGrid& grid) {
    std::string text;
    if (grid.width == 0 || grid.height == 0) {
        return text;
    }
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        if (y > 0) {
            text += '\n';
        }
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            if (x > 0) {
                text += ',';
            }
            if (const std::int32_t tile = grid.at(x, y); tile >= 0) {
                text += std::to_string(tile);
            }
        }
    }
    return text;
}

std::uint32_t Tilemap::frame() const noexcept {
    if (animation_frames <= 1 || !std::isfinite(elapsed) || !std::isfinite(animation_fps) ||
        animation_fps <= 0.0) {
        return 0;
    }
    const double index = std::floor((std::max(elapsed, 0.0) * animation_fps) + 1e-9);
    return static_cast<std::uint32_t>(std::fmod(index, static_cast<double>(animation_frames)));
}

std::uint32_t Tilemap::shown(std::uint32_t number, std::uint64_t tile_count) const noexcept {
    const std::uint64_t moved = std::uint64_t{number} + frame();
    return moved < tile_count ? static_cast<std::uint32_t>(moved) : number;
}

std::uint8_t Tilemap::alpha() const noexcept {
    const double o = std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 1.0;
    return static_cast<std::uint8_t>(std::lround(o * 255.0));
}

const TileGrid* Tilemap::grid(std::string& error) const {
    if (!parsed_once_ || parsed_text_ != tiles) {
        parse_error_.clear();
        parsed_ = parse_tiles(tiles, parse_error_);
        parsed_text_ = tiles;
        parsed_once_ = true;
    }
    if (!parsed_) {
        error = parse_error_;
        return nullptr;
    }
    return &*parsed_;
}

std::vector<std::vector<double>> parse_heights(std::string_view text) {
    std::vector<std::vector<double>> rows(1);
    std::string cell;
    const auto finish = [&] {
        double value = std::numeric_limits<double>::quiet_NaN();
        const std::size_t a = cell.find_first_not_of(" \t\r");
        if (a != std::string::npos) {
            const std::size_t b = cell.find_last_not_of(" \t\r");
            const std::string_view number(cell.data() + a, b - a + 1);
            double parsed = 0.0;
            const auto [end, error] =
                std::from_chars(number.data(), number.data() + number.size(), parsed);
            if (error == std::errc{} && end == number.data() + number.size() &&
                std::isfinite(parsed)) {
                value = std::clamp(parsed, 0.0, 16.0);
            }
        }
        rows.back().push_back(value);
        cell.clear();
    };
    for (const char c : text) {
        if (c == ',') {
            finish();
        } else if (c == '\n') {
            finish();
            rows.emplace_back();
        } else {
            cell += c;
        }
    }
    finish();
    // Rows of nothing at the end are no rows.
    while (!rows.empty() && std::all_of(rows.back().begin(), rows.back().end(),
                                        [](double v) { return std::isnan(v); })) {
        rows.pop_back();
    }
    return rows;
}

std::string format_heights(const std::vector<std::vector<double>>& heights) {
    std::string out;
    for (std::size_t r = 0; r < heights.size(); ++r) {
        if (r > 0) {
            out += '\n';
        }
        // Empty cells at the end of a row are left off.
        std::size_t used = heights[r].size();
        while (used > 0 && std::isnan(heights[r][used - 1])) {
            --used;
        }
        for (std::size_t c = 0; c < used; ++c) {
            if (c > 0) {
                out += ',';
            }
            const double v = heights[r][c];
            if (std::isnan(v)) {
                continue;
            }
            if (v == std::floor(v)) {
                out += std::to_string(static_cast<std::int64_t>(v));
            } else {
                std::array<char, 32> buffer{};
                const auto [end, error] =
                    std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
                out.append(buffer.data(), error == std::errc{} ? end : buffer.data());
            }
        }
    }
    return out;
}

double Tilemap::height_at(std::uint32_t x, std::uint32_t y) const {
    if (!heights_once_ || heights_text_ != heights) {
        heights_ = heights.empty() ? std::vector<std::vector<double>>{} : parse_heights(heights);
        heights_text_ = heights;
        heights_once_ = true;
    }
    const double fallback = std::isfinite(height) ? std::clamp(height, 0.0, 16.0) : 0.0;
    if (y >= heights_.size() || x >= heights_[y].size() || std::isnan(heights_[y][x])) {
        return fallback;
    }
    return heights_[y][x];
}

// ---------------------------------------------------------------------------------------
// Collisions
// ---------------------------------------------------------------------------------------

namespace {

/// Tolerance for positions that are a rounding error away from a tile edge.
constexpr double edge_tolerance = 1e-9;

/// The tile lines a span [low, high) covers, as [first, end), not clamped to the map.
struct Span {
    double first = 0.0;
    double end = 0.0;
};

Span covered(double low, double high, double tile) {
    return Span{std::floor((low + edge_tolerance) / tile),
                std::ceil((high - edge_tolerance) / tile)};
}

} // namespace

bool overlaps(const Box& a, const Box& b) noexcept {
    return a.left < b.right - edge_tolerance && b.left < a.right - edge_tolerance &&
           a.top < b.bottom - edge_tolerance && b.top < a.bottom - edge_tolerance;
}

double sweep_boxes(const Box& box, Axis axis, double distance, const std::vector<Box>& obstacles) {
    if (!std::isfinite(distance) || distance == 0.0) {
        return 0.0;
    }
    const bool x_axis = axis == Axis::X;
    const double low = x_axis ? box.left : box.top;
    const double high = x_axis ? box.right : box.bottom;
    const double across_low = x_axis ? box.top : box.left;
    const double across_high = x_axis ? box.bottom : box.right;
    double allowed = distance;
    for (const Box& obstacle : obstacles) {
        const double o_low = x_axis ? obstacle.left : obstacle.top;
        const double o_high = x_axis ? obstacle.right : obstacle.bottom;
        const double o_across_low = x_axis ? obstacle.top : obstacle.left;
        const double o_across_high = x_axis ? obstacle.bottom : obstacle.right;
        const bool beside = o_across_high <= across_low + edge_tolerance ||
                            o_across_low >= across_high - edge_tolerance;
        if (beside || overlaps(box, obstacle)) {
            continue;
        }
        if (distance > 0.0 && o_low >= high - edge_tolerance) {
            allowed = std::min(allowed, std::max(0.0, o_low - high));
        } else if (distance < 0.0 && o_high <= low + edge_tolerance) {
            allowed = std::max(allowed, std::min(0.0, o_high - low));
        }
    }
    return allowed;
}

namespace {

/// The parts of `world`'s solid tiles near `area` (in units), as boxes: each tile's
/// square, or the part of it its shape blocks.
std::vector<Box> solid_boxes(const world::World& world, const Box& area, std::uint32_t ppu) {
    std::vector<Box> boxes;
    const reflect::TypeInfo* type = world.registry().find_type<Tilemap>();
    if (type == nullptr) {
        return boxes;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Tilemap* tilemap = ref.as<Tilemap>();
        const Transform2D* transform = world.get<Transform2D>(entity);
        if (tilemap == nullptr || transform == nullptr || !tilemap->solid) {
            return;
        }
        std::string error;
        const TileGrid* grid = tilemap->grid(error);
        if (grid == nullptr || grid->width == 0) {
            return;
        }
        const std::uint32_t units = std::max<std::uint32_t>(ppu, 1);
        const std::uint32_t pixels = tilemap->tile_size == 0 ? units : tilemap->tile_size;
        const double tile = static_cast<double>(pixels) / static_cast<double>(units);
        const Span across = covered(area.left - transform->x, area.right - transform->x, tile);
        const Span down = covered(area.top - transform->y, area.bottom - transform->y, tile);
        if (!std::isfinite(across.first) || !std::isfinite(across.end) ||
            !std::isfinite(down.first) || !std::isfinite(down.end) || !std::isfinite(tile)) {
            return; // a broken position: nothing to compare against
        }
        // One cell more each way, so tiles just beyond an edge are known too.
        const auto first_x = static_cast<std::int64_t>(std::max(across.first - 1.0, 0.0));
        const auto end_x =
            static_cast<std::int64_t>(std::min(across.end + 1.0, static_cast<double>(grid->width)));
        const auto first_y = static_cast<std::int64_t>(std::max(down.first - 1.0, 0.0));
        const auto end_y =
            static_cast<std::int64_t>(std::min(down.end + 1.0, static_cast<double>(grid->height)));
        std::string ignored;
        const std::vector<std::pair<std::int32_t, TileShape>> shapes =
            tilemap->shapes.empty() ? std::vector<std::pair<std::int32_t, TileShape>>{}
                                    : parse_tile_shapes(tilemap->shapes, ignored);
        for (std::int64_t y = first_y; y < end_y; ++y) {
            for (std::int64_t x = first_x; x < end_x; ++x) {
                const std::int32_t t =
                    grid->at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                if (t < 0) {
                    continue;
                }
                TileShape shape = TileShape::Full;
                for (const auto& [number, given] : shapes) {
                    if (number == t) {
                        shape = given;
                    }
                }
                if (const std::optional<Box> part = tile_shape_box(shape, tile)) {
                    const double left = transform->x + (static_cast<double>(x) * tile);
                    const double top = transform->y + (static_cast<double>(y) * tile);
                    boxes.push_back(Box{left + part->left, top + part->top, left + part->right,
                                        top + part->bottom});
                }
            }
        }
    });
    return boxes;
}

} // namespace

std::vector<std::pair<std::int32_t, TileShape>> parse_tile_shapes(std::string_view text,
                                                                  std::string& error) {
    std::vector<std::pair<std::int32_t, TileShape>> out;
    error.clear();
    constexpr std::array<std::pair<std::string_view, TileShape>, 6> names{{
        {"full", TileShape::Full},
        {"none", TileShape::None},
        {"top", TileShape::Top},
        {"bottom", TileShape::Bottom},
        {"left", TileShape::Left},
        {"right", TileShape::Right},
    }};
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t comma = text.find(',', at);
        std::string_view pair =
            text.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at);
        at = comma == std::string_view::npos ? text.size() + 1 : comma + 1;
        while (!pair.empty() && pair.front() == ' ') {
            pair.remove_prefix(1);
        }
        while (!pair.empty() && pair.back() == ' ') {
            pair.remove_suffix(1);
        }
        if (pair.empty()) {
            continue;
        }
        const std::size_t space = pair.find(' ');
        std::int32_t number = -1;
        const std::string_view digits = pair.substr(0, space);
        const auto [end, failed] =
            std::from_chars(digits.data(), digits.data() + digits.size(), number);
        std::string word(space == std::string_view::npos ? std::string_view{}
                                                         : pair.substr(space + 1));
        std::transform(word.begin(), word.end(), word.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        while (!word.empty() && word.front() == ' ') {
            word.erase(word.begin());
        }
        const auto found = std::find_if(names.begin(), names.end(),
                                        [&word](const auto& n) { return n.first == word; });
        if (failed != std::errc{} || end != digits.data() + digits.size() || number < 0 ||
            found == names.end()) {
            if (error.empty()) {
                error = "\"" + std::string(pair) +
                        "\" is not a tile and a shape (full, none, top, bottom, left, right).";
            }
            continue;
        }
        out.emplace_back(number, found->second);
    }
    return out;
}

std::optional<Box> tile_shape_box(TileShape shape, double tile) noexcept {
    const double half = tile / 2.0;
    switch (shape) {
    case TileShape::None:
        return std::nullopt;
    case TileShape::Top:
        return Box{0.0, 0.0, tile, half};
    case TileShape::Bottom:
        return Box{0.0, half, tile, tile};
    case TileShape::Left:
        return Box{0.0, 0.0, half, tile};
    case TileShape::Right:
        return Box{half, 0.0, tile, tile};
    case TileShape::Full:
        break;
    }
    return Box{0.0, 0.0, tile, tile};
}

double sweep(const world::World& world, const Box& box, Axis axis, double distance,
             std::uint32_t ppu) {
    if (!std::isfinite(distance) || distance == 0.0) {
        return 0.0;
    }
    // Every solid part the box could meet on its way, then the boxes' own sweep.
    Box area = box;
    if (axis == Axis::X) {
        area.left = std::min(box.left, box.left + distance);
        area.right = std::max(box.right, box.right + distance);
    } else {
        area.top = std::min(box.top, box.top + distance);
        area.bottom = std::max(box.bottom, box.bottom + distance);
    }
    return sweep_boxes(box, axis, distance, solid_boxes(world, area, ppu));
}

bool solid_at(const world::World& world, const Box& box, std::uint32_t ppu) {
    const std::vector<Box> parts = solid_boxes(world, box, ppu);
    return std::any_of(parts.begin(), parts.end(),
                       [&box](const Box& part) { return overlaps(box, part); });
}

// ---------------------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------------------

const SheetTag* SheetInfo::find(std::string_view name) const noexcept {
    for (const SheetTag& tag : tags) {
        if (tag.name == name) {
            return &tag;
        }
    }
    return nullptr;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>>
SheetInfo::sequence(const SheetTag& tag) const {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    const auto shown = [&](std::uint32_t frame) {
        const std::uint32_t ms = frame < durations_ms.size() ? durations_ms[frame] : 100U;
        out.emplace_back(frame, std::max<std::uint32_t>(ms, 1U));
    };
    if (tag.from > tag.to) {
        return out;
    }
    switch (tag.direction) {
    case TagDirection::Forward:
        for (std::uint32_t f = tag.from; f <= tag.to; ++f) {
            shown(f);
        }
        break;
    case TagDirection::Reverse:
        for (std::uint32_t f = tag.to + 1; f-- > tag.from;) {
            shown(f);
        }
        break;
    case TagDirection::PingPong:
        for (std::uint32_t f = tag.from; f <= tag.to; ++f) {
            shown(f);
        }
        for (std::uint32_t f = tag.to; f > tag.from + 1;) {
            shown(--f);
        }
        break;
    }
    return out;
}

std::string sheet_path(std::string_view image_path) {
    std::string path(image_path);
    const std::size_t slash = path.find_last_of('/');
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        path.erase(dot);
    }
    return path + ".sheet";
}

std::optional<SheetInfo> parse_sheet(std::string_view text, std::string& error) {
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> parsed = serialize::parse(text, issues);
    if (!parsed || !parsed->is_object() || parsed->value("format", std::string()) != sheet_format) {
        error = "This is not a sprite sheet's description (\"sparxel.sheet\").";
        return std::nullopt;
    }
    const serialize::Json& json = *parsed;
    if (!json.contains("version") || !json["version"].is_number_integer() ||
        json["version"].get<int>() > sheet_version) {
        error = "This sheet was saved by a newer version of Sparxel.";
        return std::nullopt;
    }
    const auto whole = [](const serialize::Json& at, const char* key, std::int64_t low,
                          std::int64_t high) -> std::optional<std::uint32_t> {
        if (!at.contains(key) || !at[key].is_number_integer()) {
            return std::nullopt;
        }
        const auto v = at[key].get<std::int64_t>();
        if (v < low || v > high) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(v);
    };
    SheetInfo sheet;
    const std::optional<std::uint32_t> width = whole(json, "frame_width", 1, 4096);
    const std::optional<std::uint32_t> height = whole(json, "frame_height", 1, 4096);
    if (!width || !height) {
        error = "A sheet's frames are from 1 to 4,096 pixels each way.";
        return std::nullopt;
    }
    sheet.frame_width = *width;
    sheet.frame_height = *height;
    if (json.contains("durations_ms") && json["durations_ms"].is_array()) {
        for (const serialize::Json& ms : json["durations_ms"]) {
            if (sheet.durations_ms.size() >= 4096) {
                break;
            }
            sheet.durations_ms.push_back(ms.is_number_integer()
                                             ? static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                                                   ms.get<std::int64_t>(), 1, 65535))
                                             : 100U);
        }
    }
    if (sheet.durations_ms.empty()) {
        error = "A sheet needs at least one frame.";
        return std::nullopt;
    }
    const auto frames = static_cast<std::int64_t>(sheet.durations_ms.size());
    if (json.contains("tags") && json["tags"].is_array()) {
        for (const serialize::Json& tag : json["tags"]) {
            if (!tag.is_object()) {
                continue;
            }
            const std::optional<std::uint32_t> from = whole(tag, "from", 0, frames - 1);
            const std::optional<std::uint32_t> to = whole(tag, "to", 0, frames - 1);
            if (!from || !to || *from > *to) {
                continue;
            }
            SheetTag t;
            t.name = tag.value("name", std::string());
            t.from = *from;
            t.to = *to;
            const std::string direction = tag.value("direction", std::string("forward"));
            t.direction = direction == "reverse"     ? TagDirection::Reverse
                          : direction == "ping_pong" ? TagDirection::PingPong
                                                     : TagDirection::Forward;
            if (!t.name.empty()) {
                sheet.tags.push_back(std::move(t));
            }
        }
    }
    return sheet;
}

std::string sheet_to_text(const SheetInfo& sheet) {
    serialize::Json json = serialize::Json::object();
    json["format"] = sheet_format;
    json["version"] = sheet_version;
    json["frame_width"] = sheet.frame_width;
    json["frame_height"] = sheet.frame_height;
    json["durations_ms"] = sheet.durations_ms;
    serialize::Json tags = serialize::Json::array();
    for (const SheetTag& tag : sheet.tags) {
        const char* direction = tag.direction == TagDirection::Reverse    ? "reverse"
                                : tag.direction == TagDirection::PingPong ? "ping_pong"
                                                                          : "forward";
        tags.push_back(
            {{"name", tag.name}, {"from", tag.from}, {"to", tag.to}, {"direction", direction}});
    }
    json["tags"] = std::move(tags);
    return serialize::to_text(json);
}

SheetLibrary::SheetLibrary(const assets::AssetDatabase& assets) : assets_(assets) {}

const SheetInfo* SheetLibrary::find(std::string_view image) {
    const auto known = sheets_.find(std::string(image));
    if (known != sheets_.end()) {
        if (const auto& found = known->second; found.has_value()) {
            return &*found;
        }
        return nullptr;
    }
    std::optional<SheetInfo> sheet;
    const std::optional<Guid> guid = Guid::parse(image);
    const assets::AssetRecord* picture = guid ? assets_.find(*guid) : nullptr;
    const assets::AssetRecord* file =
        picture != nullptr ? assets_.find(sheet_path(picture->path)) : nullptr;
    if (file != nullptr) {
        if (const std::optional<std::vector<std::uint8_t>> bytes = assets_.read(file->guid)) {
            std::string error;
            sheet =
                parse_sheet(std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                             bytes->size()),
                            error);
        }
    }
    const auto [at, added] = sheets_.emplace(std::string(image), std::move(sheet));
    static_cast<void>(added);
    if (const auto& made = at->second; made.has_value()) {
        return &*made;
    }
    return nullptr;
}

void SheetLibrary::apply(const assets::AssetChanges& changes) {
    // A sheet belongs to a picture by path, so any change may touch one: read them again.
    if (!changes.empty()) {
        sheets_.clear();
    }
}

std::vector<world::Entity> animate(world::World& world, double seconds, SheetLibrary* sheets) {
    std::vector<world::Entity> finished;
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    // Animated tiles: their time runs on; Tilemap::frame() wraps it.
    if (const reflect::TypeInfo* maps = world.registry().find_type<Tilemap>()) {
        world.each(*maps, [seconds](world::Entity, reflect::ObjectRef ref) {
            Tilemap* map = ref.as<Tilemap>();
            if (map != nullptr && map->animation_frames > 1) {
                map->elapsed = std::isfinite(map->elapsed) ? map->elapsed + seconds : seconds;
            }
        });
    }
    const reflect::TypeInfo* type = world.registry().find_type<SpriteAnimation>();
    if (type == nullptr) {
        return finished;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        SpriteAnimation* animation = ref.as<SpriteAnimation>();
        Sprite* sprite = world.get<Sprite>(entity);
        if (animation == nullptr || sprite == nullptr || animation->frame_count == 0) {
            return;
        }
        if (animation->playing) {
            animation->elapsed += seconds;
        }
        // A tag of the sheet: its frames, each for its own time.
        const SheetInfo* sheet =
            sheets != nullptr && !animation->tag.empty() ? sheets->find(sprite->image) : nullptr;
        const SheetTag* tag = sheet != nullptr ? sheet->find(animation->tag) : nullptr;
        if (tag != nullptr) {
            const auto frames = sheet->sequence(*tag);
            double total = 0.0;
            for (const auto& [frame, ms] : frames) {
                total += ms;
            }
            double at =
                std::isfinite(animation->elapsed) ? animation->elapsed * 1000.0 + 1e-6 : 0.0;
            std::size_t shown = frames.size() - 1;
            if (animation->loop) {
                at = std::fmod(std::max(at, 0.0), total);
            } else if (at >= total) {
                at = total;
                if (animation->playing) {
                    animation->playing = false;
                    finished.push_back(entity);
                }
            }
            double start = 0.0;
            for (std::size_t i = 0; i < frames.size(); ++i) {
                start += frames[i].second;
                if (at < start) {
                    shown = i;
                    break;
                }
            }
            sprite->frame_width = sheet->frame_width;
            sprite->frame_height = sheet->frame_height;
            sprite->frame = frames[shown].first;
            return;
        }
        const double fps = std::isfinite(animation->frames_per_second)
                               ? std::max(animation->frames_per_second, 0.0)
                               : 0.0;
        const double count = static_cast<double>(animation->frame_count);
        // A tiny tolerance, so that rounding in the sum of fixed steps never shows a frame
        // one step late.
        double index = std::floor((animation->elapsed * fps) + 1e-9);
        if (!std::isfinite(index) || index < 0.0) {
            index = 0.0;
        }
        if (animation->loop) {
            index = std::fmod(index, count);
        } else if (index >= count) {
            index = count - 1.0;
            if (animation->playing) {
                animation->playing = false;
                finished.push_back(entity);
            }
        }
        sprite->frame = animation->first_frame + static_cast<std::uint32_t>(index);
    });
    return finished;
}

// ---------------------------------------------------------------------------------------
// TextureCache
// ---------------------------------------------------------------------------------------

TextureCache::TextureCache(render::Device& device, const assets::AssetDatabase& assets)
    : device_(device), assets_(assets) {}

namespace {

std::optional<std::array<std::uint8_t, 3>> hex_rgb(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        unsigned value = 0;
        const char* first = text.data() + (i * 2);
        const auto [end, failed] = std::from_chars(first, first + 2, value, 16);
        if (failed != std::errc{} || end != first + 2) {
            return std::nullopt;
        }
        rgb.at(i) = static_cast<std::uint8_t>(value);
    }
    return rgb;
}

std::string hex_of(const std::array<std::uint8_t, 3>& rgb) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out = "#";
    for (const std::uint8_t v : rgb) {
        out += digits[v >> 4U];
        out += digits[v & 15U];
    }
    return out;
}

} // namespace

std::vector<ColorSwap> parse_recolor(std::string_view text) {
    std::vector<ColorSwap> swaps;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find_first_of(", ", at);
        const std::string_view pair =
            text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        at = end == std::string_view::npos ? text.size() : end + 1;
        const std::size_t arrow = pair.find('>');
        if (arrow == std::string_view::npos) {
            continue;
        }
        const auto from = hex_rgb(pair.substr(0, arrow));
        const auto to = hex_rgb(pair.substr(arrow + 1));
        if (from && to) {
            swaps.push_back(ColorSwap{*from, *to});
        }
    }
    return swaps;
}

std::string recolor_text(const std::vector<ColorSwap>& swaps) {
    std::string out;
    for (const ColorSwap& swap : swaps) {
        if (!out.empty()) {
            out += ", ";
        }
        out += hex_of(swap.from) + ">" + hex_of(swap.to);
    }
    return out;
}

void recolor_image(assets::Image& image, const std::vector<ColorSwap>& swaps) {
    if (swaps.empty()) {
        return;
    }
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        if (image.rgba[i + 3] == 0) {
            continue;
        }
        const std::array<std::uint8_t, 3> now{image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]};
        // The first swap of a colour wins, so the colours swapped to are not swapped again.
        const auto swap =
            std::ranges::find_if(swaps, [&now](const ColorSwap& s) { return s.from == now; });
        if (swap != swaps.end()) {
            std::ranges::copy(swap->to, image.rgba.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

std::vector<std::array<std::uint8_t, 3>> image_colors(const assets::Image& image,
                                                      std::size_t most) {
    std::vector<std::array<std::uint8_t, 3>> colors;
    for (std::size_t i = 0; i + 3 < image.rgba.size() && colors.size() < most; i += 4) {
        if (image.rgba[i + 3] == 0) {
            continue;
        }
        const std::array<std::uint8_t, 3> rgb{image.rgba[i], image.rgba[i + 1], image.rgba[i + 2]};
        if (std::ranges::find(colors, rgb) == colors.end()) {
            colors.push_back(rgb);
        }
    }
    return colors;
}

const render::Texture* TextureCache::get(Guid image, std::string_view recolor) {
    if (recolor.empty()) {
        return get(image);
    }
    const std::string key = image.to_string() + " " + std::string(recolor);
    if (const auto found = recolored_.find(key); found != recolored_.end()) {
        return found->second.get();
    }
    std::optional<assets::Image> pixels = load(image);
    if (!pixels) {
        return nullptr;
    }
    recolor_image(*pixels, parse_recolor(recolor));
    std::unique_ptr<render::Texture> texture =
        device_.create_texture(pixels->width, pixels->height, pixels->rgba);
    if (texture == nullptr) {
        last_error_ = device_.last_error();
        return nullptr;
    }
    const render::Texture* result = texture.get();
    recolored_.emplace(key, std::move(texture));
    return result;
}

const render::Texture* TextureCache::get(Guid image) {
    if (const auto found = textures_.find(image); found != textures_.end()) {
        return found->second.get();
    }
    const std::optional<assets::Image> decoded = load(image);
    if (!decoded) {
        return nullptr;
    }
    std::unique_ptr<render::Texture> texture =
        device_.create_texture(decoded->width, decoded->height, decoded->rgba);
    if (texture == nullptr) {
        last_error_ = assets_.find(image)->path + ": " + device_.last_error();
        return nullptr;
    }
    const render::Texture* result = texture.get();
    textures_.emplace(image, std::move(texture));
    return result;
}

std::optional<assets::Image> TextureCache::load(Guid image) {
    const assets::AssetRecord* record = assets_.find(image);
    if (record == nullptr) {
        last_error_ = "There is no asset " + image.to_string() + ".";
        return std::nullopt;
    }
    if (record->importer != "image") {
        last_error_ = record->path + " is not an image.";
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = assets_.read(image);
    if (!bytes) {
        last_error_ = record->path + " cannot be read.";
        return std::nullopt;
    }
    std::string error;
    std::optional<assets::Image> decoded = assets::decode_png(*bytes, error);
    if (!decoded) {
        last_error_ = record->path + ": " + error;
    }
    return decoded;
}

const render::Texture* TextureCache::light_falloff() {
    if (falloff_ == nullptr) {
        constexpr std::uint32_t size = 128;
        std::vector<std::uint8_t> rgba;
        rgba.reserve(std::size_t{size} * size * 4);
        const double middle = (static_cast<double>(size) - 1.0) / 2.0;
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                const double dx = (static_cast<double>(x) - middle) / (middle + 0.5);
                const double dy = (static_cast<double>(y) - middle) / (middle + 0.5);
                const double near = std::max(0.0, 1.0 - std::sqrt((dx * dx) + (dy * dy)));
                const auto v = static_cast<std::uint8_t>(std::lround(near * near * 255.0));
                rgba.insert(rgba.end(), {v, v, v, 255});
            }
        }
        falloff_ = device_.create_texture(size, size, rgba);
        if (falloff_ == nullptr) {
            last_error_ = device_.last_error();
        }
    }
    return falloff_.get();
}

void TextureCache::apply(const assets::AssetChanges& changes) {
    const auto forget = [this](Guid guid) {
        textures_.erase(guid);
        const std::string prefix = guid.to_string() + " ";
        std::erase_if(recolored_,
                      [&prefix](const auto& entry) { return entry.first.starts_with(prefix); });
    };
    for (const Guid guid : changes.modified) {
        forget(guid);
    }
    for (const Guid guid : changes.removed) {
        forget(guid);
    }
}

// ---------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------

namespace {

/// The sprite's rotation in whole turns removed, from 0 up to 360 (0 if not finite).
double turn_of(const Sprite& sprite) noexcept {
    if (!std::isfinite(sprite.rotation)) {
        return 0.0;
    }
    const double turn = std::fmod(sprite.rotation, 360.0);
    return turn < 0.0 ? turn + 360.0 : turn;
}

} // namespace

std::array<std::array<double, 2>, 4> sprite_corners(const Sprite& sprite, double width,
                                                    double height) noexcept {
    const double sx = std::isfinite(sprite.scale_x) ? sprite.scale_x : 1.0;
    const double sy = std::isfinite(sprite.scale_y) ? sprite.scale_y : 1.0;
    // Exact for quarter turns, so turned pixel art keeps whole pixels.
    const double turn = turn_of(sprite);
    double cos_t = 1.0;
    double sin_t = 0.0;
    if (turn == 90.0) {
        cos_t = 0.0;
        sin_t = 1.0;
    } else if (turn == 180.0) {
        cos_t = -1.0;
    } else if (turn == 270.0) {
        cos_t = 0.0;
        sin_t = -1.0;
    } else if (turn != 0.0) {
        const double radians = turn * 3.14159265358979323846 / 180.0;
        cos_t = std::cos(radians);
        sin_t = std::sin(radians);
    }
    std::array<std::array<double, 2>, 4> corners{};
    const std::array<std::array<double, 2>, 4> frame{
        {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const double x = (frame[i][0] - sprite.pivot_x) * width * sx;
        const double y = (frame[i][1] - sprite.pivot_y) * height * sy;
        // Clockwise as seen, with y growing downwards.
        corners[i] = {(x * cos_t) - (y * sin_t), (x * sin_t) + (y * cos_t)};
    }
    return corners;
}

namespace {

struct Item {
    std::int32_t layer = 0;
    double y = 0.0;
    Guid guid;             ///< The entity's: a stable tie-breaker.
    std::uint8_t part = 0; ///< 0 for a tilemap, 1 for a sprite: the tilemap goes first.
    std::size_t index = 0; ///< The tile's position in its map.
    const render::Texture* texture{nullptr};
    render::Rect source;
    std::array<render::Point, 4> corners; ///< On the target, as SpriteRenderer::draw() takes them.
    render::Color tint;
};

/// The part of `texture` that frame `frame` of a `width` x `height` grid covers. A size of
/// 0, or one larger than the texture, uses the whole texture.
render::Rect frame_rect(const render::Texture& texture, std::uint32_t width, std::uint32_t height,
                        std::uint32_t frame) {
    const std::uint32_t w = width == 0 || width > texture.width() ? texture.width() : width;
    const std::uint32_t h = height == 0 || height > texture.height() ? texture.height() : height;
    const std::uint32_t columns = texture.width() / w;
    const std::uint32_t rows = texture.height() / h;
    const std::uint64_t total = std::uint64_t{columns} * rows;
    const auto f = static_cast<std::uint32_t>(frame % total);
    const std::uint32_t column = f % columns;
    const std::uint32_t row = f / columns;
    return render::Rect{static_cast<float>(column * w), static_cast<float>(row * h),
                        static_cast<float>(w), static_cast<float>(h)};
}

} // namespace

std::optional<Box> map_bounds(const world::World& world, std::uint32_t ppu) {
    const reflect::TypeInfo* type = world.registry().find_type<Tilemap>();
    if (type == nullptr) {
        return std::nullopt;
    }
    std::optional<Box> bounds;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Tilemap* tilemap = ref.as<Tilemap>();
        const Transform2D* transform = world.get<Transform2D>(entity);
        if (tilemap == nullptr || transform == nullptr) {
            return;
        }
        std::string error;
        const TileGrid* grid = tilemap->grid(error);
        if (grid == nullptr || grid->width == 0 || grid->height == 0) {
            return;
        }
        const std::uint32_t units = std::max<std::uint32_t>(ppu, 1);
        const std::uint32_t pixels = tilemap->tile_size == 0 ? units : tilemap->tile_size;
        const double tile = static_cast<double>(pixels) / static_cast<double>(units);
        const Box box{transform->x, transform->y,
                      transform->x + (tile * static_cast<double>(grid->width)),
                      transform->y + (tile * static_cast<double>(grid->height))};
        if (!std::isfinite(box.left) || !std::isfinite(box.top) || !std::isfinite(box.right) ||
            !std::isfinite(box.bottom)) {
            return;
        }
        if (!bounds) {
            bounds = box;
            return;
        }
        bounds->left = std::min(bounds->left, box.left);
        bounds->top = std::min(bounds->top, box.top);
        bounds->right = std::max(bounds->right, box.right);
        bounds->bottom = std::max(bounds->bottom, box.bottom);
    });
    return bounds;
}

namespace {

/// One axis of clamp_view(): `at` moved so [at + low, at + high] stays in [min, max].
double clamp_axis(double at, double low, double high, double min, double max) noexcept {
    if (!std::isfinite(at) || !std::isfinite(min) || !std::isfinite(max) || min > max) {
        return at;
    }
    const bool low_limited = std::isfinite(low);
    const bool high_limited = std::isfinite(high);
    if (low_limited && high_limited && high - low >= max - min) {
        return ((min + max) / 2.0) - ((low + high) / 2.0); // larger than the map: centred
    }
    if (low_limited && at + low < min) {
        return min - low;
    }
    if (high_limited && at + high > max) {
        return max - high;
    }
    return at;
}

} // namespace

std::array<double, 2> clamp_view(double x, double y, const Box& view, const Box& bounds) noexcept {
    return {clamp_axis(x, view.left, view.right, bounds.left, bounds.right),
            clamp_axis(y, view.top, view.bottom, bounds.top, bounds.bottom)};
}

double lights_2d_strength(double tint_r, double tint_g, double tint_b) {
    const double sum = tint_r + tint_g + tint_b;
    const double brightness = std::isfinite(sum) ? std::clamp(sum / 3.0, 0.0, 1.0) : 1.0;
    return 0.25 + (0.75 * (1.0 - brightness));
}

std::size_t draw_lights_2d(const world::World& world, TextureCache& textures,
                           render::SpriteRenderer& sprites, render::RenderTarget& target,
                           const Camera2D& camera, const Lights2DShading& shading) {
    const reflect::TypeInfo* light_type = world.registry().find_type<Light>();
    const render::Texture* falloff = light_type != nullptr ? textures.light_falloff() : nullptr;
    if (falloff == nullptr) {
        return 0;
    }
    const double ppu = static_cast<double>(std::max<std::uint32_t>(camera.ppu, 1));
    const double scale = static_cast<double>(std::max<std::uint32_t>(camera.scale, 1));
    // As draw_world() places things: the camera on the target's middle pixel.
    const std::uint32_t centre_x_pixel = target.width() / 2;
    const std::uint32_t centre_y_pixel = target.height() / 2;
    const auto centre_x = static_cast<double>(centre_x_pixel);
    const auto centre_y = static_cast<double>(centre_y_pixel);
    const render::Rect whole{0.0F, 0.0F, static_cast<float>(falloff->width()),
                             static_cast<float>(falloff->height())};
    std::size_t drawn = 0;
    sprites.begin_over(target);
    sprites.set_blend(render::SpriteRenderer::Blend::Light);
    world.each(*light_type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const auto* light = ref.as<Light>();
        const Transform2D* where = world.get<Transform2D>(entity);
        if (light == nullptr || where == nullptr || !light->enabled ||
            !std::isfinite(light->radius) || !std::isfinite(light->intensity) ||
            light->radius <= 0.0 || light->intensity <= 0.0) {
            return;
        }
        const double height = std::isfinite(light->height) ? light->height : 0.0;
        const double x = centre_x + (std::round((where->x - camera.x) * ppu) * scale);
        const double y = centre_y + (std::round((where->y - height - camera.y) * ppu) * scale);
        const double r = std::min(light->radius, 32.0) * ppu * scale;
        // What shows is the scene times (the look's tint plus the light), so the light is
        // divided by the tint the look multiplies by afterwards. A light can brighten by
        // more than double: drawn again, each time by an even share of the factor.
        const double scale_by =
            std::isfinite(shading.strength) ? std::clamp(shading.strength, 0.0, 1.0) : 1.0;
        const double intensity = std::min(light->intensity, 8.0) * scale_by;
        if (intensity <= 0.0) {
            return;
        }
        // Its colour, as saturated as the look leaves colours.
        const double grey =
            (0.299 * light->color_r) + (0.587 * light->color_g) + (0.114 * light->color_b);
        const double keep =
            std::isfinite(shading.saturation) ? std::clamp(shading.saturation, 0.0, 2.0) : 1.0;
        const auto saturated = [grey, keep](std::uint8_t c) {
            return std::clamp(grey + ((static_cast<double>(c) - grey) * keep), 0.0, 255.0);
        };
        const std::array<double, 3> colour{saturated(light->color_r), saturated(light->color_g),
                                           saturated(light->color_b)};
        std::array<double, 3> factor{};
        double most = 1.0;
        for (std::size_t c = 0; c < 3; ++c) {
            const double t = std::isfinite(shading.darkening.at(c))
                                 ? std::max(shading.darkening.at(c), 0.05)
                                 : 1.0;
            factor.at(c) = 1.0 + (colour.at(c) / 255.0 * intensity / t);
            most = std::max(most, factor.at(c));
        }
        const int passes = std::clamp(static_cast<int>(std::ceil(std::log2(most))), 1, 8);
        const auto part = [passes](double f) {
            const double each = std::pow(f, 1.0 / passes) - 1.0;
            return static_cast<std::uint8_t>(std::lround(std::clamp(each, 0.0, 1.0) * 255.0));
        };
        const render::Color tint{part(factor[0]), part(factor[1]), part(factor[2]), 255};
        const render::Rect to{static_cast<float>(x - r), static_cast<float>(y - r),
                              static_cast<float>(2.0 * r), static_cast<float>(2.0 * r)};
        for (int i = 0; i < passes; ++i) {
            sprites.draw(*falloff, whole, to, tint);
        }
        ++drawn;
    });
    static_cast<void>(sprites.end());
    return drawn;
}

DrawStats draw_world(const world::World& world, TextureCache& textures,
                     render::SpriteRenderer& sprites, render::RenderTarget& target,
                     const Camera2D& camera, render::Color clear) {
    DrawStats stats;
    std::vector<Item> items;
    const reflect::TypeInfo* transform_type = world.registry().find_type<Transform2D>();
    const double ppu = static_cast<double>(std::max<std::uint32_t>(camera.ppu, 1));
    const double scale = static_cast<double>(std::max<std::uint32_t>(camera.scale, 1));
    // The camera's position lands on this pixel of the target: the centre, rounded down
    // for odd sizes.
    const std::uint32_t centre_x_pixel = target.width() / 2;
    const std::uint32_t centre_y_pixel = target.height() / 2;
    const auto centre_x = static_cast<double>(centre_x_pixel);
    const auto centre_y = static_cast<double>(centre_y_pixel);
    // The source pixels in view, relative to the camera's pixel.
    const double view_left = -centre_x / scale;
    const double view_top = -centre_y / scale;
    const double view_right = (static_cast<double>(target.width()) - centre_x) / scale;
    const double view_bottom = (static_cast<double>(target.height()) - centre_y) / scale;

    // A point in source pixels, relative to the camera's pixel, on the target.
    const auto to_screen = [&](double x, double y) {
        return render::Point{static_cast<float>(centre_x + (x * scale)),
                             static_cast<float>(centre_y + (y * scale))};
    };

    if (transform_type != nullptr) {
        world.each(*transform_type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Transform2D* transform = ref.as<Transform2D>();
            if (transform == nullptr) {
                return;
            }
            Item base;
            base.layer = transform->layer;
            base.y = transform->y;
            base.guid = world.guid(entity);

            if (const Tilemap* tilemap = world.get<Tilemap>(entity);
                tilemap != nullptr && tilemap->visible) {
                const std::optional<Guid> image = Guid::parse(tilemap->tileset);
                const render::Texture* texture = image ? textures.get(*image) : nullptr;
                std::string error;
                const TileGrid* grid = tilemap->grid(error);
                const std::uint32_t size = tilemap->tile_size == 0
                                               ? std::max<std::uint32_t>(camera.ppu, 1)
                                               : tilemap->tile_size;
                if (texture == nullptr || grid == nullptr || texture->width() < size ||
                    texture->height() < size) {
                    ++stats.missing;
                } else {
                    const std::uint32_t columns = texture->width() / size;
                    const std::uint64_t tiles = std::uint64_t{columns} * (texture->height() / size);
                    const auto tile = static_cast<double>(size);
                    const double left = std::round((transform->x - camera.x) * ppu);
                    const double top = std::round((transform->y - camera.y) * ppu);
                    // Only the columns and rows that overlap the view.
                    const auto first = [&](double view, double origin, std::uint32_t count) {
                        const double n = std::floor((view - origin) / tile);
                        return static_cast<std::uint32_t>(
                            std::clamp(n, 0.0, static_cast<double>(count)));
                    };
                    const auto last = [&](double view, double origin, std::uint32_t count) {
                        const double n = std::ceil((view - origin) / tile);
                        return static_cast<std::uint32_t>(
                            std::clamp(n, 0.0, static_cast<double>(count)));
                    };
                    const std::uint32_t x0 = first(view_left, left, grid->width);
                    const std::uint32_t x1 = last(view_right, left, grid->width);
                    const std::uint32_t y0 = first(view_top, top, grid->height);
                    const std::uint32_t y1 = last(view_bottom, top, grid->height);
                    for (std::uint32_t y = y0; y < y1; ++y) {
                        for (std::uint32_t x = x0; x < x1; ++x) {
                            const std::int32_t number = grid->at(x, y);
                            if (number < 0 || static_cast<std::uint64_t>(number) >= tiles) {
                                continue; // no tile, or past the end of the tileset
                            }
                            const std::uint32_t n =
                                tilemap->shown(static_cast<std::uint32_t>(number), tiles);
                            const std::uint32_t column = n % columns;
                            const std::uint32_t row = n / columns;
                            Item item = base;
                            item.part = 0;
                            item.index = (std::size_t{y} * grid->width) + x;
                            item.texture = texture;
                            item.source = render::Rect{
                                static_cast<float>(column * size), static_cast<float>(row * size),
                                static_cast<float>(size), static_cast<float>(size)};
                            const double tile_left = left + (x * tile);
                            const double tile_top = top + (y * tile);
                            item.corners = {to_screen(tile_left, tile_top),
                                            to_screen(tile_left + tile, tile_top),
                                            to_screen(tile_left + tile, tile_top + tile),
                                            to_screen(tile_left, tile_top + tile)};
                            item.tint = render::Color{255, 255, 255, tilemap->alpha()};
                            items.push_back(item);
                        }
                    }
                }
            }

            if (const Sprite* sprite = world.get<Sprite>(entity);
                sprite != nullptr && sprite->visible) {
                const std::optional<Guid> image = Guid::parse(sprite->image);
                const render::Texture* texture =
                    image ? textures.get(*image, sprite->recolor) : nullptr;
                if (texture == nullptr) {
                    ++stats.missing;
                    return;
                }
                const render::Rect source =
                    frame_rect(*texture, sprite->frame_width, sprite->frame_height, sprite->frame);
                const auto width = static_cast<double>(source.w);
                const auto height = static_cast<double>(source.h);
                const double at_x = (transform->x - camera.x) * ppu;
                const double at_y = (transform->y - camera.y) * ppu;
                const std::array<std::array<double, 2>, 4> local =
                    sprite_corners(*sprite, width, height);
                // Snap in source pixels, then upscale by a whole number: pixel-perfect. An
                // upright picture's left and top edges land on whole pixels; a turned
                // one's pivot does.
                double from_x = 0.0; // the corners are placed relative to this point
                double from_y = 0.0;
                if (turn_of(*sprite) == 0.0) {
                    from_x = std::min({local[0][0], local[1][0], local[2][0], local[3][0]});
                    from_y = std::min({local[0][1], local[1][1], local[2][1], local[3][1]});
                }
                const double snapped_x = std::round(at_x + from_x);
                const double snapped_y = std::round(at_y + from_y);
                Item item = base;
                item.part = 1;
                item.texture = texture;
                item.source = source;
                for (std::size_t i = 0; i < 4; ++i) {
                    item.corners[i] = to_screen(snapped_x + (local[i][0] - from_x),
                                                snapped_y + (local[i][1] - from_y));
                }
                item.tint =
                    render::Color{sprite->tint_r, sprite->tint_g, sprite->tint_b, sprite->tint_a};
                items.push_back(item);
            }
        });
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return std::tie(a.layer, a.y, a.guid, a.part, a.index) <
               std::tie(b.layer, b.y, b.guid, b.part, b.index);
    });

    sprites.begin(target, clear);
    for (const Item& item : items) {
        sprites.draw(*item.texture, item.source, item.corners, item.tint);
    }
    sprites.end();
    stats.drawn = items.size();
    return stats;
}

// ---------------------------------------------------------------------------------------
// Diorama
// ---------------------------------------------------------------------------------------

namespace {

constexpr double pi = 3.14159265358979323846;

[[nodiscard]] render::Vec3 unit(render::Vec3 v) noexcept {
    const float length = std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    return length > 0.0F ? render::Vec3{v.x / length, v.y / length, v.z / length} : v;
}

/// The camera's eye and its horizontal right-hand direction.
struct Orbit {
    render::Vec3 eye;
    render::Vec3 target;
    render::Vec3 right; ///< On the ground plane: billboards face along its normal.
};

Orbit orbit(const DioramaCamera& camera) {
    const double pitch =
        std::clamp(std::isfinite(camera.pitch) ? camera.pitch : 35.0, 5.0, 85.0) * pi / 180.0;
    const double yaw = (std::isfinite(camera.yaw) ? camera.yaw : 0.0) * pi / 180.0;
    const double distance =
        std::isfinite(camera.distance) && camera.distance > 0.01 ? camera.distance : 12.0;
    Orbit o;
    o.target = render::Vec3{static_cast<float>(camera.x), 0.0F, static_cast<float>(camera.y)};
    // With yaw 0 the camera stands south of the point (larger y) and looks north.
    const double ground = distance * std::cos(pitch);
    o.eye = render::Vec3{static_cast<float>(camera.x + (std::sin(yaw) * ground)),
                         static_cast<float>(distance * std::sin(pitch)),
                         static_cast<float>(camera.y + (std::cos(yaw) * ground))};
    o.right =
        render::Vec3{static_cast<float>(std::cos(yaw)), 0.0F, static_cast<float>(-std::sin(yaw))};
    return o;
}

std::array<float, 4> lit(float light, render::Color tint) {
    return {light * static_cast<float>(tint.r) / 255.0F,
            light * static_cast<float>(tint.g) / 255.0F,
            light * static_cast<float>(tint.b) / 255.0F, static_cast<float>(tint.a) / 255.0F};
}

} // namespace

render::Vec3 sun_direction(double angle, double height) noexcept {
    constexpr double to_radians = 3.14159265358979323846 / 180.0;
    const double a = (std::isfinite(angle) ? angle : default_sun_angle) * to_radians;
    const double h =
        std::clamp(std::isfinite(height) ? height : default_sun_height, -90.0, 90.0) * to_radians;
    // North is up the map, -z in the world; east is +x.
    return render::Vec3{static_cast<float>(std::sin(a) * std::cos(h)),
                        static_cast<float>(std::sin(h)),
                        static_cast<float>(-std::cos(a) * std::cos(h))};
}

std::optional<std::array<double, 2>> shadow_offset(render::Vec3 sun) noexcept {
    const render::Vec3 s = unit(sun);
    // Below about a degree, the shadow would be endless; the sun has set.
    if (!(s.y > 0.02F)) {
        return std::nullopt;
    }
    double x = -s.x / s.y;
    double y = -s.z / s.y;
    const double length = std::hypot(x, y);
    if (length > max_shadow_length) {
        x *= max_shadow_length / length;
        y *= max_shadow_length / length;
    }
    return std::array<double, 2>{x, y};
}

float brightness(render::Vec3 normal, const DioramaLight& light) noexcept {
    const render::Vec3 n = unit(normal);
    const render::Vec3 s = unit(light.sun);
    const float facing = (n.x * s.x) + (n.y * s.y) + (n.z * s.z);
    return light.ambient + (light.diffuse * std::max(facing, 0.0F));
}

Box diorama_view_extent(const DioramaCamera& camera, double aspect) noexcept {
    constexpr double infinity = std::numeric_limits<double>::infinity();
    const Box unlimited{-infinity, -infinity, infinity, infinity};
    if (!std::isfinite(camera.yaw) || camera.yaw != 0.0 || !std::isfinite(aspect) ||
        aspect <= 0.0) {
        return unlimited;
    }
    // The same clamps as orbit() and diorama_view_projection().
    const double pitch =
        std::clamp(std::isfinite(camera.pitch) ? camera.pitch : 35.0, 5.0, 85.0) * pi / 180.0;
    const double half_fov =
        std::clamp(std::isfinite(camera.fov) ? camera.fov : 30.0, 1.0, 120.0) * pi / 360.0;
    const double distance =
        std::isfinite(camera.distance) && camera.distance > 0.01 ? camera.distance : 12.0;
    const double height = distance * std::sin(pitch);
    const double behind = distance * std::cos(pitch); // the eye, south of the point
    // Where the screen's bottom and top edges meet the ground, as distances north of the
    // eye. The bottom edge always looks down (pitch + half_fov is below 180 degrees).
    const double bottom_angle = pitch + half_fov;
    const double near = bottom_angle >= pi / 2.0 ? -height * std::tan(bottom_angle - (pi / 2.0))
                                                 : height / std::tan(bottom_angle);
    const double top_angle = pitch - half_fov;
    const double far = top_angle > 0.0 ? height / std::tan(top_angle) : infinity;
    const double half_width = distance * std::tan(half_fov) * aspect;
    return Box{-half_width, -(far - behind), half_width, behind - near};
}

Ray diorama_ray(const DioramaCamera& camera, float aspect, double ndc_x, double ndc_y) noexcept {
    const Orbit o = orbit(camera);
    // The same clamps as diorama_view_projection().
    const double half_fov =
        std::clamp(std::isfinite(camera.fov) ? camera.fov : 30.0, 1.0, 120.0) * pi / 360.0;
    const double a = aspect > 0.0F && std::isfinite(aspect) ? aspect : 1.0;
    const double fx = static_cast<double>(o.target.x) - o.eye.x;
    const double fy = static_cast<double>(o.target.y) - o.eye.y;
    const double fz = static_cast<double>(o.target.z) - o.eye.z;
    const double fl = std::sqrt((fx * fx) + (fy * fy) + (fz * fz));
    const std::array<double, 3> forward{fx / fl, fy / fl, fz / fl};
    const std::array<double, 3> right{o.right.x, o.right.y, o.right.z};
    // up = right x forward, so it points up the screen.
    const std::array<double, 3> up{(right[1] * forward[2]) - (right[2] * forward[1]),
                                   (right[2] * forward[0]) - (right[0] * forward[2]),
                                   (right[0] * forward[1]) - (right[1] * forward[0])};
    const double t = std::tan(half_fov);
    std::array<double, 3> d{};
    for (std::size_t i = 0; i < 3; ++i) {
        d.at(i) = forward.at(i) + (right.at(i) * ndc_x * t * a) + (up.at(i) * ndc_y * t);
    }
    const double dl = std::sqrt((d[0] * d[0]) + (d[1] * d[1]) + (d[2] * d[2]));
    return Ray{o.eye, render::Vec3{static_cast<float>(d[0] / dl), static_cast<float>(d[1] / dl),
                                   static_cast<float>(d[2] / dl)}};
}

std::optional<std::array<double, 2>> ground_hit(const Ray& ray, double height) noexcept {
    const double dy = ray.direction.y;
    if (!(std::abs(dy) > 1e-9) || !std::isfinite(height)) {
        return std::nullopt;
    }
    const double t = (height - ray.origin.y) / dy;
    if (!(t > 0.0)) {
        return std::nullopt; // behind the eye
    }
    return std::array<double, 2>{ray.origin.x + (ray.direction.x * t),
                                 ray.origin.z + (ray.direction.z * t)};
}

render::Mat4 diorama_view_projection(const DioramaCamera& camera, float aspect, render::Vec3* eye) {
    const Orbit o = orbit(camera);
    if (eye != nullptr) {
        *eye = o.eye;
    }
    const double fov =
        std::clamp(std::isfinite(camera.fov) ? camera.fov : 30.0, 1.0, 120.0) * pi / 180.0;
    const double distance =
        std::isfinite(camera.distance) && camera.distance > 0.01 ? camera.distance : 12.0;
    // Near and far follow the distance, so depth precision suits any camera.
    const auto near = static_cast<float>(distance / 64.0);
    const auto far = static_cast<float>((distance * 8.0) + 64.0);
    return render::Mat4::perspective(static_cast<float>(fov), aspect > 0.0F ? aspect : 1.0F, near,
                                     far) *
           render::Mat4::look_at(o.eye, o.target, render::Vec3{0.0F, 1.0F, 0.0F});
}

std::vector<render::PointLight> diorama_lights(const world::World& world, double x, double y) {
    struct Found {
        double distance;
        Guid guid;
        render::PointLight light;
    };
    std::vector<Found> found;
    if (const reflect::TypeInfo* type = world.registry().find_type<Light>()) {
        world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Light* light = ref.as<Light>();
            const Transform2D* transform = world.get<Transform2D>(entity);
            if (light == nullptr || transform == nullptr || !light->enabled) {
                return;
            }
            const double radius = std::clamp(light->radius, 0.0, 32.0);
            const double intensity = std::clamp(light->intensity, 0.0, 8.0);
            const double height = std::clamp(light->height, 0.0, 16.0);
            if (!std::isfinite(transform->x) || !std::isfinite(transform->y) ||
                !std::isfinite(radius) || !std::isfinite(intensity) || !std::isfinite(height) ||
                radius <= 0.0 || intensity <= 0.0) {
                return;
            }
            const auto channel = [intensity](std::uint8_t c) {
                return static_cast<float>(static_cast<double>(c) / 255.0 * intensity);
            };
            render::PointLight point;
            point.position =
                render::Vec3{static_cast<float>(transform->x), static_cast<float>(height),
                             static_cast<float>(transform->y)};
            point.radius = static_cast<float>(radius);
            point.color = {channel(light->color_r), channel(light->color_g),
                           channel(light->color_b)};
            found.push_back(
                Found{std::hypot(transform->x - x, transform->y - y), world.guid(entity), point});
        });
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.guid < b.guid;
    });
    std::vector<render::PointLight> lights;
    for (std::size_t i = 0; i < found.size() && i < render::max_point_lights; ++i) {
        lights.push_back(found[i].light);
    }
    return lights;
}

std::optional<float> ground_top(const world::World& world, double x, double y, std::uint32_t ppu) {
    const reflect::TypeInfo* type = world.registry().find_type<Tilemap>();
    if (type == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return std::nullopt;
    }
    const double units = static_cast<double>(std::max<std::uint32_t>(ppu, 1));
    std::optional<float> top;
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Tilemap* map = ref.as<Tilemap>();
        const Transform2D* t = world.get<Transform2D>(entity);
        std::string error;
        const TileGrid* grid = map != nullptr ? map->grid(error) : nullptr;
        if (t == nullptr || grid == nullptr || !map->visible) {
            return;
        }
        const double tile =
            (map->tile_size == 0 ? units : static_cast<double>(map->tile_size)) / units;
        const double column = std::floor((x - t->x) / tile);
        const double row = std::floor((y - t->y) / tile);
        if (column < 0.0 || row < 0.0 || column >= static_cast<double>(grid->width) ||
            row >= static_cast<double>(grid->height) ||
            grid->at(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)) < 0) {
            return;
        }
        // As draw_diorama() places it: the block's height, plus its layer's lift.
        const auto height = static_cast<float>(
            map->height_at(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)));
        const float here = height + (static_cast<float>(t->layer) * 0.002F);
        top = top ? std::max(*top, here) : here;
    });
    return top;
}

std::optional<render::SunShadow> diorama_sun_shadow(const world::World& world,
                                                    const DioramaCamera& camera,
                                                    const DioramaLight& light) {
    const reflect::TypeInfo* type = world.registry().find_type<Tilemap>();
    const std::optional<Box> bounds = map_bounds(world, camera.ppu);
    if (!(light.block_shadow > 0.0F) || type == nullptr || !bounds ||
        !shadow_offset(light.sun).has_value()) {
        return std::nullopt;
    }
    // The tallest block casts the longest shadow.
    float tallest = 0.0F;
    world.each(*type, [&](world::Entity, reflect::ObjectRef ref) {
        const Tilemap* map = ref.as<Tilemap>();
        if (map != nullptr && map->visible && std::isfinite(map->height)) {
            tallest = std::max(tallest, static_cast<float>(std::min(map->height, 16.0)));
            for (const std::vector<double>& row : parse_heights(map->heights)) {
                for (const double v : row) {
                    if (!std::isnan(v)) {
                        tallest = std::max(tallest, static_cast<float>(v));
                    }
                }
            }
        }
    });
    // What it covers: the maps, or around the camera on a large one.
    Box area = *bounds;
    const double reach = max_sun_shadow_tiles / 2.0;
    if (area.right - area.left > max_sun_shadow_tiles) {
        area.left = std::max(area.left, camera.x - reach);
        area.right = std::min(area.right, camera.x + reach);
    }
    if (area.bottom - area.top > max_sun_shadow_tiles) {
        area.top = std::max(area.top, camera.y - reach);
        area.bottom = std::min(area.bottom, camera.y + reach);
    }
    const render::Vec3 s = unit(light.sun);
    const render::Vec3 centre{static_cast<float>((area.left + area.right) / 2.0), tallest / 2.0F,
                              static_cast<float>((area.top + area.bottom) / 2.0)};
    const auto span = static_cast<float>(
        std::hypot(area.right - area.left, area.bottom - area.top) + tallest + 4.0);
    const render::Vec3 from{centre.x + (s.x * span), centre.y + (s.y * span),
                            centre.z + (s.z * span)};
    const render::Vec3 up = std::abs(s.y) > 0.99F ? render::Vec3{0, 0, -1} : render::Vec3{0, 1, 0};
    const render::Mat4 view = render::Mat4::look_at(from, centre, up);
    // The box of the area, from just below the ground to above the tallest block, in the
    // sun's view: its bounds are the projection's.
    float left = std::numeric_limits<float>::max();
    float right = -left;
    float bottom = left;
    float top = -left;
    float nearest = left;
    float farthest = -left;
    for (const double x : {area.left, area.right}) {
        for (const double z : {area.top, area.bottom}) {
            for (const float y : {-0.5F, tallest + 0.5F}) {
                const std::array<float, 4> p =
                    view.transform(render::Vec3{static_cast<float>(x), y, static_cast<float>(z)});
                left = std::min(left, p[0]);
                right = std::max(right, p[0]);
                bottom = std::min(bottom, p[1]);
                top = std::max(top, p[1]);
                nearest = std::min(nearest, -p[2]);
                farthest = std::max(farthest, -p[2]);
            }
        }
    }
    render::SunShadow shadow;
    const float depth = (farthest - nearest) + 2.0F;
    shadow.view_projection =
        render::Mat4::orthographic(left, right, bottom, top, nearest - 1.0F, farthest + 1.0F) *
        view;
    shadow.towards_sun = s;
    shadow.darkness = std::min(light.block_shadow, 1.0F);
    shadow.bias = 0.03F / depth; // three hundredths of a tile
    return shadow;
}

DrawStats draw_diorama(const world::World& world, TextureCache& textures,
                       render::WorldRenderer& renderer, render::RenderTarget& target,
                       const DioramaCamera& camera, render::Color clear, const DioramaLight& light,
                       render::Fog fog) {
    DrawStats stats;
    render::Vec3 eye;
    const render::Mat4 view_projection = diorama_view_projection(
        camera, static_cast<float>(target.width()) / static_cast<float>(target.height()), &eye);
    const Orbit o = orbit(camera);
    const double ppu = static_cast<double>(std::max<std::uint32_t>(camera.ppu, 1));
    const std::vector<render::PointLight> lights = diorama_lights(world, camera.x, camera.y);
    renderer.begin(target, clear, view_projection, eye, fog, lights);
    if (const std::optional<render::SunShadow> sun = diorama_sun_shadow(world, camera, light)) {
        renderer.set_sun_shadow(*sun);
    }

    const reflect::TypeInfo* transform_type = world.registry().find_type<Transform2D>();
    if (transform_type == nullptr) {
        renderer.end();
        return stats;
    }
    // Faces of a block, by the normal they face; side faces see the ground around.
    const float top_light = brightness(render::Vec3{0, 1, 0}, light);
    const float north_light = brightness(render::Vec3{0, 0, -1}, light);
    const float south_light = brightness(render::Vec3{0, 0, 1}, light);
    const float west_light = brightness(render::Vec3{-1, 0, 0}, light);
    const float east_light = brightness(render::Vec3{1, 0, 0}, light);
    // Billboards face the camera across the ground: their normal points at the eye.
    const render::Vec3 sprite_normal{-o.right.z, 0.0F, o.right.x};
    const float sprite_light = brightness(sprite_normal, light);
    // How each kind of face takes point lights.
    const auto surface = [](render::Vec3 normal, float wrap) {
        render::Surface s;
        s.normal = normal;
        s.wrap = wrap;
        return s;
    };

    world.each(*transform_type, [&](world::Entity entity, reflect::ObjectRef ref) {
        const Transform2D* transform = ref.as<Transform2D>();
        if (transform == nullptr) {
            return;
        }
        // Layers lift things a hair, so maps painted over each other do not flicker.
        const auto lift = static_cast<float>(transform->layer) * 0.002F;

        if (const Tilemap* tilemap = world.get<Tilemap>(entity);
            tilemap != nullptr && tilemap->visible) {
            const std::optional<Guid> image = Guid::parse(tilemap->tileset);
            const render::Texture* texture = image ? textures.get(*image) : nullptr;
            std::string error;
            const TileGrid* grid = tilemap->grid(error);
            const std::uint32_t size = tilemap->tile_size == 0
                                           ? std::max<std::uint32_t>(camera.ppu, 1)
                                           : tilemap->tile_size;
            if (texture == nullptr || grid == nullptr || texture->width() < size ||
                texture->height() < size) {
                ++stats.missing;
            } else {
                const std::uint32_t columns = texture->width() / size;
                const std::uint64_t tiles = std::uint64_t{columns} * (texture->height() / size);
                const auto tile = static_cast<float>(static_cast<double>(size) / ppu);
                const render::Color tint{255, 255, 255, tilemap->alpha()};
                const auto x0 = static_cast<float>(transform->x);
                const auto z0 = static_cast<float>(transform->y);
                const auto filled = [&](std::int64_t x, std::int64_t y) {
                    return x >= 0 && y >= 0 &&
                           grid->at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) >=
                               0;
                };
                for (std::uint32_t y = 0; y < grid->height; ++y) {
                    for (std::uint32_t x = 0; x < grid->width; ++x) {
                        const std::int32_t number = grid->at(x, y);
                        if (number < 0 || static_cast<std::uint64_t>(number) >= tiles) {
                            continue;
                        }
                        const std::uint32_t n =
                            tilemap->shown(static_cast<std::uint32_t>(number), tiles);
                        const std::uint32_t column = n % columns;
                        const std::uint32_t row = n / columns;
                        const render::Rect source{
                            static_cast<float>(column * size), static_cast<float>(row * size),
                            static_cast<float>(size), static_cast<float>(size)};
                        const float left = x0 + (static_cast<float>(x) * tile);
                        const float right = left + tile;
                        const float north = z0 + (static_cast<float>(y) * tile);
                        const float south = north + tile;
                        const auto height = static_cast<float>(tilemap->height_at(x, y));
                        const float top = height + lift;
                        // The top face: the tile as seen from above, north edge at the top.
                        renderer.draw(
                            *texture,
                            {render::Vec3{left, top, north}, render::Vec3{right, top, north},
                             render::Vec3{right, top, south}, render::Vec3{left, top, south}},
                            source, lit(top_light, tint), surface(render::Vec3{0, 1, 0}, 0.0F));
                        ++stats.drawn;
                        if (height <= 0.0F) {
                            continue;
                        }
                        // Side faces, where no neighbouring block hides them: down to the
                        // ground beside an empty cell, or to a lower neighbour's top.
                        const auto side = [&](render::Vec3 a, render::Vec3 b, float light_level,
                                              render::Vec3 normal, float bottom) {
                            renderer.draw(*texture,
                                          {render::Vec3{a.x, top, a.z}, render::Vec3{b.x, top, b.z},
                                           render::Vec3{b.x, bottom, b.z},
                                           render::Vec3{a.x, bottom, a.z}},
                                          source, lit(light_level, tint), surface(normal, 0.0F));
                            ++stats.drawn;
                        };
                        const auto gx = static_cast<std::int64_t>(x);
                        const auto gy = static_cast<std::int64_t>(y);
                        // The neighbour's top, or the ground where there is none.
                        const auto below = [&](std::int64_t nx, std::int64_t ny) {
                            if (!filled(nx, ny)) {
                                return lift;
                            }
                            return static_cast<float>(
                                       tilemap->height_at(static_cast<std::uint32_t>(nx),
                                                          static_cast<std::uint32_t>(ny))) +
                                   lift;
                        };
                        if (const float b = below(gx, gy + 1); b < top) {
                            side({left, 0, south}, {right, 0, south}, south_light, {0, 0, 1}, b);
                        }
                        if (const float b = below(gx, gy - 1); b < top) {
                            side({right, 0, north}, {left, 0, north}, north_light, {0, 0, -1}, b);
                        }
                        if (const float b = below(gx - 1, gy); b < top) {
                            side({left, 0, north}, {left, 0, south}, west_light, {-1, 0, 0}, b);
                        }
                        if (const float b = below(gx + 1, gy); b < top) {
                            side({right, 0, south}, {right, 0, north}, east_light, {1, 0, 0}, b);
                        }
                    }
                }
            }
        }

        if (const Sprite* sprite = world.get<Sprite>(entity);
            sprite != nullptr && sprite->visible) {
            const std::optional<Guid> image = Guid::parse(sprite->image);
            const render::Texture* texture =
                image ? textures.get(*image, sprite->recolor) : nullptr;
            if (texture == nullptr) {
                ++stats.missing;
                return;
            }
            const render::Rect source =
                frame_rect(*texture, sprite->frame_width, sprite->frame_height, sprite->frame);
            const auto width = static_cast<float>(static_cast<double>(source.w) / ppu);
            const auto height = static_cast<float>(static_cast<double>(source.h) / ppu);
            // Upright, facing the camera, with the pivot on the map position; scaled and
            // turned in its own plane as in Classic 2D. Each corner is how far along the
            // camera's right it lies, and how far up.
            const render::Vec3 base{static_cast<float>(transform->x), lift,
                                    static_cast<float>(transform->y)};
            const render::Vec3 r = o.right;
            std::array<std::array<float, 2>, 4> card{};
            const std::array<std::array<double, 2>, 4> local =
                sprite_corners(*sprite, static_cast<double>(width), static_cast<double>(height));
            for (std::size_t i = 0; i < 4; ++i) {
                card[i] = {static_cast<float>(local[i][0]), static_cast<float>(-local[i][1])};
            }
            const auto at = [&](const std::array<float, 2>& corner) {
                const auto [along, up] = corner;
                return render::Vec3{base.x + (r.x * along), base.y + up, base.z + (r.z * along)};
            };
            // Its shadow, on the tile it stands on (just inside its row).
            const std::optional<float> floor =
                light.sprite_shadow > 0.0F
                    ? ground_top(world, transform->x, transform->y - 0.001, camera.ppu)
                    : std::nullopt;
            const float dark = std::min(light.sprite_shadow, 1.0F);
            const std::optional<std::array<double, 2>> cast =
                light.shadows == ShadowShape::Cast ? shadow_offset(light.sun) : std::nullopt;
            if (floor && cast) {
                // Its silhouette on the ground, cast away from the sun. A sprite stands for
                // a solid figure, so its shadow is cast as if the picture turned to face
                // the sun: as wide as the picture across the shadow, and as long along it
                // as the sun's height makes the picture's height (shadow_offset()). Seen
                // with the sun behind the camera, the picture's left stays on the left.
                const float y = floor.value_or(0.0F) + 0.004F;
                const auto dx = static_cast<float>((*cast)[0]);
                const auto dz = static_cast<float>((*cast)[1]);
                const float reach = std::hypot(dx, dz);
                // Across the shadow: east when it falls north. Under a noon sun, the
                // picture's own right.
                const render::Vec3 across =
                    reach > 1e-4F ? render::Vec3{-dz / reach, 0.0F, dx / reach} : r;
                const auto onto_ground = [&](const std::array<float, 2>& corner) {
                    const auto [along, up] = corner;
                    const float above = base.y + up - y; // height above the ground
                    return render::Vec3{base.x + (across.x * along) + (dx * above), y,
                                        base.z + (across.z * along) + (dz * above)};
                };
                // Darkest where the picture is nearest the ground: fully at its lowest
                // corner, six tenths at its highest.
                const float lowest = std::min({card[0][1], card[1][1], card[2][1], card[3][1]});
                const float highest = std::max({card[0][1], card[1][1], card[2][1], card[3][1]});
                std::array<float, 4> darkness{};
                for (std::size_t i = 0; i < 4; ++i) {
                    const float rise =
                        highest > lowest ? (card[i][1] - lowest) / (highest - lowest) : 0.0F;
                    darkness[i] = dark * std::lerp(1.0F, 0.6F, rise);
                }
                renderer.draw_shadow(*texture,
                                     {onto_ground(card[0]), onto_ground(card[1]),
                                      onto_ground(card[2]), onto_ground(card[3])},
                                     source, darkness);
            } else if (floor) {
                // A soft blob at its feet.
                const float span = std::max({card[0][0], card[1][0], card[2][0], card[3][0]}) -
                                   std::min({card[0][0], card[1][0], card[2][0], card[3][0]});
                const float half_w = std::max(span * 0.4F, 0.15F);
                const float half_d = half_w * 0.5F;
                const float y = floor.value_or(0.0F) + 0.004F;
                renderer.draw_shadow({render::Vec3{base.x - half_w, y, base.z - half_d},
                                      render::Vec3{base.x + half_w, y, base.z - half_d},
                                      render::Vec3{base.x + half_w, y, base.z + half_d},
                                      render::Vec3{base.x - half_w, y, base.z + half_d}},
                                     dark);
            }
            renderer.draw(*texture, {at(card[0]), at(card[1]), at(card[2]), at(card[3])}, source,
                          lit(sprite_light, render::Color{sprite->tint_r, sprite->tint_g,
                                                          sprite->tint_b, sprite->tint_a}),
                          [&] {
                              // Billboards take point lights from all round, softly, and in their
                              // tint.
                              render::Surface s = surface(sprite_normal, 1.0F);
                              s.casts_shadow =
                                  false; // an edge-on card from the sun: its own shadow
                              s.albedo = {static_cast<float>(sprite->tint_r) / 255.0F,
                                          static_cast<float>(sprite->tint_g) / 255.0F,
                                          static_cast<float>(sprite->tint_b) / 255.0F};
                              return s;
                          }());
            ++stats.drawn;
        }
    });
    renderer.end();
    return stats;
}

} // namespace engine::scene2d
