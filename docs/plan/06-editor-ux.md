# Plan 06: Editor UX and Workflows

> How the editor looks and feels, and how each workflow runs inside it. This document
> covers usability *for non-coders*, which is the user's core requirement: "easy as hell".

---

## 1. UX principles

1. **Plain language.** No engine jargon in the default UI (see the glossary in §9).
2. **Show, don't configure.** Every setting previews live. Hovering over a preset previews
   it in the viewport before the user applies it.
3. **Progressive disclosure.** Every panel has **Basic** and **Advanced** views. Basic covers
   the common cases.
4. **Direct manipulation.** Drag a sprite into the world and it becomes an object. Drag a
   sound onto an object and it plays on interaction. Drag from port to port to link.
5. **Everything is undoable,** with a visual history list.
6. **Never lose work.** Autosave, crash recovery and snapshots are on by default.
7. **One search for everything.** A *Quick Add* palette (`Ctrl+Space`) and *Search*
   (`Ctrl+K`) find assets, behaviors, blocks, settings, docs and commands.
8. **Teach in context.** Empty states explain what to do. Every field has a tooltip and a
   "learn more" link.
9. **Consistency.** Same inspector, same pickers and same block vocabulary in every
   workspace.
10. **Fast.** The editor must feel instant (§7 targets).

## 2. Main window layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ ☰ MyGame ▾ │ Home  World  Sprite Studio  Characters  Logic  Story  Data       │
│            │ Audio  UI  Look  Playtest  Ship        ▶ Play ▾   ⟲ ⟳   🔍 Ctrl+K │
├───────────────┬──────────────────────────────────────────────┬───────────────┤
│ Assets /      │                                              │ Inspector     │
│ Outline       │          Viewport (engine-rendered)          │ (Basic ▸      │
│ (tree, tags,  │                                              │  Advanced)    │
│  search)      │                                              │               │
│               │                                              │ Behaviors     │
│               │                                              │ + Add         │
├───────────────┴──────────────────────────────────────────────┴───────────────┤
│ Context panel: palette / timeline / rules / console (per workspace)          │
├──────────────────────────────────────────────────────────────────────────────┤
│ ✓ Saved 3 s ago · Snapshot "Before boss fight" · Doctor: 2 warnings · 60 fps │
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Workspaces** are task-focused layouts. Panels can be docked and rearranged, using the
  Qt Advanced Docking System (ADR-005), and each workspace has *Reset layout*.
- **Play** is always visible. Its dropdown offers *From here*, *From start*, *From save
  slot…* and *Standalone test build*.
- The **status bar** shows save status, the latest snapshot, Project Doctor results and
  performance.

## 3. Workspaces

### 3.1 Home
- Project hub, templates gallery and tutorials (interactive "quests").
- **Style Guide**: PPU, palette, fonts and outline rules (A11).
- Offline docs and example projects.

### 3.2 World: building the diorama
**Diorama Builder tools**
- **Block brush:** click-drag to extrude blocks on the grid. `Shift` paints, `Alt` erases.
  A height slider sets the level.
- **Face painter:** pick a tile from the tileset palette and paint it onto faces. Fill
  paints the whole face. Rotate and flip the tile in place (Crocotile-style; see
  `research/03` §2.3).
- **Shape brushes:** slope, stairs, arch, cylinder, cone.
- **Prefab kits:** drag in a house or tree from the kit palette. Prefabs snap to the grid
  and the ground.
- **Scatter brush:** paint foliage and rocks with random variants and density.
- **Auto-rules:** paint "grass" or "path", and edges and transitions resolve automatically.

**Placement and logic**
- **Objects:** drag characters or objects from Assets. A properties popover appears on drop.
- **Regions:** draw boxes or polygons for triggers, encounters, audio, camera and Look
  zones.
- **Links mode** (`L`): shows event ports and links, and lets the user drag new ones.

