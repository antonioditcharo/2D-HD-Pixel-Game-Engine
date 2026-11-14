# Plan 01: Vision, Users and Principles

> Product name: **Sparxel**, chosen by the owner on 10 November 2026 (`research/05` §7). It
> replaced the earlier choice, Glimmerbox. It still needs professional trademark clearance
> before any public launch, and the product must never be called "HD-2D" (see
> `research/01` §1 and `09-risks-quality-legal.md`, R-06).
>
> "Diorama" in these documents now names only the HD-2D-style **render style** and the
> **Diorama Builder** tool, never the product.

---

## 1. Vision

> **Sparxel is a Windows-native, all-in-one, no-code game engine for making beautiful
> HD-2D-style pixel games. Every step from the first sketch to a shipped game on Steam
> happens in one app, and none of it requires writing code.**

In practice:

- **All-in-one** means drawing sprites, animating, building 3D pixel worlds, lighting,
  logic, dialogue, data, audio, UI, testing and shipping all live in the same application,
  with no export or import round-trips. This addresses pain points P1–P6 in `research/03`.
- **No-code** means a complete commercial-quality game can be built without typing a line
  of script. Optional scripting exists for power users but is never required.
- **HD-2D-style** means the engine looks great *by default*: lit pixel-art sprites in 3D
  dioramas with depth of field, bloom, fog and dynamic light. It also supports the related
  Retro 3D and Classic 2D looks (R-VIS-01).
- **Windows PC** is the platform for both the editor and the exported games (see ADR-001).

## 2. Target users (personas)

| Persona | Who they are | What they need most | What would make them quit |
|---|---|---|---|
| **The Storyteller** | A writer or designer with a JRPG in their head. No coding, limited art skills. | Templates, a ready RPG kit, dialogue and quest tools, good-looking default assets. | Any moment that requires programming; ugly first results. |
| **The Pixel Artist** | Strong pixel artist, no coding. | Their art lit beautifully in 3D; art tools as good as their current ones; instant preview in the game. | Clunky drawing tools; having to export and re-import; muddy lighting on their sprites. |
| **The Hobbyist / Student** | Learning game making. | Guided tutorials, forgiving tools, fast "it works!" moments. | Jargon; projects that break; overwhelming UI. |
| **The Small Indie Team (2–5 people)** | Wants to ship a commercial game. | Collaboration, versioning, performance, Steam integration, stability, room to grow. | Data loss; merge chaos; performance walls; licence fees that scale badly. |
| **The Power User / Modder** | Can code a bit and wants to extend. | Custom blocks, optional scripting, plugins, mod support. | A closed system with no escape hatch. |

**Primary persona for every design decision:** *the Storyteller*. If they can do it, the
others can too. The Power User is served by extension points, never by making the default
path more technical.

## 3. Product pillars

1. **No code, not no power.** Every system is reachable without code, through layered
   logic built from high-level game blocks (see `05-no-code-logic-system.md`).
2. **Beautiful by default.** A new project already looks like a finished HD-2D scene.
   Presets are tuned by artists, and guardrails prevent common art-direction mistakes
   (`research/01` §4).
3. **One app, the whole journey.** Each stage of production has a first-class workspace.
   Imports are bridges, not requirements.
4. **Instant feedback.** Edits show up live in the running game. Play-from-here takes
   seconds. Nothing needs a "build" to see a change.
5. **Safe to experiment.** Everything can be undone. Autosave, crash recovery and
   snapshots are on by default, and the project cannot be corrupted by user logic.
6. **Own your game.** Exported games are standalone executables that never owe fees or
   royalties. Project files use open, documented, text-based formats. The engine itself is
   open source under the MIT licence (`09` §5, Q-01 and Q-02; ADR-017).

## 4. Scope

### 4.1 Genres the engine is built for (v1.0)
The HD-2D style has been proven mainly in JRPGs and tactics games (`research/01` §2). v1.0
therefore leads with an RPG kit, and also ships **genre kits** for neighbouring genres that
suit the look:

