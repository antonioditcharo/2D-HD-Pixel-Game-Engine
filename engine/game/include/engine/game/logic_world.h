#pragma once

// Connects the logic runtime to the world: no-code Rules and Sequences name entities, and
// reach their components through the reflection registry (plan/05 §2, "Who").
//
// See docs/dev/game.md.

#include "engine/logic/compiler.h"
#include "engine/logic/vm.h"
#include "engine/world/world.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// The entity a logic name refers to: an entity whose GUID is `name` (32 hexadecimal
/// digits), or else the only entity with that exact name. Invalid if there is none, or if
/// several entities share the name (logic must then use the GUID).
[[nodiscard]] world::Entity resolve(const world::World& world, std::string_view name);

/// The names logic can use for `entity`: its GUID, then its name if no other entity has
/// it. Empty if the entity is not alive.
[[nodiscard]] std::vector<std::string> names_of(const world::World& world, world::Entity entity);

/// `entity`'s tags (its Tags component), in lower case, each once; empty if it has none.
[[nodiscard]] std::vector<std::string> tags_of(const world::World& world, world::Entity entity);
/// names_of(), then "#tag" for each of `entity`'s tags: every name logic can use for
/// something it is part of.
[[nodiscard]] std::vector<std::string> logic_names(const world::World& world, world::Entity entity);
/// The entities tagged `tag` (any case), in the world's order.
[[nodiscard]] std::vector<world::Entity> tagged(const world::World& world, std::string_view tag);

/// Emits one of `entity`'s events, such as an animation's "finished", under each of its
/// names (names_of()). The VM matches events by the name a Rule uses, so this reaches
/// Rules written with either name. It also reaches Rules about each of its tags ("#enemy"),
/// with the entity's GUID after the event's values, for their "it". Returns false if no
/// name was accepted (see Vm::emit()).
bool emit(logic::Vm& vm, const world::World& world, world::Entity entity, std::string_view event,
          std::span<const reflect::Value> payload = {});

/// Lets scripts in `vm` reach the entities of `world` by name or GUID, through all their
/// components. Components are looked up afresh at every access, so storage moves are safe.
/// The world must outlive the Vm's use of it; objects bound with Vm::bind() take precedence.
void attach(logic::Vm& vm, world::World& world);

/// What the logic compiler needs to check an asset against `world`: the component types
/// of each named entity.
[[nodiscard]] logic::ObjectTypes object_types(const world::World& world);

} // namespace engine::game
