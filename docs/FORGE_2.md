# Forge 2.x — Capabilities and Test Coverage

## Forge 2.10 Native Input

Independent `input_manager`/`input_keys` implement compiled native actions with keyboard chords, physical/generic modifier keys, mouse/wheel controls, fixed/wildcard gamepads, per-binding deadzones and half axes. Contexts, press/release/held/repeat state, conflict inspection/policies and capture/cancel/timeout are public APIs. Capture ignores opening controls and suppresses gameplay actions until release. The facade supplies optional scene subscriptions and SaveManager preferences, preserving rejected reload candidates and new game defaults on profile import. Editable `input.py` demonstrates a localized controls screen; no UI or configuration layout is mandatory.

Raw input and existing Python ActionMap/recordings keep their contracts. Binding profiles use a separate open `forge.input.bindings/1` format, schema and unknown-field preservation; API/project versions remain 1. Native evaluation consumes one typed frame without a Python snapshot round trip. `input_native`, embedded features and shared GPU controls tests cover state/capture/numerics/profiles/reload/UI; optional benchmarks measure native versus Python evaluation without timing gates. The current CTest set is twelve base suites plus optional shader_compiler; launcher tests remain seven integration suites. [API, examples, limits and replacement](wiki/input.md).

## Forge 2.9.2

Metal draw pipelines now cache effective vertex layout and render state, independent of mesh/VAO revisions. Presentation submits up to three command buffers without mandatory per-frame drains; uniforms stay owned until completion under the existing total arena budget. Texture subupdates use ordered staging copies, while allocation creates a separately retained resource version. Readback/resize/shutdown still drain explicitly, and shutdown reports final GPU errors through the runtime result. A native real-device probe and Metal integration regressions check layout distinctions, equivalent replacements, draw snapshots, budgets and glyph updates. Public APIs and editable GLSL/MSL remain available. [Details and actual verification](BUGFIX_2_9_2.md).

## Forge 2.9.1

The stability release fixes pending-packet accounting and empty-message backpressure in ENet, bounds network lifetime records, and compacts retired hosts in one pass. Bullet backend switching validates world poses/gravity before mutation. Font header/table checks reject empty/truncated inputs; font generations include file size, and failed glyph allocation releases bitmap memory. Replay writes use unique atomic temporary files; failed input-map construction retains no frame listener. Whole-project JSON/Python/media validation enforces the existing project path boundary. Procedural LOD references block mesh removal, and scene preloading discovers LOD/emitter resources. Asset worker startup cleans up on partial failure; the Windows Protobuf bootstrap matches Forge's dynamic MSVC runtime. Formats and API versions remain unchanged. [Fixes, regression checks and platform limits](BUGFIX_2_9_1.md).

## Forge 2.8

Native optional rendering optimization provides static model/texture distance LOD with per-camera hysteresis, frustum/distance culling, cached transformed bounds, and bounded conservative solid-proxy occlusion. Gameplay/physics remain active; screen UI/text/particles retain existing paths. The independent `RenderOptimizationPolicy` is replaceable through the public renderer API; `optimization.py` and `render_optimization_example.cpp` demonstrate configuration and native composition. See [settings and limitations](RENDER_OPTIMIZATION.md). Native `render_optimization`, rendering API, and shared rendering GPU tests cover geometry, validation, pixels, counters, cameras, and 2D texture LOD. The base CTest set now has nine suites; optional shader compiler makes ten. Existing launcher test selection remains six integration suites.

## Forge 2.7.1

The stability update covers retired-child world-position setters, draw-scoped custom uniforms, CPU asset/model dependency generations, save-directory redirection/deferred deletion/JSON recovery/size limits, event cleanup, timer compaction/overflow, legacy animation validation, and CLI frame limits. `integration.py`, `rendering.py`, `features.py`, and `rendering_graphics.py` exercise these through native APIs and actual frames, including glTF buffer hot reload. Project/save/API formats stay unchanged; shader and module implementations remain adaptable. See [bugfix details and verification limits](BUGFIX_2_7_1.md).

## Feature Map

This map links features to implementations and reproducible tests under `tests/`. It describes coverage, not the result of a particular run. Public APIs, examples, and commands: [Forge wiki](../GUIDE.md). Core boundaries: [CORE.md](../CORE.md).

