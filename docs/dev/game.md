# Game: the Runtime

> **Status: v0.1, the first playable loop.** A project folder on disk (settings, PNG
> images, a scene and Rules) opens and runs: a hero walks with the keyboard or a gamepad,
> turning and animating as it goes, stopped by walls and people; Rules react to the game
> starting, to buttons, to talking to people, to walking through doorways and to
> animations finishing; people say things in message boxes ([ui.md](ui.md)); the player's progress saves to slots and loads back exactly; and
> the game draws pixel-perfect into a window that follows the hero. No-code logic reaches the world's entities by name or GUID, through all their
> components. Every behaviour below is covered by tests.
>
> Code: [`engine/game/include/engine/game/`](../../engine/game/include/engine/game/):
> [`runtime.h`](../../engine/game/include/engine/game/runtime.h),
> [`behaviours.h`](../../engine/game/include/engine/game/behaviours.h),
> [`logic_world.h`](../../engine/game/include/engine/game/logic_world.h),
> [`save.h`](../../engine/game/include/engine/game/save.h),
> [`dialogue.h`](../../engine/game/include/engine/game/dialogue.h) and
> [`clock.h`](../../engine/game/include/engine/game/clock.h). Tests:
> [`tests/game/`](../../tests/game/).

## 1. What it is for

`engine/game` connects the engine's parts into a game. The runtime opens a project
folder, loads its settings, assets, start scene and logic, then runs a loop of fixed
steps. An exported game runs exactly this, and the editor's Play button will run it too
(`plan/03` §8).

```mermaid
flowchart LR
    P["project.sparxel.json"] --> RT["Runtime::open()"]
    A["assets/<br/>PNG, .dscene, .drules"] --> RT
    RT --> W["World<br/>entities, components"]
    RT --> VM["Logic VM<br/>compiled Rules"]
    RT --> G["GPU<br/>window or offscreen"]
    subgraph Step["Each fixed step (1/60 s)"]
        direction LR
        I["1. Input"] --> M["2. Movement"] --> E["3. Input events<br/>to Rules"] --> S["4. Waiting<br/>Sequences"] --> AN["5. Animation"]
    end
    W --> Step
    VM --> Step
```

## 2. A project on disk

```
MyGame/
├── project.sparxel.json
└── assets/
    ├── art/hero.png (+ .meta)
    ├── maps/start.dscene      # a scene file (serialization.md §5)
    └── logic/main.drules      # a logic asset (serialization.md §4)
```

`project.sparxel.json` (`plan/03` §4) holds the settings:

| Entry | Meaning | Default and range |
|---|---|---|
| `format`, `version` | `"sparxel.project"`, version 1 | Required. A newer version asks the user to update Sparxel |
| `name` | The game's name | Required |
| `start_scene` | The scene to start in, a path inside `assets/` | Required |
| `ppu` | Pixels per world unit (one tile) | 16; 8 to 64 (`plan/04` §2) |
| `width`, `height` | The internal resolution, upscaled pixel-perfect | 320 × 180; 16 to 7680 × 4320 |
| `background` | The clear colour, `[red, green, blue]` | `[20, 24, 40]` |
| `render_style` | `"classic_2d"`, flat 2D, or `"diorama"`, the same world in 3D ([scene2d.md](scene2d.md) §7) | `"classic_2d"` |
| `gravity` | For Physics Bodies, `[across, down]` in tiles per second squared ([physics.md](physics.md)) | `[0, 0]`, each -1000 to 1000 |
| `camera_pitch`, `camera_distance`, `camera_fov` | The Diorama camera, which follows the hero: degrees looking down, tiles away, and vertical field of view | 35 (5 to 85), 12 (1 to 200), 30 (5 to 90) |

- Problems are reported in plain language with where they are, for example
  `project.sparxel.json/ppu`: *"This must be a whole number from 8 to 64."* A value out of
  range refuses the project. An unknown entry is reported and ignored.
- `project_to_json()` writes the settings back; reading them again gives the same values
  (tested).

## 3. Running a project

```cpp
std::vector<serialize::Issue> issues;
auto game = game::Runtime::open("MyGame", {}, issues);   // null if it cannot run
while (game && game->frame(seconds_since_last_frame)) {}
```

`Runtime::open()`:

1. Reads the settings, registers the engine's types, and creates the world.
2. Scans `assets/` (writing `.meta` files for new assets) and loads the start scene. A
   missing or unreadable start scene stops the game.
3. Compiles every `.drules` and `.dsequence` asset against the world's entities and loads
   it into the VM. An asset that does not compile is **left out and reported** with the
   block at fault, for example `assets/logic/bad.drules#block-7`; the rest of the game
   still runs.
