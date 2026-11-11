# The editor

> **Status: v0.1.** `sparxel-editor` opens a project folder, draws its scene as the game
> draws it, lists its entities and assets, and edits every component through an inspector
> generated from the reflection registry, paints maps, places pictures, and edits logic as
> plain language. It has undo and redo, saving, Play and Export.
> It is built on Qt 6 Widgets (ADR-005), and tested on Qt's offscreen platform in CI on
> Windows and Linux.
>
> Code: [`editor/core/`](../../editor/core/) (the model, without Qt),
> [`editor/app/`](../../editor/app/) (the Qt application) and
> [`editor/exporter/`](../../editor/exporter/) ([export.md](export.md)). Tests:
> [`tests/editor/`](../../tests/editor/) and [`tests/editor_app/`](../../tests/editor_app/).

## 1. What it is for

The editor is where a creator makes a game without writing code (`plan/06`). This first
version covers the loop every later tool plugs into: open a project, see the scene, select
something, change it, paint its maps, undo, save, play, export.

![The editor with the sample village open and the elder selected](editor-v0.1.png)

## 2. Using it

```sh
sparxel-editor samples/hello
```

| Part | What it does |
|---|---|
| **Scene** (middle) | The scene drawn by the engine, as the game draws it: the project's resolution, style, lighting and look, at the largest whole-number scale that fits. **Click** selects what is drawn on top (sprites before maps) and **drag** moves it, to whole pixels, as one undo step: in Classic 2D on the flat map; in the Diorama style through the 3D camera, picking the nearest billboard and dragging along the ground, so the ground under the cursor stays under it. Drag with the right or middle button, or use the arrow keys, to move the camera |
| **Entities** (left) | Every entity, children under their parents. Selecting one selects it everywhere |
| **Assets** (left) | The project's `assets/` folder: pictures, sounds, scenes and logic. **Drag a picture into the scene** to place it where it is dropped (in either style): a new entity named after the file, with a Position at the nearest pixel and a Sprite showing the picture, selected, as one undo step. Other files, and files from outside `assets/`, are refused with a message |
| **Tiles** (left) | With a tilemap selected, its tileset's tiles and an eraser. Choosing one puts the scene in **paint mode** (in either style; in 3D the cell is where the cursor meets the ground): the left button paints the tile into the cell under the cursor, the right button erases, the middle button still moves the camera. Each stroke is one undo step; painting past the map's right or bottom edge grows it. Choosing the tile again, Escape, or selecting something else stops painting |
| **Inspector** (right) | The selected entity's name, and a group for each component with a field for each property: a tick box for yes/no, number fields with the property's range, step and unit, a list for choices, and a list of the project's pictures for an image or tileset. Read-only properties are shown but cannot be changed; *Show advanced settings* adds the advanced ones. Each group can be removed, and *Add a component…* lists those the entity does not have |
| **Logic** (right, beside the inspector) | Every logic file of the project, its Rules, Sequences and Brains as plain language: *When [who] interacts with it (elder)*, then its steps (*Say “…” (dialogue)*, *Wait until not a message is open (dialogue)*, *If the answer chosen (dialogue) = 1:*), nested as they run. **Editing:** selecting a step shows the values written into it (the words said, the seconds waited, a property's new value) as fields of their kind, with their ranges and choices, and typing changes the line as you type. **Add** puts in, from lists built from the registry and the scene: a **step** (Log, Wait, Run a Sequence, or any action of the game's objects and the scene's entities, by object) after the selected step or first under a selected Rule, Sequence or state; a **condition** on the selected Rule or "Go to"; a **Rule** on any event (the game starting, or an object's); a **Sequence**, a **Brain** (with a first state, *idle*), a **state**, a **"Go to"** out of the selected state, or a **new logic file**. Names, a "Go to"'s target and time, and the Sequence a Run step runs are values too; renaming a state or a Sequence renames what refers to it. **Delete** removes any block; **Up** and **Down** move steps. Below, the compiler says whether the file runs, or its first problem. Logic edits are steps of the same Undo as the scene's, and Save writes the files that changed |
| **Commands** | Open, Save (Ctrl+S), Undo and Redo (with what they will undo, for example *Undo Change Speed of hero*), New entity (where the camera looks), Delete, **Play** (F5: saves, then starts `sparxel-player` on the project) and **Export** (saves, asks for an empty folder, then exports: [export.md](export.md)) |

The title shows a star while there are unsaved changes, and closing asks whether to save
them.

