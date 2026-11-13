# Sparxel: Research and Plan

> An analysis of the best existing systems, engines and workflows for making HD-2D-style
> pixel games. It ends in a plan for a new **Windows-native, all-in-one, no-code** engine
> that covers every stage of making a game, from first sketch to Steam release.
>
> **Sparxel** is the product name, chosen on 10 November 2026. Its screening is in
> [`research/05`](research/05-name-screening.md) §7. It still needs professional trademark
> clearance before any public launch. "Diorama" now names only the HD-2D-style render
> style and the Diorama Builder tool.

## Decisions made (9–10 November 2026)

| Topic | Decision |
|---|---|
| Business model | Royalty-free and open source |
| Licence | MIT |
| Team | One developer now; a team later |
| First genre | RPG |
| Optional scripting | Hidden until 1.0; Custom Blocks first |
| AI assistant | Work starts after the Early Access / beta release |
| Minimum hardware | Decided after the rendering prototype is measured |
| Product name | **Sparxel** (chosen 10 November 2026, replacing Glimmerbox; professional clearance still needed) |

Details and the questions that remain open are in
[`plan/09`](plan/09-risks-quality-legal.md) §5 and §6.

---

## Executive summary

### What the research found
1. **The look is a known recipe, but it is expensive.** HD-2D combines pixel-art billboard
   sprites, fully 3D environments, dynamic lights that make sprites cast shadows, and heavy
   lens and atmosphere effects: tilt-shift depth of field, bloom, volumetric light and fog.
   Every Square Enix HD-2D title whose engine could be verified was built on Unreal Engine
   4. Sea of Stars, a lit pixel-art game built in Unity, needed a custom render pipeline,
   and one engineer spent about five years on its lighting.
   ([research/01](research/01-hd2d-visual-language.md))
2. **Nobody offers the whole package.** Professional engines can reach the look but need
   programming and technical art. No-code makers (RPG Maker, GDevelop, Construct, ACTION
   GAME MAKER, RPG in a Box) are easy to use but are 2D, voxel or genre-locked. No tool
   reviewed combines HD-2D rendering, deep no-code logic, built-in pixel-art authoring and
   a full shipping pipeline. ([research/02](research/02-engine-landscape.md))
3. **Today's workflow is a chain of separate tools:** pixel editors, normal-map
   generators, 3D tile modellers, level editors, dialogue tools and glue code. Each
   hand-off loses time and consistency, and the glue code completely blocks non-coders.
   ([research/03](research/03-tools-and-workflows.md))
4. **No-code only works when it is high-level and multi-representation.** Godot's
   VisualScript was removed after only 0.5% of users adopted it, because generic visual
   scripting without packaged game features is "of little use". The tools that succeed
   combine behaviors, event sheets, step lists for cutscenes, state machines, wiring and
   data tables, and they keep an optional escape hatch.
   ([research/04](research/04-no-code-paradigms.md))

### What the plan proposes
- **Product:** an all-in-one editor with 12 workspaces: Home, World, Sprite Studio,
  Characters, Logic, Story, Data, Audio, UI, Look, Playtest and Ship. Genre kits (RPG
  first) mean users start from a working, good-looking game.
  ([plan/01](plan/01-vision-and-principles.md), [plan/02](plan/02-feature-scope.md),
  [plan/06](plan/06-editor-ux.md))
- **No-code logic in seven layers**, all sharing one vocabulary: Behaviors, Links (wiring),
  Rules (event sheets), Sequences (cutscenes with waits), Brains (state machines), Tables
  (data and formulas) and Custom Blocks.
  - Logic compiles to a sandboxed Luau VM.
  - The debugger shows what fired and *why something did not*.
  - Optional scripting exists but is never required.

  ([plan/05](plan/05-no-code-logic-system.md))