4. Opens the GPU, an offscreen image at the internal resolution, and a window at the
   largest whole-number scale that fits 1280 × 720. A missing GPU stops the game only if
   graphics were asked for.
5. Emits `game_start`, so Rules such as *"When the game starts: …"* run once.

`RuntimeOptions`:

| Option | Use |
|---|---|
| `graphics = false` | No GPU at all: for tests, servers and tools. The simulation is identical |
| `headless = true` | Draws into the offscreen image only, with no window: for tests and thumbnails |
| `replay` | Plays an input recording instead of the keyboard and gamepads ([input.md](input.md)) |

### Each frame and each step

`frame(seconds)` asks the fixed-step clock (§7) how many steps are due, runs them, then
draws and presents. It returns false when the player closes the window. Each `step()`:

1. **Input.** The step's controls, from the recording or from the operating system.
2. **Movement.** While a message is open ([ui.md](ui.md) §3), movement and physics see
   no controls held, so the player stands still. Every enabled Top-down Mover walks, stopped by walls and Solid Bodies,
   and Walk Animations update (§4). Physics Bodies then take a step of the simulation, and
   those that start touching send **"touched"** ([physics.md](physics.md)). Then Trigger Areas send **"entered"** and
   **"exited"** for the movers that walked in or out.
3. **Input events.** Each action pressed this step emits `input.pressed` with the action,
   so *"When Interact is pressed"* Rules run. For Interact, each enabled mover (in GUID
   order) then reaches the Interactable it faces, which sends **"interacted"** (§4).
   While a message is open, Interact instead turns its page or closes it, and does
   neither.
4. **Logic update.** Events that scripts emitted since the last update are delivered,
   every Brain runs, then Sequences whose wait is over resume.
5. **Animation.** Sprite animations advance ([scene2d.md](scene2d.md) §3). Each one that
   reached its end sends its entity's **"finished"** event, so *"When the torch's
   animation finishes"* Rules run.
6. **Saving and loading** that logic asked for during the step (§8).

This follows the order in `plan/05` §5 so far; "continuously" Rules take their place in
it when they arrive.

### Drawing

`draw()` renders the world with `scene2d::draw_world()`, or in the Diorama style with
`scene2d::draw_diorama()` and the project's camera settings ([scene2d.md](scene2d.md)).
The camera follows the first entity with a Top-down Mover, so the hero stays centred. An
open message box is drawn over the world ([ui.md](ui.md) §3). The window shows the image
pixel-perfect with black bars around it ([rendering.md](rendering.md) §4).

## 4. Behaviours

Behaviours are components that give an entity a capability with no logic to write
(`plan/05` §3.1). They live in `behaviours.h`, with their systems as plain functions
(`move_characters()`, `update_triggers()`, `find_interaction()`) that the runtime calls
each step and tests call directly.

Every behaviour's area is a box **centred on the bottom of its position** (`feet_box()`),
so the feet of a character, the base of a villager and the floor of a doorway line up
with their position, which is also what y-sorting uses.

### Top-down Mover

**Top-down Mover** (`top_down_mover`):

| Property | Meaning |
|---|---|
| `speed` | Tiles per second, 0 to 64 (default 4) |
| `enabled` | Turn off to stop the player moving, for example in a cutscene |
| `hitbox_width`, `hitbox_height` (advanced) | The feet, in tiles: 0.75 wide and 0.5 deep by default, centred on the bottom of the position |
| `facing` | Down, left, right or up. Set by walking; logic can set it too, to turn a character |
| `moving` (read-only) | Whether a move action was held in the last step |

It walks with the `move_up`, `move_down`, `move_left` and `move_right` actions, so the
arrow keys, WASD and the gamepad's d-pad all work. **Diagonals are as fast as straight
lines** (the direction is normalised; tested).

**Walls.** Solid tilemaps ([scene2d.md](scene2d.md) §4) stop the hitbox exactly at their
edge. Movement is resolved along x, then along y, so walking diagonally into a wall
slides along it at the diagonal speed (tested).

**Facing.** Walking straight faces that way. Walking diagonally keeps the current facing
if it is one of the two directions (so walking up-right while facing up still faces up),
and otherwise faces left or right.

### Walk Animation

The **Walk Animation** component (`walk_animation`) shows a character sheet: one row per
facing, in the order down, left, right, up from the top, each with the frames of a walk
cycle. Set the Sprite's frame size to one cell of the sheet.

| Property | Meaning |
|---|---|
| `frames_per_direction` | Frames in each row (default 3) |
| `frames_per_second` | Speed of the cycle while walking (default 6) |
| `standing_frame` | The frame shown when standing still, counted from 0 (default 1, the middle) |
| `ping_pong` (advanced) | Back and forth, 0-1-2-1, rather than starting over, 0-1-2-0 (default on) |

