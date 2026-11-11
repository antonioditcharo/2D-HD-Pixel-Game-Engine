#pragma once

// The "lighting" object logic uses to light a Diorama scene: how bright the ambient light
// and the sun are. Rules and Brains set them for dusk, night, or walking into a cave; the
// project file gives the starting values. Lights (scene2d's Light) add to them.
//
// See docs/dev/game.md §3 and docs/dev/scene2d.md §7.

#include "engine/foundation/reflect.h"

namespace engine::game {

struct LightingObject {
    double ambient = 0.55; ///< Light from everywhere, 0 to 2: the darkness of shadows.
    double sun = 0.45;     ///< The sun's (or moon's) strength, 0 to 2.
};

/// Registers LightingObject as "lighting".
void register_lighting_types(reflect::Registry& registry);

} // namespace engine::game
