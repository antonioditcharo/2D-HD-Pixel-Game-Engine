#pragma once

// Connects the logic runtime to the world: no-code Rules and Sequences name entities, and
// reach their components through the reflection registry (plan/05 §2, "Who").
//
// See docs/dev/game.md.

#include "engine/logic/compiler.h"
#include "engine/logic/vm.h"
#include "engine/world/world.h"

#include <string_view>

namespace engine::game {

/// The entity a logic name refers to: an entity whose GUID is `name` (32 hexadecimal
/// digits), or else the only entity with that exact name. Invalid if there is none, or if
/// several entities share the name (logic must then use the GUID).
[[nodiscard]] world::Entity resolve(const world::World& world, std::string_view name);

/// Lets scripts in `vm` reach the entities of `world` by name or GUID, through all their
/// components. Components are looked up afresh at every access, so storage moves are safe.
/// The world must outlive the Vm's use of it; objects bound with Vm::bind() take precedence.
void attach(logic::Vm& vm, world::World& world);

/// What the logic compiler needs to check an asset against `world`: the component types
/// of each named entity.
[[nodiscard]] logic::ObjectTypes object_types(const world::World& world);

} // namespace engine::game