While walking, the row's frames cycle in fixed steps; on stopping, the standing frame of
the row shows and the cycle starts again next time. Do not combine it with an Animation
on the same entity: the Animation runs later in the step and would win.

### Solid Body, Trigger Area and Interactable

| Behaviour | Properties | What it does |
|---|---|---|
| **Solid Body** (`body`) | `width` 0.75, `height` 0.5 tiles; `enabled` | Blocks movers like a wall that can stand anywhere: villagers, crates, signs. Movers stop exactly at its edge and slide along it. A mover's own body never blocks it. Bodies block where they stand at the start of the step |
| **Trigger Area** (`trigger`) | `width` 1, `height` 1 tiles; `enabled` | Sends **"entered"** when a mover's hitbox starts overlapping it and **"exited"** when it stops, once each, with the mover's name as `who` (its GUID if the name is shared). For doorways, cutscene zones and traps. Disabling it makes everyone inside exit |
| **Interactable** (`interactable`) | `width` 1, `height` 1 tiles; `enabled` | Sends **"interacted"**, with `who`, when a mover presses Interact facing it: the mover's hitbox moved half a tile (`interact_reach`) the way it faces must overlap it. The nearest one wins; ties go to the lower GUID |

Events are sent in a fixed order (exits before entries, then by trigger and mover GUID),
so a replay always runs Rules in the same order.

## 5. Logic meets the world

A Rule such as *"When the chest is opened: unlock it, add 10 gold"* names an object, the
chest, and members that live on different components: `unlock` on a Lock, `gold` on a
Purse. `logic_world.h` lets the logic runtime ([logic-runtime.md](logic-runtime.md)) find
that object in the world ([world.md](world.md)), and lets the compiler check the Rule
against it. The runtime does this for every project; on their own:

```cpp
world::World w(registry);
logic::Vm vm(registry);
game::attach(vm, w);   // scripts now reach w's entities

logic::CompiledLogic compiled = logic::compile(asset, registry, game::object_types(w));
vm.load(asset.name, compiled.source);
vm.emit("chest", "opened");
```

### How names resolve

| Name used in logic | Resolves to |
|---|---|
| 32 hexadecimal digits that are an entity's GUID | That entity |
| An entity's exact name, shared by no other entity | That entity |
| A name shared by several entities, or no entity at all | Nothing: the compiler reports *"There is no object called 'barrel'."*, and so does the VM at run time |

GUIDs never change, so the editor will write them into logic assets. Names are for
people, and for logic written by hand while they are unique. The runtime also binds the
`input` object, for conditions such as *"Interact is held"*.

The VM matches an event by the exact name a Rule was written with. So when the engine
sends an entity's own event, such as an animation's "finished", `game::emit()` sends it
under each name that reaches the entity (`names_of()`): its GUID, then its name if no
other entity shares it. A Rule runs once, whichever name it uses (tested).

## 6. Objects made of several parts

- `Vm::set_object_resolver()` lets the VM ask for an object's parts by name whenever a
  script uses one. For an entity, the parts are its components. `sx.call`, `sx.get`,
  `sx.set` and `Vm::emit()` use the first part that has the member asked for.
- **No addresses are kept.** The resolver runs at every access. Adding a component, or
  creating entities, can move a component's data (flecs stores components in tables);
  logic keeps working because it never holds the old address (tested).
- The compiler's `ObjectTypes` gives all of an object's types, and every lookup searches
  across them, so Rules are checked against everything the entity really has.
- Objects bound with `Vm::bind()` still work and take precedence over the resolver.

## 7. The fixed-step clock

`FixedStepClock` (`clock.h`) turns real frame times into a whole number of simulation
steps (1/60 s by default), plus `alpha`, how far the next step has come, for interpolated
drawing (`plan/03` §7).

- Exact frame times give exact steps: 600 frames of 1/60 s are 600 steps (tested), despite
  rounding in the running sum.
- After a long stall, at most `max_steps` (8) run in one frame and the rest of the backlog
  is dropped (reported in `dropped`), so the game recovers instead of spiralling.
- The runtime numbers its steps itself (`Runtime::step_count()`), which is the step
  number input recordings use.

## 8. Saved games

A save (`Runtime::save()`, format `"sparxel.save"` version 1, `plan/03` §10) holds:

| Entry | What |
|---|---|
| `game` | The project's name: another game's saves are refused |
| `step` | The step count, so recorded input and timers continue from the same step |
| `world` | Every entity and component, as a scene with GUIDs ([serialization.md](serialization.md) §5), including hidden state such as an animation's time |
| `variables` | Every logic asset's variables, by asset and name (`Vm::variables()`) |

