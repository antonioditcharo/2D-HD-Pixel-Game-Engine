# Plan 09: Risks, Quality Strategy, Legal and Decisions

---

## 1. Risk register

Likelihood (L) and impact (I) are rated High, Medium or Low. The register is reviewed at
every phase gate.

| ID | Risk | L | I | Mitigation | Early warning sign |
|---|---|---|---|---|---|
| R-01 | **Scope explosion.** "All the systems users will ever need" has no natural boundary. | H | H | Genre-focused kits (`01` §4.1); α/1.0/Post tags (`02`); phase gates (`08`); extensibility for everything else. | Gate criteria slipping; features added without removing others |
| R-02 | **No-code ceiling.** Users hit things they cannot express. | M | H | Layered model, Custom Blocks and a Luau escape hatch (`05`); usability rounds every gate; a no-script vertical slice as proof. | Testers asking "how do I code this?"; kits needing Script blocks |
| R-03 | **The HD-2D look is hard to tune.** Even professionals invest years in it (Sea of Stars [S15]). | M | H | Phase 0 reference scene; golden-image tests; Look presets reviewed against the checklist in `research/01` §4. When a team forms, a rendering engineer and a technical artist are the first two roles (`08` §6). | Reference scene not reaching the target look or budget by Gate 0 |
| R-04 | **Editor UX complexity** overwhelms non-coders. | M | H | Usability tests with non-coders at every gate (§3); Basic/Advanced split; glossary (`06` §9); measured SUS and task completion. | SUS < 68 at Gate 2 |
| R-05 | **Dependency risk:** SDL GPU API feature gaps, Qt licence compliance, library abandonment. | M | M | Own RHI with a D3D12 fallback (ADR-004); no GPL-only Qt modules (ADR-005); only mature, actively maintained libraries; vendored versions. | Spike R finds missing features; upstream inactivity |
| R-06 | **Trademark:** "HD-2D" is a Square Enix trademark [S11][S12][S13]. The repository name contains "2D-HD" (planned rename: "Sparxel-Game-Engine-2DHD", which contains "2DHD"), close to that mark. | M | H | Never use "HD-2D" in product name or marketing; describe the style generically ("pixel diorama", "2.5D pixel art"); the product name Sparxel was screened (`research/05` §7); professional clearance before public launch; the owner chose to keep "2DHD" in the repository name, so never use it in marketing copy. | Any marketing copy using the term |
| R-07 | **IP similarity in templates and kits:** copying specific games' assets, characters or signature mechanics. | L | H | All template art and audio is original; battle systems are configurable and generic; legal review of templates before release. | Template reviews flag resemblance |
| R-08 | **Data loss** in user projects. | L | H | Command journal, autosave, crash recovery, snapshots, atomic writes, format migrations with backups (`06` §5). | Any data-loss report |
| R-09 | **Texel and pixel inconsistency** makes user games look "off". | M | M | PPU enforcement, texel-density view, Project Doctor warnings (R-VIS-07). | Template or user scenes failing the density view |
| R-10 | **Performance cliffs** in user-made content: too many lights, continuous rules. | M | M | Budgets, plain-language performance overlay, Project Doctor lints, quality tiers. | Alpha projects under 60 fps on minimum spec |
| R-11 | **Sustainability.** One developer, no revenue from the engine (open source, royalty-free), and a multi-year scope. | H | H | The solo alpha scope (`08` §5); gates, not dates; every gate leaves something usable; open development so contributors can join; funding question tracked as Q-13. | No gate passed in a planning period; scope added without removing anything |
| R-12 | **Platform shifts:** Windows 10 support ended 14 October 2025 [S82]. | L | L | Windows 11 primary; Windows 10 best-effort only. | Testers on Windows 10 reporting issues |
| R-13 | **AI feature backlash or liability.** | M | M | Not started before Gate 5; opt-in, transparent, edits via undoable commands, no generative art (`06` §8). | Community feedback |
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

