# Development, Diagnostics, and Testing

[Forge wiki](../../GUIDE.md) · **Development, Diagnostics, and Testing**

Use dev for content reload, inspect logs and counters for failures, and select tests that exercise the changed subsystem.

- [Development and reload](#development-and-reload)
- [Terminal and file logging](#terminal-and-file-logging)
- [Profiling and crash reports](#profiling-and-crash-reports)
- [Watcher and dispatch diagnostics](#watcher-and-dispatch-diagnostics)
- [World scaling and benchmarks](#world-scaling-and-benchmarks)
- [Validation and test suites](#validation-and-test-suites)
- [Measure performance](#measure-performance)
- [Troubleshooting](#troubleshooting)

## Development and reload

Dev watches settings, paths, and python_paths in a background worker with a default 0.3-second wall-clock interval; development.watch_interval changes it ([watcher diagnostics](#watcher-and-dispatch-diagnostics)). Python bytecode/cache metadata is excluded. Scene, Behavior, imported module, object, material, model, texture, shader, font, or locale changes reload the current scene. Changing entry_scene loads the new initial scene unless --scene overrides it.

Reload recreates entities and Behavior instances, resetting local scene state. Preserve progress with save/load. Startup script objects live for the whole session; restart dev to apply their code changes. Renderer/window reload staging and rollback follow [storage and transactions](persistence.md#storage-locations-and-persistence-transactions) and the runtime transaction contract. C++ changes require compilation and restart.

Invalid Python, JSON, or shaders during reload record the error and pause gameplay. Fix the file to retry at the next watch check. Errors from on_update use the same recovery mechanism. Failure on the initial load exits; fix and relaunch. A successful replacement invokes the previous scene's on_destroy.

Reload prepares resources and configuration before switching. GPU resources, entities, localization, imports, audio, and managed input/configuration are restored on candidate failure. Initialization commands apply after success. The old scene/Behavior/startup may implement on_reload_failed(message) to inspect restored state; gameplay updates remain paused until recovery.

Transactions cover managed engine state. forge.save, SaveManager.write, and SaveManager.delete defer disk changes during hot reload and JSON candidate preparation through editor apply/patch/undo/redo; rejection cancels the queue. Ordinary initial startup or scene loading outside these transactions retains immediate save/write/delete behavior. Use forge.defer_persistence(callable) for custom persistence. Arbitrary open()/Path.write_text(), network calls, external startup-object mutations, and elapsed playback time are not automatically rolled back. Avoid irreversible operations in candidate build()/on_start without explicit deferral. Files edited on disk are not restored from memory. The previous scene remains rendered after a rejected reload.


Reload replaces Forge-managed sys.path entries without accumulating duplicates, removes retired directories, and preserves other paths added by game code. Failure restores sys.path and the engine's managed-path list.

Imports from modules/scripts/scenes are invalidated during reload. Retired python_paths roots also invalidate cached modules and namespace packages; unchanged extra paths retain cached packages, including native extensions. Changed files are selectively invalidated by the watcher; changes seen while a candidate is rejected stay pending until a reload commits, so a later fix also refreshes packages edited before the failure. Startup modules can retain imported references; restart to refresh those references. Additional python_paths are watched.

## Terminal and file logging

Each session appends to forge.log in the configured project/user log directory ([storage and transactions](persistence.md#storage-locations-and-persistence-transactions)). Messages include time and INFO/WARN/ERROR/DEBUG levels. forge.log() and print() write to terminal and file; sys.stderr is also redirected. A partial print line is emitted on flush or shutdown.

```python
import forge
forge.log('Starting load')
forge.log('Optional resource missing', 'WARN')
print('Progress:', 50, '%')
```

A user forge.log(..., 'ERROR') records a message but does not itself exit or open the log. An unhandled script/core exception records diagnostics, opens the log through the OS application, and makes run exit with status 1. In dev, on_update exceptions pause execution until a watched edit triggers recovery. Automatic opening occurs once per session; failed opening leaves a path and warning in the terminal. Open manually with forge.open_log().

build/validate errors, missing files, malformed JSON, and SyntaxError appear in terminal/log with exit status 1; these commands do not open the log automatically. The launcher retains full CMake/MSVC/Clang output, including filenames and line numbers.

Use forge.quit() for controlled exit. Do not call sys.exit() in game scripts: SystemExit is treated as a script error.

The configured log directory must be writable. Native faults such as SIGSEGV, forced termination, or OS failure cannot guarantee log opening. Built-in crash reports and the minimal native fatal handler are described in [crash reports](#profiling-and-crash-reports); OS crash diagnostics remain necessary for native stacks.

## Profiling and crash reports

forge.profile() reports callbacks/audio/scripts/physics/frame timings and assets/renderer statistics. renderer_stats() includes draw_calls, batches, triangles, gpu_bytes/budget, glyphs/pages, render_targets, and render_ms. Timings are CPU wall time, not GPU timestamps; frame_ms excludes waiting for the next frame. profile()['localization_translations'] counts translations in the last refresh; unchanged text_key/params at the same localization revision are cached. Language/catalog/key/parameter changes invalidate it. Assigning a literal Entity.text clears its binding; assigning text_key again requests a refreshed translation.

Unhandled runtime errors create JSON in crash-reports beside the current log, with version, scene, time, profile, and message/Python traceback; log-opening behavior remains. forge.crash_report(message) creates a report manually. Fatal native signals/exceptions write native-last.txt; consult OS crash reports/Windows Error Reporting for native stacks. The C++ fatal handler uses a separate stack and minimal safe writes without Python/GL. Reports may contain game paths/messages; the developer/user must separately arrange any transmission.

## Watcher and dispatch diagnostics

Frame listeners use hash membership and deferred removal: O(L) traversal/compaction instead of repeated linear searches. Removed listeners are skipped; new listeners start next frame. Contacts index changed pairs by entity ID then traverse scripts, retaining script order and enter-before-exit. Profile fields: listener_checks, listener_calls, contact_pairs_scanned, contact_callbacks.

The filesystem watcher recursively scans in a worker. The main thread reads prepared results and still performs reload/loading. development.watch_interval is 0.05..10 seconds, default .3, using wall-clock time. It checks paths/python_paths and creation/removal/mtime/size, excluding .git/__pycache__/pyc. It calls no Python/OpenGL and captures no mutable Config. Scan errors preserve the previous snapshot and report diagnostics. profile()['watcher'] includes scans/files/scan_ms/error.

Initial/reconfigured baselines are synchronous; shutdown waits for an active scan. Steady frames do no recursive scanning. Snapshots are metadata, not content hashes; unchanged size/timestamps can hide edits and intervals are not exact reload deadlines. Unchanged extra packages retain state; changed python_paths files invalidate imports, and retired directories/namespaces become unavailable. More than 8192 queued changed paths bounds the queue and invalidates managed roots wholesale. Candidate failure retains old imports/paths/world/playback/watcher. New watcher baselines precede scene reads so edits during preparation are not lost.

Window/input live in window.cpp, frame orchestration in game_loop.cpp, and dispatch/watcher in separate implementations. Renderer still combines GPU orchestration and scene passes; this does not promise fully independent concurrent renderers/runtimes.

## World scaling and benchmarks

forge.find uses a hash index of live entities, supporting removal/pruning/ID reuse. Legacy broadphase sorts X bounds and sends only candidate pairs to the solver in original entity-index order. Pairs are fixed before resolving each substep; a new overlap caused by correction may appear next substep. Dense scenes still have quadratic candidate counts. Raycast/controller/descendant queries remain linear or tree-dependent; this is not an all-purpose spatial index. world_stats() reports entities/indexed/candidate_pairs; the last is legacy-step statistics, not Bullet broadphase.

```sh
python tools/benchmark_world.py build/bin/forge
python tools/benchmark_world.py build/bin/forge --entities 1000 5000 --backend bullet
```

The benchmark creates isolated scenes of separated static boxes, warms bodies, measures 10N find calls and ten physics_step(1/120) calls. Results include Python bridge/transform checks, not GUI or dense dynamic scenes. It supplies no timing gate/FPS promise. Measure your game's distribution and workloads. Scriptless objects do not consume the 8192-per-pass scripted lifecycle limit; physics/memory budgets are independent.

Editor, hierarchy, geometry, and materials have separate implementations. Renderer receives Runtime explicitly. Shared scene/model headers do not pull in the Python API; moved engine.hpp contracts retain core origin (CORE.md). A process still supports one active Python runtime; a second reports an error. Window/input/GPU remain Renderer responsibilities; splitting files does not fully decouple subsystems.

## Validation and test suites

Run these commands from the engine checkout, using the compiled binary. macOS examples use `build/bin/forge`; Windows multi-config builds use `build/bin/Release/forge.exe` (single-config builds may use `build/bin/forge.exe`). Use `.tools/bin/ctest` or `.tools\Scripts\ctest.exe` if the environment is not activated.

```sh
python tools/forge.py validate --no-open-log
ctest --test-dir build -C Release --output-on-failure
```

The base CTest set has **twelve** suites. Enabling the shader translator registers a thirteenth, `shader_compiler` (enabled by the default macOS/Windows native-backend builds). The launcher `test` runs seven integration suites, omitting `numeric`, `packager_paths`, `input_native`, `network_native`, and `render_optimization`.

| Check | Command | Requires |
| --- | --- | --- |
| Numerical arithmetic, packaging paths, culling geometry | `ctest --test-dir build -C Release --output-on-failure -R "^(numeric\|packager_paths\|render_optimization)$"` | Compiled test targets |
| Native input actions, physical key snapshots, capture/profile rules | `ctest --test-dir build -C Release --output-on-failure -R '^input_native$'` | Compiled test target; no window |
| Lifecycle, reload, configuration, localization, packaging | `python tests/integration.py build/bin/forge` | Native runtime; packaging services for OS-specific checks |
| Assets, audio APIs, input/replay, preferences, distribution | `python tests/features.py build/bin/forge` | Native runtime; macOS system packaging/signing services |
| Prefabs, property/skeletal clips, retarget, morph, watchers | `python tests/authoring.py build/bin/forge` | Native runtime |
| Open documents and shell coexistence | `python tests/project_api.py build/bin/forge` | Native runtime |
| PBR, hierarchy, procedural meshes, LOD/culling APIs | `python tests/rendering.py build/bin/forge` | Native runtime |
| Bullet, particles, forces/queries, rejected reload | `python tests/simulation.py build/bin/forge` | Native runtime |
| Native ENet/Valve sockets, bytes, scene/reload lifetime | `ctest --test-dir build -C Release --output-on-failure -R "^(network_native\|networking)$"` | Localhost UDP allowed; `sockets` covered when compiled |
| Optional GLSL/HLSL/MSL translator | `ctest --test-dir build -C Release --output-on-failure -R '^shader_compiler$'` | Translator compiled; no device required |
| UI/text/shader reload/audio hardware | `python tests/graphics.py build/bin/forge` | Desktop, graphics device, audio device unless silent |
| Cameras/UV/batching/shadows/skinning/editor | `python tests/features_graphics.py build/bin/forge` | Desktop/graphics; editor checks require builtin shell |
| Particles/instancing/depth/cameras/reload | `python tests/simulation_graphics.py build/bin/forge` | Desktop/graphics |
| PBR/morph/retarget/editor/LOD/culling pixels | `python tests/rendering_graphics.py build/bin/forge` | Desktop/graphics |

GPU suites are separate from CTest; run them one at a time. Their default backend is OpenGL. `FORGE_TEST_BACKEND=metal` or `direct3d11` selects a compiled native backend on its OS. `FORGE_TEST_SILENT_AUDIO=1` avoids hardware playback and therefore cannot verify it. [Metal](../METAL.md) and [Direct3D](../DIRECT3D11.md) describe backend-specific suites and device validation. `ui_components.py` and `model_fixture.py` are helpers, not standalone suite entry points.

CI builds macOS/Windows with and without builtin editor, runs CTest and standalone verification, and has separate shared/backend-specific graphics jobs. Linux Mesa/Xvfb provides software OpenGL coverage; Windows WARP provides software Direct3D coverage. A configured CI job is not proof that it passed. Software rendering, headless checks, and one device's results do not certify all physical macOS/Windows GPUs, audio hardware, architectures, or OS versions. [Coverage map and release records](../FORGE_2.md) distinguish implementation from actual verification.

## Measure performance

```sh
python tools/benchmark_hotpaths.py build/bin/forge
python tools/benchmark_hotpaths.py build/bin/forge --particles
```

The optional particle benchmark opens a real graphics window. It measures scalar/bulk transforms, rays, Python transfer, and particle work without timing gates or promised FPS. Use `forge.profile()`, `world_stats()`, `physics_stats()`, `asset_stats()`, and `renderer_stats()` to locate your own workload. Rendering optimization has [per-camera counters](../RENDER_OPTIMIZATION.md); it does not suspend game logic/physics or automatically stream the world.

## Troubleshooting

| Symptom | Next action |
| --- | --- |
| Engine is not compiled | Follow [installation](installation.md); `dev` does not compile C++ |
| Missing resource or directory | Check the selected settings file, `paths`, reference group, filename, and symlink containment |
| Python import fails | Use engine run/dev; check configured modules/python_paths and binary-extension Python/OS/architecture compatibility |
| Reload pauses the game | Read the traceback/compiler message in the log, fix the watched file, and retry; initial load failures require relaunch |
| Editor will not reload | Preserve/save the unsaved draft or explicitly load/reset it; conflicts never choose a winner automatically |
| Backend unavailable or device fails | Check compiled support and device requirements; backend changes need restart |
| Blank/incorrect custom shader | Follow the complete shader stage/uniform/attribute contract; verify actual frames on the selected backend |
| Save reports recoverable | Load through SaveManager; validate/migrate before applying game state; recovery does not rewrite the primary automatically |
| Build output already exists | Use a new versioned destination; builds never overwrite an existing output |
| Logs or saves cannot be written in an installed game | Select user storage with a stable application_id; avoid writing into a signed bundle |

Detailed limitations belong to each subsystem page. Forge currently lacks IBL/HDR, cascaded shadows, mesh colliders/joints/navmesh, full bidi/shaping, and fully independent concurrent Python runtimes. Terrain, chunks, voxel water, and genre rules remain game modules.

Networking suites use real loopback traffic, independent native hosts and the embedded Python bridge. They do not verify multiple machines, firewall/NAT conditions, Steam accounts, authenticated P2P/relay, overlay, stats, or matchmaking. The Steam SDK/client branch requires separate target-platform compilation and live-client validation.
