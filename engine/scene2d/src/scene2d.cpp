#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <cmath>
#include <optional>
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
}

bool register_components(world::World& world) {
    const bool transform = world.register_component<Transform2D>();
    const bool sprite = world.register_component<Sprite>();
    return transform && sprite;
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
    Guid guid; ///< The entity's: a stable tie-breaker.
    const render::Texture* texture{nullptr};
    render::Rect destination;
    render::Color tint;
};

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

    if (transform_type != nullptr) {
        world.each(*transform_type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Transform2D* transform = ref.as<Transform2D>();
            const Sprite* sprite = world.get<Sprite>(entity);
            if (transform == nullptr || sprite == nullptr || !sprite->visible) {
                return;
            }
            const std::optional<Guid> image = Guid::parse(sprite->image);
            const render::Texture* texture = image ? textures.get(*image) : nullptr;
            if (texture == nullptr) {
                ++stats.missing;
                return;
            }
            const auto width = static_cast<double>(texture->width());
            const auto height = static_cast<double>(texture->height());
            // Snap in source pixels, then upscale by a whole number: pixel-perfect.
            const double left =
                std::round((transform->x - camera.x) * ppu - sprite->pivot_x * width);
            const double top =
                std::round((transform->y - camera.y) * ppu - sprite->pivot_y * height);
            Item item;
            item.layer = transform->layer;
            item.y = transform->y;
            item.guid = world.guid(entity);
            item.texture = texture;
            item.destination =
                render::Rect{static_cast<float>(centre_x + left * scale),
                             static_cast<float>(centre_y + top * scale),
                             static_cast<float>(width * scale), static_cast<float>(height * scale)};
            item.tint =
                render::Color{sprite->tint_r, sprite->tint_g, sprite->tint_b, sprite->tint_a};
            items.push_back(item);
        });
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return std::tie(a.layer, a.y, a.guid) < std::tie(b.layer, b.y, b.guid);
    });

    sprites.begin(target, clear);
    for (const Item& item : items) {
        sprites.draw(*item.texture,
                     render::Rect{0, 0, static_cast<float>(item.texture->width()),
                                  static_cast<float>(item.texture->height())},
                     item.destination, item.tint);
    }
    sprites.end();
    stats.drawn = items.size();
    return stats;
}

} // namespace engine::scene2d
