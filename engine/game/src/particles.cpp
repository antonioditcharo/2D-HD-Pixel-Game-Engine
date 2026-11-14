#include "engine/game/particles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace engine::game {

namespace {

/// What a preset's particles do: where they start, how they move, how long they last,
/// and their colour from birth to death.
struct Recipe {
    double rate;  ///< A second.
    double width; ///< The area, in tiles.
    double depth;
    double start_h_low; ///< Height they start at.
    double start_h_high;
    double speed_low; ///< Up (or down, below 0), in tiles a second.
    double speed_high;
    double spread; ///< Sideways speed, either way, in tiles a second.
    double lift;   ///< Height gained a second a second (gravity below 0).
    double drift;  ///< Side to side, as snow and dust drift.
    double wind;   ///< Across, in tiles a second (rain slants).
    double life_low;
    double life_high;
    render::Color from;
    render::Color to;
};

Recipe recipe(ParticlePreset preset) {
    switch (preset) {
    case ParticlePreset::Smoke:
        return {8,
                0.5,
                0.25,
                0.3,
                0.6,
                0.5,
                0.9,
                0.15,
                0.0,
                0.25,
                0.2,
                2.0,
                3.0,
                {150, 150, 155, 170},
                {70, 70, 80, 0}};
    case ParticlePreset::Rain:
        return {120,
                20,
                12,
                6.0,
                8.0,
                -12.0,
                -11.0,
                0.0,
                0.0,
                0.0,
                -1.5,
                0.6,
                0.7,
                {170, 200, 255, 200},
                {150, 180, 240, 140}};
    case ParticlePreset::Snow:
        return {40,
                20,
                12,
                6.0,
                8.0,
                -1.6,
                -1.2,
                0.1,
                0.0,
                0.6,
                0.2,
                4.5,
                5.5,
                {250, 250, 255, 240},
                {230, 235, 255, 200}};
    case ParticlePreset::Dust:
        return {6,
                6,
                4,
                0.2,
                1.5,
                -0.05,
                0.05,
                0.2,
                0.0,
                0.4,
                0.1,
                2.5,
                3.5,
                {230, 210, 170, 0},
                {230, 210, 170, 0}};
    case ParticlePreset::Magic:
        return {20,
                0.75,
                0.4,
                0.0,
                0.4,
                0.6,
                1.0,
                0.25,
                0.0,
                0.8,
                0.0,
                1.0,
                1.4,
                {140, 240, 255, 255},
                {190, 110, 255, 0}};
    case ParticlePreset::Sparks:
        return {0,
                0.25,
                0.25,
                0.2,
                0.4,
                2.0,
                4.0,
                3.0,
                -14.0,
                0.0,
                0.0,
                0.35,
                0.5,
                {255, 240, 150, 255},
                {255, 120, 30, 0}};
    case ParticlePreset::Fire:
        break;
    }
    return {30,
            0.5,
            0.25,
            0.0,
            0.1,
            1.4,
            2.4,
            0.25,
            0.6,
            0.15,
            0.0,
            0.5,
            0.9,
            {255, 235, 130, 255},
            {210, 40, 20, 0}};
}

/// A number from 0 to 1 from xorshift32: an emitter's particles replay the same.
double random01(std::uint32_t& state) {
    if (state == 0) {
        state = 0x9E3779B9U;
    }
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<double>(state % 100000U) / 100000.0;
}

double between(std::uint32_t& state, double low, double high) {
    return low + ((high - low) * random01(state));
}

std::uint8_t mix(std::uint8_t a, std::uint8_t b, double t) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(a + ((static_cast<double>(b) - a) * t)), 0L, 255L));
}

/// Dust glows in and out, rather than fading only: fully seen in the middle of its life.
double dust_alpha(double t) {
    return 150.0 * std::sin(t * 3.14159265358979323846);
}

