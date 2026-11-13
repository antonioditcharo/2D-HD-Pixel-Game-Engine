#pragma once

// Pixel particles (plan/02 D12): fire, smoke, rain, snow, dust, magic and sparks, each a
// preset a creator picks, then tunes by how many and over how wide an area. Particles are
// whole pixels of colour, moving on the ground (across and down the map) and in height
// above it, so the same emitter reads the same in Classic 2D (height is up the screen) and
// in the Diorama style (height is up in the world).
//
// See docs/dev/game.md §4.

#include "engine/foundation/reflect.h"
#include "engine/render/render.h"
#include "engine/scene2d/scene2d.h"
#include "engine/world/world.h"

#include <cstdint>
#include <vector>

namespace engine::game {

enum class ParticlePreset : std::uint8_t { Fire, Smoke, Rain, Snow, Dust, Magic, Sparks };

struct Particle {
    double x = 0.0; ///< On the ground, in tiles.
    double y = 0.0;
    double h = 0.0; ///< Height above the ground, in tiles.
    double vx = 0.0;
    double vy = 0.0;
    double vh = 0.0;
    double age = 0.0;
    double life = 1.0;
    double phase = 0.0; ///< For drifting from side to side.
};

/// "Particles": an emitter of pixel particles at the entity's position.
struct ParticleEmitter {
    ParticlePreset preset = ParticlePreset::Fire;
    bool emitting = true;
    double rate = 0.0;  ///< Particles a second; 0 uses the preset's.
    double width = 0.0; ///< The area they start in, in tiles; 0 uses the preset's.
    double height = 0.0;
    std::uint32_t size = 1; ///< Each particle's side, in pixels, 1 to 4.
    // Not saved: the particles flying now, and when the next is due.
    std::vector<Particle> particles;
    double due = 0.0;
    std::uint32_t seed = 0;
    std::uint32_t pending_burst = 0;

    void start() { emitting = true; }
    void stop() { emitting = false; }
    /// Sends out `count` particles at once (up to 500): sparks from a hit, a puff of smoke.
    void burst(std::int64_t count);
};

/// The most particles one emitter has flying.
inline constexpr std::size_t most_particles = 2000;

/// Registers ParticleEmitter as "particles", and its presets.
void register_particle_types(reflect::Registry& registry);

/// One step of `seconds`: every emitter sends out new particles and moves and ages its
/// own; old ones go. The same steps always give the same particles.
void update_particles(world::World& world, double seconds);

/// Where particles are drawn: through a Classic 2D camera, or a Diorama camera's
/// view-projection, onto a target of `width` x `height` pixels.
struct ParticleView {
    bool diorama = false;
    scene2d::Camera2D flat;
    render::Mat4 view_projection;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Draws every particle as a square of its emitter's size, faded with its age, over what
/// `target` holds (with `white`, a texture of one white pixel). Returns the number drawn.
std::size_t draw_particles(const world::World& world, render::SpriteRenderer& sprites,
                           const render::Texture& white, render::RenderTarget& target,
                           const ParticleView& view);

} // namespace engine::game
