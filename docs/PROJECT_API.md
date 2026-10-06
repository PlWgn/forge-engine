# Forge 2.5 Shells and Open Project Format

The developer owns the project. Builtin ImGui, third-party GUIs, terminal tools, and manual edits share configuration/resources without a hidden editor database. run/dev/validate/build require neither UI nor SDK. The game runtime still uses C++/CPython and GLFW/OpenGL.

## Format and Compatibility

engine.json is UTF-8 JSON with schema_version:1; rename it and select it through --project. paths defines resource groups relative to the configuration root; resolved symlinks must remain inside the project. python_paths adds import directories. Python scenes/scripts remain source files. JSON scenes contain mode/entities/camera/physics/rendering/emitters and optional script. Entities store local TRS, parent, assets, scripts/data, animator/morph settings. parent references IDs rather than row indices. Scene scripts resolve under scenes; Behaviors under scripts. Materials are JSON; images/models/audio/shaders keep standard formats. @mesh: refers to a runtime registry whose geometry is not automatically stored in documents.

Engine version, schema_version, project API, and game API are independent. A shell must not bump format versions for its own UI. schemas/{project,scene,entity,prefab,material}.schema.json defines basic structure and allows additionalProperties. The native engine checks references, numerical arithmetic, hierarchy, physics, and resources. Schema does not replace validate, media checks, or Python execution. GUIDE.md describes defaults/extended contracts. Optional $schema is preserved as ordinary data.

Use stable nonempty entity/emitter IDs. Arrays of entities/emitters with unique string IDs merge by ID so reordered rows do not mix objects. Without such IDs, the entire array is atomic. Vectors/scripts/keyframes/other arrays are also atomic. Unknown fields at every level are preserved. extensions["org.example.shell"] is a useful naming convention, not a restriction. Panel state/private caches/undo history are optional and not needed to run the game.

Saving may normalize indentation to two spaces; key order/original number spelling are not guaranteed. Unknown values are preserved; semantically unchanged documents are not rewritten. Comments, duplicate keys, NaN/Infinity are unsupported interchange JSON. Arbitrary Python state/closures/external files/script effects are not automatically serialized.

## Document API Without a GUI

Commands are available through the native executable or launcher:

```sh
build/bin/forge project --project engine.json --request request.json
python tools/forge.py project --project engine.json --request request.json
python tools/forge.py project --project engine.json --serve
```

On Windows use build/bin/Release/forge.exe or build/bin/forge.exe. Without --request/--serve, one JSON command is read from stdin to EOF. --serve uses local stdin/stdout JSON-lines with one request/response per line; EOF stops the server. No network listener, account, or GUI is involved. stdout contains JSON only; diagnostics go to stderr/forge.log without auto-opening it. An invalid request does not stop serve. A failed single command exits 1. Requests are limited to 16 MiB after reading.

Request:

```json
{"api_version":1,"id":7,"op":"read","group":"scenes","file":"room.json"}
```

Success:

```json
{"api_version":1,"id":7,"ok":true,"result":{"exists":true,"revision":"opaque-content-revision","data":{"entities":[]}}}
```

Failure:

```json
{"api_version":1,"id":7,"ok":false,"error":{"code":"conflict","message":"...","paths":["/entities/player/name"]}}
```

id is optional and echoed unchanged; malformed JSON may lack it. revision is an opaque JSON-content identifier, not a security checksum. Saving uses the **complete baseline base**, not revision equality alone.

| op | Arguments | Result |
| --- | --- | --- |
| capabilities | — | api_version, engine_version, base_shell, operations |
| inspect | — | Project configuration and settings filename |
| list | optional group | Relative group filenames, or entire root without group |
| read | file, optional group | exists/revision/data; absent file has data/revision=null |
| check | file, group, data | Validate without writing: valid=true or error |
| commit | file, group, base, data | Merge with disk, validate, atomically write, return snapshot |
| patch | file, group, base, data | data is RFC 6902 operations against base, followed by commit |
| apply_patch | data, patch | RFC 6902 in memory; no scene checks or writes |
| merge | base, local, disk | Three-way merge in memory; no asset checks or writes |

Without group, file resolves from root; with group, from its configured directory. Semantic scene/material/prefab type follows actual location even without group. Config checks new paths/project; scene checks JSON/numbers/hierarchy/physics/references; prefab checks inheritance/local IDs; material checks fields/maps. These do not execute user Python or create GPU contexts. Full validate adds media/animation/Python syntax checks; use validate/build before shipping.

