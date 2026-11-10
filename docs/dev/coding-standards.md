# Coding Standards

These rules apply to all C++ code in the repository. They implement ADR-002 in
[`../plan/07-tech-stack-adrs.md`](../plan/07-tech-stack-adrs.md). The tools enforce most of
them, so in practice: run the formatter, fix what the compiler and `clang-tidy` report.

## Language

- **C++20.** No compiler extensions.
- **Runtime code (`engine/`) is built without RTTI.** Do not use `dynamic_cast` or
  `typeid` there.
- **Errors in runtime code are returned, not thrown,** on hot paths. Exceptions are
  allowed at module boundaries in the editor.
- **No raw owning pointers.** Use values, `std::unique_ptr`, or handles.
- Mark functions `[[nodiscard]]` when ignoring the result is a bug, and `noexcept` when
  they cannot fail.

## Naming

| Thing | Style | Example |
|---|---|---|
| Namespaces | `lower_case` | `engine` |
| Types (classes, structs, enums) | `CamelCase` | `Version` |
| Enumerators | `CamelCase` | `ValueKind::Float` |
| Constants (`inline constexpr` values) | `lower_case` | `property_flag::read_only` |
| Functions | `lower_case` | `version_string()` |
| Variables and parameters | `lower_case` | `frame_count` |
| Private member variables | `lower_case_` with a trailing underscore | `size_` |
| Macros | `UPPER_CASE` with the `ENGINE_` prefix | `ENGINE_VERSION_MAJOR` |
| Files | `lower_case.h`, `lower_case.cpp` | `version.h` |

`clang-tidy` checks these names.

## Formatting

- Formatting is defined by [`.clang-format`](../../.clang-format): 4 spaces, 100 columns.
- Format before committing:
  `clang-format -i $(git ls-files '*.h' '*.cpp')`
- CI uses **clang-format 18**. Other versions can format differently.

## Includes

Order, with a blank line between groups (the formatter sorts them):

1. The file's own header first, in a `.cpp` file.
2. Engine headers: `"engine/..."`.
3. Third-party headers.
4. Standard library headers.

Use `#pragma once` in every header.

## Warnings and static analysis

- Warning flags are set in [`cmake/CompilerWarnings.cmake`](../../cmake/CompilerWarnings.cmake).
- CI treats warnings as errors on every compiler. Locally they stay warnings unless you add
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`.
- CI runs `clang-tidy` with the rules in [`.clang-tidy`](../../.clang-tidy).

## Tests

- Tests use [doctest](https://github.com/doctest/doctest).
- Every module has tests under `tests/<module>/`. New behavior comes with a test.
- Test names say what is true, for example
  `"version_string() is major.minor.patch"`.

## Comments

- Public declarations get a short `///` comment that says what the thing does.
- Comment *why*, not *what*, inside functions.
