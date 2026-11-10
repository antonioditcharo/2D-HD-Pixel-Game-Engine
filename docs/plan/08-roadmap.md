# Plan 08: Roadmap, Milestones and Team

> A phased plan from an empty repository to Sparxel 1.0. Each phase ends at a **gate** with
> explicit exit criteria. Work does not move on until the gate passes, which is how scope
> and quality stay under control (`09`, risk R-01).
>
> **Baseline: one developer.** The owner decided on 9 November 2026 to start solo and add a
> team later (`09` §5, Q-03). Solo work is planned **by gates, not dates**.
>
> The month figures in this document come from a 6–10 person reference team. They are kept
> to show the relative size of each phase. They are not a solo schedule.

---

## 1. Assumptions

- **Who:** one developer now (§5). A team may join later (§6).
- **Timeline:** no solo timeline is given yet. Nothing measured exists to base one on.
  Phase 0 produces the first velocity data, and the plan is re-estimated at Gate 0 and at
  every gate after it.
- **Scope:** the α and 1.0 feature tags in `02`. The α tags describe the solo alpha
  (§5).
- **No hidden dependencies:** no console ports, no online services beyond Steam.
- **Reuse:** the library strategy in ADR-000 holds; we do not rewrite physics, audio, VM or
  ECS.

## 2. Phase overview

```mermaid
gantt
    dateFormat  YYYY-MM
    axisFormat  %Y-%m
    title Reference team scenario, 6 to 10 people (not the solo schedule)
    section Foundations
    P0 Foundations and spikes          :p0, 2027-01, 3M
    section Core
    P1 Runtime core                    :p1, after p0, 6M
    P2 Editor core                     :p2, 2027-08, 6M
    section Game systems
    P3 Game systems and narrative      :p3, after p2, 6M
    section Shipping
    P4 Ship pipeline to Closed Alpha   :p4, after p3, 4M
    P5 Beta / Early Access             :p5, after p4, 6M
    P6 1.0 hardening                   :p6, after p5, 4M
```

The chart shows the **reference team scenario**, not the solo plan. Its calendar dates are
placeholders that assume a January 2027 start.

- For the reference team, adding up the per-phase ranges below and subtracting the roughly
  two-month overlap between Phases 1 and 2 gives about **26–33 months** of scheduled work,
  or about three years with a contingency buffer.
- **One developer will take substantially longer.** How much longer is not estimated here,
  because there is no measured basis for a number. What the chart does show reliably is the
  order of the phases and their relative sizes.

## 3. Phases in detail

The month ranges in the headings are for the reference team.

### Phase 0: Foundations and spikes (≈ 2–3 months)
**Goal:** de-risk the four biggest unknowns before committing to the architecture.

| Work item | Output |
|---|---|
| Repo, CMake/vcpkg, CI (Windows), coding standards, issue tracker | Building skeleton |
| **Spike R: rendering** on the SDL3 GPU API. Reference scene: a diorama village with 50 lit billboard sprites, CSM, 32 point lights, tilt-shift DoF, bloom and fog | ADR-004 decision; first measured budgets (`04` §11) |
| **Spike U: editor shell**, Qt 6 vs WebView2: docking, embedded viewport, reflected inspector, a node widget | ADR-005 decision |
| **Spike L: logic**, visual sample → Logic IR → Luau, with block highlighting, interrupt budgets and a stress test | ADR-007 validation |
| **Spike A: assets**, file watcher → import → cache → hot reload of a sprite into the running viewport | Asset pipeline design confirmed |
| Reference hardware procured (minimum and recommended) | ADR-001 minimum specification fixed |
| Paper prototypes and clickable mock-ups of World, Logic and Sprite Studio, tested with 5 non-coders | UX direction confirmed |

**Gate 0 exit criteria**
- The reference scene runs at ≥ 60 fps at 1080p on minimum-spec hardware, Medium tier, or a
  costed plan exists to get there.
- ADR-004, ADR-005 and ADR-007 are decided and recorded.
- Clickable UX mock-ups score acceptable with non-coders: the Logic mock-up task completes
  in ≥ 70% of attempts.
- ADR-000 has been revisited with the measured velocity.

### Phase 1: Runtime core (≈ 5–6 months)
- Foundation: reflection, job system, memory and logging.
- Platform (SDL3) and RHI backend.
- **Renderer v1:** the Diorama style; sprites with all billboard modes, lighting and
  shadows; the post stack; Looks; texel-density tooling. Debug views.
- **World:** flecs integration, scenes, prefabs with overrides, serialization (JSON + GUID).
- **Physics:** the Collision facade with Jolt; the 3D character controller.
- **Audio v1:** playback, buses, spatial audio, loops and crossfades.
- **Logic runtime v1:** Luau host, scheduler, Rules, Sequences, the Brain runner and
  messages.