base:null creates a file and never overwrites an existing one. For updates, read first and pass returned data as base. Remove an entity with /entities/N patch or a field by JSON Pointer rather than overwriting an entire stale document. Filesystem deletion/image moves/import/Python editing remain ordinary filesystem operations without a hidden editor index. The API supplies builtin document operations without replacing a file manager.

Disjoint local/disk changes merge. Incompatible changes to a field, delete-versus-edit, or incompatible order changes return conflict with paths. IDs identify keyed entries; /@order identifies ordering. Draft/disk are retained. Read the new version, present the conflict, and establish a new baseline; the server does not choose a winner by timestamp.

Writes use adjacent temporary files and atomic replacement (MoveFileExW on Windows, rename on POSIX), validating first. Cooperative writers use .name.json.forge-lock; after a crash, remove a stale lock manually once the writer is stopped. Disk content is rechecked before replacement. External editors may ignore locks, leaving a narrow check-to-rename race. This is optimistic coordination, not distributed locking or guaranteed fsync/power-loss recovery. Already-present manual edits are included; conflicts never cause delete-before-rename.

## Python SDK and Extensions

The standard-library SDK lives in sdk/forge_editor. Install with python -m pip install ./sdk or add sdk to the search path; launcher shell does this automatically. Shell authors choose Qt/GTK/web UI with their own local host/terminal. Any language can speak JSON-lines without the SDK.

```python
from forge_editor import Client, ProjectError

with Client("/path/to/forge", "/path/to/project/engine.json") as client:
    document = client.document("room.json", "scenes")
    document.patch([{"op":"replace", "path":"/entities/0/name", "value":"Player"}])
    document.undo()
    document.redo()
    try:
        document.save()
    except ProjectError as error:
        print(error.code, error.paths)  # draft remains available
    process = client.launch("dev", scene="room.json")
    process.wait()
```

Document.data/base return deep snapshots. replace(data) validates/deep-copies; patch/check/undo/redo do not write. History holds 100 snapshots. save() updates baseline/data with merged external fields and clears history; reload() explicitly discards the draft. dirty compares data with base. An absent new file has data=None; replace creates a draft. Client serializes exchanges under one lock without multiplexing. launch() returns Popen; the shell handles cancellation/progress/exit status. CLI run/dev/validate/build defines their behavior/errors.

An extension is an explicitly loaded, trusted Python file:

```python
API_VERSION = 1

def register(client):
    client.register("studio.tag", tag)

def tag(client, file, value):
    doc = client.document(file, "scenes")
    data = doc.data
    data.setdefault("extensions", {})["studio.tag"] = value
    doc.replace(data).save()
    return doc.data
```

client.extension(path) invokes register; client.command("studio.tag",file="room.json",value="work") invokes the command. Names are unique/namespaced; forge.* is reserved. Registration failure restores the registry, not arbitrary import side effects. Plugins are not sandboxed; do not auto-load plugins from unknown projects. Native FORGE_MODULE and game Python modules retain their public interfaces.

The same examples/editor/labels_extension.py works in external Client and builtin RuntimeClient, adapting the shared document API without another Python runtime. Builtin **Extension commands** displays registered commands and accepts JSON arguments. Configure edit:

```json
{"editor":{"extensions":["tools/my_extension.py"]}}
```

Configuration paths are project-relative. Alternatively use edit --extension path/to/plugin.py with a cwd-relative/absolute CLI path. run/dev/build do not load editor plugins. Shell/plugin source changes require restarting the session; game reload does not replace the host. External shells build panels with their own toolkit; builtin hosting offers commands rather than an arbitrary ImGui widget ABI.

## Shell Selection and Runtime Editing API

```sh
# Default official shell.
python tools/forge.py edit --scene editor-empty.json
# Viewport without builtin UI.
python tools/forge.py edit --shell none --scene editor-empty.json
# Project Python controller for a custom viewport.
python tools/forge.py edit --shell examples/editor/viewport_shell.py --scene editor-empty.json
# Independent SDK shell.
python tools/forge.py shell --shell examples/editor/terminal_shell.py --extension examples/editor/labels_extension.py
```

A viewport shell is a project .py with API_VERSION=1 and optional on_start(), on_update(dt), on_destroy(). on_update runs while gameplay is paused, including after a rejected reload. It can repair watched files or request editor load; a shell error in this mode suppresses repeated updates until successful load/restart. Use forge input/entities/editor_command; retain IDs rather than old entity references. The shell survives scene changes until edit exits. UI represented as game entities belongs to the world and may enter exports; separate SDK GUIs avoid that constraint. External shells can launch native preview processes; API 1 provides neither Qt/GTK viewport embedding nor live-game RPC. Runtime APIs are in-process; document RPC serves external processes.

