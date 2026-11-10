#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>

namespace engine::scene2d {

void register_types(reflect::Registry& registry) {
    using reflect::property_flag::advanced;
    auto transform = registry.add_type<Transform2D>("transform_2d")
                         .display_name("Position")
                         .category("Basics")
                         .tooltip("Where the object is in the map.");
    transform.property<&Transform2D::x>("x")
        .unit(reflect::Unit::Tiles)
        .tooltip("Distance from the left of the map.");
    transform.property<&Transform2D::y>("y")
        .unit(reflect::Unit::Tiles)
        .tooltip("Distance from the top of the map.");
    transform.property<&Transform2D::layer>("layer").range(-100, 100).flags(advanced).tooltip(
        "Higher layers are drawn on top. Within a layer, lower objects are drawn on top.");

    auto sprite =
        registry.add_type<Sprite>("sprite").display_name("Sprite").category("Visuals").tooltip(
            "Shows a picture at the object's position.");
    sprite.property<&Sprite::image>("image").tooltip("The picture to show.");
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

    auto animation = registry.add_type<SpriteAnimation>("sprite_animation")
                         .display_name("Animation")
                         .category("Visuals")
                         .tooltip("Plays frames of a sprite sheet one after another.");
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
    tilemap.property<&Tilemap::solid>("solid").tooltip(
        "Every tile of this map blocks movement. Paint walls on their own map.");
}

bool register_components(world::World& world) {
    const bool transform = world.register_component<Transform2D>();
    const bool sprite = world.register_component<Sprite>();
    const bool animation = world.register_component<SpriteAnimation>();
    const bool tilemap = world.register_component<Tilemap>();
    return transform && sprite && animation && tilemap;
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

double sweep(const world::World& world, const Box& box, Axis axis, double distance,
             std::uint32_t ppu) {
    if (!std::isfinite(distance) || distance == 0.0) {
        return 0.0;
    }
    const reflect::TypeInfo* type = world.registry().find_type<Tilemap>();
    if (type == nullptr) {
        return distance;
    }
    double allowed = distance;
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
        // In the map's own space, "along" the moving axis and "across" the other one.
        const bool x_axis = axis == Axis::X;
        const double origin_along = x_axis ? transform->x : transform->y;
        const double origin_across = x_axis ? transform->y : transform->x;
        const double low = (x_axis ? box.left : box.top) - origin_along;
        const double high = (x_axis ? box.right : box.bottom) - origin_along;
        const double across_low = (x_axis ? box.top : box.left) - origin_across;
        const double across_high = (x_axis ? box.bottom : box.right) - origin_across;
        if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(across_low) ||
            !std::isfinite(across_high) || !std::isfinite(tile)) {
            return; // a broken position: nothing to compare against
        }
        const auto along_count = static_cast<double>(x_axis ? grid->width : grid->height);
        const auto across_count = static_cast<double>(x_axis ? grid->height : grid->width);

        // Tile lines as whole numbers, clamped to the map. The map is at most
        // max_tilemap_side tiles a side, so these never overflow.
        const auto clamp_line = [](double line, double count) {
            return static_cast<std::int64_t>(std::clamp(line, -1.0, count));
        };
        const Span across = covered(across_low, across_high, tile);
        const std::int64_t across_first = clamp_line(std::max(across.first, 0.0), across_count);
        const std::int64_t across_end =
            clamp_line(std::min(across.end, across_count), across_count);
        if (across_first >= across_end) {
            return; // the box passes beside the map
        }
        const auto solid_line = [&](std::int64_t line) {
            for (std::int64_t other = across_first; other < across_end; ++other) {
                const auto a = static_cast<std::uint32_t>(line);
                const auto b = static_cast<std::uint32_t>(other);
                if ((x_axis ? grid->at(a, b) : grid->at(b, a)) >= 0) {
                    return true;
                }
            }
            return false;
        };
        const Span now = covered(low, high, tile);
        if (distance > 0.0) {
            // The lines entered: from the first one past the box to the last one reached.
            const std::int64_t first = clamp_line(now.end, along_count);
            const std::int64_t last = clamp_line(
                std::ceil((high + distance - edge_tolerance) / tile) - 1.0, along_count - 1.0);
            for (std::int64_t line = std::max<std::int64_t>(first, 0); line <= last; ++line) {
                if (solid_line(line)) {
                    allowed =
                        std::min(allowed, std::max(0.0, (static_cast<double>(line) * tile) - high));
                    break;
                }
            }
        } else {
            const std::int64_t first = clamp_line(now.first - 1.0, along_count - 1.0);
            const std::int64_t last =
                clamp_line(std::floor((low + distance + edge_tolerance) / tile), along_count);
            for (std::int64_t line = first; line >= std::max<std::int64_t>(last, 0); --line) {
                if (solid_line(line)) {
                    allowed = std::max(allowed,
                                       std::min(0.0, (static_cast<double>(line + 1) * tile) - low));
                    break;
                }
            }
        }
    });
    return allowed;
}

