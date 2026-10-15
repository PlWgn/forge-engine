# Editors and Alternative Shells

[Forge wiki](../../GUIDE.md) · **Editors and Alternative Shells**

The builtin editor is one optional shell over the open format and public APIs. Terminal tools, external GUIs, and hand editing can coexist.

- [Builtin scene editor](#builtin-scene-editor)
- [Launch or replace the shell](#launch-or-replace-the-shell)
- [Open documents and runtime editing](#open-documents-and-runtime-editing)

## Builtin scene editor

`python tools/forge.py edit --scene editor-empty.json` opens the default Forge workspace with gameplay paused. Compile normally to include it; no additional GUI installation or service is needed. Forge 2.13 uses a dark, pane-based layout inspired by familiar game editors.

| Area | Tools |
| --- | --- |
| Toolbar | File/save/reload, Create, Undo/Redo, Play/Pause, World settings, workspace reset and language switching |
| Hierarchy, left | Parent/child tree, search by name or ID, shared selection, double-click to frame an entity |
| Scene, center | Dedicated rendered viewport, optional ground grid, selected-center marker, center picking, camera navigation |
| Inspector, right | Name, local TRS/parent, color/visibility, texture/model, PBR metallic/roughness, collider/dynamic/trigger, text and complete entity JSON |
| Project, bottom | Configured path groups, searchable file/type list; double-click scenes to open, textures/models to assign, or a model to create a mesh when nothing is selected |
| Console, bottom | Last 64 KiB of the session log, refreshed twice per second; text filter, warnings/errors filter, follow scrolling |
| Extensions, bottom | Registered SDK commands with JSON arguments and results/errors in the status bar |

Drag the vertical separators to resize Hierarchy and Inspector; drag the horizontal separator to resize the bottom dock. Proportions adapt when the window resizes. **Workspace → Reset layout** restores the defaults. Layout is session-local. English/Russian labels use engine localization keys, so projects can override `engine.editor.*`; Workspace changes language without saving a game preference.

The Scene view renders into its own target, including postprocessing, rather than hiding the game behind panels. 3D uses the panel aspect ratio; 2D preserves the game-window aspect with letterboxing. Logical game-window dimensions and ordinary run/dev/custom-shell rendering retain their contracts. Screen UI inside a 3D scene follows the panel fit; game input coordinates are still window coordinates, so interactive game UI should also be tested with `run`/`dev`.

Create an empty object, cube, flat cube ground plane, sprite, or text from **Create**. Select an object to edit it; use **World** for scene mode/background, physics/gravity, camera/FOV and lights. Reparenting keeps local TRS and validates hierarchy rules, including cycles. The advanced entity JSON editor supports scripts/data, rigid-body settings, full material fields and other existing entity properties without inventing a second component format. **Read selection** refreshes its draft after undo or another change; **Apply JSON** validates the full candidate. The buffer is 32 KiB; larger entities remain editable through the document SDK or source file. Asset listings are cached until Refresh or a configured-path change, exclude escaping symlinks, and stop at 10,000 files.

Inspector, create, duplicate/delete and asset assignment use the same transactional scene API as custom shells. Failed candidates preserve the managed scene/resources and undo history. Edits may reconstruct the scene and repeat initialization callbacks; callbacks with external side effects need a game-defined strategy. Gameplay preview disables these authoring controls. Animation authoring retains its separate validated window, opened explicitly for an imported model. Undo/Redo stores up to 100 declarative snapshots.

| Shortcut / gesture | Action |
| --- | --- |
| Ctrl+S / Cmd+S | Save to the Scene file / Save as field |
| Ctrl/Cmd+Z, Shift+Ctrl/Cmd+Z, Ctrl/Cmd+Y | Undo / redo while paused and outside a text field |
| F over the scene / Frame selection | Aim the camera at the selected object |
| WASD over the scene | Move the camera while paused |
| Middle mouse over the scene | Pan while paused |
| Click within 24 logical pixels of an entity center | Select the nearest projected center; click empty space to deselect |

Picking is a center-distance aid, not mesh/occlusion raycasting. Grid and selection markers are editor overlays without scene depth testing. There are no draggable transform gizmos, arbitrary panel docking, visual scripting or shader/animation graphs. Tiny windows may require scrolling inside panels; 1280×720 or larger is recommended.

`scene_data()` / `save_scene(file)` serialize declarative data including scripts/data and JSON `scene.script`. Saving original JSON preserves unknown fields and projects authored changes onto the source with external-edit merging; Save As creates a complete snapshot and requires a fresh target. Conflicts do not overwrite files. Python source, closures, and arbitrary state are not serialized. Runtime-created objects, including UI, enter snapshots; start authoring with `editor-empty.json` and attach Behavior through JSON scripts/data or code. Play preview does not reset the scene; Pause retains its current state and Reload explicitly reads the source. An unsaved editor draft blocks automatic world replacement. Undo restores declarations while paused, not Python state, external I/O or audio progress.

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
