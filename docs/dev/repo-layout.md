# Repository Layout

```
.
├── engine/            Runtime code. Everything here can ship inside an exported game.
│   ├── foundation/    Lowest layer: version info, GUIDs and the reflection registry today;
│   │                  memory, jobs and logging later.
│   ├── logic/         The logic runtime: the Logic IR, its compiler, and the sandboxed
│   │                  Luau VM that runs no-code logic.
│   ├── world/         Entities, GUIDs, the hierarchy and components (flecs inside).
│   ├── serialize/     Reflected objects, logic assets and scenes to and from JSON.
│   ├── render/        The GPU device, render targets and sprites (SDL3 GPU inside);
│   │                  HLSL shaders in render/shaders/.
│   ├── assets/        The asset database (GUIDs in .meta files) and importers.
│   ├── scene2d/       Classic 2D scenes: transforms and sprites drawn from the world.
│   ├── input/         Named actions bound to keys and gamepad buttons; recording and replay.
│   ├── audio/         Sound effects and music: buses, fades, decoding (miniaudio inside).
│   ├── physics/       Rigid bodies and contacts (Box2D inside).
│   ├── ui/            Text: the built-in pixel font, measuring, wrapping and drawing.
│   └── game/          The runtime: opens a project and runs it; logic reaching the world; the clock.
├── player/            sparxel-player: plays a project folder; what exported games run.
├── samples/           Sample projects (hello: a small village), made by tools/make_sample.py.
├── editor/            Editor-side code. May use engine/, never the other way round.
│   ├── core/          The editor's model: documents, undo, picking, preview (no Qt).
│   ├── app/           sparxel-editor, the Qt 6 application.
│   └── exporter/      Exporting games, and the sparxel-export command.
├── tests/             Unit tests, one folder per engine module; shared helpers in
│                      tests/support/.
├── tools/             Development scripts. Never shipped.
├── third_party/       Vendored third-party code, only when vcpkg cannot provide it.
├── cmake/             CMake helper modules.
├── docs/
│   ├── research/      Analysis of existing engines, tools and techniques.
│   ├── plan/          What will be built and how.
│   ├── dev/           How to build and contribute (this folder).
│   └── sources.md     Every source cited by the research and plan.
├── .github/workflows/ Continuous integration.
├── CMakeLists.txt     Root build file.
├── CMakePresets.json  Named build configurations.
└── vcpkg.json         Third-party dependencies and the pinned vcpkg version.
```

## Rules

1. **`engine/` never depends on `editor/`.** Exported games contain no editor code and no
   Qt (see `plan/03` §1).
2. **One module, one folder, one CMake target.** A module `engine/<name>/` builds the
   target `engine_<name>` and is used through the alias `engine::<name>`.
3. **Public headers live under `include/engine/<name>/`.** Code includes them as
   `"engine/<name>/<file>.h"`. Everything under `src/` is private to the module.
4. **Tests mirror modules:** `tests/<name>/`.
5. **Names stay neutral.** Targets and namespaces use `engine`, not the product name
   Sparxel. The name has not been professionally cleared yet (`plan/09` §5, Q-09), so the
   code should not depend on it. User-facing files may use it, for example the project file
   `project.sparxel.json` (`plan/03` §4).
