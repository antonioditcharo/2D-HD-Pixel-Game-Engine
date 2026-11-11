# Building

The repository builds the engine modules under `engine/` (see
[repo-layout.md](repo-layout.md)) and one unit-test executable per module. Third-party
libraries come from vcpkg, as listed in `vcpkg.json`: today flecs, Luau, nlohmann/json and SDL3, plus SDL_shadercross as a build-time shader
compiler and, for tests, doctest. The renderer's tests need a GPU; on Linux without one,
install Mesa's software Vulkan driver (`sudo apt-get install mesa-vulkan-drivers`). See
[rendering.md](rendering.md) §6.

## What you need

| Tool | Version | Notes |
|---|---|---|
| A C++20 compiler | Visual Studio 2022 or newer with the "Desktop development with C++" workload | Windows is the only product target (ADR-001) |
| CMake | 3.25 or newer | |
| Ninja | any recent version | Included with Visual Studio's CMake tools |
| vcpkg | tag `2026.10.16` | Provides third-party libraries |
| Git | any recent version | |
| Qt | 6.4 or newer, Widgets | Only for the editor ([editor.md](editor.md)). Without it, everything else builds and the editor is skipped. On Windows, the Qt online installer or `python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64`; then pass its folder as `CMAKE_PREFIX_PATH` and put its `bin` on `PATH` to run the editor |

## 1. Get vcpkg (once)

The project pins vcpkg tag `2026.10.16`. Clone it anywhere outside this repository and
point `VCPKG_ROOT` at it.

```bat
git clone --branch 2026.10.16 https://github.com/microsoft/vcpkg C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
setx VCPKG_ROOT C:\dev\vcpkg
```

Open a new terminal afterwards so `VCPKG_ROOT` is set.

## 2. Build and test on Windows

Run these from an **"x64 Native Tools Command Prompt for VS"** (it puts the compiler on the
path), in the repository root:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

The first configure downloads and builds the dependencies listed in `vcpkg.json`, so it
takes longer than later runs.

`windows-clang-cl` is a second preset that builds with clang-cl for extra diagnostics. It
needs the "C++ Clang tools for Windows" component of Visual Studio, or LLVM on the path.

## Build presets

| Preset | Compiler | Purpose |
|---|---|---|
| `windows-msvc` | MSVC (`cl`) | The primary build |
| `windows-clang-cl` | clang-cl | Extra diagnostics |
| `linux-dev` | The system's default compiler | Development and CI convenience for platform-independent code. Not a product target |

Build output goes to `build/<preset>/`, which Git ignores.

## Linux (development only)

On Ubuntu, `sudo apt-get install qt6-base-dev qt6-base-dev-tools libgl-dev` gives the
editor Qt.

```sh
git clone --branch 2026.10.16 https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg

cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev
```

## Useful options

| Option | Effect |
|---|---|
| `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` | Treat warnings as errors, as CI does |
| `-DENGINE_BUILD_TESTS=OFF` | Skip the unit tests |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug build. The default is `RelWithDebInfo` |

Add them to the `cmake --preset <name>` line.

## Sanitizers (Linux)

A Debug build with AddressSanitizer and UndefinedBehaviorSanitizer catches memory and
undefined-behaviour bugs that tests alone miss. CI does not run it yet; run it before
larger changes:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -fno-sanitize-recover=all"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

- **`-fno-sanitize=vptr` is required.** The engine's runtime libraries build without
  RTTI (exported games stay smaller), while tests build with it. The `vptr` check needs
  RTTI on both sides: a template instantiated without RTTI (for example a `shared_ptr`
  control block inside nlohmann/json) has no type information, so the check reports a
  false "does not point to an object of type" error. Every other check stays on.
- For the GPU tests on lavapipe, preload the driver as
  [rendering.md](rendering.md) §6 explains.

## Checks that CI runs

| Check | Command |
|---|---|
| Build and tests | The three commands above, on Windows (MSVC and clang-cl) and Linux, with warnings as errors |
| Formatting | `clang-format --dry-run -Werror $(git ls-files '*.h' '*.cpp')` with clang-format 18 |
| Static analysis | `clang-tidy -p build/linux-dev $(git ls-files '*.cpp')` with clang-tidy 18 |
| Documentation | `python3 tools/check_docs.py` |

The workflow is [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).

## Troubleshooting

- **"Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake"**: `VCPKG_ROOT` is
  not set in this terminal.
- **"CMAKE_CXX_COMPILER not set" or `cl` not found on Windows**: you are not in a Visual
  Studio developer prompt.
- **A preset is "disabled"**: presets only appear on their own operating system.
