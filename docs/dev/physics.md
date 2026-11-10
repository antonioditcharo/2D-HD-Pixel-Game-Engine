# Physics

> **Status: v0.1.** Rigid bodies on Box2D v3 (ADR-008): boxes and circles that fall,
> slide, bounce and push, collide with solid tilemaps, and tell Rules what they touch.
> Players with a Physics Body push crates mass for mass. Tested for resting heights,
> bounces, pushes, damping, contacts, hostile input and determinism.
>
> Code: [`engine/physics/include/engine/physics/physics.h`](../../engine/physics/include/engine/physics/physics.h),
> [`src/physics.cpp`](../../engine/physics/src/physics.cpp), and the Physics Body and
> `PhysicsLink` in [`engine/game/include/engine/game/behaviours.h`](../../engine/game/include/engine/game/behaviours.h).
> Tests: [`tests/physics/physics_test.cpp`](../../tests/physics/physics_test.cpp) and the
> physics test in [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

Crates to push, boulders that roll, things that fall and bounce, and side-view games with
gravity. ADR-008 chose **Box2D v3** (MIT) for 2D; `engine/physics` wraps it so its types
never appear in a header, as `engine/audio` does for miniaudio.

Most RPG movement does not need physics: a Top-down Mover without a Physics Body moves by
the exact sweep against walls and Solid Bodies ([game.md](game.md) §4), which never
pushes anything. Physics is for what should behave like objects.

## 2. The facade

```cpp
auto world = physics::PhysicsWorld::create(0.0, 20.0);   // gravity: down the map is +y
physics::BodyDef crate;
crate.x = 3.0; crate.y = 0.0;                             // the centre, in tiles
const physics::BodyId id = world->add(crate);
world->step(1.0 / 60.0);
const auto where = world->state(id);                      // position, angle, velocity
for (const physics::Contact& c : world->contacts()) { /* started touching */ }
```

- **Units:** one tile is one metre to Box2D, which is tuned for objects of 0.1 to 10
  metres. Maps use y downwards, so gravity down is +y.
- **Bodies:** static (walls), dynamic (moved by forces and collisions), kinematic (moved
  by velocity only: pushes dynamic bodies, is never pushed). Boxes and circles. Density,
  friction, restitution (bounciness), damping and gravity scale. Rotation is fixed by
  default, because sprites do not turn.
- **Each step** is four substeps. Contacts that began during it are listed by the `user`
  value each body was given.
- **Hostile input:** non-finite or enormous positions are refused; sizes, densities and
  the rest are clamped to sensible ranges, so scene data cannot break the simulation.
- **Deterministic:** the same steps give the same result (tested), as Box2D v3 promises.

## 3. Physics in games

**Physics Body** (`physics_body`) puts an entity in the simulation:

| Property | Meaning |
|---|---|
| `type` | Fixed (static), Moving (dynamic) or Driven (kinematic) |
| `shape`, `width`, `height`, `radius` | A box or a circle, in tiles, sitting on the position like every behaviour's box |
| `bounciness`, `damping` | 0 to 1; and how fast it slows down (top-down games need some, or pushed things never stop) |
| `density`, `friction`, `gravity_scale` (advanced) | Weight for its size, grip, and how strongly gravity pulls |

- Action *"Push it by (x, y)"* gives an instant push; event *"When it touches {who}"*
  happens when it starts touching another body or a wall (`who` is the other's name, or
  its GUID if the name is shared).
- **Who drives whom:** a Moving body's Position follows the simulation. Fixed and Driven
  bodies follow their Position, so logic moves them; a Driven body gets the speed it needs
  to reach its new Position within the step, pushing what is in the way.
- **Players:** a Top-down Mover that also has a Moving Physics Body is moved by the
  simulation instead of the sweep: its input sets the body's speed, and the simulation
  stops it at walls and lets it push crates according to their mass. Facing and walk
  animations work the same.
- **Walls:** every solid Tilemap is static boxes, one per run of solid tiles in a row.
- **Gravity** is the project's `gravity` setting ([game.md](game.md) §2): `[0, 0]` for
  top-down games, `[0, 30]` or so for side views.
- **Order in the step:** after movement, before trigger areas ([game.md](game.md) §3).
- **Saved games:** positions are saved with the world; after loading, the simulation is
  rebuilt from the loaded world (velocities start from rest).
- New Physics Bodies join the simulation at the next step, and removed ones leave it.
  Tilemaps changed during play are not rebuilt until a save is loaded.

## 4. Tests

| Test | Checks |
|---|---|
| Falling and resting | A box falls (down is +y, speeding up), rests with its bottom on the floor's top within 1%, does not rotate, and touches the floor exactly once |
| Bounce, push, damping | A bouncy ball comes back up fast; a kinematic body pushes a box ahead and is never pushed back; damping stops a pushed box while an undamped one keeps its speed |
| Bad input | Non-finite and huge positions refused; absurd sizes and densities clamped; removed bodies are gone |
| Determinism | Ten boxes and circles piling up give identical positions twice |
| In a game | With gravity, a crate falls onto a solid tilemap floor and its Rule names the floor; top-down, a player with a body pushes the crate until a wall stops it exactly, its Rule naming the player and the wall; after loading a save, the crate is back and physics continues from there |

## 5. Next increments

1. **Jolt** for the Diorama style's 3D physics (ADR-008), behind the same facade.
2. **Sensors** (areas that report bodies passing through) and **joints** (doors on
   hinges, chains).
3. **One-way platforms and a character mover** for side-view games (Box2D v3.1).
4. **Physics debug drawing** in the editor.
