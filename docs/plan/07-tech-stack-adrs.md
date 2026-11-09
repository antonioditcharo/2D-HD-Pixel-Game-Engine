# Plan 07: Technology Stack and Architecture Decision Records

> Each decision is recorded as a lightweight ADR covering context, decision, alternatives
> and consequences, plus a **validation** step that confirms or reverses it in Phase 0.
>
> Licences and versions were checked in November 2026 (`[S#]` in
> [`../sources.md`](../sources.md)). Re-verify licences before each release; this is not
> legal advice.

---

## ADR-000: Build a custom engine from proven libraries

- **Context:** the user asked for a *new custom engine*. Three realistic paths exist:
  - **(A)** A custom engine assembled from permissively licensed libraries.
  - **(B)** A product built on top of an open-source engine such as Godot (MIT). ACTION
    GAME MAKER (built on Godot) [S40][S41] and RPG in a Box [S43] took this path.
  - **(C)** A layer on top of Unity or Unreal, as RPG Maker Unite did on Unity [S38].
- **Decision: (A).** "Custom engine, not custom everything." Build in-house what makes the
  product unique:
  - the HD-2D renderer;
  - the no-code logic system and reflection;
  - the authoring tools;
  - the editor UX.

  Use proven libraries for everything else: platform, physics, audio, ECS, VM and text.
- **Why not (B)?** It is faster to a first release. Of the three, it is the only real
  alternative. The trade-offs:
  - The host engine's architecture would constrain the renderer, the editor and the
    reflection-driven no-code layer.
  - Engine upgrades become a recurring merge cost.
  - The work no longer matches the user's explicit request.
- **Why not (C)?**
  - Licensing and pricing depend on a third party.
  - Non-coders would face the host engine's editor (`research/04`, L8).
- **Consequences:** a longer path to 1.0, mitigated by library reuse and a vertical-slice
  plan (`08`).
- **Validation:** at the Phase 0 gate, compare actual spike velocity against the plan. If
  time-to-market becomes the overriding priority, revisit option (B) explicitly.

## ADR-001: Target platform and minimum specification

- **Decision:** the editor and exported games target **Windows 11 x64** as the primary
  platform. Windows 10 22H2 is best-effort.
- **Windows 10 status:** Microsoft ended Windows 10 support on 14 October 2025. Consumer
  Extended Security Updates run through 12 October 2027 [S82].
- **GPU:** a Direct3D 12-capable GPU is required. The exact minimum specification is chosen
  in Phase 0 by benchmarking the reference scene.
- **Proposed starting point for procuring reference hardware** (an assumption, to be
  validated):
  - a 4-core CPU;
  - 8 GB RAM for games and 16 GB for the editor;
  - a mid-range desktop GPU from around 2016–2017 for the "minimum" test machine;
  - a current mid-range GPU for the "recommended" machine.
- **Future platforms:** the RHI keeps a Vulkan path open (ADR-004) for Steam Deck and Linux
  after 1.0.

## ADR-002: Language and build tooling

- **Decision:**
  - **C++20** for the runtime and editor.
  - **MSVC** as the primary compiler, with **clang-cl** in CI for diagnostics.
  - **CMake + Ninja**, with **vcpkg** manifests for dependencies.
- **Rules:**
  - The runtime is built without RTTI. It uses explicit error results, not exceptions,
    in hot paths.
  - The editor may use exceptions at module boundaries.
  - Formatting and static analysis are enforced in CI.
- **Alternatives:**
  - Rust would offer memory safety. Our assessment is that its GUI options for a
    polished, accessible desktop editor are less mature than Qt, and integration with Qt
    is less direct.
  - C# for the editor would mean two runtimes and adds interop cost.

## ADR-003: Platform layer, SDL3

- **Decision:** SDL3 provides the window, input, gamepads, timing and the file system. It is
  **zlib-licensed** [S66] and actively maintained, with 3.4.x releases through November
  2026 [S65].
- **Alternatives:** raw Win32 plus XInput or GameInput. That gives more control but means
  far more code to write and maintain.

## ADR-004: Graphics abstraction (RHI)

- **Decision:** write a **thin in-house RHI**.
  - **Backend 1** uses the **SDL3 GPU API**. It is modelled on Metal, Vulkan and Direct3D
    12, offers `vulkan`, `direct3d12` and `metal` drivers [S62], and supports **compute
    pipelines** with read-only and read-write storage textures and buffers [S63].
  - **Backend 2**, a native **Direct3D 12** backend, is built only if Phase 0 finds
    features we need missing from Backend 1, or a performance gap.
- **Why:** fastest path to a working renderer, with Vulkan available "for free" later, and
  no lock-in thanks to our own RHI interface.
- **Validation spike (Phase 0):** implement the reference HD-2D scene (`04` §4) on the SDL
  GPU backend and check:
  - compute-based light clustering;
  - shadow atlas and CSM;
  - depth-of-field and bloom chains;
  - GPU timing and profiling needs;
  - pipeline caching;
  - frame time against the `04` §11 budgets.

  Record the outcome in this ADR.

