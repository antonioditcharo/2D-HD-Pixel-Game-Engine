# Research 02: Engines and No-Code Tools

> **What this document answers:** which engines and game makers people use today for
> HD-2D and pixel games, what each does well, where each falls short for a **non-coder**
> making an **HD-2D-style** game, and what our engine should borrow or avoid.
>
> Research date: November 2026. Version and pricing facts change often, so re-check them
> before quoting them anywhere public. `[S#]` refers to [`../sources.md`](../sources.md).
> Sections labelled **Analysis** are the author's assessment, not sourced fact.

---

## 1. Evaluation criteria

Each tool is assessed against what our target user needs:

| Code | Criterion | Question |
|---|---|---|
| C1 | **HD-2D readiness** | Can it produce the look from `research/01` (3D world, lit billboard sprites, depth of field, bloom, volumetrics) without custom engine or shader work? |
| C2 | **No-code depth** | Can a non-programmer build complete game logic, not just toggle presets? |
| C3 | **Pixel-art authoring** | Can sprites, animations, tiles and 3D pixel environments be made inside the tool? |
| C4 | **Genre breadth** | Is it locked to one genre, or general? |
| C5 | **Full pipeline** | Does it cover everything from an empty project to a shipped Windows build with store integration? |
| C6 | **Cost model** | Licence and fees. |

## 2. General-purpose engines

### 2.1 Unreal Engine (Epic Games)
**Facts**
- Every Square Enix HD-2D title whose engine could be verified in this research was built
  on **Unreal Engine 4**:
  Octopath Traveler [S2], Triangle Strategy [S6], Live A Live [S7] and Octopath Traveler II
  [S5].
- **Licence:** free to use. A 5% royalty applies only to lifetime gross product revenue
  above US$1 million, and revenue from the Epic Games Store is exempt. Epic also documents a
  reduced 3.5% rate for games that launch on the Epic Games Store first or at the same time
  as other stores [S27].
- **2D animation:** Unreal's built-in 2D support is the Paper2D plugin. 2D animation state
  machines usually come from the third-party **PaperZD** plugin, sold on itch.io for
  US$44.99. The most recent engine-version support confirmed during this research was
  UE 4.26 [S28].

**Analysis**
- **Strengths:** proven HD-2D results, a top-tier renderer, and Blueprints node-based
  scripting with context-sensitive search.
- **Gaps for our user:**
  - Blueprints is still programming: execution flow, types, casting and references.
  - There are no pixel-art authoring tools.
  - The editor is very large and complex for a solo non-coder.
  - Reaching the HD-2D look still takes technical-art skill.

### 2.2 Unity
**Facts**
- **2D lighting:** Unity 6's URP 2D Renderer provides 2D lights that can use normal-map and
  mask textures linked to sprites. It also provides **Shadow Caster 2D** and a
  **Pixel Perfect Camera** [S26].
- **Pricing:**
  - Unity cancelled the Runtime Fee on 12 September 2024.
  - Unity Personal stays free, with a revenue cap raised to US$200,000.
  - Unity Pro rose to US$2,200 per seat per year from 1 January 2025 [S25].
- **Shipped HD-2D and pixel games:**
  - **Sea of Stars** was built in Unity with a *custom render pipeline* for dynamic lighting
    [S14]. One engineer spent about five years on that lighting [S15].
  - **Eiyuden Chronicle: Hundred Heroes** uses Unity, with 2D sprites over 3D backgrounds
    [S16].
- **RPG Maker Unite** is an RPG maker that runs *inside* the Unity editor [S38].

**Analysis**
- **Strengths:** good 2D lighting building blocks and a huge ecosystem.
- **Gaps for our user:**
  - The HD-2D-in-3D look needs custom pipeline work, as Sea of Stars shows.
  - The node-based visual scripting is a general-purpose programming tool.
  - Non-coders have to learn the whole Unity editor first.

### 2.3 Godot Engine
**Facts**
- **Recent releases:**
  - Godot 4.6 (27 January 2026) made **Direct3D 12 the default on Windows** and **Jolt the
    default 3D physics engine**. It also added inverse kinematics, reworked screen-space
    reflections and introduced initial LibGodot embedding [S21].
  - Godot 4.7 reached stable in mid-June 2026 [S22].
