# Forge 2.12.1 — Stability and Optimization Fixes

Historical release record. For current behavior and commands, use the [Forge wiki](../../GUIDE.md).

[Forge wiki](../../GUIDE.md) · [Release coverage](../FORGE_2.md)

This maintenance release preserves project/schema/API/SDK versions and existing defaults. Core-origin changes share scene settings validation through `scene_runtime.cpp` and the `scene.hpp` World contract; see [CORE.md](../../CORE.md).

## Fixed behavior

| Area | Problem | Result |
| --- | --- | --- |
| Scene builders | Python descriptions accepted extra vector components and converted booleans to numeric camera/gravity/color values that JSON scenes rejected. | Both paths share checked settings validation. Builder-created objects/resources and omitted view settings survive; invalid reload candidates preserve the old world. |
| Preloader cleanup | Failed construction retained acquired asset pins through exception tracebacks. | Explicit cleanup releases partial handles before propagating the error. |
| Preloader retries | A model was marked expanded before all its textures were discovered; an error could cause later polls to report readiness while skipping a dependency. | Mark expansion complete only after dependency discovery succeeds. Restore a missing texture and retry; failed native handles still need replacement. |
| VR orientations | Quaternion-to-Euler conversion used a different composition order from native Entity transforms. | Combined-axis rotations and gimbal poles reproduce the tracked quaternion's matrix and aim direction. |
| Deep hierarchies | Recursive children/cascade destruction repeatedly scanned all entities for reverse-declared chains. | Index parent relationships once and preserve the previous scan-wave order through an ordered work queue. |
| Asset bookkeeping | Each cache miss scanned unrelated entries for failed generations; every `asset_stats()` call scanned the cache to count loads, including in frame profiling. | Use the matching key range for generation cleanup and maintain loading counts under the existing mutex. Pins, eviction and memory accounting retain their contracts. |

## Compatibility and measurements

Malformed Python scene vectors and boolean numeric fields are now errors, matching JSON. Supply exactly three numeric components for gravity/camera vectors, four for background, and a numeric fov in `(0,179)`. Valid scenes need no migration. Existing projects using the incorrect VR rotation conversion should remove any compensating conversion after updating the helper.

A local Release benchmark with 4,000 reverse-declared entities and 4,000 distinct small assets measured recursive traversal at about 1,096 ms before and 1.7 ms after; 1,000 asset-stat calls fell from about 16 ms to 1.5 ms. Asset requests fell from about 190 ms to 138 ms. These are individual local measurements, not timing gates or guarantees for other hardware/workloads.

## Regression coverage

Embedded regressions cover JSON/Python settings parity, builder side effects, rejected reload preservation/recovery, retained preloader exceptions, dependency retry, shared success/failure loading counts, deep hierarchy ordering/destruction, and combined/gimbal-pole VR rotation matrices. Native and packaging checks remain in CTest. Math/headless tests do not verify physical headset tracking, rendering or haptics; Windows/Direct3D and Android/Quest require target-platform validation.

The macOS Release build passed all 17 CTest suites, whole-project validation, starter generation/validation, and all 17 rendering pixel tests on each of OpenGL and Metal. GPU runs used silent audio and do not establish hardware playback behavior. Commands from the source root:

```sh
.tools/bin/python tools/forge.py compile
.tools/bin/ctest --test-dir build -C Release --output-on-failure
.tools/bin/python tools/forge.py validate --no-open-log
FORGE_TEST_BACKEND=opengl FORGE_TEST_SILENT_AUDIO=1 .tools/bin/python tests/rendering_graphics.py build/bin/forge
FORGE_TEST_BACKEND=metal FORGE_TEST_SILENT_AUDIO=1 .tools/bin/python tests/rendering_graphics.py build/bin/forge
```

Networking and macOS packaging/icon services require OS access beyond a restricted sandbox; the full suite passed with that access. Local benchmark files, screenshots and detailed logs remain ignored artifacts rather than distribution requirements.