| Feature | Implementation | Verification |
| --- | --- | --- |
| Multiple cameras | FBO targets, 2D/3D, layers, @target:name textures | features_graphics: camera/monitor |
| Sprite sheets / batching | Entity.uv, SpriteSheet, compatible CPU quad batches | GPU: two UV regions, ≤4 draw calls for 100 sprites + 200 glyphs |
| Postprocessing / uniforms | Grain, bloom, aberration, CRT, fade/gamma; global/entity uniforms | GPU: frame comparisons/custom GLSL |
| Fallback fonts | TTF chain, matching measurement/drawing, WARN U+code | measure_text/glyph tests; no shaping/RTL |
| Assets / memory | Four workers, handles/pins, preload, CPU LRU/GPU budget | features: status/bytes/preload/budget refusal |
| UI styles/transitions/navigation | Theme, Widget.style/opacity, ScreenStack/state, action navigation | features and existing UI regressions |
| Actions/controllers/events/replay | Contexts/rebinding, GLFW gamepads, snapshots/versioned recording | features: key/mouse/pad/context/press/release/dt |
| Glyph atlases / profiling | Font/raster/codepoint atlas, quad batches, CPU timers/subsystem stats | GPU batching/profile API |
| Pan/spatial/DSP | Voice settings, listener, lowpass/highpass/delay nodes | Native silent PCM and desktop audio tests |
| Subtitles | SRT/JSON/key/params against playback cursor | features: pause/resume/stop |
| Ducking/voices/streaming | Channel rules, priorities/limits/overflow, decode/stream/auto | features: limits/DSP/duck/stream/cursor |
| Physics toggle/active colliders | Independent physics_enabled; active colliders in solver/raycast/controller | Existing regressions/controller checks |
| Particles | Bounded native pools, seeds, lifetime/color/size, attachments, billboards/UV/blending | simulation: capacity/seed/lifetime/teardown; GPU: 5000 quads ≤3 calls, pixel curves/UV, camera/depth/sort/shader rollback |
| Bullet 3D | Double box/sphere/capsule, rotation/forces/torques, friction/restitution/sleep/CCD/masks | simulation: overlap/rays, fast bodies/thin walls, bounce/friction/sleep/gravity, numeric rollback/substep forces |
| Capsule controller | Convex sweep/slide, slope normals, bounded penetration recovery | simulation: rotated ramp landing/sliding; simulation.json example |
| AI/state/timeline | Scheduler budget/clock, StateMachine, Timeline/Sequence | features: ordering/pause/loop/cancellation |
| Reload/persistence | Managed save deferral and explicit deferred callbacks | features: rejected candidate retains three files; accepted candidate updates them |
| .app / signing | Bundle/plist/icon/private Python; ad-hoc packaging/Developer ID/notarytool commands | features: codesign/manifest/launch; real notarization needs credentials |
| User directories/window/crash | Application Support/AppData, preferences/UI, JSON/native crash markers | features: external storage/error reports/preferences |
| AABB character controller | Sweep/slide, gravity/grounded jumping | features: wall blocking/support/jump |
| Lighting/shadows | 16 directional/point/spot lights, ambient, one PCF directional shadow map | GPU: shadowed/unshadowed frames differ |
| Skeletal animation | Assimp hierarchy/TRS, 128-bone GPU palette/four weights | Original glTF fixture/pose/GPU frames |
| glTF/GLB/FBX/DAE | Assimp importers, materials/embedded/external maps | glTF fixture/FBX box; other variants depend on importer |
| Editor | ImGui hierarchy/inspector/assets, transforms/colliders/text/light/camera, add/delete/duplicate/JSON/undo/preview | Editor window/serialization; Python-state limits documented |

## Forge 2.2 and 2.3

