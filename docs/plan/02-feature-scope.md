# Plan 02: Systems and Features

> The master catalogue of every system and workflow Sparxel provides.
>
> **Release tags:**
> - **α** (Alpha): needed for the closed alpha at the end of Phase 4. This is the solo
>   "Minimum Lovable Engine" scope (`08` §5).
> - **1.0**: needed for the 1.0 release.
> - **Post**: planned after 1.0.
>
> Phases are defined in [`08-roadmap.md`](08-roadmap.md). Requirement IDs such as `R-VIS-xx`
> come from `research/01`.
>
> Section letters **L** and **S** are deliberately skipped, so feature IDs never look like
> no-code lesson IDs (`L1`–`L10` in `research/04`) or source citations (`[S#]`).

---

## A. Project and workflow

| ID | Feature | Tag |
|---|---|---|
| A1 | **Project hub:** create, open and recent projects, with thumbnails | α |
| A2 | **Project wizard:** genre kit, Render Style (Diorama / Retro 3D / Classic 2D), base resolution, PPU, palette, input scheme | α |
| A3 | **Templates:** one playable template per genre kit, built only with no-code tools | α (RPG) / 1.0 (rest) |
| A4 | **Asset browser:** thumbnails, tags, search, favourites, "find usages", safe rename and move (references use GUIDs) | α |
| A5 | **Universal undo/redo** across every workspace, with a visual history | α |
| A6 | **Autosave and crash recovery** | α |
| A7 | **Snapshots:** named restore points with thumbnails, using Git under the hood (ADR-012) | α |
| A8 | **Optional Git integration** with a friendly commit, history and branch UI | 1.0 |
| A9 | **Team features:** asset locking, merge-friendly text formats, a conflict-resolution UI | 1.0 |
| A10 | **Project Doctor:** finds missing assets, broken references, unreachable maps, unused variables and performance hazards | α |
| A11 | **Style Guide:** a project-level PPU, palette, outline rules and font set that every tool enforces | α |
| A12 | **Asset packs:** import and export bundles of sprites, prefabs, behaviors and audio | 1.0 |
| A13 | **Asset marketplace** | Post |
| A14 | **Cloud project sync and real-time collaboration** | Post |

## B. Pixel art and animation (Sprite Studio)

| ID | Feature | Tag |
|---|---|---|
| B1 | **Drawing tools:** pencil, eraser, line, shapes, fill (contiguous and global), selection (rectangle, lasso, magic wand), move and transform, colour picker | α |
| B2 | **Pixel-art helpers:** pixel-perfect strokes, shading ink along palette ramps, dithering brushes, symmetry, tile and wrap mode, outline generator | α |
| B3 | **Layers:** groups, blend modes, opacity, lock and hide; reference image layers | α |
| B4 | **Palettes:** palette library, ramps, palette-swap variants (recolour enemies, day and night) | α |
| B5 | **Animation timeline:** frames, per-frame durations, **tags** (clips), onion skin, playback, loop modes | α |
| B6 | **Frame events:** footstep, hit or sound cues authored on frames | α |
| B7 | **Hitboxes and hurtboxes** per frame, plus pivots and anchors | α |
| B8 | **Directional sets:** 1/2/4/8 directions, auto-mirroring, and a preview orbiting a 3D camera | α |
| B9 | **Lighting maps:** normal-map generator (presets), **normal paint brush**, emissive, specular and roughness masks; preview under the scene's lights (R-VIS-15) | α |
| B10 | **Slices and 9-slice** for UI art | α |
| B11 | **Automatic atlas packing** with no manual sprite-sheet management | α |
| B12 | **Import:** PNG, sprite sheets (grid or auto-slice), `.aseprite` with live reload (tags → clips, slices → 9-slice and pivots) | α |
| B13 | **Character generator:** modular parts (body, hair, clothes) with palette variants | 1.0 |
| B14 | **3D-to-pixel sprite baker** (R-VIS-17) | Post |
| B15 | **Tile authoring:** tilesets with auto-tile rule templates (blob, 47-tile, Wang-style) | α |