- Input actions, save/load v1, localization tables, text (FreeType, HarfBuzz, bitmap
  fonts), game UI runtime v1.
- **Packaging v1:** cooked pack and a standalone runtime executable.

**Gate 1:** a hand-authored (JSON) sample scene plays as a standalone `.exe`, with
walking, talking, a cutscene, a save and a load. It meets the performance budget and
passes golden-image tests.

### Phase 2: Editor core (≈ 5–6 months, overlapping Phase 1 by about 2 months)
- **Shell:** workspaces, docking, the command system (undo/redo), autosave and crash
  recovery, snapshots v1.
- **Asset database and browser** with GUIDs, thumbnails, search and safe rename; Project
  Doctor v1.
- **Inspector** generated from reflection, with Basic/Advanced views.
- **World workspace:** Diorama Builder (blocks, face painting, prefabs, scatter,
  auto-rules), entities, regions, Links mode, camera preview, time-of-day scrubber.
- **Sprite Studio v1:** drawing tools, layers, palettes, animation timeline, tags, onion
  skin, normal generator and brush, `.aseprite` import.
- **Look workspace:** presets, sliders and guardrails.
- **Logic v1 UI:** behaviors gallery, Links, Rules editor, live highlight, watches.
- **Play-in-editor** with live editing.

**Gate 2:** in a moderated usability test, **5 of 8 non-coders** build a walkable, lit
village with an NPC conversation and a lever-gate puzzle in **≤ 30 minutes** without docs.
SUS ≥ 68 at this gate (target 75 by alpha).

### Phase 3: Game systems and narrative (≈ 5–6 months)
- **Logic v2:**
  - the Sequence list and timeline editors;
  - Brain templates, tuned in the inspector;
  - Tables with formulas and curves;
  - the "why didn't this fire?" explainer and event log;
  - the Logic map.
- **Story workspace:** dialogue graph, speakers, text effects, quests, localization
  CSV round-trip.
- **Common kit and RPG kit:**
  - inventory, shops, save points and menus;
  - turn-based battle;
  - party, stats, skills, items and equipment;
  - encounters.
- **UI workspace** with templates, plus automatic gamepad navigation.
- **Audio workspace:** SFX generator, music setup and zones.
- **Characters workspace;** NPC and enemy presets; pathfinding.
- **Internal vertical slice:** a **30–45 minute JRPG demo** in the Diorama style, built
  *only* in the editor, with zero script.

**Gate 3:** the vertical slice is complete and playable start to finish with no script
blocks. Usability round 2 passes ≥ 75% task completion.

### Phase 4: Ship pipeline and Closed Alpha (≈ 3–4 months)
- **Ship workspace:**
  - build profiles;
  - executable branding;
  - Project Doctor blocking checks;
  - standalone test build;
  - third-party notices generation.
- Performance overlay, input record and replay (if it is ready early), and the cheat
  panel.
- Template projects, interactive tutorials, offline docs and example annotations.
- Hardening: importer fuzzing, save compatibility, crash-reporting pipeline.

**Gate 4 (Closed Alpha):**
- 50–100 external testers. All α features from `02` are present.
- The vertical slice is published as a **free demo** on itch.io or Steam, built with the
  public toolchain.
- The crash-free session rate is at least 99% (target 99.5% by 1.0).

### Phase 5: Beta / Early Access (≈ 5–6 months)
- **More genre kits:** Tactics, Action-Adventure, Platformer and Adventure.
- **Rendering:** the Classic 2D and Retro 3D styles, the 2D map mode and Box2D mover,
  baked lighting and probes, terrain, level streaming.
- **Logic:** the full Brain editor, Custom Blocks, optional Luau scripting, input replay.
- **Steam forms:** achievements, stats, leaderboards and cloud saves; upload helper;
  patch builds.
- **Team features:** Git UI, asset locking, merge tools.
- **Editor:** accessibility pass (screen reader for custom widgets), translations, player
  accessibility options template.

**Gate 5:** an Early Access release, with community feedback loops in place. Third-party
games are in production.

### Phase 6: 1.0 hardening (≈ 3–4 months)
- File-format freeze with migration guarantees, an LTS policy and performance polish.
- Documentation completeness and stability.
- **AI assistant work starts here,** after Gate 5 (`09` §5, Q-06). It is opt-in and
  follows the rules in `06` §8. It ships when it is ready, at 1.0 or after.

**Gate 6 (1.0):**
- All 1.0 features from `02` are present.
- Every success metric in `01` §6 is met.
- At least one third-party game has shipped commercially (aspirational).

### After 1.0
- Plugin SDK (C++), editor extension API and a marketplace.
- Mod and Workshop support and the 3D-to-pixel baker.
- Shader graph and palette-locked lighting.
- Vulkan backend hardening, Steam Deck support and a Linux runtime.
- Cloud collaboration.

## 4. Critical path and dependencies