- **VisualScript was removed in Godot 4.0.** In a poll of more than 5,000 users, only 0.5%
  used it as their main language. The team said it lacked the **high-level, packaged game
  features** that make visual tools useful, so "VisualScript by itself was of little use"
  [S23].
- **Accessibility:** Godot 4.5 added experimental screen-reader support via **AccessKit**.
  In the editor it currently covers the Project Manager, standard UI nodes and the
  inspector [S24].
- **No-code products built on Godot:**
  - **ACTION GAME MAKER**, which is built on Godot. Its Steam page now advertises access to
    Godot Engine 4.7.0's 2D features [S40][S41].
  - **RPG in a Box**, a standalone product made with Godot [S43].
  - The **Pixelorama** pixel editor, which is MIT-licensed [S47].

**Analysis**
- **Strengths:** free and open source under the MIT licence [S93], a clean scene and prefab
  model, and a proven base for no-code products built on top of it.
- **Gaps for our user:**
  - There is no official no-code layer.
  - There are no HD-2D presets.
  - There is no pixel-art authoring in the editor.
- **Main alternative to building from scratch:** see `plan/07-tech-stack-adrs.md`, ADR-000.

## 3. No-code and low-code game makers

### 3.1 GameMaker
- **Licence** [S29]:
  - Free for non-commercial use on non-console platforms.
  - Selling a game requires a **one-time US$99.99 Professional licence**.
  - Console export needs the Enterprise subscription.
- **From 1 January 2027**, commercial games built on the new **GMRT** runtime will not
  need a Professional licence [S30].
- **Logic:** GML Visual lets users drag actions from a toolbox and chain them into blocks,
  so the flow of the code is visible [S85]. Its alternative is the GML scripting language.
- **Analysis:** strong for 2D. The visual blocks map one-to-one onto code concepts, so they
  teach programming rather than replace it.

### 3.2 GDevelop
- **Licence:** open source. The core, the runtimes, the IDE and all extensions are
  **MIT-licensed** [S31].
- **Rendering and physics:** 2D and 3D games render through PixiJS and Three.js (WebGL).
  The editor is built with React and Electron. 3D physics uses Jolt [S31].
- **Event model** [S32]:
  - Events have **conditions** and **actions**, and run top to bottom.
  - Sub-events run only after the parent's conditions are true and its actions have run.
  - Conditions also **pick** which object instances the actions apply to.
  - An event with no conditions runs every frame.
- **Ecosystem:** more than 130 ready-made behaviors and extensions, plus an **AI agent**
  that can build content or answer "how to" questions [S33].
- **Analysis:** the strongest open no-code event model, and proof that "AI as a builder"
  can be part of a no-code tool. Its WebGL renderer (PixiJS/Three.js) is general-purpose and
  is not built around the HD-2D look.

### 3.3 Construct 3
- **Logic** [S88]:
  - Event sheets.
  - Behaviors that add their own actions, conditions and expressions to an object.
  - Event logic can be reused through functions and custom actions.
- **Free edition limits** [S34]:
  - 25 events as a guest, or 50 when logged in.
  - 2 layers and 2 effects.
  - No desktop, Android or iOS publishing.
  - No monetisation.
- **3D:** a 3D camera has existed since release r268 (2021). A built-in glTF/GLB 3D-model
  plugin arrived in the r469 beta (January 2026) [S35]. This was taken from Construct's
  release notes as indexed by search; construct.net blocked direct fetching.
- Paid prices could not be confirmed from an official page during this research.
- **Analysis:** excellent event-sheet ergonomics, and it is gaining 3D features. It is
  2D-first.

### 3.4 RPG Maker MZ
- **Events** [S36]:
  - Events are organized into **pages**.
  - A page has appearance **conditions**: switches, variables, self switches, items and
    actors.
  - A page has a **trigger**: Action Button, Player Touch, Event Touch, Autorun or Parallel.
- **Autorun vs Parallel:** Parallel events let the player keep moving while they run,
  whereas Autorun suspends normal control. That makes Autorun the usual choice for
  cutscenes [S36][S37].
