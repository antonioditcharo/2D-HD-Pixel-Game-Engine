# editor/

The editor application will live here: the Qt 6 shell, the workspaces and the tools
described in [`docs/plan/06-editor-ux.md`](../docs/plan/06-editor-ux.md).

Built so far: [`exporter/`](exporter/), which exports a project as a game folder, and the
`sparxel-export` command ([`docs/dev/export.md`](../docs/dev/export.md)). The editor
application starts after the Phase 0 spikes decide the UI framework (ADR-005 in
[`docs/plan/07-tech-stack-adrs.md`](../docs/plan/07-tech-stack-adrs.md)).

Rule: the editor may depend on `engine/`, but `engine/` never depends on the editor.