## 3. How it works

- **`editor::Document`** (in `editor/core`, no Qt) holds the open project: its settings,
  asset database, a registry of every type the game uses (`game::register_types`), and a
  world with the start scene loaded. Every edit goes through it.
- **Undo by snapshots.** Each edit records the scene before and after, as the JSON the
  scene file holds; undo loads the earlier one. This is correct for every kind of edit
  without one command class per kind. Entities are found again by GUID, which survives a
  reload, so selection survives undo. Consecutive changes to one property of one entity
  (typing, spinning a number, dragging) **merge** into a single step until the field
  loses focus or the drag ends. Scenes are small enough that copying them per edit costs
  nothing noticeable; large scenes will need commands that record only what changed.
- **The inspector knows no component.** It reads each component's `TypeInfo`: display
  names, tooltips, kinds, ranges, steps, units, choices and flags. A component registered
  tomorrow gets its fields with no editor change.
- **`editor::Preview`** draws a Document on a headless GPU device and returns pixels:
  the same `draw_world` / `draw_diorama` and `PostProcessor` the game uses, so what the
  editor shows is what the game shows. The scene view puts those pixels on screen.
- **Logic as language** (`editor::describe`): each Rule, Sequence and Brain becomes
  lines built from the blocks' own reflected texts (`block_text`): an action's text with
  its values filled in, a property's display name, events with their values in brackets,
  and operators as symbols. Members are found on the game's objects by name, and on
  entities through their components, so a new component's blocks read well with no
  editor change. Every line keeps its block id, for highlighting and explaining later.
- **Editing logic** (`Document::logic_slots()`, `set_logic_slot()`, `add_logic_step()`,
  `remove_logic_step()`, `move_logic_step()`): the Document holds the logic files, and
  its undo snapshots hold them beside the scene, so one Undo history covers both and
  Save writes each file that changed. A value's kind, range and choices come from the
  block's reflected parameter or property, and a value that does not fit is refused in
  plain language; typing into one value merges into one step. A new step's values start
  at the parameters' defaults, and it gets a block id no block of the file uses.
  `check_logic()` runs the real compiler against the registry and the scene (the game's
  objects by name, entities through their components, as the game resolves them), so
  renaming an entity a Rule names shows at once.
- **Picking in 3D** (`editor::pick_diorama`) projects each billboard's corners through the
  game's camera (`diorama_camera()`, which the preview uses too, so they always agree) and
  takes the nearest that covers the point; else the map where the ray from the eye
  (`scene2d::diorama_ray()`) meets the ground (`scene2d::ground_hit()`).
- **Picking** (`editor::pick`) finds the entity drawn on top at a map point in Classic 2D,
  in the draw order the game uses (layer, then y, then GUID): a sprite whose picture
  covers the point, else a tilemap with a tile there.
- **Qt stays in the editor** (ADR-005). The engine and exported games never link it. Qt's
  `emit`, `signals` and `slots` keywords are turned off (`QT_NO_KEYWORDS`), because they
  clash with the engine's own names; the editor uses `Q_EMIT` and `Q_SIGNALS`.
- **Building.** With Qt 6.4 or later found by CMake, the editor is built; without it,
  everything else still builds and a message says the editor was skipped. CI sets
  `SPARXEL_REQUIRE_EDITOR=ON`, so a missing Qt fails the build instead. Linux CI uses
  Ubuntu's Qt 6.4; Windows CI installs Qt 6.8.3 for MSVC with `aqtinstall`
  ([building.md](building.md)).

## 4. Tests

