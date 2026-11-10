# Building

The repository builds the engine modules under `engine/` (see
[repo-layout.md](repo-layout.md)) and one unit-test executable per module. Third-party
libraries come from vcpkg, as listed in `vcpkg.json`: today Luau and nlohmann/json and, for tests, doctest.

## What you need

| Tool | Version | Notes |
|---|---|---|
| A C++20 compiler | Visual Studio 2022 or newer with the "Desktop development with C++" workload | Windows is the only product target (ADR-001) |
| CMake | 3.25 or newer | |
| Ninja | any recent version | Included with Visual Studio's CMake tools |
| vcpkg | tag `2026.10.16` | Provides third-party libraries |
| Git | any recent version | |

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