- **Renderer:** one pipeline with three Render Styles.
  - **Diorama** (HD-2D style), **Retro 3D** and **Classic 2D**.
  - Lit, shadow-casting billboard sprites; tilt-shift depth of field, bloom, volumetrics
    and colour grading.
  - Artist-tuned "Look" presets, and guardrails that keep pixel art looking like pixel art.

  ([plan/04](plan/04-rendering-pipeline.md))
- **Architecture:** a C++20 engine, assembled from proven, permissively licensed libraries:
  SDL3, the SDL GPU API (D3D12), flecs, Jolt, Box2D, miniaudio, Luau, FreeType and
  HarfBuzz. The editor uses Qt 6.
  - **Reflection-driven,** so every engine feature automatically appears in the no-code
    vocabulary, the inspectors and the docs.
  - **Command-based,** so everything can be undone, with autosave, crash recovery and
    snapshots protecting work.
  - **No compiler needed** on the user's machine to export a game.

  ([plan/03](plan/03-architecture.md), [plan/07](plan/07-tech-stack-adrs.md))
- **Roadmap:** seven gated phases, planned for one developer.
  - Phase 0 de-risks the renderer, editor UI and logic VM with spikes.
  - Phase 3 produces a 30–45 minute JRPG vertical slice built with **zero code**.
  - The solo alpha is cut to a "Minimum Lovable Engine": the HD-2D style and the RPG kit.
  - A 6–10 person reference team would need roughly three years to 1.0. One developer
    will take substantially longer; no solo timeline is given until Phase 0 is measured.

  ([plan/08](plan/08-roadmap.md))
- **Risks and decisions:** a risk register, quality and usability-testing strategy, a legal
  checklist, the owner's decisions, and the questions still open. "HD-2D" is a Square Enix
  trademark and must not be used as the product name.
  ([plan/09](plan/09-risks-quality-legal.md))

---

## Document map

### Research (analysis of what exists)
| # | Document | Question it answers |
|---|---|---|
| 01 | [HD-2D visual language](research/01-hd2d-visual-language.md) | What is the look, and what does it demand technically? |
| 02 | [Engines and no-code tools](research/02-engine-landscape.md) | What do Unreal, Unity, Godot, GameMaker, GDevelop, Construct, RPG Maker and others do well or badly for this? |
| 03 | [Content tools and workflows](research/03-tools-and-workflows.md) | What does production look like end to end, and where does it hurt? |
| 04 | [No-code paradigms](research/04-no-code-paradigms.md) | How can non-coders build logic, and what has worked or failed? |
| 05 | [Name screening](research/05-name-screening.md) | Which candidate product names collide with existing trademarks or products? |

### Plan (what to build and how)
| # | Document | Contents |
|---|---|---|
| 01 | [Vision and principles](plan/01-vision-and-principles.md) | Vision, personas, pillars, scope, success metrics |
| 02 | [Systems and features](plan/02-feature-scope.md) | Every system and feature, tagged α / 1.0 / Post |
| 03 | [Architecture](plan/03-architecture.md) | Modules, project format, asset, logic and build pipelines, threading |
| 04 | [Rendering pipeline](plan/04-rendering-pipeline.md) | Render Styles, sprites in 3D, lighting, post, Looks, budgets |
| 05 | [No-code logic system](plan/05-no-code-logic-system.md) | The seven logic layers, execution model, debugging, examples |
| 06 | [Editor UX](plan/06-editor-ux.md) | Workspaces, onboarding, safety, accessibility, AI policy, glossary |
| 07 | [Tech stack and ADRs](plan/07-tech-stack-adrs.md) | 18 decision records and the licence summary |
| 08 | [Roadmap](plan/08-roadmap.md) | Phases, gates, critical path, the solo baseline, the reference team, first 30 days |
| 09 | [Risks, quality and legal](plan/09-risks-quality-legal.md) | Risk register, QA strategy, usability protocol, legal checklist, decisions, open questions |