## C. World building (World workspace)

| ID | Feature | Tag |
|---|---|---|
| C1 | **Diorama Builder:** paint tiles onto the faces of 3D geometry, Crocotile-style | α |
| C2 | **Block brushes:** cubes, slopes, stairs, arches, cylinders; per-face tile assignment; UV auto-fit to PPU | α |
| C3 | **Prefab kits:** houses, trees, fences, bridges. Prefabs nest and can be overridden | α |
| C4 | **Auto-tiling rules** for 2D layers and 3D surfaces (LDtk-style rules and biomes) | α (3D surfaces) / 1.0 (2D layers) |
| C5 | **Scatter and variation brushes:** foliage, rocks, random tile variants (fights the "lonely sprite" problem, `research/01` §3) | α |
| C6 | **2D map mode:** tile layers, parallax layers, for Classic 2D and side-view games | 1.0 |
| C7 | **Entities:** place characters, objects and devices. Typed properties and entity-to-entity references | α |
| C8 | **Regions and zones:** triggers, encounter areas, audio zones, camera zones, Look zones | α |
| C9 | **Water, sky and weather presets** in pixel style (R-VIS-16) | α |
| C10 | **Navigation:** grid pathfinding (tile games) and navmesh (free movement), auto-generated | α |
| C11 | **Map connections:** doors, edges and transitions with fade presets; world-map and overworld node | α |
| C12 | **glTF 2.0 import**, with automatic PPU validation | α |
| C13 | **Large worlds:** level streaming and chunked maps | 1.0 |
| C14 | **Terrain:** height-painted ground with pixel-texture splatting | 1.0 |

## D. Rendering and Look (Look workspace)

Full specification: [`04-rendering-pipeline.md`](04-rendering-pipeline.md).

| ID | Feature | Tag |
|---|---|---|
| D1 | **Render Styles:** Diorama, Retro 3D and Classic 2D (R-VIS-01, R-VIS-12) | α (Diorama) / 1.0 (Classic 2D, Retro 3D) |
| D2 | **Billboard sprites:** all modes, depth-correct, alpha-tested (R-VIS-02, R-VIS-09) | α |
| D3 | **Lights:** point, spot, directional and area; shadows; light cookies; flicker presets (R-VIS-04) | α |
| D4 | **Sprite lighting and shadows:** silhouette, blob or none (R-VIS-03) | α |
| D5 | **Time of day:** sun and moon, sky gradients, colour ramps, scheduled Look blending | α |
| D6 | **Post-processing stack:** depth of field (tilt-shift and physical), bloom, fog, volumetric shafts, SSAO, LUT grading, vignette, grain, chromatic aberration, CRT (R-VIS-05) | α |
| D7 | **Look presets** with zone- and time-based blending (R-VIS-06) | α |
| D8 | **Texel-density checker and PPU enforcement** (R-VIS-07) | α |
| D9 | **Filtering modes:** nearest, crisp-AA, bilinear (R-VIS-08) | α |
| D10 | **Camera rigs:** follow with dead zone and look-ahead, zones, rails, orbit limits, shake, cutscene cameras (R-VIS-10) | α |
| D11 | **Parallax backdrops and pixel skyboxes** (R-VIS-11) | α |
| D12 | **Pixel particle system and editor:** pixel-snapped, palette-aware, with presets (fire, rain, magic, dust) | α |
| D13 | **Layer-based material editor** with no nodes: base texture + effects (scroll, flash, dissolve, outline, palette swap, wind sway) | α |
| D14 | **Quality tiers** and per-pass GPU budgets (R-VIS-14) | α |
| D15 | **Node-based shader graph** for technical artists | Post |
| D16 | **Palette-locked lighting** (R-VIS-18) | Post |
| D17 | **Baked lighting and light probes** for rich static scenes | 1.0 |

## E. Gameplay logic (Logic workspace)

Full specification: [`05-no-code-logic-system.md`](05-no-code-logic-system.md).