## ADR-005: Editor UI framework

- **Decision:** **Qt 6 Widgets** with the **Qt Advanced Docking System**, and
  engine-rendered viewports embedded through native window handles.
  - Qt is available under **LGPLv3**. Some modules are **GPLv3-only**, including Qt Graphs,
    Qt Quick 3D, Qt Quick Timeline, Qt Virtual Keyboard, Qt HTTP Server, Qt Network
    Authorization and Qt Qml Compiler [S74]. **We do not use GPL-only modules.**
  - Qt exposes the UI's semantic structure to platform assistive tools for both Widgets and
    Quick [S75].
  - Qt Advanced Docking System is **LGPL-2.1**, supports Qt 6, and is used by Qt Creator
    and Qt Design Studio [S76].
- **Alternatives considered:**

  | Option | Verdict |
  |---|---|
  | **Dear ImGui** | **Rejected for the end-user editor.** Its README says it is not aimed at "UI for the average end-user" and lacks accessibility and full internationalization [S73]. Still allowed for internal debug overlays. |
  | **Custom engine-rendered UI with AccessKit** (Godot's path [S24]) | Full control and dogfooding, but a very large cost. Godot's own accessibility is still partial. Deferred. |
  | **WebView2 with a web UI** | Strong UI ecosystem and accessibility. The Evergreen runtime is preinstalled on Windows 11 but not shipped with Windows 10 [S81]. Costs: native-viewport layering ("airspace"), a two-language stack and IPC latency. **Runner-up**, compared in the Phase 0 spike. |

- **Consequences:**
  - LGPL compliance: dynamic linking, licence notices, and allowing relinking.
  - Qt lives **only in the editor**. Exported games never contain Qt.
  - Custom widgets (logic editors, timelines, graphs) must implement accessibility
    interfaces.
- **Validation spike (Phase 0):** build the shell, a dockable layout, an embedded D3D12
  viewport, an inspector generated from reflection, and a node and timeline widget, in both
  Qt and WebView2. Score each on frame pacing, input latency, accessibility,
  implementation speed and polish. Record the decision here.

## ADR-006: World model, flecs ECS

- **Decision:** **flecs** (MIT) [S70]. It provides:
  - entity relationships;
  - native prefabs and hierarchies;
  - a reflection framework with a JSON serializer;
  - a multi-threaded scheduler;
  - C99 and C++17 APIs;
  - a web-based explorer for debugging.

  Prefabs, relationships and reflection map directly onto Diorama's prefabs, links and
  generated inspectors.
- **Alternatives:** EnTT, a widely used C++ ECS that is a viable option but was not evaluated
  in depth here, and a custom ECS (cost). flecs is preferred because its documented
  built-ins (relationships, prefabs, JSON reflection [S70]) map directly onto features we
  need.
- **Note:** flecs reflection *feeds* the engine's own reflection registry (`03` §1.2). It
  does not replace it, because editor metadata (tooltips, units, icons) is ours.

## ADR-007: Logic VM, Luau

- **Decision:** **Luau** (MIT) [S71] is the execution target for all visual logic and the
  optional scripting escape hatch.
- **Why Luau:**
  - **Sandboxing:** per-script isolated globals and protected built-ins.
  - **Gradual typing,** which matches our typed blocks.
  - **Performance:** a heavily optimized VM derived from Lua 5.1.
  - **Coroutines** from its Lua heritage, which power Sequences.
  - **Embeddability:** the compiler is separate from the VM.
  - **Proven** in Roblox and shipped titles [S71].
- **Alternatives:**
  - A custom bytecode VM: full control, but cost and risk.
  - .NET/C# hosting: heavy, and sandboxing is hard.
  - JavaScript engines: not evaluated in depth.
- **Validation spike (Phase 0):**
  - compile a Rules, Sequence and Brain sample through Logic IR to Luau;
  - map blocks for highlighting;
  - enforce per-frame budgets via interrupts;
  - measure a stress scene of 5,000 rules and 200 concurrent sequences.

## ADR-008: Physics, Jolt (3D) and Box2D v3 (2D)

- **Decision:** one **Collision facade** with two backends.
  - **Jolt Physics** (MIT) [S69] for diorama maps. It is deterministic, multi-core
    friendly, offers *virtual* and rigid-body character controllers, and is used in Horizon
    Forbidden West and Death Stranding 2.
  - **Box2D v3** (MIT) [S67] for Classic 2D and side-view maps. It offers continuous
    collision, a character mover, cross-platform determinism and snapshot/restore. v3.1
    (April 2025) roughly doubled continuous-collision performance and added the character
    mover [S68].
- **Grid-based movement** in RPG and Tactics kits runs on the kits' own grid logic, not on
  physics.

## ADR-009: Audio, miniaudio

- **Decision:** **miniaudio**, public domain or MIT-0 [S72]. It is a single-file library
  with a WASAPI backend, a node graph for mixing and effects, and optional 3D
  spatialization. It decodes WAV, FLAC and MP3. Vorbis/OGG needs an added, permissively
  licensed decoder, to be selected later.
- **Diorama builds on top of it:** buses, snapshots, ducking, adaptive music, zones and
  footstep surfaces.
- **Alternatives:** FMOD or Wwise as the core engine. Those add licence tiers (Wwise is
  free under a US$250K budget [S55]) and an external authoring tool. They are planned as
  optional plugins after 1.0.

## ADR-010: Text and fonts

- **Decision:**
  - **FreeType** under its FreeType License: BSD-style with a credit clause [S80].
  - **HarfBuzz** under the "Old MIT" licence [S79].
  - First-class **bitmap pixel fonts**.
- **Obligation:** FreeType's credit clause. Diorama auto-adds a credits entry in exported
  games' credits template and documentation.

## ADR-011: Project and asset formats

- **Decision:**
  - JSON text project files with stable GUIDs and schema versions (`03` §4).
  - Open interchange: **glTF 2.0** for 3D, **PNG**, **`.aseprite` import** via the public
    spec [S46], **LDtk** import [S53], and WAV, FLAC and MP3 audio [S72].
  - A cooked binary pack (zstd) for shipping.
- **Why:** diffable, mergeable and future-proof. Users own their data (pillar 6).

## ADR-012: Versioning, libgit2 snapshots

- **Decision:** snapshots and optional Git use **libgit2**. It is GPLv2 *with a linking
  exception*, so it can be linked from any program, but modifications to libgit2 itself
  must be published [S77].
- **Open item:** large binary handling (Git LFS or an alternative) needs evaluation.
  Phase 3 spike.

## ADR-013: Diagnostics and profiling

- **Decision:**
  - **Tracy** (BSD-3) [S78] for engine developers.
  - A friendly in-game performance overlay for creators (N5).
  - Windows minidumps for crash capture, with opt-in upload.
  - Structured logging with categories.

## ADR-014: Platform services, Steamworks behind an interface

- **Decision:** Steamworks for achievements, stats, leaderboards, Cloud, overlay, rich
  presence, Steam Input and Workshop [S58]. All calls go through a `PlatformServices`
  interface, so games run without Steam (DRM-free builds) and other stores can be added
  later.
- The Steamworks SDK is required only to upload content. Everything else is optional [S58].

## ADR-015: Shader pipeline

- **Decision:**
  - **HLSL** as the single shader language.
  - Offline compilation, with **SDL_shadercross** translating for the SDL GPU backends
    [S64]. It accepts HLSL or SPIR-V and outputs for every backend the API implements.
    Compiling HLSL at runtime would need the DXC libraries [S64]; we compile offline.
  - Pipeline caches are pre-warmed during export.

## ADR-016: Testing and continuous integration

- **Decision:**
  - Unit tests and integration tests (headless runtime).
  - **Golden-image** rendering tests and **performance** tests on dedicated GPU-equipped
    reference machines (self-hosted runners).
  - Importer fuzzing.
  - Save-compatibility tests.
  - Usability tests at each phase gate (`09` §3).
  - CI builds every commit for Windows. Nightly runs cover the full rendering and
    performance suite.

---

## Dependency licence summary

| Component | Licence (as researched) | Ships in exported games? | Main obligation |
|---|---|---|---|
| SDL3 | zlib [S66] | Yes | Do not misrepresent origin; keep the notice in the source |
| Luau | MIT [S71] | Yes | Include the licence notice |
| flecs | MIT [S70] | Yes | Include the licence notice |
| Jolt Physics | MIT [S69] | Yes | Include the licence notice |
| Box2D v3 | MIT [S67] | Yes | Include the licence notice |
| miniaudio | Public domain or MIT-0 [S72] | Yes | None |
| FreeType | FTL (BSD-style with credit) or GPLv2 [S80]; we use FTL | Yes | Credit in documentation |
| HarfBuzz | "Old MIT" [S79] | Yes | Include the licence notice |
| Steamworks SDK | Valve SDK terms (not reviewed here) | Optional | Steamworks agreement |
| Qt 6 (non-GPL modules) | LGPLv3 [S74] | **No (editor only)** | Dynamic linking, notices, allow relinking |
| Qt Advanced Docking System | LGPL-2.1 [S76] | **No (editor only)** | As above |
| libgit2 | GPLv2 with linking exception [S77] | **No (editor only)** | Publish modifications to libgit2 itself |
| Tracy | BSD-3 [S78] | Dev builds only | Include the licence notice |
| Dear ImGui | MIT [S73] | Debug overlays only | Include the licence notice |

The export pipeline automatically generates a `THIRD_PARTY_NOTICES.txt` for every exported
game, containing exactly the components that game includes.
