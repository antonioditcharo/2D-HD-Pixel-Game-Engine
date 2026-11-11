#include "editor/core/preview.h"

#include "engine/game/runtime.h"
#include "engine/render/render.h"
#include "engine/scene2d/scene2d.h"

#include <cstddef>
#include <utility>

namespace editor {

namespace game = engine::game;
namespace render = engine::render;
namespace scene2d = engine::scene2d;

struct Preview::Impl {
    std::unique_ptr<render::Device> device;
    std::unique_ptr<render::SpriteRenderer> sprites;
    std::unique_ptr<render::WorldRenderer> world;
    std::unique_ptr<render::PostProcessor> post;
    std::unique_ptr<render::RenderTarget> scene;
    std::unique_ptr<render::RenderTarget> frame;
    // Textures belong to one document's assets.
    const Document* textures_for = nullptr;
    std::unique_ptr<scene2d::TextureCache> textures;
};

Preview::Preview(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Preview::~Preview() = default;

std::unique_ptr<Preview> Preview::create(std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->device = render::Device::create(render::DeviceOptions{true, false}, error);
    if (impl->device == nullptr) {
        return nullptr;
    }
    impl->sprites = render::SpriteRenderer::create(*impl->device);
    impl->world = render::WorldRenderer::create(*impl->device);
    impl->post = render::PostProcessor::create(*impl->device);
    if (impl->sprites == nullptr || impl->world == nullptr || impl->post == nullptr) {
        error = impl->device->last_error();
        return nullptr;
    }
    return std::unique_ptr<Preview>(new Preview(std::move(impl)));
}

std::optional<engine::assets::Image> Preview::render(Document& document, double x, double y,
                                                     std::string& error) {
    Impl& p = *impl_;
    const game::ProjectSettings& settings = document.settings();
    for (std::unique_ptr<render::RenderTarget>* target : {&p.scene, &p.frame}) {
        if (*target == nullptr || (*target)->width() != settings.width ||
            (*target)->height() != settings.height) {
            *target = p.device->create_render_target(settings.width, settings.height);
            if (*target == nullptr) {
                error = p.device->last_error();
                return std::nullopt;
            }
        }
    }
    if (p.textures_for != &document) {
        p.textures = std::make_unique<scene2d::TextureCache>(*p.device, document.assets());
        p.textures_for = &document;
    }

    // As the game draws it (engine/game/src/runtime.cpp): the style, its light, the look.
    render::Look look = settings.look;
    if (settings.style != game::RenderStyle::Diorama) {
        look.tilt_shift = false; // a lens effect for the 3D style only
    }
    const bool post = !look.neutral();
    render::RenderTarget& target = post ? *p.scene : *p.frame;
    if (settings.style == game::RenderStyle::Diorama) {
        const scene2d::DioramaCamera camera = diorama_camera(settings, x, y);
        scene2d::draw_diorama(document.world(), *p.textures, *p.world, target, camera,
                              settings.background,
                              game::diorama_light(settings, game::starting_lighting(settings)));
    } else {
        scene2d::Camera2D camera;
        camera.x = x;
        camera.y = y;
        camera.ppu = settings.ppu;
        scene2d::draw_world(document.world(), *p.textures, *p.sprites, target, camera,
                            settings.background);
    }
    if (post && !p.post->apply(*p.scene, *p.frame, look)) {
        error = p.device->last_error();
        return std::nullopt;
    }
    engine::assets::Image image;
    image.width = p.frame->width();
    image.height = p.frame->height();
    image.rgba = p.frame->read_pixels();
    if (image.rgba.size() != std::size_t{image.width} * image.height * 4) {
        error = p.device->last_error();
        return std::nullopt;
    }
    return image;
}

} // namespace editor