void spawn(ParticleEmitter& emitter, const Recipe& r, double x, double y) {
    if (emitter.particles.size() >= most_particles) {
        return;
    }
    std::uint32_t& s = emitter.seed;
    const double width = emitter.width > 0.0 ? emitter.width : r.width;
    const double depth = emitter.height > 0.0 ? emitter.height : r.depth;
    Particle p;
    p.x = x + between(s, -width / 2.0, width / 2.0);
    p.y = y + between(s, -depth / 2.0, depth / 2.0);
    p.h = between(s, r.start_h_low, r.start_h_high);
    const double angle = between(s, 0.0, 6.283185307179586);
    const double sideways = between(s, 0.0, r.spread);
    p.vx = (std::cos(angle) * sideways) + r.wind;
    p.vy = std::sin(angle) * sideways;
    p.vh = between(s, r.speed_low, r.speed_high);
    p.life = between(s, r.life_low, r.life_high);
    p.phase = between(s, 0.0, 6.283185307179586);
    emitter.particles.push_back(p);
}

} // namespace

void ParticleEmitter::burst(std::int64_t count) {
    pending_burst = static_cast<std::uint32_t>(std::min<std::int64_t>(
        static_cast<std::int64_t>(pending_burst) + std::max<std::int64_t>(count, 0), 500));
}

