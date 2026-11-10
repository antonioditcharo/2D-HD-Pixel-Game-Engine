# editor/

The editor application will live here: the Qt 6 shell, the workspaces and the tools
described in [`docs/plan/06-editor-ux.md`](../docs/plan/06-editor-ux.md).

Nothing is built from this folder yet. Editor work starts after the Phase 0 spikes decide
the UI framework (ADR-005 in
[`docs/plan/07-tech-stack-adrs.md`](../docs/plan/07-tech-stack-adrs.md)).

Rule: the editor may depend on `engine/`, but `engine/` never depends on the editor.
