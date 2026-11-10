# Sparxel Game Engine

HD Pixel game engine with all the needed pipelines and systems built in, including sprite editing, animating, and all other workflows to make a pixel game.

**Sparxel** is a Windows-native, all-in-one, **no-code** engine for making HD-2D-style
pixel games, from first sketch to a shipped game on Steam, without writing code.

It is **open source under the [MIT licence](LICENSE)**. Games made with it are yours and
owe no fees or royalties.

## Status

**Early setup.** The research and the full plan are written, and the build skeleton is in
place. There is no engine to use yet.

| Area | State |
|---|---|
| Research and plan | Written: [`docs/`](docs/README.md) |
| Owner decisions (licence, scope, team) | Recorded: [`docs/plan/09`](docs/plan/09-risks-quality-legal.md) §5 |
| Product name | **Sparxel**, chosen after a preliminary screening ([`docs/research/05`](docs/research/05-name-screening.md) §7). Professional trademark clearance still needed |
| Build system, tests and CI | In place: [`docs/dev/building.md`](docs/dev/building.md) |
| Engine | Started. The reflection registry: settings, choice lists and the no-code building parts (actions, conditions, values, events) ([`docs/dev/reflection.md`](docs/dev/reflection.md)). The logic runtime: a sandboxed Luau VM with waits, events and budgets, and the compiler from no-code Rules and Sequences to Luau, with a block debug map and live tracing ([`docs/dev/logic-runtime.md`](docs/dev/logic-runtime.md)). The world: entities with GUIDs, a hierarchy and reflected components, on flecs ([`docs/dev/world.md`](docs/dev/world.md)). JSON serialization for objects, logic assets and scenes ([`docs/dev/serialization.md`](docs/dev/serialization.md)). The first renderer step: a GPU device on the SDL3 GPU API and batched pixel-exact sprites, tested on a software GPU ([`docs/dev/rendering.md`](docs/dev/rendering.md)). The asset database: GUIDs in `.meta` files that survive renames and moves, change detection, and PNG import ([`docs/dev/assets.md`](docs/dev/assets.md)). The first end-to-end pipeline: a scene of sprites and tilemaps from PNG assets, with sprite-sheet animation, rendered pixel-perfect, with hot reload ([`docs/dev/scene2d.md`](docs/dev/scene2d.md)). Input as named actions with replay, a fixed-step clock, and pixel-perfect windows ([`docs/dev/input.md`](docs/dev/input.md)). The first playable loop: the game runtime opens a project folder and runs it, with a walking hero, Rules reacting to the game and to buttons, and no-code logic acting on the world's entities ([`docs/dev/game.md`](docs/dev/game.md)). Next: the rest of the Phase 0 prototypes ([`docs/plan/08`](docs/plan/08-roadmap.md)) |
| Editor | Not started |

Start with the [executive summary](docs/README.md#executive-summary).

## Building

See [`docs/dev/building.md`](docs/dev/building.md). In short, on Windows from a Visual
Studio developer prompt, with `VCPKG_ROOT` set:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

> Note: "HD-2D" is a trademark of Square Enix. This project describes a similar *art
> style* and is not affiliated with Square Enix. See
> [`docs/plan/09-risks-quality-legal.md`](docs/plan/09-risks-quality-legal.md) (R-06).