| Feature | Implementation | Verification |
| --- | --- | --- |
| PBR | GGX/Smith/Schlick, albedo/normal/MR/AO/emission, JSON/inline/glTF maps | rendering: atomic validation; GPU: factors/maps/embedded emission change pixels |
| Transparency | opaque/mask/blend, depth order, no blend depth writes, cutout shadows | GPU: order/alpha/mask pixels |
| Parent/child objects | Local TRS/world pose, cycles/reparent/subtree lifetime | rendering: JSON/Python/shear/overflow/both backend colliders; GPU: child sprites |
| Procedural meshes | Scene-owned @mesh/revisions/normals/UV/colors/CPU-GPU budgets | rendering: validation/lifetime/budget/reload; GPU: updates/free/name reuse |
| World scaling | Hash-index find, legacy X sweep, indexed Bullet sync | rendering: 10000 boxes/30000 lookups; benchmark_world.py without timing gates |
| Texture sampling/captures | nearest/linear, accounted mip chains, user_screenshot | GPU: mip budgets/user capture |
| Module boundaries | Separate editor/hierarchy/geometry/material, lightweight contracts, explicit Runtime | Existing native/GPU checks; one Python runtime remains |
| Save metadata snapshots | Deep JSON copy for deferred write | rendering: accepted/rejected reload, nested data/metadata, mesh/tree restoration |
| Particle instancing | OpenGL 3.3 52-byte stream, one snapshot, CPU alpha sorting/additive grouping, path cache | GPU: UV/color/rotation/depth, 5001 instances/260052 bytes, zero repeated resolves, custom/fallback shader rollback |
| Transform cache/bulk | Subtree setters and atomic set_positions | rendering: 2000 roots/2001 pose updates, no per-setter audit, parent/child bulk, duplicate/NaN/overflow/destruction/ID reuse |
| Physics cache/batch | Raw shape/pose snapshots, unchanged bodies retained, one sync per raycast_many | simulation: both backends/counters/body changes/destruction/batch validation |
| Python bridge | Direct dict/list/scalars, independent copies, JSON fallback | rendering: nested copies/Unicode/tuples/large ints/key coercion/rejected invalid types |
| Implementation separation | python_api/scene_runtime/reload/python_bridge; particle_render/gpu_resources/text/media | Previous contracts preserved; relocated Core retains origin |

A minimal game needs `engine.json` and a scene; new modules are optional. Defaults remain compatible: project storage, basic lighting for empty lights, existing play_sound/Entity/UI APIs. advanced.json combines features; simulation.json shows Bullet/particles; materials.json shows PBR/hierarchy/live meshes; editor-empty.json starts authoring.

Boundaries: legacy remains AABB; Bullet has box/sphere/capsule without mesh/joints/navmesh/automatic step climbing. Particles use CPU simulation without collisions/GPU compute/shadows; local following inherits position only. PBR has no IBL/environment maps/HDR; one shadow map has no cascades. Animation supports layers/morph/retarget since 2.4 but no full blend-tree graph. Text lacks bidi/shaping; editor lacks visual scripting/gizmos. Replay stores input/dt, not external services/RNG. Crash reporting sends nothing automatically. macOS results do not establish Windows behavior.

Current CI runs nine base CTest suites on macOS/Windows, the optional shader_compiler suite (enabled by default for native macOS/Windows backends), and four GPU suites on Linux Mesa/Xvfb with virtual audio. Windows also runs shared and Direct3D-specific WARP checks. The workflow describes checks, not proof of an unexecuted result. GPU suites are separate from CTest; the wiki documents [PBR](wiki/materials.md), [hierarchy](wiki/entities.md), and [geometry](wiki/assets.md) contracts and commands.

Dynamic bodies must be hierarchy roots; visual children and static/kinematic child colliders are allowed. Render matrices inherit scale while collider dimensions remain explicit. Blend sorts centers without OIT or joint particle sorting. Material channels/first UV set are fixed; arbitrary vertex layout requires graphics changes. Procedural vertices do not serialize into scene JSON. Python/window/input/GPU are not completely isolated; concurrent Python runtimes are unsupported.

`tools/benchmark_hotpaths.py` measures scalar setters, repeated rays, and Python/JSON roundtrips in a temporary project; --particles adds actual rendering. This is a local benchmark without timing assertions/FPS guarantees. Public-field audits remain linear and legacy rays scan colliders; batching reduces audits. IBL/HDR/cascades/mesh colliders/joints/navmesh/bidi/shaping remain absent.

## Forge 2.4

