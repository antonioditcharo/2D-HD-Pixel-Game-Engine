# Exporting games

> **Status: v0.1.** A project exports to a folder that plays the game when its executable
> is opened, with no Sparxel installed. Before exporting, the project is checked by
> opening it as the player would. Exporting the sample village and playing the result,
> from another folder with the project moved away, is a test that runs on every platform
> in CI.
>
> Code: [`editor/exporter/include/editor/exporter/exporter.h`](../../editor/exporter/include/editor/exporter/exporter.h),
> [`src/exporter.cpp`](../../editor/exporter/src/exporter.cpp), and the `sparxel-export`
> command in [`src/main.cpp`](../../editor/exporter/src/main.cpp). Tests:
> [`tests/exporter/`](../../tests/exporter/).

## 1. What it is for

A creator finishes a game and wants to give it to players. `plan/03` §9 sets the rule:
**no compiler on the creator's machine**. The player is prebuilt, so exporting is
checking and copying. This is the first piece of editor-side code; the editor's Export
button will call `editor::export_game()`.

## 2. Using it

```sh
sparxel-export samples/hello out/hello
out/hello/"Hello Sparxel"          # on Windows: out\hello\Hello Sparxel.exe
```

`--player path` ships another player than the one the build made. In code:

```cpp
editor::ExportOptions options{project_folder, player_executable, output_folder};
std::vector<engine::serialize::Issue> issues;
std::optional<editor::ExportReport> report = editor::export_game(options, issues);
```

## 3. What it does

1. **The output folder** must be new or empty, so nothing in it is overwritten, and not
   inside the project.
2. **The player** must exist, with `LICENSE.txt` and `THIRD_PARTY_NOTICES.md` beside it.
   The build puts them there. The engine's MIT licence, and those of the libraries in the
   player, require every copy to carry its notices, so exporting refuses without them.
3. **The project is checked** by opening it as the player would, without graphics or
   sound: the settings, the start scene, every asset and every Rule. **Any issue stops
   the export**, even one the player would only warn about, such as an unknown setting.
   A game that ships is a game with no known problems.
4. **Copying.** The folder that results:

| Path | What |
|---|---|
| `<Game Name>.exe` | The player, named after the game (no extension on Linux) |
| `*.dll` | The libraries beside the player, such as `SDL3.dll` on Windows |
| `LICENSE.txt`, `THIRD_PARTY_NOTICES.md` | The licence notices |
| `game/project.sparxel.json` | The project's settings |
| `game/assets/` | Every asset with its `.meta` file, which holds the GUID everything refers to. Hidden files and folders (names starting with a dot, such as `.git`) are left out, and so is everything outside `assets/`. Sprite Studio's `.dsprite` sources stay behind too: the game draws the sprite sheet saved beside each ([sprite-studio.md](sprite-studio.md)) |

- **The executable's name** is the game's name without the characters Windows does not
  allow (`< > : " / \ | ? *` and control characters), and without trailing dots and
  spaces. It is `game` if nothing is left or the name is reserved (`CON`, `NUL`, `COM1`,
  …). Names are UTF-8 throughout, so *Café Quest* gets `Café Quest.exe` (tested).
- **A failed copy removes what was written**, so a half-exported game is never left.
- **Started with no folder,** the player plays the `game` folder beside its executable
  ([player.md](player.md)), so opening the executable starts the game.

## 4. Tests

| Test | Checks |
|---|---|
| Names | Forbidden characters, trailing dots and spaces, control characters, UTF-8 kept, every reserved name (in any case), and look-alikes that are allowed (`COM0`, `Console`) |
| The sample | Exactly the expected files, and no others on disk; the player and an asset byte for byte; hidden files, a hidden folder, a `.dsprite` and a file outside `assets/` left out; the exported project opens with no issues |
| Beyond ASCII | A game called *Café Quest* exports to `Café Quest` plus the player's extension |
| Refusals | A non-empty output folder, left untouched; output inside the project; no player; a player without its licence files; a game that cannot start; an unknown setting; no output folder. Each writes nothing |
| `export_and_play` | CTest exports a copy of the sample with `sparxel-export`, moves the project away, and runs the exported game with no folder from another directory: it must print the welcome and report no problems. On Windows this also proves the DLLs went with it |

## 5. Next increments

In the order of `plan/03` §9:

1. **Logic as bytecode,** so exported games skip compiling and do not carry Rules as
   text.
2. **A content pack** (`content.pak`) of only the assets the game reaches, cooked
   (atlases, compressed audio).
3. **Branding:** the game's icon and version information in the executable, through
   Windows resource updating.
4. **Signing** with the creator's own certificate, a **zip** of the folder, and the
   optional Steam upload (ADR-014).
5. **Build profiles** (development, demo, release) and **reproducible** output: the same
   project at the same version gives the same content hash.