void register_particle_types(reflect::Registry& registry) {
    registry.add_choice_list<ParticlePreset>("particle_preset")
        .display_name("Particles")
        .entry(ParticlePreset::Fire, "fire")
        .entry(ParticlePreset::Smoke, "smoke")
        .entry(ParticlePreset::Rain, "rain")
        .entry(ParticlePreset::Snow, "snow")
        .entry(ParticlePreset::Dust, "dust")
        .entry(ParticlePreset::Magic, "magic")
        .entry(ParticlePreset::Sparks, "sparks");
    auto emitter = registry.add_type<ParticleEmitter>("particles")
                       .display_name("Particles")
                       .category("Visuals")
                       .tooltip("Pixel particles where it stands: fire, smoke, rain, snow, "
                                "dust, magic or sparks.");
    emitter.property<&ParticleEmitter::preset>("preset").display_name("Kind").tooltip(
        "What the particles are: each moves and colours its own way.");
    emitter.property<&ParticleEmitter::emitting>("emitting")
        .display_name("On")
        .tooltip("Whether new particles keep coming.");
    emitter.property<&ParticleEmitter::rate>("rate")
        .range(0.0, 500.0)
        .step(1.0)
        .display_name("How many")
        .tooltip("Particles a second; 0 uses the kind's own (30 for fire, 120 for rain).");
    emitter.property<&ParticleEmitter::width>("width")
        .range(0.0, 64.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .display_name("Area across")
        .tooltip("How wide the area they start in is; 0 uses the kind's own (20 for rain).");
    emitter.property<&ParticleEmitter::height>("height")
        .range(0.0, 64.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .display_name("Area down")
        .tooltip("How deep the area they start in is, down the map; 0 uses the kind's own.");
    emitter.property<&ParticleEmitter::size>("size")
        .range(1.0, 4.0)
        .unit(reflect::Unit::Pixels)
        .display_name("Pixel size")
        .tooltip("How big each particle is, in pixels.");
    emitter.action<&ParticleEmitter::start>("start")
        .text("Start the particles")
        .tooltip("New particles keep coming.");
    emitter.action<&ParticleEmitter::stop>("stop")
        .text("Stop the particles")
        .tooltip("No new particles come; those flying finish.");
    emitter.action<&ParticleEmitter::burst>("burst", {"count"})
        .text("Send out {count} particles")
        .tooltip("A burst at once: sparks from a hit, a puff of smoke.")
        .param("count")
        .range(1.0, 500.0)
        .default_value(reflect::Value{std::int64_t{20}});
}

void update_particles(world::World& world, double seconds) {
    const reflect::TypeInfo* type = world.registry().find_type<ParticleEmitter>();
    if (type == nullptr || !std::isfinite(seconds) || seconds < 0.0) {
        return;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        ParticleEmitter* emitter = ref.as<ParticleEmitter>();
        const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
        if (emitter == nullptr || at == nullptr || !std::isfinite(at->x) || !std::isfinite(at->y)) {
            return;
        }
        if (emitter->seed == 0) {
            std::uint32_t hash = 2166136261U;
            for (const char c : world.guid(entity).to_string()) {
                hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
            }
            emitter->seed = hash == 0 ? 1U : hash;
        }
        const Recipe r = recipe(emitter->preset);
        // Move and age; the old and those that hit the ground go.
        for (Particle& p : emitter->particles) {
            p.age += seconds;
            p.vh += r.lift * seconds;
            const double sway = r.drift * std::sin((p.age * 2.0) + p.phase);
            p.x += (p.vx + sway) * seconds;
            p.y += p.vy * seconds;
            p.h += p.vh * seconds;
        }
        std::erase_if(emitter->particles,
                      [](const Particle& p) { return p.age >= p.life || p.h < -0.05; });
        // New ones: at the rate, as whole particles come due; then any burst.
        const double rate = emitter->rate > 0.0 ? std::min(emitter->rate, 500.0) : r.rate;
        if (emitter->emitting && rate > 0.0) {
            emitter->due += rate * seconds;
            while (emitter->due >= 1.0) {
                emitter->due -= 1.0;
                spawn(*emitter, r, at->x, at->y);
            }
        } else {
            emitter->due = 0.0;
        }
        for (; emitter->pending_burst > 0; --emitter->pending_burst) {
            spawn(*emitter, r, at->x, at->y);
        }
    });
}

std::size_t draw_particles(const world::World& world, render::SpriteRenderer& sprites,
                           const render::Texture& white, render::RenderTarget& target,
                           const ParticleView& view) {
    const reflect::TypeInfo* type = world.registry().find_type<ParticleEmitter>();
    if (type == nullptr) {
        return 0;
    }
    std::size_t drawn = 0;
    bool begun = false;
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
    const double ppu = static_cast<double>(std::max<std::uint32_t>(view.flat.ppu, 1));
    const double w = static_cast<double>(view.width);
    const double h = static_cast<double>(view.height);
    world.each(*type, [&](world::Entity, reflect::ObjectRef ref) {
        const ParticleEmitter* emitter = ref.as<ParticleEmitter>();
        if (emitter == nullptr || emitter->particles.empty()) {
            return;
        }
        const Recipe r = recipe(emitter->preset);
        const auto side = static_cast<float>(std::clamp<std::uint32_t>(emitter->size, 1, 4));
        for (const Particle& p : emitter->particles) {
            double sx = 0.0;
            double sy = 0.0;
            if (view.diorama) {
                const std::array<float, 4> clip = view.view_projection.transform(render::Vec3{
                    static_cast<float>(p.x), static_cast<float>(p.h), static_cast<float>(p.y)});
                if (!(clip[3] > 0.0F)) {
                    continue; // behind the eye
                }
                sx = (static_cast<double>(clip[0] / clip[3]) + 1.0) / 2.0 * w;
                sy = (1.0 - static_cast<double>(clip[1] / clip[3])) / 2.0 * h;
            } else {
                sx = ((p.x - view.flat.x) * ppu) + (w / 2.0);
                sy = ((p.y - p.h - view.flat.y) * ppu) + (h / 2.0);
            }
            sx = std::floor(sx);
            sy = std::floor(sy);
            if (!std::isfinite(sx) || !std::isfinite(sy) || sx < -4.0 || sy < -4.0 || sx > w ||
                sy > h) {
                continue;
            }
            const double t = std::clamp(p.age / std::max(p.life, 1e-6), 0.0, 1.0);
            render::Color colour{mix(r.from.r, r.to.r, t), mix(r.from.g, r.to.g, t),
                                 mix(r.from.b, r.to.b, t), mix(r.from.a, r.to.a, t)};
            if (emitter->preset == ParticlePreset::Dust) {
                colour.a = static_cast<std::uint8_t>(std::lround(dust_alpha(t)));
            }
            if (!begun) {
                sprites.begin_over(target);
                begun = true;
            }
            // Rain falls as streaks two pixels long.
            const float tall = emitter->preset == ParticlePreset::Rain ? side * 2.0F : side;
            sprites.draw(white, one,
                         render::Rect{static_cast<float>(sx), static_cast<float>(sy), side, tall},
                         colour);
            ++drawn;
        }
    });
    if (begun) {
        static_cast<void>(sprites.end());
    }
    return drawn;
}

} // namespace engine::game