### Developer docs
| Document | Contents |
|---|---|
| [Building](dev/building.md) | How to build and test the code |
| [Coding standards](dev/coding-standards.md) | Language rules, naming, formatting |
| [Repository layout](dev/repo-layout.md) | What lives where |
| [Reflection registry](dev/reflection.md) | How engine features describe their data for the editor and no-code blocks |
| [Logic runtime](dev/logic-runtime.md) | The Logic IR, its compiler, and the sandboxed Luau VM that runs no-code logic |
| [World](dev/world.md) | Entities, GUIDs, the hierarchy and components |
| [Rendering](dev/rendering.md) | The GPU device, sprites, offline shaders, and testing without a graphics card |
| [Assets](dev/assets.md) | The asset database: GUIDs in `.meta` files, change detection, image import |
| [Classic 2D scenes](dev/scene2d.md) | Sprites, sprite-sheet animation, tilemaps and solid tiles, pixel-perfect drawing, hot reload |
| [Game](dev/game.md) | The runtime that plays a project: behaviours, logic reaching the world, the fixed-step clock, saved games |
| [Physics](dev/physics.md) | Rigid bodies on Box2D: falling, pushing, bouncing, contacts, Physics Bodies in games |
| [Audio](dev/audio.md) | Sound effects and music: buses, fades, decoding, sound in no-code logic |
| [Menus](dev/menus.md) | The ready-made title screen and pause menu: items, quests, party, saving and loading |
| [Battles](dev/battle.md) | Turn-based battles from the data tables: the party, commands, speed order, rewards, the battle screen |
| [Localization](dev/localization.md) | Games in other languages: texts collected into a translations table, the language chosen by the project or the player, a pseudo language |
| [Shops](dev/shop.md) | The ready-made shop screen: buying and selling the items of `data/items` for gold, by quantity |
| [Data tables](dev/tables.md) | Items, enemies, skills and levels as tables: typed columns, formulas, CSV, the grid editor, logic reading cells |
| [Story: conversations](dev/story.md) | Conversations as graphs: the Story editor, speakers and name plates, choices, conditions, effects and flags, played by the game |
| [Sprite Studio](dev/sprite-studio.md) | Drawing and animating pixel-art sprites: tools, layers, frames, tags, onion skin, the `.dsprite` file and its sprite sheet, hot reload |
| [The editor](dev/editor.md) | `sparxel-editor`: the scene view, entity list, assets, the inspector generated from reflection, undo, saving, Play and Export |
| [Exporting games](dev/export.md) | Packaging a project with the player into a folder that plays the game; the checks first |
| [UI: text and dialogue](dev/ui.md) | The built-in pixel font, measuring and wrapping text, and message boxes from no-code logic |
| [Player and sample](dev/player.md) | Playing a project folder, headless runs and screenshots, and the Hello Sparxel sample village |
| [Input](dev/input.md) | Named actions, bindings and presets, recording and replay, input in no-code logic |
| [Serialization](dev/serialization.md) | How objects, logic assets and scenes are saved as JSON: determinism, versions, error reporting |

### Sources
[sources.md](sources.md) lists 105 sources, each with an ID (`[S#]`) and a note on how it was
verified.

---

## How this was researched
- Factual claims about third-party products cite a source. A few widely known basics are
  stated without one, for example that Unreal ships the Paper2D plugin and Blueprints.
- Each source is marked **Fetched** (read directly), **Indexed** (confirmed via a search
  index where direct retrieval was blocked or not attempted) or **Third-party**.
- Engineering interpretation is labelled **Analysis**, so it is never mistaken for reported
  fact.
- Claims that could not be verified were either removed or explicitly marked unverified.
  Examples: the engines behind the Dragon Quest HD-2D remakes and Octopath Traveler 0, FMOD's
  current thresholds, and Construct's paid prices.
- Research date: **November 2026.** Versions, prices and licences should be re-checked
  before being quoted publicly.