forge.editor_command(dict) is available to any viewport controller, including --shell none, headless edit, and builds without ImGui:

| op | Arguments | Action |
| --- | --- | --- |
| snapshot | — | scene/selected/preview and undo/redo counts |
| select | id or empty string | Select current entity or clear selection |
| preview | enabled | Run/pause simulation; no reset |
| apply | scene | Prepare/validate a full JSON candidate, lifecycle transaction, commit |
| patch | patch | RFC 6902 snapshot update followed by apply |
| undo / redo | — | Up to 100 snapshots; rejection retains history/world |
| load | file | Explicit deferred load, discarding unsaved draft |
| save | file | JSON save/export through shared document commit |
| commands | — | Registered extension command names |
| command | name, arguments | Invoke shared extension command |

Add/delete/duplicate, parent/TRS, assets/material/camera/light/physics/text, animator/morph/prefab authoring use snapshot+patch or public forge/prefabs/animation/document APIs. Procedural entities also need registered geometry. scene_data()/save_scene(file)/editor_select(entity) retain existing workflows; selection is shared. project_request(dict) returns a result or native exception; project_response(dict) returns the RPC structured envelope. RuntimeClient uses the latter for shared ProjectError behavior.

Runtime document commit/patch and save_scene are unavailable during hot reload; invoke after commit.

apply/patch/undo/redo require a paused, fully initialized editor outside reload/teardown. Replacement executes lifecycle, retires old entities, and may repeat user callbacks. Rejection retains world/scripts/resources/history; arbitrary callback side effects require a game-defined strategy. This is not interpreter serialization or undo of plugin file writes.

Saving original JSON computes a runtime-baseline delta and applies it to source without materializing unchanged defaults/inherited prefab values, then merges disk changes. Save As creates a full snapshot and becomes the current initialized edit document for subsequent baseline saves. Export from run/dev does not switch scenes. Existing unopened targets require read/load before overwrite. Python-scene exports use new JSON names without changing .py. Unknown root/entity/camera/extension fields survive. After external fields merge on disk, explicitly Reload to show them in the viewport.

Automatic reload refuses a world with unsaved editor changes and reports the preserved work. Save/undo or explicitly reload, then edit a watched file to retry. A clean draft keeps ordinary reload behavior. Play preview does not reset the scene. Direct Python setters remain available, but shared undo is guaranteed for editor_command/builtin authoring, not arbitrary Python.

In 2.5.1, JSON candidates isolate procedural registries/listener membership. Synchronous on_frame apply stops remaining old-scene callbacks. Managed forge.save/SaveManager.write/delete defer until commit and cancel on rejection; write data/metadata are copied. This is not arbitrary Python serialization or a filesystem-wide transaction: completed commit callbacks do not roll back if another fails. Ordinary loading outside authoring/reload keeps immediate save behavior.

## Building Without the Builtin Interface

```sh
python tools/dependencies.py --without-editor
python tools/forge.py compile --without-editor
python tools/forge.py run
python tools/forge.py edit --shell none
python tools/forge.py build --output dist/Game
```

CMake: -DFORGE_WITH_EDITOR=OFF. Bootstrap with --without-editor does not download ImGui; editor.cpp/animation_editor.cpp are excluded. Document service, runtime editing, scenes, renderer, animation APIs, validate/build/run remain. capabilities.base_shell / forge.capabilities().base_editor report the actual build. Windowed edit --shell builtin without it gives a clear error; select none/custom. Headless edit needs no UI. Ordinary compile restores ON. Games do not load SDK/plugins or require an installed editor.

## Verification

tests/project_api.py <binary> checks independent shells/manual edits/unknown fields, ID/order merging/conflicts, atomic failure, Unicode/paths/symlinks, config/material/prefab checks, SDK history, shared adapters, CLI stdout/status, and paused lifecycle/rollback. CTest/launcher include it; GPU suites cover viewport/ImGui/animation tools. CI builds macOS/Windows ON/OFF; GPU suites use builtin UI. One active Python runtime, existing subsystem limits, and dependency licenses remain.


## Graphics Backends in Forge 2.6

The document capabilities response includes graphics_backends (compiled support). Renderer fields are documented in [DIRECT3D11.md](DIRECT3D11.md) and schemas/project.schema.json. Every shell may edit backend/shader settings through the same document API, preserving unknown fields. Backend/driver/debug changes take effect after runtime restart; live shader candidates retain rollback. SDK Client.launch(silent_audio=True) disables hardware audio independently of graphics. Project and shell protocol versions remain 1.