| ID | Feature | Tag |
|---|---|---|
| E1 | **Behaviors library:** attach and configure; the most common needs covered by presets | α |
| E2 | **Links:** device-style event → action wiring, visible in the World view | α |
| E3 | **Rules:** When / If / Do event sheets with sub-rules, else, for-each and groups | α |
| E4 | **Sequences:** step lists with waits, branches and loops, plus a timeline view for cutscenes | α |
| E5 | **Brains:** hierarchical state machines for characters, AI and game flow | α (ready-made templates, tuned in the inspector) / 1.0 (Brain editor) |
| E6 | **Variables:** global, scene, object and save-persistent, all typed | α |
| E7 | **Expressions:** a formula editor with pickers, autocomplete and units | α |
| E8 | **Custom Blocks:** user-made actions, conditions and behaviors with parameters | 1.0 |
| E9 | **Messages and signals** across objects and scenes; timers; spawners | α |
| E10 | **Live logic debugger:** highlight firing logic, watch values, breakpoints, "why didn't this fire?" | α |
| E11 | **Optional Luau scripting:** script blocks and new block definitions (ADR-007) | 1.0 |
| E12 | **Rewind debugging:** scrub back through recent game state | Post |

## F. Genre kits

Every kit is made from the same public building blocks as user content: behaviors, Data
Tables, UI templates, Sequences and Brains. Users can open, inspect and modify everything.

| ID | Kit | Contents | Tag |
|---|---|---|---|
| F1 | **Common** | Dialogue, inventory, shops, quests and journal, save points, minimap, pause menu, title screen, options, credits, achievements bridge | α |
| F2 | **RPG** | Party, classes, stats and growth curves, skills, items, equipment, status effects, configurable turn-based battle (turn order, action points, weaknesses), encounters (random, visible, scripted), bestiary, battle UI | α |
| F3 | **Tactics** | Grid with height and facing, movement and attack ranges, terrain effects, turn order, AI profiles | 1.0 |
| F4 | **Action-Adventure** | Real-time combat, combo states, hitstop, knockback, invincibility frames, projectiles, enemy AI presets, boss phases | 1.0 |
| F5 | **Platformer** | Side-view controller (coyote time, jump buffering, variable jump height), moving platforms, ladders, hazards, checkpoints | 1.0 |
| F6 | **Adventure** | Point-and-interact, inventory puzzles, item combination, pushable blocks, switches and keys | 1.0 |

The turn-based battle system is **configurable and original**. It must not reproduce any
specific commercial game's signature mechanics (`09`, R-07).

## G. Story and narrative (Story workspace)

| ID | Feature | Tag |
|---|---|---|
| G1 | **Dialogue graph:** lines, choices, conditions and effects, using the shared logic vocabulary | α |
| G2 | **Screenplay mode:** type dialogue as text, which becomes the graph automatically | 1.0 |
| G3 | **Speakers:** portraits, emotions, name plates, voice blips and voice-over clips | α |
| G4 | **Text effects:** wave, shake, colour, speed, pauses, inline icons | α |
| G5 | **Quests:** objectives, stages, rewards, journal entries | α |
| G6 | **Story outline:** chapters → scenes → beats, linked to maps and sequences | 1.0 |
| G7 | **Localization:** string tables with automatic keys, CSV/XLIFF round-trip, pseudo-localization, per-language fonts and fallbacks, in-editor language switching | α (tables, CSV) / 1.0 (rest) |
| G8 | **ink and Yarn import** | Post |

## H. Characters and AI

| ID | Feature | Tag |
|---|---|---|
| H1 | **Character asset:** sprite set + collision + behaviors + stats in one place | α |
| H2 | **NPC behaviors:** wander, patrol, follow, face the player, daily schedules | α |
| H3 | **Enemy AI presets:** patrol → chase → attack → flee, as Brains | α (presets) / 1.0 (editable in the Brain editor) |
| H4 | **Party followers** (caterpillar or free) | α |
| H5 | **Pathfinding:** grid A* and navmesh, with dynamic obstacles | α |

## I. Physics and collision

