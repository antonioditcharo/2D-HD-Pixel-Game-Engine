# third_party/

Third-party code that cannot come from vcpkg is placed here, one folder per library, each
with its licence file.

Rules:

- Prefer a vcpkg dependency in [`vcpkg.json`](../vcpkg.json). Vendor code here only when
  vcpkg cannot provide it.
- Every dependency must be listed in the licence table in
  [`docs/plan/07-tech-stack-adrs.md`](../docs/plan/07-tech-stack-adrs.md).
- **Never commit proprietary SDKs** such as the Steamworks SDK. Their licences do not allow
  redistribution. Optional modules that need them read the SDK from a path the user
  supplies (ADR-014).

Nothing is vendored here today: every dependency comes from vcpkg
([`vcpkg.json`](../vcpkg.json)). Their licences are in the table in
[`docs/plan/07-tech-stack-adrs.md`](../docs/plan/07-tech-stack-adrs.md), and the notices
games ship in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
