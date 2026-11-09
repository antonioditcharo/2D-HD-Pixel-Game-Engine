# Plan 09: Risks, Quality Strategy, Legal and Open Questions

---

## 1. Risk register

Likelihood (L) and impact (I) are rated High, Medium or Low. The register is reviewed at
every phase gate.

| ID | Risk | L | I | Mitigation | Early warning sign |
|---|---|---|---|---|---|
| R-01 | **Scope explosion.** "All the systems users will ever need" has no natural boundary. | H | H | Genre-focused kits (`01` §4.1); α/1.0/Post tags (`02`); phase gates (`08`); extensibility for everything else. | Gate criteria slipping; features added without removing others |
| R-02 | **No-code ceiling.** Users hit things they cannot express. | M | H | Layered model, Custom Blocks and a Luau escape hatch (`05`); usability rounds every gate; a no-script vertical slice as proof. | Testers asking "how do I code this?"; kits needing Script blocks |
| R-03 | **The HD-2D look is hard to tune.** Even professionals invest years in it (Sea of Stars [S15]). | M | H | A dedicated rendering engineer; a technical artist owns Look presets; golden-image tests; Phase 0 reference scene. | Reference scene not reaching the target look or budget by Gate 0 |
| R-04 | **Editor UX complexity** overwhelms non-coders. | M | H | A UX researcher on the core team; Basic/Advanced split; glossary (`06` §9); measured SUS and task completion. | SUS < 68 at Gate 2 |
| R-05 | **Dependency risk:** SDL GPU API feature gaps, Qt licence compliance, library abandonment. | M | M | Own RHI with a D3D12 fallback (ADR-004); no GPL-only Qt modules (ADR-005); only mature, actively maintained libraries; vendored versions. | Spike R finds missing features; upstream inactivity |
| R-06 | **Trademark:** "HD-2D" is a Square Enix trademark [S11][S12][S13]. The repository name "2D-HD" is close to it. | M | H | Never use "HD-2D" in product name or marketing; describe the style generically ("pixel diorama", "2.5D pixel art"); professional trademark search before naming. | Any marketing copy using the term |
| R-07 | **IP similarity in templates and kits:** copying specific games' assets, characters or signature mechanics. | L | H | All template art and audio is original; battle systems are configurable and generic; legal review of templates before release. | Template reviews flag resemblance |
| R-08 | **Data loss** in user projects. | L | H | Command journal, autosave, crash recovery, snapshots, atomic writes, format migrations with backups (`06` §5). | Any data-loss report |
| R-09 | **Texel and pixel inconsistency** makes user games look "off". | M | M | PPU enforcement, texel-density view, Project Doctor warnings (R-VIS-07). | Template or user scenes failing the density view |
| R-10 | **Performance cliffs** in user-made content: too many lights, continuous rules. | M | M | Budgets, plain-language performance overlay, Project Doctor lints, quality tiers. | Alpha projects under 60 fps on minimum spec |
| R-11 | **Funding and team** can't sustain a multi-year effort. | M | H | Phased plan with shippable milestones (free demo at alpha, Early Access at beta); solo or small-team scope cut (`08` §6). | Gates slipping by more than 50% |
| R-12 | **Platform shifts:** Windows 10 support ended 14 October 2025 [S82]. | L | L | Windows 11 primary; Windows 10 best-effort only. | Testers on Windows 10 reporting issues |
| R-13 | **AI feature backlash or liability.** | M | M | Opt-in, transparent, edits via undoable commands, no generative art at 1.0 (`06` §8). | Community feedback |
| R-14 | **Security:** malicious projects or mods. | L | M | Luau sandbox, no native code in projects or mods, fuzzed importers, no network access from user logic. | Fuzzing findings |

## 2. Quality strategy