```mermaid
flowchart LR
    REF["Reflection registry"] --> INS["Generated inspector"]
    REF --> VOC["Logic vocabulary"]
    VOC --> LC["Logic compiler"] --> RULES["Rules/Sequences/Brains UIs"]
    RHI["RHI decision (P0)"] --> RND["Renderer v1"] --> VP["Editor viewports"]
    CMD["Command system"] --> UNDO["Undo/autosave/snapshots"]
    CMD --> ALLED["Every editor tool"]
    ASSET["Asset DB + GUIDs"] --> ALLED
    RULES --> KITS["Genre kits"] --> VS["Vertical slice"]
    VP --> WB["World builder"] --> VS
```

**Items that block everything else, so they are done first and done well:**

1. The reflection registry.
2. The command system.
3. The asset database with GUIDs.
4. The RHI decision.
5. The logic compiler.

## 5. The solo baseline (current plan)

One developer builds the engine, so the alpha is cut to a **"Minimum Lovable Engine"**. The
α tags in `02` already reflect this cut.

1. **Phase 0 is unchanged.** The spikes matter even more when time is scarce.
2. **The solo alpha contains:**
   - the Diorama render style only. Classic 2D and Retro 3D move to Phase 5;
   - the Common kit and the RPG kit only;
   - Behaviors, Links, Rules, Sequences and Tables in full. Brains ship as ready-made
     templates tuned in the inspector, and the full Brain editor moves to Phase 5;
   - Sprite Studio for drawing, animation and lighting maps, with `.aseprite` import as the
     path for professional artists;
   - no team features.
3. **Lean on imports first.** `.aseprite` and glTF import can come before the matching
   built-in tools are complete.
4. **Build for one vertical slice.** Every feature is judged by whether the Phase 3 JRPG
   demo needs it.
5. **Roles one person cannot fill are bought in small pieces or deferred:**
   - *Usability testing* still happens at every gate (`09` §3). It needs participants, not
     staff.
   - *Template art and Look presets* need an artist's eye. Until an artist joins, use a
     small set of original placeholder assets and treat the presets as provisional.
6. **Re-plan at every gate.** Gate 0 gives the first measured velocity.

## 6. Scaling up: the reference team

When the project grows, these are the roles the plan assumes. The first two hires that
remove the most risk are a **rendering engineer** (R-03) and a **technical or pixel
artist** (Look presets, template art).

| Role | Headcount | Owns |
|---|---|---|
| Tech lead / engine architect | 1 | Foundation, reflection, ECS integration, architecture integrity |
| Rendering engineer | 1–2 | RHI, renderer, Looks, performance |
| Tools / editor engineers | 2–3 | Qt shell, command system, World, Sprite Studio, Logic UIs |
| Gameplay systems engineer | 1–2 | Logic runtime and compiler, kits, physics, audio |
| UX designer / researcher | 1 | Workflows, usability testing, glossary, onboarding |
| Technical artist / pixel artist | 1 | Look presets, template art, Sprite Studio requirements, golden images |
| QA, docs and community | 1 | Test plans, docs, tutorials, alpha and beta programmes |
| Producer (part-time or combined with lead) | 0.5 | Gates, scope, risk register |

## 7. First 30 days (concrete next steps)

Order of work for one developer. "Week" is a unit of ordering here, not a promise.

| Step | Actions | Status |
|---|---|---|
| 1 | Approve the plan and answer the open questions in `09` | Done (`09` §5) |
| 1 | Screen candidate product names and choose one | Done: **Sparxel** (`research/05` §7) |
| 1 | Set up the repository structure, licence, CMake, vcpkg and CI | Done (`docs/dev/`) |
| 2 | Write coding standards and the folder layout | Done (`docs/dev/`) |
| 2 | Draft the reflection registry API (it drives everything) | v0.2 done: types, properties, choice lists, actions, conditions, getters and events (`docs/dev/reflection.md`) |
| 2 | Choose and obtain the reference test hardware (ADR-001) | Open |
| 3 | Start Spike R (rendering), then Spike L (logic). Paper-prototype the World and Logic workspaces | Spike L started first, because it needs no GPU. Done: the VM host with interrupt budgets, and Rules and Sequences compiled from the Logic IR to Luau with block mapping and highlighting (`docs/dev/logic-runtime.md`). Stress test passed (ADR-007). Open: Brains. Spike R started: a device and pixel-exact sprites on the SDL GPU API, tested headless on a software GPU (`docs/dev/rendering.md`). Spike A started: the asset database with `.meta` GUIDs, change detection and PNG import (`docs/dev/assets.md`). The first playable loop: a project folder opened and run, with input, movement, Rules and pixel-perfect drawing (`docs/dev/game.md`). The paper prototypes are open |
| 4 | Start Spike U (Qt vs WebView2). Run the first usability test of the paper prototypes with 5 non-coders. Write up spike findings | Open |
