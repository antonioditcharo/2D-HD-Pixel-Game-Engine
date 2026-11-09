# Research 04: No-Code Paradigms

> **What this document answers:** what ways exist to let people build game logic without
> writing code, what each is good and bad at, what history teaches us, and what that means
> for this engine's design.
>
> The user requirement this serves is **"a no-code environment, so non-coders can use it
> easily."** This is the single most important product risk in the whole plan, so it gets
> its own research document.
>
> `[S#]` refers to [`../sources.md`](../sources.md). Sections labelled **Analysis** are the
> author's assessment.

---

## 1. The paradigms

### P-A: Behaviors and presets ("attach a capability, set its options")
- **How it works:** you attach a ready-made behavior to an object, such as "platformer
  character" or "follow path", and adjust its parameters in an inspector.
- **Who uses it:**
  - GDevelop ships more than 130 ready-made behaviors and extensions [S33].
  - Construct's behaviors likewise add ready-made actions, conditions and expressions to an
    object [S88].
- **Best for:** common mechanics, which covers most objects in most games.
- **Breaks down when:** you need something the preset author did not foresee.

### P-B: Event sheets ("when these conditions are true, do these actions")
- **How it works** [S32]:
  - Rows of **conditions** and **actions**, evaluated top to bottom.
  - **Sub-events** nest under a parent.
  - Conditions **pick** which object instances the actions affect.
  - An event with no conditions runs every frame.
  - Event types include standard, else, for-each, repeat, while, group, link and JavaScript.
- **Who uses it:** GDevelop [S32] and Construct [S88].
- **Best for:** game rules, reactions, simple AI and UI logic.
- **Breaks down when:**
  - Sheets grow very long.
  - Users misunderstand object picking.
  - Per-frame evaluation has hidden costs or timing bugs. GDevelop's docs warn that
    per-frame changes need `TimeDelta()` [S32].

### P-C: Event pages and command lists ("this NPC, when touched, runs these steps")
- **How it works** [S36][S37]:
  - Each event has **pages**.
  - A page has appearance **conditions** (switches, variables, self switches, items,
    actors) and a **trigger** (Action Button, Player Touch, Event Touch, Autorun, Parallel).
  - A page runs a sequential **command list**.
  - Autorun suspends player control, which is why it is used for cutscenes. Parallel lets
    play continue.
- **Who uses it:**
  - RPG Maker MZ [S36].
  - SMILE GAME BUILDER builds events as flowcharts or lines [S44].
  - RPG Maker Unite builds events from presets and command variables [S38].
- **Best for:** story, cutscenes, NPC interactions, puzzles: anything *sequential* with
  waits ("show text, then wait, then move, then fade").
- **Breaks down when:** real-time action logic or complex AI is needed.

### P-D: State machines ("a character is Idle, Running or Attacking, and switches when...")
- **How it works:** states hold behavior. Transitions fire on conditions.
- **Who uses it:**
  - ACTION GAME MAKER's visual scripting, which lays out "a character's states as boxes"
    connected with arrows [S41].
  - PaperZD animation state machines in Unreal [S28].
- **Best for:** character controllers, enemy AI, animation selection, game flow (title →
  play → game over).
- **Breaks down when:** used for one-off rules. A state machine for a single door is
  overkill.

### P-E: Execution and data-flow node graphs ("wire nodes that are functions")
- **How it works:** nodes are operations, and wires carry execution flow and values.
- **Who uses it:**
  - Unreal Blueprints and Unity's visual scripting.
  - RPG in a Box's node-based script editor [S43].
  - **Godot's VisualScript, which was removed.** In a poll of more than 5,000 users, only
    0.5% used it as their main language [S23].
- **Best for:** programmers who prefer visuals, and technical artists.
- **Breaks down when:** the user is *not* a programmer. Godot's own post-mortem says
  VisualScript lacked the high-level building blocks that make visual tools useful [S23].

### P-F: Device wiring ("when this lever is switched, open that door")
- **How it works:** Fortnite Creative and UEFN **direct event binding** binds an *event* of
  one placed device to a *function* of another, chosen from the device's options in the
  Details panel. No scripting is involved [S59].
- **History:** it replaced channel-based wiring, and became the default for all islands made
  with the Creative toolset in v25.00 [S60].
- **Best for:** level scripting by designers: switches, doors, spawners, traps, triggers
  and timers.
- **Breaks down when:** logic needs variables, loops or maths beyond what the devices
  expose.

### P-G: Toy-like node programming for beginners
- **How it works:** Nintendo's *Game Builder Garage* (2021) teaches visual programming with
  **Nodon**. These are colour-coded node creatures in input, middle, output and object
  categories. Players connect them, for example a Stick Nodon to a Person Nodon to move a
  character [S61].
- **Best for:** teaching and onboarding.
- **Breaks down when:** a project grows to real game scale.

### P-H: Block-based code
- **How it works:** GameMaker's **GML Visual** lets users drag actions from a toolbox and
  chain them into action blocks, so the flow of the code is visible [S85].
- **Best for:** a gentle path toward programming.
- **Breaks down when:** blocks map one-to-one onto code concepts. The user still has to
  think like a programmer.

### P-I: AI-assisted building
- **How it works:** GDevelop's **AI agent** can build parts of a game or answer "how to"
  questions inside the editor [S33].
- **Best for:** getting started, discovering features and producing boilerplate.
- **Breaks down when:** the output is opaque or cannot be edited, or the user cannot verify
  it.

### P-J: The escape hatch: optional text scripting
Every successful maker reviewed keeps one:
- JavaScript events in GDevelop [S32].
- JavaScript feature extensions in Pixel Game Maker MV [S42].
- JavaScript plugins in RPG Maker MZ [S90].
- GDScript for custom actions and conditions in ACTION GAME MAKER [S41].
- **Bauxite** in RPG in a Box [S43].