| ID | Feature | Tag |
|---|---|---|
| I1 | **Automatic collision** from tiles, blocks and sprite alpha; manual shape editing | α |
| I2 | **Collision layers and masks** with friendly names | α |
| I3 | **Character controllers:** a 3D "virtual character" for diorama maps (Jolt) and a 2D mover (Box2D) (ADR-008) | α (3D) / 1.0 (2D mover) |
| I4 | **Triggers, raycasts and overlap queries** exposed as conditions | α |
| I5 | **Rigid-body props**: crates, pushable blocks | 1.0 |

## J. Audio (Audio workspace)

| ID | Feature | Tag |
|---|---|---|
| J1 | **Import:** WAV, FLAC, MP3 (miniaudio [S72]), OGG through an added decoder | α |
| J2 | **SFX generator** (sfxr-style) with presets: coin, jump, hit, explosion, UI | α |
| J3 | **Music:** loop points, intro + loop, crossfades, **adaptive layers**, stingers, beat-synced transitions | α (loops, crossfades) / 1.0 (adaptive) |
| J4 | **Ambience and reverb zones**; surface-based footsteps | α |
| J5 | **Mixer:** buses, volume snapshots, dialogue ducking | α |
| J6 | **Spatial audio** for 3D maps | α |
| J7 | **FMOD / Wwise plugins** | Post |

## K. UI (UI workspace)

| ID | Feature | Tag |
|---|---|---|
| K1 | **UI designer:** anchors, layouts (stack, grid), 9-slice, themes | α |
| K2 | **Widget library:** text, image, button, list, grid, bar, slider, toggle, tabs, scroll | α |
| K3 | **Templates:** title, pause, options, save/load, inventory, shop, equipment, battle HUD, dialogue box, quest log | α |
| K4 | **Automatic gamepad and keyboard navigation**; device-specific button prompts | α |
| K5 | **Pixel-perfect UI scaling** and bitmap fonts (R-VIS-13) | α |
| K6 | **Player-facing accessibility options:** text size, high contrast, subtitles, input remapping, reduced flashing and shake | 1.0 |
| K7 | **UI animations:** transitions and tweens, without code | α |

## M. Save system

| ID | Feature | Tag |
|---|---|---|
| M1 | **Automatic persistence:** mark variables or entities "persistent" and they are saved | α |
| M2 | **Save slots**, autosave, quicksave; save UI template | α |
| M3 | **Save versioning and migration** across game updates | 1.0 |
| M4 | **Steam Cloud** sync | 1.0 |

## N. Playtest, debugging and QA (Playtest workspace)

| ID | Feature | Tag |
|---|---|---|
| N1 | **Play from here / play from start** in the editor, in seconds | α |
| N2 | **Live editing while playing:** art, data and logic hot reload | α |
| N3 | **Time controls:** pause, slow motion, frame step | α |
| N4 | **No-code debug panel:** teleport, give item, set flag, toggle god mode, jump to a quest stage | α |
| N5 | **Performance overlay** with plain-language budgets ("Lighting is using 40% of the frame") | α |
| N6 | **Input record and replay** for regression tests and bug reports | 1.0 |
| N7 | **Standalone test build** that runs exactly like the shipped game | α |
| N8 | **Automated smoke tests:** load every map, run every sequence headless | 1.0 |

## O. Build, release and live operations (Ship workspace)

| ID | Feature | Tag |
|---|---|---|
| O1 | **One-click Windows build:** a prebuilt runtime plus cooked content pack. No compiler needed on the user's machine | α |
| O2 | **Build profiles:** development, demo and release; content filtering for demos | α |
| O3 | **Executable branding:** icon, version info, splash, window title | α |
| O4 | **Steamworks forms:** achievements, stats, leaderboards, rich presence, cloud, overlay [S58]. An optional module that uses the creator's own copy of the Steamworks SDK (ADR-014) | 1.0 |
| O5 | **Steam upload helper** (steamcmd) and depot configuration guide | 1.0 |
| O6 | **Code-signing guide and integration** (user-supplied certificate) | 1.0 |
| O7 | **Opt-in crash reporting** for shipped games (minidumps) | 1.0 |
| O8 | **Patch and DLC builds** (delta-friendly content packs) | 1.0 |
| O9 | **Workshop / mod support:** players load mods sandboxed through Luau | Post |
| O10 | **Store asset helper:** screenshot mode, capsule-image templates, trailer capture | 1.0 |

