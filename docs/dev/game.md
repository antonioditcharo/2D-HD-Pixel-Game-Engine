# Game: Logic Meets the World

> **Status: v0.1.** No-code logic reaches the entities of a world by name or GUID, through
> all their components, safely across storage moves. Every behaviour below is covered by
> tests.
>
> Code: [`engine/game/include/engine/game/logic_world.h`](../../engine/game/include/engine/game/logic_world.h)
> and [`src/logic_world.cpp`](../../engine/game/src/logic_world.cpp). Tests:
> [`tests/game/logic_world_test.cpp`](../../tests/game/logic_world_test.cpp).

## 1. What it is for

A Rule such as *"When the chest is opened: unlock it, add 10 gold"* names an object, the
chest, and members that live on different components: `unlock` on a Lock, `gold` on a
Purse. This module lets the logic runtime ([logic-runtime.md](logic-runtime.md)) find
that object in the world ([world.md](world.md)), and lets the compiler check the Rule
against it. `engine/game` will grow into the runtime that connects all the engine's parts.

## 2. Using it

```cpp
world::World w(registry);
logic::Vm vm(registry);
game::attach(vm, w);   // scripts now reach w's entities

logic::CompiledLogic compiled = logic::compile(asset, registry, game::object_types(w));
vm.load(asset.name, compiled.source);
vm.emit("chest", "opened");
```

## 3. How names resolve

| Name used in logic | Resolves to |
|---|---|
| 32 hexadecimal digits that are an entity's GUID | That entity |
| An entity's exact name, shared by no other entity | That entity |
| A name shared by several entities, or no entity at all | Nothing: the compiler reports *"There is no object called 'barrel'."*, and so does the VM at run time |

GUIDs never change, so the editor will write them into logic assets. Names are for
people, and for logic written by hand while they are unique.

## 4. Objects made of several parts

- `Vm::set_object_resolver()` lets the VM ask for an object's parts by name whenever a
  script uses one. For an entity, the parts are its components. `sx.call`, `sx.get`,
  `sx.set` and `Vm::emit()` use the first part that has the member asked for.
- **No addresses are kept.** The resolver runs at every access. Adding a component, or
  creating entities, can move a component's data (flecs stores components in tables);
  logic keeps working because it never holds the old address (tested).
- The compiler's `ObjectTypes` gives all of an object's types, and every lookup searches
  across them, so Rules are checked against everything the entity really has.
- Objects bound with `Vm::bind()` still work and take precedence over the resolver.

## 5. Next increments

1. **Spawning and destroying from logic,** with entities created by Rules addressable by
   GUID.
2. **"Who" picking** (`plan/05` §2): *"all objects tagged Enemy"*, *"the other object"* in
   a touch event.
3. **The game loop:** a fixed-step update that runs input, physics, logic and drawing in
   the order of `plan/05` §5.