## 2. Which paradigm fits which task (Analysis)

Fit ratings: ✅ natural fit, ◐ workable, ❌ poor fit.

| Task | Behaviors (A) | Event sheet (B) | Command list (C) | State machine (D) | Node graph (E) | Device wiring (F) |
|---|---|---|---|---|---|---|
| Lever opens a door | ◐ | ◐ | ◐ | ❌ | ◐ | ✅ |
| Player movement (top-down or platformer) | ✅ | ◐ | ❌ | ◐ | ◐ | ❌ |
| NPC conversation with choices | ❌ | ◐ | ✅ | ❌ | ◐ | ❌ |
| Cutscene (camera, moves, dialogue, music) | ❌ | ❌ | ✅ | ❌ | ◐ | ❌ |
| Enemy AI (patrol → chase → attack) | ◐ | ◐ | ❌ | ✅ | ◐ | ❌ |
| Score, health and game-over rules | ◐ | ✅ | ◐ | ◐ | ◐ | ◐ |
| Damage formula, XP curve | ❌ | ◐ | ❌ | ❌ | ◐ | ❌ |
| Shop, inventory, quests | ✅ (as a kit) | ◐ | ◐ | ❌ | ◐ | ❌ |

Damage formulas and XP curves are best handled by **data tables with formulas**,
spreadsheet-style, which none of the columns above covers well.

**The key observation:** no single paradigm handles every task. Tools that force one
paradigm onto everything make the poorly fitting tasks painful. The tools that work best
mix paradigms: RPG Maker pairs a database with event pages, and GDevelop pairs behaviors
with events.

## 3. Lessons

| # | Lesson | Evidence |
|---|---|---|
| L1 | **High-level building blocks are the product.** A visual language without packaged game features is "of little use". | Godot's VisualScript post-mortem [S23] |
| L2 | **Make the common case need zero logic.** Behaviors and kits should cover most objects. | GDevelop's 130+ behaviors and extensions [S33]; RPG Maker's database model [S36] |
| L3 | **Use the right representation per task:** rules, sequences, state machines, wiring and data. | §2 matrix; the mixes RPG Maker and GDevelop use [S32][S36] |
| L4 | **Sequential storytelling needs "wait" semantics.** Show text, wait for the player, then continue. | RPG Maker Autorun vs Parallel [S36][S37] |
| L5 | **Spatial wiring is the most intuitive form of level logic.** | UEFN direct event binding replaced channels [S59][S60] |
| L6 | **Execution order and per-frame evaluation must be visible**, or users get surprising bugs. | GDevelop top-to-bottom order and TimeDelta guidance [S32] |
| L7 | **Keep an escape hatch, but never require it.** | JS / GDScript / Bauxite in GDevelop, PGMMV, AGM and RPG in a Box [S32][S41][S42][S43] |
| L8 | **A no-code layer inside a professional editor makes users learn both.** | Mixed community reception of RPG Maker Unite, which runs inside Unity (anecdotal) [S38][S86] |
| L9 | **AI help is now expected, but it must produce editable, inspectable results.** | GDevelop AI agent [S33]; the trust requirement is Analysis |
| L10 | **Beginner-friendliness and depth are not opposites** if complexity is *progressively disclosed*. | Game Builder Garage teaches the basics [S61]; the professional tools above show the depth end (Analysis) |

## 4. Design implications for this engine (Analysis)

These are specified in detail in [`../plan/05-no-code-logic-system.md`](../plan/05-no-code-logic-system.md).

1. **Layered logic model.** Each layer is the natural representation for its kind of task,
   and they all share **one vocabulary** of conditions, actions, values and events:
   - **Presets and Behaviors.**
   - **Links**, which is device wiring.
   - **Rules**, which are event sheets.
   - **Sequences**, which are command lists and timelines with waits.
   - **Brains**, which are state machines.
   - **Tables**, which are data with formulas.
   - **Custom Blocks**, which are user-made reusable actions and behaviors. Construct's
     functions and custom actions are a precedent [S88].
2. **Generated from engine reflection.** Every engine feature exposed to logic is declared
   once, with metadata, so it automatically appears in every layer, the inspector, search
   and the documentation. This prevents the gap that sank VisualScript (L1).
3. **Genre kits first.** RPG, Tactics, Action, Platformer and Adventure kits ship as
   behaviors, tables, UI and templates. Users start from a working game, not an empty
   canvas (L2).
4. **No syntax errors are possible.** Editing is structural: typed slots, pickers and
   autocomplete. Free typing is allowed in expression fields but always validated.
5. **Visible execution.** The debugger highlights rules and sequences as they fire, shows
   live values, and explains *why a rule did not fire* (L6).
6. **Compiled, not interpreted from the UI.** Visual logic compiles to a sandboxed VM
   (Luau, see ADR-007). It runs fast and safely in shipped games, and the same VM hosts
   the optional scripting escape hatch (L7).
7. **AI as an assistant, not an author.** Any AI feature edits through the same undoable
   command system and shows a reviewable diff of rules or sequences (L9).

## 5. How we will measure "easy" (Analysis)

No-code success is measured with non-coders through **usability tests at every milestone**.
The full plan is in `plan/09`.

| Metric | Target (initial, to be validated) |
|---|---|
| A first-time user builds a walkable, lit scene with one NPC conversation | ≤ 30 minutes, without docs |
| Task completion rate on a standard 10-task test (door and lever, chest, shop, enemy, cutscene, ...) | ≥ 80% unassisted |
| Errors that need a "programmer explanation" to fix | 0 per session |
| System Usability Scale (SUS) score | ≥ 75 |
| Share of logic in the internal vertical-slice game built **without** optional scripting | 100% |
