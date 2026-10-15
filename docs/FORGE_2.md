# Forge 2.x — Capabilities and Test Coverage

[Forge wiki](../GUIDE.md) · [Documentation index](README.md) · [Release archive](releases/README.md)

This page describes the current implementation and the checks registered for it. A test or CI definition describes coverage; it does not prove that a particular platform or device passed. Historical release results and migrations belong in the release archive.

## Forge 2.13.0 — default editor workspace

The optional builtin shell now provides resizable Hierarchy/Scene/Inspector/Project panels, a bounded log Console, extension commands, language switching, center picking/framing and a grid. Scene rendering has a dedicated target and panel aspect; 2D is letterboxed. Inspector/Create/asset edits use the existing transactional runtime SDK, preserving rejection and undo semantics. Imported-model animation tools open explicitly. [Workspace usage and limits](wiki/editor.md).

The native editor and workspace helper remain independent adaptable modules; this release does not change Core contracts or the exhaustive license boundary. Project/schema/game API/SDK protocol versions remain 1, and the Python SDK package version remains unchanged because its contract is unchanged. The engine version advances for the new default interface. Reparenting now explicitly keeps local transforms; preview locks authoring controls, and initialization callbacks can repeat when applying a candidate. Existing projects and custom shells require no format migration.

GPU coverage adds workspace compositing, postprocessing, panel isolation and three window sizes; existing custom-shell and animation checks remain. Device-specific results must be reported separately; source tests alone do not establish Windows/Direct3D verification.

## Capabilities and implementation coverage

| Area | Implementation | Relevant checks |
| --- | --- | --- |
| Scene lifecycle, Python bridge, localization, reload | C++/embedded Python, shared JSON/Python validation, candidate preparation/rollback | `integration`, `features`, `project_api` |
| Entities, hierarchy, geometry, materials | Indexed IDs, local/world transforms, procedural meshes, PBR maps, alpha modes, immutable optimization definitions | `rendering`, `authoring`, `rendering_graphics.py` |
| Cameras and scene rendering | Render targets, sprite UVs, batching, text atlases/fallback, postprocessing/uniforms, lights/shadows | `features_graphics.py`, `graphics.py`, `rendering_graphics.py` |
| Rendering optimization | Opt-in mesh/texture LOD and distance/frustum/conservative proxy occlusion; replaceable native policy | `render_optimization`, `rendering`, `rendering_graphics.py`; optional `render_policy_extension.py` |
| Animation and authoring | Prefabs, property clips, skeletal layers/fades/markers/masks, rest-delta retargeting, morphs | `authoring`, `rendering_graphics.py` |
| Physics | Legacy AABB by default; optional double-precision Bullet boxes/spheres/capsules, forces, CCD, filters, convex controller | `simulation`, `numeric`, `features`, `simulation_graphics.py` |
| Particles | Seeded, bounded CPU pools, continuous/burst emission, lifetime curves, billboards, instancing/custom-shader path | `simulation`, `simulation_graphics.py` |
| Assets and budgets | Four workers, pinned handles, generation/dependency cache, LRU and explicit budget refusal | `features`, `rendering`, shared GPU suites |
| UI, dialogue, input, replay | Editable Python widgets/menus; native action capture/rebinding, profiles, raw input compatibility | `input_native`, `features`, `features_graphics.py` |
| Audio and subtitles | Channels/fades, spatial/pan/DSP, voice/stream limits, cursor-based subtitles | `features`, `graphics.py` with a device for playback |
| Saves and user storage | Atomic/versioned/checksummed slots, backups/migrations, managed persistence deferral | `integration`, `features`, `project_api` |
| Documents and editor shells | Open JSON/schema, unknown-field preservation, baseline merging/conflicts, JSON-lines/SDK/runtime adapters; optional ImGui | `project_api`, `authoring`, shared GPU suites with/without builtin shell where configured |
| OpenGL, Metal, Direct3D 11 | Shared draw contract, editable GLSL/native shader pairs, optional translator, resource snapshots and explicit synchronization | `shader_compiler` when compiled, shared GPU suites, backend-specific tests |
| Networking and Steam | Default ENet LAN, optional Valve IP sockets, developer-supplied Steamworks SDK, bounded native bytes/events | `network_native`, `networking`; explicit `steam_client.py` for official SDK/client |
| VR and OpenXR | Optional Windows/OpenGL SteamVR/Quest Link; Android arm64 Quest host, stereo views, controller poses/input/haptics | `xr_native`, `xr_bridge`, `quest_packager`; separate target builds/device checks |
| Standalone/protected packaging | Private Python, confined sources/destinations, hashes/notices, optional authenticated desktop content container | `packager_paths`, `protected_content`, `integration`, `features`, `verify_package.py` |