**Loading** (`Runtime::load()`) checks the whole save first, loading its world into a
scratch world. A save from another game or a newer version, or a broken world, is
refused and **changes nothing** (tested). Otherwise the world is replaced, waiting
Sequences stop (`Vm::stop_all()`; a save does not hold them, so save where none is
running, as RPGs do at save points), open messages close, and the variables are restored. A variable that no
longer exists, or now holds another kind of value, keeps its current value and is
reported; the rest still loads. Movers standing in Trigger Areas count as already
inside, so loading does not set off "entered". Saving straight after loading gives the
same save (tested).

**Slots.** The **Game** object (`game`) gives logic *"Save the game in slot [n]"*, *"Load
the game from slot [n]"* and the condition *"Slot [n] has a save"*, for slots 1 to 99.
Saving and loading happen at the end of the step (step 6), after every script has run,
so no Rule ever sees a half-loaded world. Problems are collected in `problems()`.

- Slot files (`slot-07.sparxel-save`) are JSON, written **atomically**: to a temporary
  file that then replaces the slot in one step, so a crash or power cut while saving
  leaves the previous save intact.
- They go in `RuntimeOptions::save_folder`, or by default the player's own folder for the
  game that the operating system suggests (`SDL_GetPrefPath`; on Windows under
  `AppData\Roaming\Sparxel`), found only when a save is first used.

## 9. Tests

| Test | Checks |
|---|---|
| Project settings | A missing file; a round trip; out-of-range values, a wrong style and an unknown entry, each reported at its path; another format or a newer version refused |
| Recorded input | A temporary project on disk. Right held for 30 steps, in frames of one and two steps, walks the hero exactly 2 tiles; the game-start Rule logs once; Cancel does not trigger the Interact Rule, Interact does; the torch's one-shot animation runs its "finished" Rule once, at 0.3 s |
| Diagonals | Right and down for one second move 4/√2 tiles on each axis; a disabled mover stays put |
| Walls | Walking right stops with the hitbox on the wall (x = 2.625 for a wall at 3); adding down slides along it at the diagonal speed |
| Bodies, triggers, interaction | Boxes touching versus overlapping; sweeping against boxes in every direction; a villager stopping the hero at its edge, and not when disabled; a doorway reporting a mover already inside, a mover walking in and out once each, exits in GUID order, and everyone leaving when disabled; the reach in each facing, the nearest of two, disabled ones and the mover itself skipped |
| Talking and doorways | In the runtime: Interact facing the elder runs its "interacted" Rule after the Interact input Rule, and the next Interact closes what the elder said; walking left runs the doorway's "entered" Rule with `who` = "hero" at exactly 4.2 tiles, and "exited" after leaving it |
| Facing and walking | Every facing rule; exact frames of the cycle, back and forth and starting over, at 6 frames per second; the standing frame; clamped settings; the runtime turning the hero left and animating it |
| Problems | A Rules asset naming a missing object is reported at its block and left out, and the game still runs; a missing start scene stops it |
| Drawing (GPU) | Exact pixels: the hero centred, and after walking one tile the rock drawn 8 pixels to the left |
| Diorama (GPU) | The same project in the Diorama style: the hero drawn as a billboard above the centre, lit as `brightness()` says; the settings round-trip; an unknown style and an out-of-range camera refused |
| Logic and world | Names and GUIDs resolve; an entity's event reaches Rules written with its name and with its GUID, once each; Rules act through several components; logic survives storage moves |
| Clock | Exact stepping, stalls and the step cap |
| Save files | Slot file names and limits; atomic writes creating folders and replacing files, leaving no temporary file; a failed write reported |
| Saved games | A save restores a moved hero, a destroyed rock, a variable and the step count; an exact round trip; logic acting on the loaded world and its restored variable; other formats, newer versions, other games and broken worlds refused with nothing changed; a vanished variable reported while the rest loads |
| Slots from logic | Cancel saves in slot 2 and Menu loads it at the end of their steps, putting the hero back; an empty slot and slot 0 reported |

## 10. Next increments

1. **Choices in dialogue** and the rest of [ui.md](ui.md) §5.
2. **Hot reload while playing:** pass asset changes to the texture cache and reload
   changed logic.
3. **Spawning and destroying from logic,** with entities created by Rules addressable by
   GUID.
4. **"Who" picking** (`plan/05` §2): *"all objects tagged Enemy"*, *"the other object"* in
   a touch event.
5. **Play in the editor** (`plan/03` §8), starting the runtime that `sparxel-player`
   already runs ([player.md](player.md)).
