# Forge Release History

[Current wiki](../../GUIDE.md) · [Documentation index](../README.md) · [Current capabilities and coverage](../FORGE_2.md)

This archive describes when features and fixes arrived. Past test counts, commands, measurements, and platform results apply to the named release; current test requirements are maintained in the [testing guide](../wiki/development.md#validation-and-test-suites). Project/schema/game API/SDK versions are independent of engine release numbers.

## Releases

| Version | Changes and migration records |
| --- | --- |
| [2.13.0](../FORGE_2.md#forge-2130--default-editor-workspace) | Default workspace, scene viewport, searchable hierarchy/assets, transactional inspector and log console |
| [2.12.2](../BUGFIX_2_12_2.md) | Documentation organization, current test inventory, complete starter documentation |
| [2.12.1](BUGFIX_2_12_1.md) | Shared JSON/Python scene validation, preloader cleanup/retry, VR Euler order, hierarchy/cache bookkeeping |
| 2.12.0 | Optional authenticated/encrypted desktop content; shipped key and runtime plaintext remain explicit limits. [Current packaging contract](../wiki/packaging.md#protected-content-builds-212) |
| 2.11.0 | Optional OpenXR/SteamVR/Quest Link and standalone Quest host/build tooling. [Current VR contract](../wiki/vr.md) |
| [2.10.1](BUGFIX_2_10_1.md) | Audio use-after-free prevention, LAN peer lifetime, reload/import recovery, save/document temporary files, asset/input/model fixes |
| 2.10.0 | Native action input, capture/rebinding, contexts, profiles, optional controls UI. [Current input contract](../wiki/input.md) |
| [2.9.2](BUGFIX_2_9_2.md) | Metal pipeline reuse and bounded asynchronous submissions |
| [2.9.1](BUGFIX_2_9_1.md) | Network accounting/lifetimes, font/model/asset checks, persistence and packaging fixes |
| 2.9.0 | ENet LAN, optional Valve sockets and developer-supplied Steamworks SDK/services. [Current networking contract](../wiki/networking.md) |
| 2.8.0 | Opt-in LOD, distance/frustum/proxy occlusion, public rendering-policy interface. [Current optimization reference](../RENDER_OPTIMIZATION.md) |
| [2.7.1](BUGFIX_2_7_1.md) | Retired-entity safety, draw-scoped uniforms, cache generations, saves/events/timers/numeric fixes |
| 2.7.0 | Optional macOS Metal, GLSL-to-MSL, editable native stage pairs. `auto` can select Metal after rebuilding; select `opengl` to retain the previous device. [Current Metal reference](../METAL.md) |
| 2.6.0 | Optional Windows Direct3D 11, GLSL translation and native HLSL, independent silent audio. [Current Direct3D reference](../DIRECT3D11.md) |
| [2.5.1](BUGFIX_2_5_1.md) | Transactional authoring/persistence, translation caching and renderer/audio/path fixes |
| 2.5.0 | Open JSON document API, optional builtin editor, shell/SDK/extension coexistence. [Current project API](../PROJECT_API.md) |
| 2.4.0 | Prefab hierarchies, property/skeletal layers/fades/events, retargeting/morphs, editor authoring and worker watcher. [Current animation contract](../wiki/animation.md) |
| 2.3.0 | Particle instancing, subtree setters/bulk transforms/queries, changed-body synchronization and direct Python bridge conversion |
| 2.2.0 | PBR/material maps/transparency, local/world hierarchies, procedural geometry, indexed entity lookup and legacy broadphase |
| 2.1.0 | Seeded native particles and optional double-precision Bullet 3D |
| 2.0.0 | Cameras/render targets, UV/batching/text atlases, postprocessing, asset handles/budgets, extended input/audio/AI/editor capabilities |
| 1.2.x | UI/layout/dialogue/menus, save-slot recovery, localization and early packaging/numeric corrections; superseded by the current manuals |

## Historical Direct3D 2.6 validation record

The 2.6 development session on macOS passed nine CTest suites with editor/translator enabled, eight base suites with both disabled, 30 shared OpenGL GPU tests, an alternative viewport shell, fresh-project checks and standalone headless/OpenGL verification. Direct3D sources were cross-compiled and a stubbed link probe resolved the Windows graphics libraries. That record did not establish an executed Windows engine/device test or a completed CI run. Current backend checks and platform limits are in the reference guides.
