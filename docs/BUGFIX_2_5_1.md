# Forge 2.5.1 — Bugfix

Fixes following the Forge 2.5 review. Project format, shell API, and save envelope remain version 1. The builtin editor stays optional; licenses, Core boundaries, and examples are preserved.

## Confirmed Bugs

- **Editor rollback.** Rejected apply could modify shared procedural geometry or write/delete saves. Candidates now isolate registries and defer saves until commit. Listener membership restores on failure; synchronous apply inside on_frame retires remaining old-scene callbacks.
- **Shell recovery.** Custom on_update stopped with gameplay after rejected reload. It now continues, allowing explicit load or watched-file repair.
- **Save/package paths.** Save/backup symlinks could escape storage; core save could overwrite an existing .tmp. Nested packaging symlinks could copy external files or cycle. Checks now precede writes/copying. Game folders test/tests/tkinter are no longer dropped as CPython test content.
- **Project validation.** Fractional schema_version/window sizes truncated, and integer overflow could produce another size. Integers/ranges are strict; set_window validates the complete request. validate/build includes python_paths syntax. Generated entity_N IDs avoid explicit IDs declared later.
- **Physics.** Failure in a later body could leave earlier bodies moved. Legacy restores the entire step; collision corrections refresh children. Bullet mode/gravity setters reject invalid changes before mutation.
- **Graphics/numbers.** Large text measurement could return Infinity and maximum int32 uniforms were wrongly rejected. Large finite camera/light directions overflowed float normalization, producing black frames. Double intermediates are checked before narrowing; audio listener normalization follows the same principle.
- **Audio.** Failed decode at capacity stopped the old voice. Decode/DSP prepare before replacement. Settings/options reject fractional limits and invalid types; removing ducking rules releases their gains.
- **Preload.** Inherited prefab assets and PBR maps are included; @mesh/@target are not file paths. Loaders can be reused after close.

## Optimization

Entities cache translation key, parameters, and localization revision. Unchanged text no longer translates each frame; language/key/parameter changes refresh it. profile()['localization_translations'] reports the last refresh count. Derived cache state is not serialized.

On macOS arm64, an isolated headless benchmark with 4000 localized entities and no physics/rendering used three 125-frame runs, excluding the first five frames. Median frame_ms across runs: 2.0149 ms for 2.5 and 0.2533 ms for 2.5.1, about 7.95 times less. Entity traversal remains; this is not a general game-FPS claim.

## Compatibility

Supported projects remain compatible. Strict validation intentionally rejects inputs previously truncated or allowed to corrupt state. Config window dimensions: integer 1..16384; set_window: width 320..16384, height 240..16384. fullscreen/vsync/stream/spatial/follow_camera require bool; voice limits are integers 1..1024.

Ordinary startup retains immediate save/write/delete. Reload/JSON authoring defer until commit. defer_persistence(callable,include_initialization=True) retains its default; False excludes ordinary initialization only. Arbitrary Python I/O is not rolled back; callback queues are not multi-file atomic transactions. See [guide sections 18.8 and 24](../GUIDE.md) and [PROJECT_API.md](PROJECT_API.md).

## Release Validation

On macOS arm64, Release, CMake 3.31.6:

- Compilation/full CTest: 8/8 with FORGE_WITH_EDITOR=ON and 8/8 with OFF;
- graphics/features_graphics/simulation_graphics/rendering_graphics: 29/29 on actual desktop/OpenGL/audio hardware;
- validate, standalone directory packaging, hashes/notices, and three scenes launched from another cwd with invalid PYTHONHOME/PYTHONPATH;
- features additionally checks macOS .app/signing/standalone launch.

New regressions reproduced failures against the previous binary/code before fixes. Windows/Linux were not run locally; their workflow checks remain. This does not establish absence of all bugs or add mesh/joints/navmesh, IBL/HDR, RTL/shaping, or game terrain/voxel logic.

## Updating

Update sources/modules, install pinned dependencies, and recompile:

```sh
python tools/forge.py compile
python tools/forge.py validate --no-open-log
ctest --test-dir build -C Release --output-on-failure
```

Add --without-editor to compile for the optional-shell variant. Package into a new directory with build --output dist/MyGame-2.5.1; existing distributions are not replaced. On Windows use python.exe/ctest.exe from .tools\Scripts or an activated environment.
