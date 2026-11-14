# Contributing

Thank you for your interest. The project is at an early stage: the plan is written and the
build skeleton exists, but there is no engine yet. Read
[`docs/README.md`](docs/README.md) first to see where things are heading.

## Licence of contributions

The project is under the [MIT licence](LICENSE). By submitting a contribution you agree
that it is licensed under the same terms.

## Before you open a pull request

1. Build and run the tests: see [`docs/dev/building.md`](docs/dev/building.md).
2. Format C++ code with `clang-format` (version 18) and follow
   [`docs/dev/coding-standards.md`](docs/dev/coding-standards.md).
3. If you changed documentation, run `python3 tools/check_docs.py`.
4. Keep each pull request to one topic.

Continuous integration runs the same checks on every pull request.

## What not to commit

- Proprietary SDKs or assets you do not have the right to redistribute.
- Build output (`build/` is ignored).
- The term "HD-2D" as a product name. It is a Square Enix trademark; see risk R-06 in
  [`docs/plan/09-risks-quality-legal.md`](docs/plan/09-risks-quality-legal.md).

## Design changes

Decisions about architecture and technology are recorded as ADRs in
[`docs/plan/07-tech-stack-adrs.md`](docs/plan/07-tech-stack-adrs.md). Propose a change to
an ADR in a pull request before writing code that depends on it.
