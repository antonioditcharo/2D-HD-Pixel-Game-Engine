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
| Engine and editor | Not started. Next: the reflection registry and the Phase 0 prototypes ([`docs/plan/08`](docs/plan/08-roadmap.md)) |

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
