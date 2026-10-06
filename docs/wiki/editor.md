# Editors and Alternative Shells

[Forge wiki](../../GUIDE.md) · **Editors and Alternative Shells**

The builtin editor is one optional shell over the open format and public APIs. Terminal tools, external GUIs, and hand editing can coexist.

- [Builtin scene editor](#builtin-scene-editor)
- [Launch or replace the shell](#launch-or-replace-the-shell)
- [Open documents and runtime editing](#open-documents-and-runtime-editing)

## Builtin scene editor

edit --scene editor-empty.json opens the windowed editor with simulation initially paused. Hierarchy selects entities; Inspector edits transforms, collider, color, visibility, screen/dynamic/trigger/casts_shadow, and text. Add/Duplicate/Delete, resource browsing, lights/camera/gravity, Undo/Redo (100 snapshots), Play preview, and JSON saving are available. Middle mouse moves the camera; WASD moves; Ctrl+S/Cmd+S saves the selected JSON name inside scenes. Dev/edit supports reload; an unsaved editor draft blocks automatic world replacement.

scene_data()/save_scene(file) serialize declarative data including scripts/data and JSON scene.script. Updates to original JSON preserve unknown fields and project authored changes onto the source with external-edit merging; exporting to another file creates a complete snapshot. Conflicts do not overwrite the file. Python source, closures, and arbitrary state are not serialized. Runtime-created objects, including UI, enter snapshots; start authoring with editor-empty.json and attach Behavior through JSON scripts/data or code. Preview scripts may change the world; undo restores scene data while simulation is paused, not arbitrary Python state. Stopping preview does not reset; Reload reads the source. The editor has no visual scripting, shader/animation graph, or transform gizmos.

## Launch or replace the shell

Run from the engine checkout; add `--project` for another game:

```sh
python tools/forge.py edit --scene editor-empty.json
python tools/forge.py edit --shell none --scene editor-empty.json
python tools/forge.py edit --shell examples/editor/viewport_shell.py --scene editor-empty.json
python tools/forge.py shell --shell examples/editor/terminal_shell.py --extension examples/editor/labels_extension.py
```

The first command opens builtin panels when compiled. `none` retains the viewport and public in-process authoring API. A project viewport shell supplies `API_VERSION = 1` and optional `on_start`, `on_update(dt)`, `on_destroy`; its updates continue while gameplay is paused, including reload recovery. The `shell` command runs an independent SDK host outside the gameplay process. Toolkit choice is up to the shell author.

`compile --without-editor` excludes ImGui independently of the graphics backend. It retains document/runtime editing, headless operation, build, run, and custom viewport shells. An ordinary compile restores the builtin shell. Extensions use the same public commands in any shell; no private builtin privileges are required.

## Open documents and runtime editing

The full version-1 contract, request/response examples, SDK installation, extension registration, and conflict handling are documented in [Project API](../PROJECT_API.md). It is the canonical reference for shell authors.

| Interface | Use |
| --- | --- |
| `python tools/forge.py project --serve` | Local stdin/stdout JSON-lines document service; no GUI or network listener |
| SDK `Client` / `Document` | Read/patch/check/undo/merge/save and launch runtime processes |
| SDK `RuntimeClient` | Same commands inside a viewport shell |
| `forge.editor_command({...})` | Snapshot/select/preview/apply/patch/undo/redo/load/save and extension commands |
| `forge.project_request` / `project_response` | Native document operations inside embedded Python |

`engine.json`, scene/prefab/material JSON, Python scripts, and ordinary asset files are the project format; there is no required editor database. [Schemas](../../schemas/project.schema.json) allow unknown fields and are structural aids, not substitutes for native validation. Keep stable IDs for entities/emitters and namespace your shell metadata under `extensions` when useful.

Document editing preserves unknown values, original defaults, prefab overrides, and disjoint manual changes. Read first and supply the complete baseline when committing; `base: null` creates a new file only. Conflicts retain disk and draft and report paths; do not overwrite them automatically. Cooperative file locks and atomic replacement coordinate local writers, without guaranteeing distributed locking or preservation against every external write race.

Runtime apply/patch/undo/redo require a paused, fully initialized editor outside reload/teardown. They use candidate validation/lifecycle transactions and may run callbacks again. Save As requires a fresh target unless it was explicitly loaded; it becomes the current edit document. Saving original JSON projects authored changes onto the original document rather than expanding unchanged defaults. Unknown/external fields merged into the saved file need explicit Reload to appear in the viewport.

An unsaved runtime draft blocks automatic replacement. Save/undo or explicitly load, then retry a watched edit. Undo stores scene declarations, not arbitrary Python instances, I/O, audio progress, or procedural-generation history. A failed candidate preserves managed world/scripts/resources/history and cancels managed deferred persistence. Plugin effects outside managed APIs remain the plugin's responsibility.

API 1 supplies a local document service and in-process viewport authoring. It does not supply remote live-game RPC, third-party viewport embedding, an ImGui widget ABI, visual scripting, or graph/gizmo editing. The [animation authoring window](animation.md#state-machines-and-visual-authoring) supports clips/layers/markers/TRS/morphs/retargeting within these limits.