| Feature | Implementation | Verification |
| --- | --- | --- |
| Prefab hierarchy/inheritance | prefab.cpp/prefabs.py, JSON Merge Patch, local IDs/atomic creation | authoring: independent copies/attachment/cycles/duplicate IDs/overflow rollback |
| Layered animation/playback | animation.cpp/api, masks/TRS blending/interruptible fades/markers | authoring: weighted poses/authored keys/pause/seek/loop/events/budget refusal |
| Retarget | Name mapping, local rest delta, translation_scale | authoring: renamed/proportioned skeleton; GPU: actual movement |
| Morph targets | Imported deltas/default/animated weights, CPU deformation before GPU skinning | authoring: weights/numeric refusal; GPU: per-instance pixel isolation |
| Property clips/state machine | Python tracks/events/parameters/triggers/exit_time | authoring: 2D playback/native graph transitions |
| Animation editor | Layers/scrub/markers/morphs/retarget JSON/prefab export | GPU: panel rendering; API authoring checks; button interaction requires manual QA |
| Dispatch | Listener IDs/indexed contact changes | authoring: 5000 listeners/add/remove; lifecycle/contact regressions |
| Background watcher | Worker snapshots of paths/python_paths | Reload/path/rollback tests; metadata rather than content hashes |
| Stable extensions/build | API/capabilities/light contracts/shader dependencies/Threads | Compile/CTest/standalone manifest-notice-launch verification |

The current tree has nine base CTest suites, an optional shader_compiler suite, and four shared GPU suites. Retargeting excludes IK/anatomy inference/foot locking; morphs use CPU upload, up to 32 targets/part. The editor supports playback/layers/markers/settings/TRS keys without a state-machine node graph. One active Python runtime and renderer orchestration boundaries remain. The [animation wiki](wiki/animation.md) details defaults, budgets, and compatibility.

## Forge 2.5

| Feature | Implementation | Verification |
| --- | --- | --- |
| Shared document format | Schemas/documents.cpp/JSON Patch/ID merge/optimistic atomic commit | project_api: independent/manual/unknown fields/order/delete/conflicts/invalid paths/numbers |
| Shells without builtin GUI | FORGE_WITH_EDITOR, JSON-lines/SDK/terminal/viewport examples | CLI/native adapters; macOS/Windows ON/OFF CI |
| Public runtime editing | editor_session.cpp/editor_command/shared selection/history/lifecycle | Paused patch/undo/redo and script/numeric rollback |
| Shared extensions | Client/RuntimeClient registry and Extension commands panel | Same extension in external/in-process hosts; GUI rendering |
| Coexistence with manual edits | Authored delta projection/disk merge/unknown-field preservation | Original camera/entity metadata/defaults/conflicts retain disk/draft |

Current CTest has nine base suites plus optional shader_compiler; launcher test has six integration suites; four shared GPU suites remain separate. Open format/API need no SDK/UI for game run/build. API 1 lacks third-party viewport embedding, remote live-game RPC, arbitrary Python/I/O undo, ImGui widget ABI, or distributed locking. Unsaved editor changes block automatic reload without losing the world; explicit load/reset is user-controlled. See [PROJECT_API.md](PROJECT_API.md) and the [editor wiki](wiki/editor.md).

## Forge 2.5.1

Bugfix retains open format/API 1, optional editor, and existing defaults.

| Fix | Regression coverage |
| --- | --- |
| Editor listener/geometry/save/write/delete rollback; shell recovery after reload failure | project_api |
| Integer dimensions/versions, atomic window requests, strict audio settings | project_api/features/features_graphics |
| Save symlinks/temp; nested copy symlinks/cycles; game tests directories preserved | integration/packager_paths/standalone package |
| python_paths syntax and reserving explicit forward IDs | integration |
| Legacy arithmetic rollback and consistent Bullet setters | integration/simulation |
| Text overflow/int32 uniforms/large finite camera-light directions | integration/rendering_graphics |
| Failed replacement decode, ducking cleanup, prefab/PBR preload | features/existing audio-GPU suites |
| Translation key/params/revision caching | integration/localization/UI |

Release validation on macOS arm64 passed eight CTest suites with editor ON/OFF, four desktop/OpenGL/audio suites with editor (29 tests), validate, and standalone verification. Windows/Linux were not run locally. A localization benchmark used 4000 entities, three 125-frame headless runs excluding the first five frames: median frame_ms was 2.01 ms in 2.5 versus 0.253 ms in 2.5.1. This measures that scenario, not overall game performance.