## P. Extensibility

| ID | Feature | Tag |
|---|---|---|
| P1 | **Custom Blocks and behavior packages** (no code) | 1.0 |
| P2 | **Luau packages:** new blocks, behaviors and importers, sandboxed | 1.0 |
| P3 | **C++ plugin SDK** with a stable C ABI | Post |
| P4 | **Editor extension API:** panels, tools, importers | Post |

## Q. Learning and assistance

| ID | Feature | Tag |
|---|---|---|
| Q1 | **Interactive tutorials** ("quests" inside the editor) | α |
| Q2 | **Contextual help:** every field has a tooltip and a link to its docs page (generated from reflection) | α |
| Q3 | **Example projects**, with every piece of logic annotated | 1.0 |
| Q4 | **Searchable offline docs** inside the editor | α |
| Q5 | **Optional AI assistant**, off by default. It edits only through undoable commands and shows a reviewable diff (`06` §8) | Work starts after Gate 5 (`09` §5, Q-06) |

## R. Editor accessibility and localization

| ID | Feature | Tag |
|---|---|---|
| R1 | **Full keyboard navigation** | α |
| R2 | **Screen-reader exposure** through Qt's accessibility support [S75] | 1.0 |
| R3 | **UI scaling** from 100% to 200%, high-contrast theme, colour-blind-safe logic colours (icons as well as colours) | α (scaling) / 1.0 (rest) |
| R4 | **Editor translations** (English first; Japanese, Spanish, Portuguese, French, German and Chinese as candidates) | 1.0 |

## T. Game data and tables (Data workspace)

Full specification: [`05-no-code-logic-system.md`](05-no-code-logic-system.md) §3.6.

| ID | Feature | Tag |
|---|---|---|
| T1 | **Typed Data Tables:** items, enemies, skills, loot, dialogue speakers, or any user-defined table. Columns have types, ranges and references to other tables or assets | α |
| T2 | **Form view and grid view** for each table; bulk edit | α |
| T3 | **Formulas:** spreadsheet-style computed columns and curves (e.g. XP per level) with no code; validated and unit-aware | α |
| T4 | **Balancing charts:** plot any column or curve, compare enemies and items side by side | 1.0 |
| T5 | **CSV import and export** for spreadsheet users | α |

## U. User input

| ID | Feature | Tag |
|---|---|---|
| U1 | **Action mapping:** keyboard, mouse and gamepads (SDL3) with presets per genre | α |
| U2 | **Player-facing rebinding UI** generated automatically | α |
| U3 | **Rumble**; Steam Input compatibility | 1.0 |
| U4 | **Local multiplayer:** multiple gamepads | 1.0 |

---

## Coverage check against "start to finish" (`01` §5)

| Stage | Covered by |
|---|---|
| 1 Start a project | A1–A3 |
| 2 Art direction | A11, D7 |
| 3 Sprites, tiles, animation | B1–B15 |
| 4 World | C1–C14 |
| 5 Lighting and mood | D1–D17 |
| 6 Characters | H1–H5, B8, I3 |
| 7 Logic | E1–E12, F1–F6 |
| 8 Story | G1–G6 |
| 9 Data | T1–T5, F2 |
| 10 Audio | J1–J7 |
| 11 UI | K1–K7 |
| 12 Playtest | N1–N8 |
| 13 Translation | G7 |
| 14 Release | O1–O7, O10 |
| 15 Post-launch | O8, O9, M3 |

Stages 1–14 all have α coverage for a Diorama-style RPG, which is the solo alpha scope.
Classic 2D games, the other genre kits and the full Brain editor arrive at 1.0. Stage 15
(post-launch: patches, save migration, DLC) is covered at 1.0, and mod support comes after
1.0.
