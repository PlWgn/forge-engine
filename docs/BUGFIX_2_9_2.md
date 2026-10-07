# Forge 2.9.2 — Metal Pipeline Reuse and GPU Synchronization

[Forge wiki](../GUIDE.md) · [Metal configuration and shaders](METAL.md)

This update addresses runtime stalls caused by rebuilding procedural geometry and forced GPU completion waits. Project/schema/API versions, default OpenGL selection, editor independence, editable shaders and Core boundaries are unchanged. The implementation is engine-wide and contains no pickup or other game-specific rules.

## Changes

- **Pipeline reuse:** Metal keys draw variants by effective vertex formats, offsets, strides, stepping, blend state and depth-attachment presence within each shader program. Mesh handles, vertex buffer identities and revisions no longer invalidate equivalent pipelines. True layout/state differences retain distinct variants. Each program caches up to 256 variants with LRU eviction; program deletion/relinking releases them.
- **Bounded submissions:** Presentation submits asynchronously, with at most three command buffers in flight. Uniform arenas remain owned by their submission until completion; the existing arena budget covers all current/pending/idle chunks together. Capacity or budget pressure waits for safe reuse.
- **Ordered texture updates:** Subupdates copy from retained staging buffers on the GPU, preserving earlier draws without draining the queue. Full texture replacement creates a new resource version. Mutable vertex uploads continue to retain encoded versions even when a mesh/buffer is replaced or deleted.
- **Explicit completion:** Readback, resize and shutdown still drain outstanding work. Completed command errors are collected during later frames or drains. A small Core-origin runtime/renderer shutdown hook checks final GPU work before returning success; errors use the existing terminal/log and exit-result path.
- **Diagnostics:** `renderer_stats()` exposes cumulative draw-pipeline builds/cache entries, submissions, completion waits, explicit drains and current/peak/maximum in-flight commands. See the [Metal guide](METAL.md) for counter semantics. These are diagnostics, not FPS guarantees.

Drawable availability, vsync, actual new layouts, resource uploads, shaders, the driver and insufficient budgets can still cause waits. The fix removes the identified unnecessary work; it does not promise that every game frame is free of stalls. GLSL/MSL overrides and adaptable native graphics modules remain supported.

## Regression checks

After a Metal-enabled native build, use a real macOS desktop/device:

```sh
MTL_DEBUG_LAYER=1 build/metal_backend_tests
MTL_DEBUG_LAYER=1 python tests/metal_graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 python tests/graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 python tests/features_graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 python tests/simulation_graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 python tests/rendering_graphics.py build/bin/forge
```

Run GPU suites sequentially. The native target is intentionally outside headless CTest; GPU-enabled macOS CI runs it alongside native shader checks. It exercises equivalent and different VAOs, stride/offset/format/divisor/disabled-attribute variants, deleted/replaced buffer/texture ownership same-program GLSL relinking, and uniform snapshots across submissions under a 64 KiB arena budget. Integration checks repeatedly replace/recreate procedural meshes and upload new glyphs during play, with actual pixel comparisons. Existing reload, budget, shader, camera, lighting, particle, authoring and editor checks remain relevant.

The original instrumented 2.9.1 implementation reproduced 19 draw-pipeline builds during two-mesh replacement churn, and 58 forced drains in the changing-glyph scene. These counts describe regression fixtures on an Apple M2, not a benchmark of a private game or universal frame-rate improvement.

Windows/Direct3D, Intel Metal GPUs and all macOS versions require their own verification. Tests do not intentionally provoke GPU resets or hardware faults; those completion-error paths are reviewed in source and retain existing diagnostics.

## Verified for this release

On Apple M2 / arm64 / macOS 27.0.1:

- Release compilation and all **12 CTest suites** passed (35.42 seconds).
- The direct native Metal probe passed with Apple's validation layer enabled, including changed layouts, retained draw versions, same-program relinking, 64 KiB uniform reuse and explicit completion.
- **45 Metal GPU tests** passed sequentially with validation enabled: 7 Metal-specific, 8 general graphics, 7 features, 6 simulation and 17 rendering checks. Silent audio was used; this does not verify playback hardware.
- Two OpenGL procedural-mesh/texture regressions passed, exercising the unchanged backend path and shared shutdown hook.
- A fresh standalone directory package passed all **775 manifest files** and four actual Metal scenes with private Python from another working directory under invalid host `PYTHONHOME`/`PYTHONPATH`.
- Changed Python sources parse; documentation links and Git whitespace checks pass.

The procedural replacement regression now builds no additional draw pipelines after warmup, including mesh deletion/recreation and repeated vertex/color replacement. The glyph regression uploads all 26 letter glyphs without extra explicit queue drains between its readbacks. These checks assert counters and pixels rather than timing thresholds.