## Forge 2.6

| Feature | Implementation | Verification |
| --- | --- | --- |
| Optional Direct3D 11 / retained OpenGL | graphics_device/direct3d11, configuration selection, GLFW without GL context | Native configuration/capability regressions, Windows cross-compilation, Windows WARP GPU job |
| Editable portable GLSL | Linked glslang/SPIR-V/SPIRV-Cross/HLSL, scalar/vector/matrix arrays/sampler mapping | Optional shader_compiler CTest; real D3DCompile on Windows, shared GPU suites |
| Native HLSL overrides | Per-pipeline stage pairs, reflected uniforms, source examples | direct3d_graphics: uniforms/UV/clipping/orientation/reload recovery |
| Backend-neutral editor/presentation | ImGui OpenGL/DX11 choice, camera/depth targets, screenshots, vsync/resize | Shared UI/editor/PBR/particle GPU suites with WARP; separate OpenGL suites |
| OpenGL-only delivery / compiler notices | Optional build/bootstrap flags, static compiler linkage, complete source assets | Packaging/manifest/standalone graphical verifier, ON/OFF editor CI |

At the 2.6 release there were eight base CTest suites and one additional shader_compiler suite when enabled; the current suite list is in the [testing wiki](wiki/development.md#validation-and-test-suites). Direct3D is Windows-only, feature level 11_0/SM5, without DX12/DXR, compute/geometry/tessellation extensions, or automatic device-loss recovery. OpenGL defaults and game APIs remain. Changing devices requires restart; shader edits retain transactional reload. One graphics/Python runtime remains. Hardware performance and audio are not established by WARP. [Contracts and actual verification limits](DIRECT3D11.md).


## Forge 2.7

| Capability | Implementation | Verification |
| --- | --- | --- |
| Optional native macOS Metal | Independent metal.mm device/resources/pipelines/readback and optional ImGui backend | Shared GPU suites and Metal-specific pixels on Apple M2 |
| Editable portable/native shaders | Linked GLSL to MSL, retained public names/logical array shapes, MSL pairs/entry points | Real device compilation, custom uniforms, rollback/recovery; HLSL varying-order regression |
| Platform/shell independence | OpenGL default; macOS Metal/Windows Direct3D selectable; editor-free builds | Configuration/native suites, custom shell, standalone verifier |

Auto selects compiled Metal on macOS in 2.7; explicit OpenGL retains previous behavior. Format/shell API stays 1. Uniform snapshots have a separate bounded arena; presentation is synchronized, without async-throughput/performance claims. No compute/HDR/IBL/device-recovery/multi-runtime feature is implied. [Metal contracts and limitations](METAL.md).


## Forge 2.9

| Capability | Implementation | Verification |
| --- | --- | --- |
| Independent native LAN | ENet reliable/unreliable binary messages, channels, frame servicing, bounded queues/stats | Real localhost native and embedded Python exchanges, empty/binary/fragmented packets |
| Optional Valve IP LAN | GameNetworkingSockets adapter, IPv4/IPv6, same host contract | Compiled pinned open-source backend and real localhost exchange on macOS arm64 |
| Managed sessions | Scene scope and named application hosts, rejected candidate cleanup and guarded wire effects | Native ownership checks and successful/rejected reload regression |
| Optional Steamworks | SDK-configured platform service, Steam IP/P2P, stats/achievements, friends/presence/overlay and lobbies | Disabled/unavailable SDK paths verified; official SDK/client branch requires target-platform compilation and live-client tests |
| Modular delivery | Public transport factory, replaceable Python facades, private dependency builder/notices, editable project App ID | Default/optional build contracts, source fixtures and standalone private-Python packaging |

Project/schema/API versions remain 1. Steam defaults off; App ID 480 is for development testing and can be replaced without C++ recompilation. No automatic game replication, discovery, remote execution, mandatory editor/account/service, Cloud/Workshop/game-server/inventory feature is implied. Valve standalone IP tests do not establish Steam-client behavior. [Networking wiki and current limitations](wiki/networking.md).
