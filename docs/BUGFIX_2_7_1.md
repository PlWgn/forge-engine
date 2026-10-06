# Forge 2.7.1 — Stability and Resource Cleanup

This update preserves project format, Python API, shell protocol, and save envelope version 1. OpenGL remains the default; Metal and Windows Direct3D 11 remain optional. The editor, built-in examples, editable shaders, license, and exhaustive Core boundary are unchanged.

## Fixes

- **Retired entities.** Setting `world_position` on a destroyed child could dereference its missing parent and crash the process. The setter now rejects entities outside the current world before inspecting the hierarchy.
- **Shader isolation.** Custom uniforms from one entity leaked into later entities and frames when an override was absent. Removed overrides now restore the global value or zero; removed postprocess overrides reset to zero. Engine uniforms are applied after cleanup. State belongs to its shader resources and follows reload commit/rollback.
- **Asset generations.** The CPU cache could return old bytes after a size change with preserved modification time. Generation keys now include file size. Pinned handles retain their original generation. Oversized decoded assets are refused before evicting otherwise usable entries, with overflow-safe budget arithmetic.
- **Model sidecars.** Editing an external glTF buffer or OBJ material file left the main model's metadata unchanged, so cached imported data survived reload. Importer reads now record dependency paths, resolved targets, times, and sizes. Changed/missing dependencies produce a new generation; old pinned models retain their data and budget accounting. Failed model requests can retry after a sidecar is repaired.
- **Storage boundaries.** A directory redirected after creating `SaveManager` could escape storage. Each operation, including slot listing, rechecks the native storage boundary; deferred deletion validates again at commit. Changing the configured storage location requires a new manager.
- **Save recovery.** Excessive JSON nesting could raise `RecursionError` outside corruption handling, bypassing backup recovery and menu status reporting. It now follows the ordinary corrupt/recoverable path. `max_bytes` requires a positive integer; bool, fractional, NaN, and infinite values are rejected.
- **Event cleanup.** Unsubscribe functions retained callback owners and repeated cleanup raised errors. Cleanup is now idempotent and releases the subscription's callback. Unknown emitted names do not accumulate empty listener entries; duplicate subscriptions and dispatch order remain supported.
- **Timer queues.** Cancellation retained every dead heap entry until its deadline, allowing memory and skipped work to accumulate. Amortized compaction bounds retained tombstones while preserving live timer order and callback budgets.
- **Timer arithmetic.** Finite inputs could overflow the clock or deadline. Invalid computed times are rejected before mutation. A repeating timer whose next deadline overflows is retired with `ValueError`; its already executed callback is not undone.
- **Legacy animation.** `Tween` accepted invalid durations, vector shapes, and time steps; failed native setters advanced its clock. `SpriteAnimation` committed elapsed time before converting an overflowing frame number. Inputs/intermediates are now checked and elapsed time commits after successful property application. Interpolation avoids subtracting opposite large endpoints.
- **CLI frame limits.** `--frames 2junk`, `2.5`, and `1e3` silently executed only their integer prefix. The complete argument must now parse as a positive integer in the native `int` range; invalid/overflowing arguments report the option and allowed range.

The native uniform helper also checks vector length before writing its four-component stack buffer. Public JSON/Python validation remains in place; this additionally protects calls from C++ extensions.

## Optimization and Compatibility

Timer compaction bounds retained canceled entries relative to live work. Event cleanup releases references and avoids empty namespace growth. The renderer passes uniform JSON by reference and tracks only reset values, avoiding repeated deep copies of global uniform maps during draws. These are resource/work reductions, not a general FPS guarantee.

An isolated comparison of the 2.7.0 and 2.7.1 Scheduler modules inside the same native runtime scheduled ten live timers and canceled 5,000 future timers. Retained heap entries fell from 5,010 to 38. Both preserved the ten callbacks' order and a budget of three callbacks per update. This measures queue retention rather than game FPS or a timing threshold.

Set explicit global defaults for shader parameters that need nonzero values; an earlier draw is no longer an implicit default. Custom uniform types must match the shader, and `u_*` names remain engine-owned. Valid vector tweens and ordinary timers retain their APIs. Previously accepted invalid input now raises an error.

Cache invalidation uses metadata, not content hashes. Same-size edits with preserved modification time remain undetectable through that key alone. Save checks do not sandbox arbitrary Python I/O or guarantee protection against concurrent filesystem replacement by another process. Managed persistence queues are not multi-file atomic transactions.

## Verification

New regressions reproduce the retired-child crash, stale cache reads, shader leakage, storage/deferred-delete escapes, damaged-JSON recovery failure, retained timers/callbacks, and invalid arithmetic. They run inside the real embedded Python runtime; shader tests compare actual pixels. Existing lifecycle, reload rejection, authoring, physics, packaging, and compiler suites remain required.

The only Core implementation change is the CLI argument validation in `main.cpp`; it does not alter valid launch commands. Other fixes use adaptable modules and require no game Core edits. API and format versions remain unchanged.

Release checks on macOS arm64 / Apple M2, Release build, CMake 3.31.6, CPython 3.14.3, with editor and Metal enabled:

- Compilation, project validation, and all nine CTest suites passed.
- All four shared GPU suites passed on OpenGL (33 tests) and Metal (33 tests); the five native Metal tests also passed. Metal ran with `MTL_DEBUG_LAYER=1`; graphical tests used silent audio.
- The new standalone directory's 764 manifest hashes/notices were checked. Welcome, simulation, and authoring launched with private Python from another working directory and invalid host `PYTHONHOME`/`PYTHONPATH`, in headless, OpenGL, and Metal modes.
- A fresh `init` starter retained the new release guide and passed validation. The native features suite also checked macOS `.app` packaging/signing/launch.

Windows/Direct3D, Intel macOS, and Linux were not executed locally for this release. CI remains configured for those relevant build/compiler/GPU paths, but its configuration does not establish an unexecuted result. Physical audio playback was not retested; this update changes no audio implementation. These checks do not establish absence of every possible bug.

Release verification commands:

```sh
python tools/forge.py compile
python tools/forge.py validate --no-open-log
ctest --test-dir build -C Release --output-on-failure
FORGE_TEST_BACKEND=opengl FORGE_TEST_SILENT_AUDIO=1 python tests/rendering_graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 MTL_DEBUG_LAYER=1 python tests/rendering_graphics.py build/bin/forge
python tools/forge.py build --output dist/Forge-2.7.1
python tools/verify_package.py dist/Forge-2.7.1
```

The full GPU checks additionally include `graphics.py`, `features_graphics.py`, and `simulation_graphics.py`; Metal has `metal_graphics.py`. GPU suites run sequentially. Windows uses its current `forge.exe`; Direct3D tests require a Windows device or WARP. Headless/compiler checks do not establish graphical correctness, silent audio does not verify speakers, and one physical Mac does not establish Intel Mac or Windows behavior.