- **Analysis:**
  - **Strengths:** the gold standard for **genre completeness**. It ships with an RPG
    database and an event-command list that maps directly to cutscenes and story beats.
  - **Weaknesses:** it is locked to tile-based 2D JRPGs, and going beyond the defaults
    depends on JavaScript plugins. Plugins live in the project's `js/plugins` folder and are
    managed in a Plugin Manager [S90].

### 3.5 RPG Maker Unite
- **Release:** early access on the Unity Asset Store from 27 April 2023, at US$99.99 [S38].
- **Features** [S38]:
  - Events are built without code by combining presets and command variables.
  - An **Outline Editor** uses "switch lines" to show visually how events and maps relate.
  - Full HD resolution, a first for the Maker series.
  - Functionality can be extended with add-ons.
- **Availability:** the Steam store page now says it is no longer available on Steam [S39].
- Community sentiment in a Steam forum thread was mixed. One poster pointed to bad reviews;
  another said first-time users were mainly struggling with Unity itself [S86]. This is
  anecdotal.
- **Analysis:** the lesson is that a no-code layer hosted inside a professional engine's
  editor makes non-coders learn *both*. Our editor must be the whole environment.

### 3.6 Pixel Game Maker MV → ACTION GAME MAKER
- **Pixel Game Maker MV** [S42][S89]:
  - A 2D action maker with flowchart-style visual scripting and no programming required.
  - Features can be added in JavaScript.
  - The first Maker title with physics.
  - Local play for up to four people, versus or co-op.
  - Fully released on Steam in September 2019, after early access that began in 2018.
- **ACTION GAME MAKER (16 June 2025)** [S40][S41]:
  - Its successor in the same series: Pixel Game Maker MV's Japanese title was *Action Game
    Maker MV* [S87].
  - Built on **Godot**. Its Steam page now advertises Godot Engine 4.7.0 [S41].
  - Visual scripting means "laying out a character's states as boxes and connecting them
    with arrows", which is a state-machine model. The page calls it "the greatest strength
    of the ACTION GAME MAKER series" [S41].
  - No programming is required, but custom visual-scripting actions and conditions can be
    written in GDScript [S41].
- **Analysis:** state-machine-style logic suits action characters well. Moving from a
  proprietary runtime to Godot shows the "proven engine underneath, no-code layer on top"
  pattern.

### 3.7 RPG in a Box
- A Godot-based standalone tool for **voxel 3D** RPGs and adventures. It targets users with
  no programming or modelling knowledge [S43].
- **Logic:** a node-based visual script editor, plus **Bauxite**, a custom Lua-like
  language [S43].
- **Export:** Windows, macOS and Linux [S43].
- **Analysis:** the closest existing example of "model the world, script it and ship it in
  one app" for 3D. Its look is voxel, not HD-2D.

### 3.8 SMILE GAME BUILDER
- **What it is:** an RPG maker for Windows with **3D maps** and no programming required,
  released in 2016 [S44].
- **Events:** built as flowcharts or as lines [S44].
- **Assets:** more than 1,000 bundled materials [S44].
- **Analysis:** proof that 3D map building can be made accessible to non-modellers. Users
  describe it as less versatile than RPG Maker.

## 4. Comparison matrix (Analysis)

Ratings: **Strong** = meets the need out of the box. **Partial** = possible with significant
effort or with limits. **Weak** = not really supported. **?** = not verified during this
research.

