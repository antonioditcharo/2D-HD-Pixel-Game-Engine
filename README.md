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
| Engine | Started. The reflection registry: settings, choice lists and the no-code building parts (actions, conditions, values, events) ([`docs/dev/reflection.md`](docs/dev/reflection.md)). The logic runtime: a sandboxed Luau VM with waits, events and budgets, and the compiler from no-code Rules and Sequences to Luau, with a block debug map and live tracing ([`docs/dev/logic-runtime.md`](docs/dev/logic-runtime.md)). The world: entities with GUIDs, a hierarchy and reflected components, on flecs ([`docs/dev/world.md`](docs/dev/world.md)). JSON serialization for objects, logic assets and scenes ([`docs/dev/serialization.md`](docs/dev/serialization.md)). The first renderer step: a GPU device on the SDL3 GPU API and batched pixel-exact sprites, tested on a software GPU ([`docs/dev/rendering.md`](docs/dev/rendering.md)). The asset database: GUIDs in `.meta` files that survive renames and moves, change detection, and PNG import ([`docs/dev/assets.md`](docs/dev/assets.md)). The first end-to-end pipeline: a scene of sprites and tilemaps from PNG assets, with sprite-sheet animation, rendered pixel-perfect, with hot reload ([`docs/dev/scene2d.md`](docs/dev/scene2d.md)). The Diorama style: the same scene in 3D, with billboarded sprites, block terrain, lighting, depth and fog ([`docs/dev/rendering.md`](docs/dev/rendering.md) §7). Input as named actions with replay, a fixed-step clock, and pixel-perfect windows ([`docs/dev/input.md`](docs/dev/input.md)). The first playable loop: the game runtime opens a project folder and runs it, with a hero who walks, turns and animates, bumps into walls and people, talks to them and walks through doorways; Rules reacting to all of it; and saved games in slots ([`docs/dev/game.md`](docs/dev/game.md)). Sound effects and music with fades ([`docs/dev/audio.md`](docs/dev/audio.md)). Text in a built-in pixel font, and message boxes with questions that people speak in ([`docs/dev/ui.md`](docs/dev/ui.md)). Exporting a project as a game folder that plays on its own ([`docs/dev/export.md`](docs/dev/export.md)). Physics on Box2D: crates to push, things that fall and bounce ([`docs/dev/physics.md`](docs/dev/physics.md)). Next: the rest of the Phase 0 prototypes ([`docs/plan/08`](docs/plan/08-roadmap.md)) |
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

## Try it

After building, play the sample village (arrow keys to walk, Z to talk to the elder):

```bat
build\windows-msvc\player\sparxel-player.exe samples\hello
```

Set `"render_style"` to `"diorama"` in `samples/hello/project.sparxel.json` to see it in
3D. See [`docs/dev/player.md`](docs/dev/player.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

> Note: "HD-2D" is a trademark of Square Enix. This project describes a similar *art
> style* and is not affiliated with Square Enix. See
> [`docs/plan/09-risks-quality-legal.md`](docs/plan/09-risks-quality-legal.md) (R-06).