## Test entry points

The desktop CMake configuration registers **16 base CTest suites**, plus `shader_compiler` when the translator is compiled: **17** in the default macOS/Windows native-backend builds. `python tools/forge.py test` runs seven integration suites. Shared GPU suites and backend/device checks are separate. The authoritative names/options are in `CMakeLists.txt`; commands and requirements are in [development and testing](wiki/development.md#validation-and-test-suites).

The configured CI workflows cover macOS/Windows editor ON/OFF and OpenGL-only builds, optional Valve sockets, Windows WARP, Linux Mesa/Xvfb, Metal when a device is available, standalone/protected packages, and optional Windows OpenXR/Android Quest builds. Physical GPU/audio/headset behavior, signing credentials, and store acceptance require the corresponding target environment. CI configuration alone is not evidence of a successful run.

## Implemented limits

| Area | Boundary |
| --- | --- |
| Runtime and reload | One active Python/graphics runtime. Reload reconstructs scene objects and covers managed engine state, not arbitrary Python I/O/network/services or serialization of all game state. |
| Physics | Legacy solver is translational AABB. Bullet has no mesh colliders, joints, navmesh, or step climbing. Convex recovery is bounded, without universal deep-overlap/moving-platform guarantees. Collider dimensions do not inherit visual scale; dynamics are hierarchy roots. |
| Particles | CPU simulation without collisions, lighting, or GPU compute. Following inherits position, not rotation/scale. Alpha sorting is not a combined particle/mesh OIT solution. |
| Materials and shadows | Direct-light PBR without IBL/HDR/glTF extensions; one directional shadow map without cascades. Transparency sorts centers. Author conservative bounds for animation/displacement and solid occluders inside actual opaque geometry. |
| Animation | Layered clips, authored TRS and rest-delta retargeting are available; no full blend-tree/IK/DCC system. Morphs deform on CPU before skinning, with up to 32 targets per part; no exact glTF cubic-spline promise. |
| Text and editor | No bidi/shaping/color emoji. Builtin editor has no visual scripting/gizmos, animation graph, or arbitrary Python-state undo. Custom shells use the same public contracts. |
| Optimization and budgets | Culling changes draw submission, not gameplay. No hardware Hi-Z, automatic decimation, or scene streaming. Resource budgets bound accounted pools, not total process/driver/decoder memory. |
| Input and networking | Replay records input/dt, not external world/RNG state. Networking adds bytes/transports/services, not automatic replication/discovery/game rules; transmitted effects cannot be rolled back. |
| VR and distribution | No hand skeletons, eye tracking, passthrough, composition-layer UI, or Metal/Direct3D XR binding. Protected content deters casual extraction with a recoverable shipped key and runtime plaintext; it is not DRM. |

Detailed contracts: [physics](wiki/physics.md), [particles](wiki/particles.md), [rendering](wiki/rendering.md), [optimization](RENDER_OPTIMIZATION.md), [animation](wiki/animation.md), [persistence](wiki/persistence.md), [networking](wiki/networking.md), [VR](wiki/vr.md), and [packaging](wiki/packaging.md). The [license/Core boundary](../CORE.md) remains exhaustive and unchanged.