| Tool | C1 HD-2D readiness | C2 No-code depth | C3 Pixel authoring | C4 Genre breadth | C5 Full pipeline | C6 Cost model |
|---|---|---|---|---|---|---|
| Unreal Engine | Strong (needs tech art) | Weak (Blueprints is coding) | Weak | Strong | Partial | Royalty above US$1M |
| Unity 6 | Partial (custom pipeline for 3D) | Weak | Weak | Strong | Partial | Free under US$200k, then per seat |
| Godot 4.x | Partial | Weak (no official visual scripting) | Weak | Strong | Partial | Free (MIT) |
| GameMaker | Weak | Partial (GML Visual) | ? | Partial (2D) | Partial | One-time fee; GMRT free from 2027 |
| GDevelop | Weak | Strong | ? | Partial | Partial | Free (MIT) |
| Construct 3 | Weak | Strong | ? | Partial (2D-first) | Partial (desktop on paid plans) | Subscription |
| RPG Maker MZ | Weak | Strong (for JRPGs) | ? | Weak (JRPG) | Partial | One-time |
| RPG Maker Unite | Weak | Partial | ? | Weak (JRPG) | Partial | One-time, plus Unity |
| ACTION GAME MAKER | Weak | Strong (2D action) | ? | Weak (2D action) | Partial | One-time |
| RPG in a Box | Weak (voxel look) | Partial | Partial (voxels) | Weak | Partial | One-time |
| SMILE GAME BUILDER | Weak | Partial | Weak | Weak (RPG) | Partial | One-time |

## 5. What shipped pixel games had to build themselves

| Game | Engine | What the team had to build | Source |
|---|---|---|---|
| Octopath Traveler | UE4 | The HD-2D look, with a small team of up to six programmers relying on UE4's editors | [S2] |
| Sea of Stars | Unity | A custom render pipeline for fully dynamic lighting; one engineer, about five years | [S14][S15] |
| Eiyuden Chronicle: Hundred Heroes | Unity | 2D sprites composed over 3D backgrounds | [S16] |
| Dead Cells | Heaps engine, Haxe language [S84] | A private tool that converts 3D animation to pixel art; 3ds Max → FBX → batch conversion | [S17][S18] |

## 6. Findings (Analysis)

1. **The gap.** No tool reviewed combines all four of:
   - HD-2D-grade rendering out of the box,
   - deep no-code logic beyond a single genre,
   - integrated pixel-art and 3D-pixel environment authoring, and
   - a complete pipeline to a shipped Windows game.

   Professional engines cover the first; makers cover the second. Nobody covers all four.
   This is the product opportunity.
2. **High-level beats general.** The no-code tools that succeed are opinionated: genre
   databases, behaviors, presets and event commands. The tool that tried generic,
   low-level visual scripting (Godot's VisualScript) failed for exactly that reason [S23].
3. **"Engine underneath, no-code on top" works,** provided the no-code layer *is* the
   product and users never have to drop into the host engine's UI. RPG Maker Unite (Unity),
   ACTION GAME MAKER (Godot) and RPG in a Box (Godot) all follow this pattern.
4. **The look is the hard part, even for professionals.** The Sea of Stars and Dead Cells
   pipelines cost years of specialist time. A turnkey pipeline is the core value we offer.
5. **Every successful no-code tool keeps an escape hatch:**
   - JavaScript in GDevelop [S32], Pixel Game Maker MV [S42] and RPG Maker [S90].
   - GDScript in ACTION GAME MAKER [S41].
   - Bauxite in RPG in a Box [S43].

   We should keep one too, but never *require* it.

## 7. Borrow and avoid

| From | Borrow | Avoid |
|---|---|---|
| Unreal | Viewport quality, context-sensitive node search, cinematic depth-of-field controls | Exposing programming concepts (types, casting, references) to non-coders |
| Unity | The 2D-light model with normal and mask maps; per-sprite Shadow Caster 2D; Pixel Perfect Camera | Fragmentation across many packages and render pipelines |
| Godot | Scene and prefab composition, open text formats, AccessKit-style accessibility | Generic low-level visual scripting without high-level building blocks |
| GDevelop | Conditions, actions, sub-events and object picking; behaviors and extensions; AI as a builder | Running every rule every frame by default without making it visible |
| Construct | Event-sheet ergonomics and behavior presets | Event-count limits that block learning |
| RPG Maker MZ | Event pages with conditions and triggers; command lists for cutscenes; database forms | Genre lock; needing plugins for basic features |
| RPG Maker Unite | An Outline Editor that visualizes how events and maps connect | Making users learn a host engine's editor |
| ACTION GAME MAKER | State-machine logic for action characters | Restricting the tool to one genre |
| RPG in a Box | World modelling, logic and export in one app | A voxel-only look |
| SMILE GAME BUILDER | 3D maps without any modelling skill | Limited versatility |