**Preview**
- A **time-of-day scrubber** and the active Look preview live.
- **Camera preview** shows exactly what the player sees, with rig limits drawn as a cone.
- **2D map mode** for Classic 2D and side-view: tile layers, parallax layers, auto-tiling.

### 3.3 Sprite Studio: pixel art and animation
**Drawing**
- Canvas with pixel grid and tile mode.
- Tools: pencil, line, shapes, fill, selection, shading ink, dither and symmetry.
- **Palette panel** with ramps and swap variants.
- **Layers panel** with reference layers.

**Animation**
- **Timeline:** frames, durations, tags (clips), onion skin, frame events, and hitbox mode.

**Lighting**
- **Lighting maps:** a normal-map generator with presets, plus a **normal brush**: pick a
  light direction from a sphere widget and paint it.
- **Preview** under the current scene's lights. A directional orbit shows the sprite on a
  turntable at the project camera angle.

**Import**
- Drop an `.aseprite` or PNG file. Tags become clips and slices become 9-slice and pivots.
  With live link on, saving in Aseprite updates the game instantly.

### 3.4 Characters
One place to assemble a character:
- sprite set and directions;
- collision shape (auto-generated);
- behaviors (Mover, Talkable, Health…);
- stats (from Tables);
- a Brain, which is optional and can start from a template.

A **test pen** viewport lets the user drive the character immediately with the keyboard or
a gamepad.

### 3.5 Logic
- An overview of all logic in the project: Links, Rules, Sequences, Brains and Custom
  Blocks.
- The **Logic map** (`05` §6).
- Editors open in context. Double-clicking a rule badge on an object in World opens its
  rules with the object pre-selected.

### 3.6 Story
- Dialogue graph and screenplay mode (1.0).
- Quests, story outline (1.0) and speaker setup.
- **Localization tables:** progress per language, plus a pseudo-localization preview.

### 3.7 Data
- Tables in grid or form view, formula columns, curves and balancing charts.
- CSV import and export.

### 3.8 Audio
- Library with waveform previews, and the SFX generator.
- **Music setup:** loop points, layers and transitions.
- Mixer buses and snapshots, and zone painting in the World view.

### 3.9 UI
- Designer canvas with anchors and layouts, plus templates (title, pause, inventory, shop,
  battle HUD…).
- **Multi-resolution preview** and a gamepad navigation preview.

### 3.10 Look
- **Look presets gallery:** hover to preview, click to apply.
- **Sliders grouped by intent:** *Depth & Focus*, *Glow*, *Atmosphere*, *Colour*, *Lens*.
- A before/after split view, and the time-of-day timeline.
- **Guardrails:** saturation and contrast histogram, plus texel-density warnings
  (`research/01` §4).

### 3.11 Playtest
- Runs the game inside the editor.
- Debug panels: watches, breakpoints, event log, "why didn't this fire?", Brain live view,
  and a cheat panel.
- Performance overlay, input record and replay, and time controls.

### 3.12 Ship
- **Build profiles:** dev, demo and release.
- **Branding:** icon, name, version and splash.
- **Steam forms:** achievements, stats, leaderboards and cloud.
- Validation report, then **Build** (one click) and an optional upload helper.
- Store asset helper: screenshot mode and capsule templates.

## 4. Onboarding and learning

1. **First launch:** choose a template, such as "Village RPG" or "Forest Platformer". It
   opens directly into a playable, good-looking scene. Press **Play** within the first
   minute.
2. **Guided quest (10–15 minutes):**
   - paint a path;
   - place a house prefab;
   - add an NPC with *Talkable* and write two lines;
   - link a lever to a gate;
   - change the Look to *Moonlit*;
   - press Play.
   Each step validates the user's action and celebrates completion.
3. **Interactive tutorial quests** for every workspace, unlocked progressively and
   available offline.
4. **Example projects** in which every piece of logic carries a comment explaining it.
5. **Empty states that teach.** For example, an empty Rules panel shows: *"Rules react to
   things that happen. Try: When Player touches this → Give item."* with a one-click
   example.

