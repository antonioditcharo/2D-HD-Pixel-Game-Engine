# The World

> **Status: v0.1.** Entities with GUIDs, names and an ordered hierarchy, holding components
> that are reflected types, stored in flecs. Scenes save and load as JSON
> ([serialization.md](serialization.md) §5). Every behaviour below is covered by tests.
>
> Code: [`engine/world/include/engine/world/world.h`](../../engine/world/include/engine/world/world.h)
> and [`src/world.cpp`](../../engine/world/src/world.cpp); GUIDs in
> [`engine/foundation/include/engine/foundation/guid.h`](../../engine/foundation/include/engine/foundation/guid.h).
> Tests: [`tests/world/world_test.cpp`](../../tests/world/world_test.cpp).

## 1. What it is for

Everything placed in a map is an **entity**: the hero, a lamp, a door, an invisible
trigger. What an entity *is* comes from its **components**, such as a transform, a sprite
or a light. The world holds them all (ADR-006, `plan/03` §2).

Components are ordinary C++ structs registered in the reflection registry
([reflection.md](reflection.md)). So every component automatically gets an inspector,
no-code blocks and a place in scene files, with nothing extra to write.

## 2. Using it

```cpp
struct Lamp { bool lit = true; double radius = 4.0; };

reflect::Registry registry;
registry.add_type<Lamp>("lamp").property<&Lamp::lit>("lit").property<&Lamp::radius>("radius");

world::World w(registry);
w.register_component<Lamp>();

world::Entity house = w.create("House");
world::Entity lamp = w.create("Porch lamp");
w.set_parent(lamp, house);
w.add<Lamp>(lamp)->radius = 6.0;

// Type-erased, as the editor and scene files use it:
reflect::ObjectRef ref = w.get(lamp, *registry.find_type<Lamp>());
```

## 3. Entities

| Feature | Behaviour |
|---|---|
| **GUID** | Every entity has a 128-bit GUID, random (version 4) unless given, and unique in the world. References between entities, assets and files use GUIDs, never names or positions (`plan/03` §4) |
| **Name** | For people. Names need not be unique; `find_named()` lists the entities with a name. Logic uses a name only while it is unique ([game.md](game.md)) |
| **Handles** | `Entity` is a small handle. A destroyed entity's handle never becomes valid again, even when its storage is reused: every operation checks it first |
| **Hierarchy** | Every entity has at most one parent and an **ordered** list of children. `roots()` lists parentless entities in creation order. The order is kept so that saved scenes are deterministic and the editor's outline does not shuffle |
| **Cycles** | `set_parent` refuses to put an entity under itself or one of its descendants |
| **Destroying** | Destroys the whole subtree |

## 4. Components

- **Registering:** `register_component<T>()` for any reflected type that is
  default-constructible, copyable and movable (checked at compile time). The world builds
  the constructor, destructor, copy and move operations from the C++ type, so components
  may hold `std::string` and other non-trivial members (tested).
- **Type-erased access** by `TypeInfo`: `add`, `get`, `remove`, `components` and `each`.
  The typed `add<T>` and `get<T>` are shortcuts.
- `components(entity)` lists types in the order they were registered, which keeps scene
  files deterministic.
- **Component pointers move.** flecs stores components in tables per combination of
  components. Adding or removing a component, or creating or destroying an entity, can
  move data. An `ObjectRef` or `T*` is valid only until the next such change; look it up
  again after one.

## 5. Rules and design decisions

| Rule | Why |
|---|---|
| **flecs is an implementation detail.** Only `world.cpp` includes it, through its C API. | Every other module works with the reflection registry's `TypeInfo`, which is what the editor and files need. The ECS could be replaced without touching them. |
| **The engine's reflection registry is the source of truth** for components; flecs is told about the types, not the other way round. | Editor metadata such as tooltips, units and ranges is ours (`plan/03` §1, ADR-006). |
| **GUIDs, names and the hierarchy live in the world's own records**, not in flecs relationships. | Children need a stable order for deterministic files, and the world must update its GUID index when a subtree is destroyed. |
| **Every handle is checked before it reaches flecs.** | flecs aborts on invalid ids in debug builds. User content must never crash the engine (`plan/03` §1). Tested with stale and made-up handles in the sanitizer build, which uses flecs's debug library. |
| **Linear hierarchy building.** A child without children cannot create a cycle, so attaching one is constant time; only moving a subtree walks up from the new parent. | Loading a 20,000-deep chain took 5.2 s with the general check and takes 0.14 s with this one. |

## 6. Next increments

In order:

1. **Transforms:** a standard transform component with local and world positions, updated
   down the hierarchy.
2. **Prefabs with overrides** (`plan/03` §4), using flecs prefabs underneath.
3. **Queries and systems** over several components, for rendering and physics.
4. **A custom flecs build without its HTTP and REST addons,** so exported games contain no
   web server code at all. Sparxel never starts it, but it is compiled in today
   (`THIRD_PARTY_NOTICES.md`).