| Kit | Covers |
|---|---|
| **RPG** | Exploration; turn-based battle (configurable turn systems); party, stats, skills, items, equipment, status effects; shops; quests. |
| **Tactics** | Grid battles with height and facing; movement and attack ranges; turn order; AI. |
| **Action-Adventure** | Real-time top-down or 3/4-view combat; hitboxes; knockback; projectiles; enemy AI; puzzles. |
| **Platformer / Side-view** | Side-view character controller; platforms, ladders, hazards. |
| **Adventure / Narrative** | Exploration, interaction, inventory puzzles, heavy dialogue. |

Other genres (roguelikes, shoot-'em-ups, metroidvanias, simulation) must be *buildable*
with the generic logic layers and shared systems. Dedicated kits for them come after 1.0.

One developer builds the engine for now, so the alpha ships the RPG kit only and the other
kits follow at 1.0 (`08` §5).

### 4.2 Non-goals for v1.0
- Console, mobile or web export. Consoles also need platform-holder agreements.
- macOS or Linux editors. The Windows-only scope is the user's requirement. A Vulkan path
  is kept open (ADR-004) so Steam Deck and Linux are possible later.
- Online multiplayer. Local co-op through multiple gamepads *is* in scope.
- Photoreal 3D, open-world streaming at AAA scale, or general-purpose 3D games outside the
  pixel and diorama styles.
- Code-first workflows on par with Unity or Godot. Scripting is an escape hatch, not the
  main path.
- Generative-AI art in the box (see `06` §8 for the reasoning).

## 5. What "start to finish" means

The engine must cover every stage below. Each stage maps to a workspace in `06` and to
systems in `02`.

| # | Stage | Sparxel workspace(s) |
|---|---|---|
| 1 | Start a project (genre, style, resolution, palette) | **Home** (project wizard, templates) |
| 2 | Define the art direction | **Home → Style Guide**, **Look** |
| 3 | Make sprites, tiles and animations | **Sprite Studio** |
| 4 | Build the world (3D dioramas, 2D maps, world map) | **World** |
| 5 | Light it and set the mood | **Look** |
| 6 | Create characters, enemies and objects | **Characters** (sprites, behaviors, stats) |
| 7 | Add gameplay logic | **Logic** (Rules, Links, Sequences, Brains) |
| 8 | Write story, dialogue and quests | **Story** |
| 9 | Fill in game data and balance it | **Data** |
| 10 | Add sound and music | **Audio** |
| 11 | Design menus and HUD | **UI** |
| 12 | Playtest and debug | **Playtest** (debugger, overlay, replay) |
| 13 | Translate | **Story → Localization** |
| 14 | Build, integrate Steam and release | **Ship** |
| 15 | Patch, add DLC, support mods | **Ship** (patch builds, Workshop) |

## 6. Success metrics

| Area | Metric | Target |
|---|---|---|
| Onboarding | Time for a first-time user to build a walkable, lit scene with an NPC conversation | ≤ 30 min |
| No-code | The internal vertical-slice game ships with **zero** lines of optional script | 100% no-code |
| No-code | Unassisted completion of the standard 10-task usability test | ≥ 80% |
| Usability | System Usability Scale (SUS) | ≥ 75 |
| Look | Blind comparison: testers rate template scenes "looks like a commercial HD-2D game" | ≥ 70% agree |
| Performance | Template scenes on the reference minimum-spec PC (ADR-001) | 60 fps at 1080p, Medium preset |
| Iteration | Edit → visible in the running game | < 1 s for logic, data and art changes |
| Stability | Editor sessions without a crash (opt-in telemetry) | ≥ 99.5% |
| Safety | User-reported data-loss incidents after beta | 0 |
| Shipping | A game made only with Sparxel released publicly before 1.0 | Yes (the vertical slice, as a free demo on itch.io or Steam) |

All targets are initial proposals. Each is validated or adjusted at the Phase 0 and Phase 2
gates (`08-roadmap.md`).
