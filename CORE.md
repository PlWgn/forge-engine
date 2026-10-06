# Forge Core and Adaptable Components

[LICENSE](LICENSE) defines the binding terms. This document explains them without adding restrictions.

Forge should remain universal and simple: games select configuration, scenes, and independent modules rather than adopt genre rules embedded in the Core. All source is available for study and modification. Core changes are permitted too, but are not recommended for ordinary game development because its contracts support startup, updates, and packaging. Engine development and bug fixes may require Core changes without approval beyond the license terms.

## What Counts as Core

The list for this license version is exhaustive:

| File | Purpose |
| --- | --- |
| `engine/src/main.cpp` | Entry point, commands, interpreter startup |
| `engine/src/config.cpp` | Configuration reading and validation |
| `engine/src/log.cpp` | Terminal/file logging and error-log opening |
| `engine/src/runtime.cpp` | Game loop and orchestration; API/lifecycle/reload have separate implementations |
| `engine/include/forge/engine.hpp` | Shared Core contracts and module interfaces |
| `tools/packager.py` | Project packaging invoked by the Core |

In 2.2, type declarations moved from `engine.hpp` into `engine/include/forge/types.hpp`, `scene.hpp`, and `image.hpp`. These retain Core origin and license status; the exhaustive LICENSE list does not gain a new independent component. Renaming/moving does not remove Core status. An external component does not become Core merely because it lives under `engine/`.

In 2.3, Python API, lifecycle, and reload code moved from `runtime.cpp` into `python_api.cpp`, `scene_runtime.cpp`, and `reload.cpp`; Python/JSON conversion from `config.cpp`/`runtime.cpp` moved into `python_bridge.cpp`. These continue the listed Core implementations and retain their origin. Splitting responsibilities does not add physics, graphics, or independent modules to the Core list.

## What Can Be Changed for a Game

Everything outside that list may be changed, replaced, or extended unless a component's separate license says otherwise. Examples:

- Configuration, scenes, user scripts, objects, and materials;
- Textures, models, audio, and other game assets;
- `graphics/*`, including GLSL shaders;
- `modules/*`, including Python/C++ extensions;
- World/legacy physics in `world.cpp` and the Bullet bridge in `physics.cpp` / `physics.hpp`;
- Particles in `particles.cpp` / `particles.hpp`, their Python module, and shaders;
- Hierarchy in `hierarchy.cpp`, procedural geometry in `geometry.cpp` / `geometry.hpp`, materials in `material.cpp` / `material.hpp`, and editor in `editor.cpp` / `editor.hpp`;
- Renderer `render.cpp`, separate `particle_render.cpp`, `gpu_resources.cpp`, `text.cpp`, `media.cpp`, related headers, and OpenGL loader `gl.hpp`;
- Audio implementation `audio.cpp`;
- Asset loading, model import, localization, and platform services in `assets.cpp`, `model.cpp`, `localization.cpp`, `platform.cpp`, and additional APIs in `features.cpp`;
- `CMakeLists.txt`, launcher `tools/forge.py`, and build configuration.

Physics/graphics changes alone retain “Uses the Forge engine”. If they also require changes to Core contracts/runtime, use “Built on the Forge engine (modified core)” for your own changes outside official releases/documented approval.

Dependencies and Noto Sans retain their licenses. Permission to change shaders does not cancel the bundled font's SIL OFL. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Identifying Origin

| Situation | Notice on loading screen and in menu |
| --- | --- |
| Unchanged official Core; modified scenes/shaders/physics/other non-Core components | “Uses the Forge engine” |
| Official release or Core changes documented as approved by affected copyright holders | “Uses the Forge engine” |
| Your own Core changes, including fixes outside official releases and without that approval | “Built on the Forge engine (modified core)” |

Core modification needs no separate permission when the license is followed. “Non-recommended” identifies a change's origin rather than its subjective quality, making attribution verifiable.

Compiling unchanged source with another compiler, OS, configuration, or architecture requires no different notice. Replacing a Core implementation in a modified binary requires modified-origin attribution.

## Application to This Repository

This repository is the original official Forge project; starter content comprises development examples. LICENSE section 3 defines your distributed game's UI notices, which the game developer places. The runtime has no hidden attribution checks or UI lockouts.

In 2.4, `dispatch.cpp` and `game_loop.cpp` continue runtime orchestration; `config.hpp`, `assets.hpp`, `logger.hpp`, `localization.hpp`, `renderer.hpp`, and `audio.hpp` split former engine.hpp contracts. This moved code retains Core origin. New independent `file_watch`, `prefab`, `animation`, `animation_api`, `animation_editor`, `morph`, and the extracted adaptable `window` implementation remain extensible; their engine-directory placement does not expand LICENSE. Prefab/animation setup, replacement animation, retargeting, and morph rendering do not inherently require game Core changes.

In 2.5, independent `documents.cpp`/`documents.hpp`, `editor_session.cpp`/`editor_session.hpp`, SDK, schemas, shells, and extension registries are outside the Core list and replaceable. Changes to main/runtime/game_loop/scene_runtime/config and shared headers add project CLI, optional editor hosting, scene candidates, and source metadata; affected Core-origin contracts retain their origin. Games/shells use public APIs without changing them. Disabling ImGui, changing plugins/UI, schema annotations, or extensions alone is not a Core change.

Bugfix 2.5.1 adjusts Core-origin configuration/Python validation, listener membership, authoring persistence, and translation caches in config/main/runtime/scene_runtime/python_api/features and shared headers. These fixes do not expand the Core list. Physics/renderer/audio/preloader/SDK implementations remain replaceable under their existing boundaries; the optional defer_persistence argument is documented in GUIDE.md.


In 2.6, graphics_device, graphics_settings, direct3d11, and shader_compiler are independent adaptable implementations outside the Core list, as are all GLSL/HLSL shaders and editor-backend integration. Core-origin config/main/python_api changes validate renderer choices, expose additive backend capabilities, and provide silent audio for graphical runs. No additional files become Core. Game/shell APIs remain version 1; device changes require restart and shader reload remains transactional.