| Layer | What | When |
|---|---|---|
| Unit tests | Foundation, reflection, serialization, logic compiler, formulas | Every commit |
| Integration tests | Headless runtime: load scenes, run sequences, save/load round-trips | Every commit |
| **Golden-image tests** | Reference scenes per Render Style and Look, compared within perceptual tolerances | Nightly and on renderer changes |
| **Performance tests** | Reference scenes on minimum and recommended hardware; per-pass budgets (`04` §11); editor timings (`06` §7) | Nightly |
| Determinism and replay | Recorded inputs replayed, final state hashes compared | Nightly (after N6 exists) |
| Importer fuzzing | `.aseprite`, PNG, glTF, LDtk, audio, project JSON | Continuous (nightly corpus runs) |
| Save compatibility | Saves from every previous release load in the current build | Every release candidate |
| Project format migrations | Projects from every previous release open with no data loss | Every release candidate |
| **Usability tests** | §3 protocol | Every phase gate |
| Dogfooding | The vertical slice and template games are built only in the editor | Continuous from Phase 3 |
| Crash telemetry | Opt-in crash and stability reports from alpha and beta | Continuous from alpha |

## 3. Usability-testing protocol (the "easy as hell" check)

1. **Participants:** 5–8 per round. All non-programmers. A mix of the Storyteller,
   Pixel Artist and Hobbyist personas (`01` §2). New participants each round, plus 2
   returning participants to measure learning.
2. **Format:** moderated think-aloud sessions, 60–90 minutes, screen recorded. Moderators
   do not help unless the participant has been stuck for more than 3 minutes. Each such
   intervention is counted as a "programmer explanation" if it involved logic concepts.
3. **Tasks:** the standard set in `05` §10, plus workspace-specific tasks (paint a sprite,
   build a house, set a Look, build and run the game).
4. **Metrics:** task completion rate, time on task, error count, interventions, SUS, and
   a single "how easy was this?" rating (1–7).
5. **Output:** a ranked list of issues. Every severity-1 issue (blocks task completion) is
   fixed before the next gate.

## 4. Legal and compliance checklist

This is not legal advice. Each item should be reviewed by qualified counsel before release.

- [ ] **Product name** chosen and trademark-searched. It must not use "HD-2D" (R-06).
- [ ] **Third-party licences:** an automated notices file for the editor and per exported
      game (`07`, dependency table).
- [ ] **LGPL compliance** for Qt and Qt Advanced Docking System: dynamic linking,
      licence texts, relinking instructions (editor only).
- [ ] **libgit2:** unmodified, or modifications published (GPLv2 with linking exception
      [S77]).
- [ ] **FreeType credit** included (FTL [S80]).
- [ ] **Steamworks agreement** for Steam features (O4–O5).
- [ ] **Template and sample assets:** all original, or licensed with redistribution rights
      for use inside users' commercial games.
- [ ] **End-user licence for Diorama itself**, which depends on the business model (Q-01).
- [ ] **Privacy policy** covering opt-in telemetry and crash reports, and the AI assistant
      if enabled.
- [ ] **Code signing** of the editor installer (Authenticode).

## 5. Open questions for the project owner

These decisions change the plan. Answers are needed by the end of the first 30 days
(`08` §7).

| ID | Question | Why it matters | Recommendation |
|---|---|---|---|
| Q-01 | **Business model:** free, one-time purchase, subscription, or revenue share? Should exported games ever owe royalties? | Shapes licensing, marketplace, legal and the "own your game" pillar | Royalty-free exported games. Choose between a one-time licence and an open-core model after market research. |
| Q-02 | **Open source or proprietary?** | Community contributions vs revenue protection; affects ADR choices (all chosen libraries are compatible with either) | Decide before Phase 1; it affects the contributor workflow. |
| Q-03 | **Team and budget:** core team (6–10) or solo/duo? | Determines the timeline and the α scope (`08` §6) | Plan for the solo or duo scope if unsure, and scale up later. |
| Q-04 | **First genre focus:** RPG first, as recommended, or something else? | Determines the first kit and the vertical slice | RPG, because it matches where the style is proven (`research/01` §2). |
| Q-05 | **Optional scripting at launch,** or no scripting until 1.0? | Power-user adoption vs keeping focus | Hidden until 1.0 (E11); Custom Blocks first. |
| Q-06 | **AI assistant stance:** include after 1.0 (opt-in), never, or earlier? | Community trust, cost, privacy | After 1.0, opt-in, logic-only (`06` §8). |
| Q-07 | **Minimum hardware target:** how low-end should exported games run? | Renderer budgets and feature tiers | Decide after Spike R measurements. |
| Q-08 | **Product name.** | Branding, trademark (R-06) | Run a trademark search on 3–5 candidates. |