| Test | Checks |
|---|---|
| Opening | The sample's settings, scene and its 7 entities in order; component types sorted by name, behaviours included; a missing project refused |
| Properties | An edit changes the world and marks the scene changed, labelled *Change Speed of hero*; typing three values is one undo step back to the original; after a field loses focus the next change is its own step; a new edit clears redo; a value of the wrong kind and a component the entity lacks are refused, with nothing changed |
| Painting | A stroke over three cells is one undo step; painting past the right edge grows the map, with empty cells between; cells above or left of its corner, and entities without a tilemap, are refused; erasing; a new stroke after `end_merge()` is its own step |
| Entities and components | Create, add a component (twice refused), rename, delete and undo (same GUID and components back), remove a component and undo; a drag of five moves is one step; undoing everything returns to the scene as opened |
| Picking in 3D | Through the sample's Diorama camera: the hero's billboard rectangle lies below the middle of the screen; its middle picks the hero, beside it the ground; the elder; the wall along the bottom edge; nothing in the sky |
| Logic as language | The sample's logic file found and read in full, line for line (its three Rules with their steps, the If and Otherwise, and the weather Brain's states, steps and timed transitions); every line traced to a block; conditions, all-of, not, waits, repeats, runs, Otherwise, event values and an unknown object read sensibly |
| Placing pictures | `place_picture()`: the hero's picture at (3.03, 4.94) becomes an entity "hero" at (3, 79/16) with a Sprite showing it, labelled *Place hero*, undone in one step; a sound, a missing picture and a point that is not a number are refused with messages, recording nothing |
| Editing logic | The elder's greeting: one value, its text, changed in place (two edits typed, one step); a number is refused for it. A Brain state's ambient light: its range from the registry refuses 3, takes 1. The actions offered, sorted by object. A chime added after the greeting with its default value, compiling; moved up; the first step not moved higher; deleted. A step under a Rule's header goes first; under a Brain, refused; an unknown object refused. Renaming the elder makes the compiler report its Rule; undoing everything returns the logic exactly; redo; saving writes the file and reopening reads the edit |
| Authoring logic | A new file (badly named, existing and escaping ones refused), compiling empty; a Rule on the elder's event from `logic_events()` (an unknown event refused); a condition from `logic_conditions()` (an action refused); steps, including Run and Wait; two Sequences, a Brain with a second state and a "Go to", reading line for line as expected and compiling; renaming the state moves the "Go to"; its time changed; a missing target, a taken name and a bad name refused; Run's Sequence checked, and renaming the Sequence renames the Run; deleting a condition, a "Go to", a state, the Brain, a Sequence and the Rule; undoing everything removes the file, redo brings it back; saving writes it with its `.meta`, and reopening reads it |
| Saving | The scene file is written; reopening finds the edit; saving keeps the history |
| Picking | The hero's exact picture bounds; the hero, the elder, the ground and the walls (drawn after the ground) at points, and nothing outside the map |
| Preview (GPU) | The sample drawn at 320 × 180 with the hero's robe and the wall's stone at exact pixels, and the hero moved 2 tiles after an edit |
| The application | Opening the sample: title, list and a 320 × 180 scene. Selecting in the list fills the inspector; a number field edits the world; undo puts it and the field back; renaming in the inspector renames in the list; field labels are never squeezed out of sight. Clicking the hero in the scene selects it; dragging moves it 2 tiles right and 1 down to the nearest pixel, as one undo step; clicking the ground selects it. Painting: the ground's palette has its three tiles and an eraser; a left-button stroke paints a path four tiles long without changing the selection; a right-button click erases; each undoes on its own; Escape and selecting something else stop painting. In the Diorama style: clicking the hero's billboard selects it; dragging it by its feet moves it along the ground to within a picture pixel of where the cursor points, as one undo step; painting puts the tile in the cell under the cursor. A new entity gets a Light from *Add a component…*; saving writes it; deleting and exporting (which saves first) work; exporting over a project is refused. Dragging from the assets: the list's own drag data dropped on the scene (Qt's drag-enter and drop events) places the elder's picture at the map point under the drop, selected and listed, as one undo step; a sound and a file outside `assets/` are refused with a message in the status bar. The Logic view: selecting the elder's greeting shows its text in a field and "runs: no problems"; typing changes the line as you type, marks the project changed and is one undo step; Delete is enabled for steps only; Add step's menu adds a chime after the greeting, selected; Up moves it before; Delete removes it; Undo goes back through it all; renaming the elder shows the file does not run. Authoring from the Add menu: a new file, a Rule on the game starting, a Wait step, a Brain, a state renamed by typing, a "Go to" back to *idle*, "runs: no problems", and saving writes the file and its `.meta` |

## 5. Next increments

1. **Logic, further:** values from properties, variables and event values, not only
   typed; If, Repeat and Wait until steps with their own conditions; "not", "and" and
   "or"; variables; steps dragged; and problems shown on their lines.
2. **Rectangle and fill tools** for painting, and placing other kinds of assets by
   dragging (a sound as an ambient source, a scene as a door to it).
3. **Move gizmos** and multiple selection.
4. **Docking layouts** with Qt Advanced Docking System (ADR-005), saved per user.
5. **Project creation** from templates, and the project settings in a dialog.
6. **Shipping:** `windeployqt` for a self-contained editor on Windows, and Qt's LGPL
   notices beside it.