## 5. Safety, versioning and collaboration

| Feature | Behavior |
|---|---|
| Autosave | Every 30–60 s (configurable) and on focus loss. Uses a crash-safe journal of Commands |
| Crash recovery | On restart: "Sparxel closed unexpectedly. Restore your work from 12 seconds before?" |
| Snapshots | Named, thumbnailed restore points (*"Before boss fight"*). Created automatically before risky operations such as bulk delete or format migration |
| History browser | A visual timeline of snapshots. Preview any one, then restore all of it or just one asset |
| Git (optional, 1.0) | Friendly UI. "Save to history" means commit; "Share" means push. Uses libgit2 (ADR-012) |
| Team mode (1.0) | Asset locking for binary assets. Merge-friendly JSON for everything else. Visual conflict resolution for scenes, tables and logic |

## 6. Editor accessibility and internationalization
- **Keyboard:** every action is reachable by keyboard, with a visible focus ring and a
  shortcut cheat sheet.
- **Screen readers:** Qt exposes UI semantics to platform assistive tools [S75]. Custom
  widgets such as the logic editors and timelines must implement accessibility interfaces.
  This is an explicit work item (R2).
- **Visual:** UI scaling from 100% to 200%, a high-contrast theme, and colour-blind-safe
  type colours, always paired with icons and shapes.
- **Motion:** a reduced-motion setting for editor animations.
- **Languages:** the editor UI ships in English at α, with translations at 1.0 (R4). All
  strings are externalized from day one.

## 7. Editor performance targets (initial)

| Operation | Target |
|---|---|
| Cold start to project hub | ≤ 5 s |
| Open a typical project (≈ 2,000 assets) | ≤ 10 s, with cached derived data |
| Press Play → controllable | ≤ 2 s |
| Edit → visible in running game (art, data, logic) | < 1 s |
| Viewport frame rate while editing a template scene | ≥ 60 fps on reference hardware |
| Undo / redo | < 100 ms |

## 8. AI assistant policy (Q5, after Gate 5)

AI help is now expected in no-code tools; GDevelop ships an AI agent [S33]. The owner
decided that work on the assistant starts after the Early Access / beta release, which is
Gate 5 (`09` §5, Q-06). Trust matters more than novelty, so the assistant follows strict
rules:

1. **Off by default,** with explicit opt-in. It never runs on project content without
   consent.
2. **Edits only through the Command system.** Every change is undoable and grouped as one
   step.
3. **Shows a reviewable diff** of rules, sequences, tables or objects *before* applying.
   It never edits silently.
4. **Produces normal, editable no-code logic,** never hidden code. The user can always read
   and change what it made (`research/04`, L9).
5. **Explains:** it can answer "why didn't this fire?" in natural language, using the
   debugger's data.
6. **No bundled generative art.** This is a recommendation, not a sourced fact. The
   assistant does not generate sprites or tiles. The reasons:
   - The product's promise is great tools *for* artists.
   - Licensing and provenance of generated art are unsettled.
   - Pixel-art communities are a core audience whose trust matters.

   This can be revisited later with a clear licensing position.
7. **Provider-agnostic** behind an interface. The engine is open source and free, so
   users bring their own AI provider account or a local model. The engine does not resell
   AI usage.

## 9. Glossary: plain language in the UI

| Engine term (hidden) | What the UI says |
|---|---|
| Entity / GameObject | **Object** |
| Component | **Behavior** (logic) or **Property** (data) |
| Prefab | **Prefab** (with the tooltip "a reusable object") |
| Event handler / event sheet | **Rule** / **Rules** |
| Signal binding | **Link** |
| Coroutine / script | **Sequence** |
| State machine | **Brain** |
| Shader / material graph | **Material** |
| Post-processing volume | **Look** |
| Boolean / int / float / string / array | **Yes/No · Whole number · Number · Text · List** |
| Serialization / persistence | **Saved with the game** |
| Build / compile / cook | **Build game** |
| Commit / push | **Save to history / Share** |
