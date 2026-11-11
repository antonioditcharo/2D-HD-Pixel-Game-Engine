# The editor

> **Status: v0.1.** `sparxel-editor` opens a project folder, draws its scene as the game
> draws it, lists its entities and assets, and edits every component through an inspector
> generated from the reflection registry. It has undo and redo, saving, Play and Export.
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
| **Scene** (middle) | The scene drawn by the engine, as the game draws it: the project's resolution, style, lighting and look, at the largest whole-number scale that fits. In Classic 2D, **click** selects what is drawn on top (sprites before maps) and **drag** moves it, to whole pixels, as one undo step. Drag with the right or middle button, or use the arrow keys, to move the camera |
| **Entities** (left) | Every entity, children under their parents. Selecting one selects it everywhere |
| **Assets** (left) | The project's `assets/` folder: pictures, sounds, scenes and logic |
| **Tiles** (left) | With a tilemap selected, its tileset's tiles and an eraser. Choosing one puts the scene in **paint mode** (Classic 2D): the left button paints the tile into the cell under the cursor, the right button erases, the middle button still moves the camera. Each stroke is one undo step; painting past the map's right or bottom edge grows it. Choosing the tile again, Escape, or selecting something else stops painting |
| **Inspector** (right) | The selected entity's name, and a group for each component with a field for each property: a tick box for yes/no, number fields with the property's range, step and unit, a list for choices, and a list of the project's pictures for an image or tileset. Read-only properties are shown but cannot be changed; *Show advanced settings* adds the advanced ones. Each group can be removed, and *Add a component…* lists those the entity does not have |
| **Logic** (right, beside the inspector) | Every logic file of the project, its Rules, Sequences and Brains as plain language: *When [who] interacts with it (elder)*, then its steps (*Say “…” (dialogue)*, *Wait until not a message is open (dialogue)*, *If the answer chosen (dialogue) = 1:*), nested as they run. Read-only for now |
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
| Logic as language | The sample's logic file found and read in full, line for line (its three Rules with their steps, the If and Otherwise, and the weather Brain's states, steps and timed transitions); every line traced to a block; conditions, all-of, not, waits, repeats, runs, Otherwise, event values and an unknown object read sensibly |
| Saving | The scene file is written; reopening finds the edit; saving keeps the history |
| Picking | The hero's exact picture bounds; the hero, the elder, the ground and the walls (drawn after the ground) at points, and nothing outside the map |
| Preview (GPU) | The sample drawn at 320 × 180 with the hero's robe and the wall's stone at exact pixels, and the hero moved 2 tiles after an edit |
| The application | Opening the sample: title, list and a 320 × 180 scene. Selecting in the list fills the inspector; a number field edits the world; undo puts it and the field back; renaming in the inspector renames in the list; field labels are never squeezed out of sight. Clicking the hero in the scene selects it; dragging moves it 2 tiles right and 1 down to the nearest pixel, as one undo step; clicking the ground selects it. Painting: the ground's palette has its three tiles and an eraser; a left-button stroke paints a path four tiles long without changing the selection; a right-button click erases; each undoes on its own; Escape and selecting something else stop painting. A new entity gets a Light from *Add a component…*; saving writes it; deleting and exporting (which saves first) work; exporting over a project is refused |

## 5. Next increments

1. **Editing logic** in the Logic view: blocks picked from the reflected lists,
   values typed in place, and steps dragged, all compiled and checked as you go.
2. **Placing assets** by dragging them from the assets list into the scene, and
   rectangle and fill tools for painting.
3. **Selection and gizmos in the Diorama style,** picking through the 3D camera.
4. **Docking layouts** with Qt Advanced Docking System (ADR-005), saved per user.
5. **Project creation** from templates, and the project settings in a dialog.
6. **Shipping:** `windeployqt` for a self-contained editor on Windows, and Qt's LGPL
   notices beside it.
