# Repository Layout

```
.
├── engine/            Runtime code. Everything here can ship inside an exported game.
│   └── foundation/    Lowest layer: version info and the reflection registry today;
│                      memory, jobs and logging later.
├── editor/            The editor application (not started). May use engine/, never the
│                      other way round.
├── tests/             Unit tests, one folder per engine module.
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