- [x] **Product name** chosen: Sparxel, after a preliminary screening (`research/05` §7).
- [ ] **Professional trademark clearance** of Sparxel, and a decision on filing it in the
      software classes (`research/05` §7.3). Before any public launch.
- [x] **Repository renamed** to "Sparxel-Game-Engine-2DHD" by the owner.
- [ ] **Third-party licences:** an automated notices file for the editor and per exported
      game (`07`, dependency table). Until then, `THIRD_PARTY_NOTICES.md` in the repository
      root is kept by hand.
- [ ] **LGPL compliance** for Qt and Qt Advanced Docking System: dynamic linking,
      licence texts, relinking instructions (editor only).
- [ ] **libgit2:** unmodified, or modifications published (GPLv2 with linking exception
      [S77]).
- [ ] **FreeType credit** included (FTL [S80]).
- [ ] **Steamworks agreement** for Steam features (O4–O5). The Steamworks SDK is never
      committed to this repository: its licence allows local use and redistribution of
      only the `redistributable_bin` folder [S91]. Users supply their own copy (ADR-014).
- [ ] **Template and sample assets:** all original, or licensed with redistribution rights
      for use inside users' commercial games.
- [x] **Licence for the engine itself:** MIT (`LICENSE`, ADR-017).
- [ ] **Contributor terms** decided (Q-10) and **brand ownership** decided (Q-12).
- [ ] **Privacy policy** covering opt-in telemetry and crash reports, and the AI assistant
      if enabled.
- [ ] **Code signing** of the editor installer (Authenticode).

## 5. Owner decisions

The project owner answered the original open questions on 9 November 2026.

| ID | Question | Decision | Where it is applied |
|---|---|---|---|
| Q-01 | Business model | **Royalty-free and open source.** Exported games never owe fees or royalties. | `01` pillar 6; ADR-017 |
| Q-02 | Open source or proprietary | **Open source, MIT licence.** | ADR-017; `LICENSE` |
| Q-03 | Team and budget | **Solo for now.** A team is added later. | `08` §1 and §5 (solo baseline); α tags in `02` |
| Q-04 | First genre | **RPG first.** | `01` §4.1; `02` section F |
| Q-05 | Optional scripting | **Hidden until 1.0.** Custom Blocks come first. | `02` E8, E11 |
| Q-06 | AI assistant | **Work starts after the Early Access / beta release (Gate 5).** Opt-in and logic-only. | `06` §8; `02` Q5 |
| Q-07 | Minimum hardware target | **Decide after the rendering spike** (Spike R) has measurements. | ADR-001; `08` Phase 0 |
| Q-08 | Product name | **Screen 3–5 candidate names** for trademark and product conflicts, then choose. | `research/05`; R-06 |
| Q-09 | Final product name (decided 10 November 2026) | **Sparxel.** It replaced Glimmerbox, chosen earlier the same day. | `research/05` §7; R-06 |

## 6. Open questions

These follow from the decisions above. None blocks Phase 0. (Q-09, the final product name,
was decided on 10 November 2026; see §5.)

| ID | Question | Why it matters | Recommendation |
|---|---|---|---|
| Q-10 | **Contributor terms:** plain "contributions are MIT", a sign-off (DCO), or a contributor licence agreement (CLA)? | Decides how easily outside contributions can be accepted, and whether the licence could ever change | Plain MIT contributions for now (what `CONTRIBUTING.md` says). Revisit when outside contributors arrive. |
| Q-11 | **Licence for bundled template art and audio.** | Users ship these assets inside their own commercial games | CC0, so users owe no attribution. Decide before the first template ships (Phase 3). |
| Q-12 | **Who owns the name and logo?** MIT covers the code, not the brand. | Stops forks from shipping under the same name | Keep the name and logo with the owner, as GDevelop does [S31]. Decide with Q-09. |
| Q-13 | **How the project is funded** while it is solo and open source: none, donations, sponsorship, or paid services later. | Sustainability (R-11) | Nothing is needed for Phase 0. Revisit at Gate 2. |
