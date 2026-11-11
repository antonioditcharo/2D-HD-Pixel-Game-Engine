# editor/

The editor: the tools described in
[`docs/plan/06-editor-ux.md`](../docs/plan/06-editor-ux.md).

| Folder | What |
|---|---|
| [`core/`](core/) | The editor's model, without any user interface: an open project and scene, undo and redo, picking, and the preview the scene view shows |
| [`app/`](app/) | `sparxel-editor`, on Qt 6 Widgets (ADR-005): the scene view, entity list, asset browser and an inspector generated from reflection |
| [`exporter/`](exporter/) | Exporting a project as a game folder, and the `sparxel-export` command |

See [`docs/dev/editor.md`](../docs/dev/editor.md) and
[`docs/dev/export.md`](../docs/dev/export.md).

Rule: the editor may depend on `engine/`, but `engine/` never depends on the editor.