// ---------------------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------------------

std::vector<world::Entity> animate(world::World& world, double seconds) {
    std::vector<world::Entity> finished;
    const reflect::TypeInfo* type = world.registry().find_type<SpriteAnimation>();
    if (type == nullptr) {
        return finished;
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
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

const render::Texture* TextureCache::get(Guid image) {
    if (const auto found = textures_.find(image); found != textures_.end()) {
        return found->second.get();
    }
    const assets::AssetRecord* record = assets_.find(image);
    if (record == nullptr) {
        last_error_ = "There is no asset " + image.to_string() + ".";
        return nullptr;
    }
    if (record->importer != "image") {
        last_error_ = record->path + " is not an image.";
        return nullptr;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = assets_.read(image);
    if (!bytes) {
        last_error_ = record->path + " cannot be read.";
        return nullptr;
    }
    std::string error;
    const std::optional<assets::Image> decoded = assets::decode_png(*bytes, error);
    if (!decoded) {
        last_error_ = record->path + ": " + error;
        return nullptr;
    }
    std::unique_ptr<render::Texture> texture =
        device_.create_texture(decoded->width, decoded->height, decoded->rgba);
    if (texture == nullptr) {
        last_error_ = record->path + ": " + device_.last_error();
        return nullptr;
    }
    const render::Texture* result = texture.get();
    textures_.emplace(image, std::move(texture));
    return result;
}

void TextureCache::apply(const assets::AssetChanges& changes) {
    for (const Guid guid : changes.modified) {
        textures_.erase(guid);
    }
    for (const Guid guid : changes.removed) {
        textures_.erase(guid);
    }
}

// ---------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------

namespace {

struct Item {
    std::int32_t layer = 0;
    double y = 0.0;
    Guid guid;             ///< The entity's: a stable tie-breaker.
    std::uint8_t part = 0; ///< 0 for a tilemap, 1 for a sprite: the tilemap goes first.
    std::size_t index = 0; ///< The tile's position in its map.
    const render::Texture* texture{nullptr};
    render::Rect source;
    render::Rect destination;
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

    const auto to_screen = [&](double left, double top, double width, double height) {
        return render::Rect{static_cast<float>(centre_x + (left * scale)),
                            static_cast<float>(centre_y + (top * scale)),
                            static_cast<float>(width * scale), static_cast<float>(height * scale)};
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
                            const auto n = static_cast<std::uint32_t>(number);
                            const std::uint32_t column = n % columns;
                            const std::uint32_t row = n / columns;
                            Item item = base;
                            item.part = 0;
                            item.index = (std::size_t{y} * grid->width) + x;
                            item.texture = texture;
                            item.source = render::Rect{
                                static_cast<float>(column * size), static_cast<float>(row * size),
                                static_cast<float>(size), static_cast<float>(size)};
                            item.destination =
                                to_screen(left + (x * tile), top + (y * tile), tile, tile);
                            item.tint = render::Color{255, 255, 255, 255};
                            items.push_back(item);
                        }
                    }
                }
            }

            if (const Sprite* sprite = world.get<Sprite>(entity);
                sprite != nullptr && sprite->visible) {
                const std::optional<Guid> image = Guid::parse(sprite->image);
                const render::Texture* texture = image ? textures.get(*image) : nullptr;
                if (texture == nullptr) {
                    ++stats.missing;
                    return;
                }
                const render::Rect source =
                    frame_rect(*texture, sprite->frame_width, sprite->frame_height, sprite->frame);
                const auto width = static_cast<double>(source.w);
                const auto height = static_cast<double>(source.h);
                // Snap in source pixels, then upscale by a whole number: pixel-perfect.
                const double left =
                    std::round(((transform->x - camera.x) * ppu) - (sprite->pivot_x * width));
                const double top =
                    std::round(((transform->y - camera.y) * ppu) - (sprite->pivot_y * height));
                Item item = base;
                item.part = 1;
                item.texture = texture;
                item.source = source;
                item.destination = to_screen(left, top, width, height);
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
        sprites.draw(*item.texture, item.source, item.destination, item.tint);
    }
    sprites.end();
    stats.drawn = items.size();
    return stats;
}

} // namespace engine::scene2d
