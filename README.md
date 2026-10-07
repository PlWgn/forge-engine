# Forge 2.9.2

The Metal update reuses pipelines across equivalent mesh layouts, submits bounded asynchronous GPU work, and queues texture updates without forced completion waits. Readback, resize and shutdown retain explicit synchronization and diagnostics. [Fixes and verification](docs/BUGFIX_2_9_2.md).

Modular game runtime: C++17, Python 3.10+, GLFW, OpenGL 3.3 / optional Metal and Direct3D 11, macOS and Windows.

Forge 2.9 adds independent native ENet LAN, optional open-source Valve IP sockets, and an optional Steamworks SDK adapter for Steam IP/P2P, identity, friends, presence, overlay, achievements/stats and lobbies. Steam is disabled by default; the testing App ID 480 is editable in each project. Bounded messages use bytes directly, and named sessions survive scene/reload changes. [Setup, API, ownership and verification limits](docs/wiki/networking.md). Steam-client integration needs your official SDK and target-platform verification; it is not established by the open-source LAN tests.

Forge 2.8 adds optional native LOD, frustum/distance culling, and conservative occlusion with editable solid proxies. One backend-independent policy serves OpenGL, Metal, and Direct3D; a public C++ interface allows replacement without modifying the Core. Existing projects keep optimization disabled by default. [Setup, examples, customization, and limits](docs/RENDER_OPTIMIZATION.md).

Bugfix 2.7.1 fixes a retired-entity crash, shader values leaking between draws, stale asset generations, save-path/recovery checks, and timer/animation arithmetic. Canceled timers use bounded retained queues; event cleanup releases callbacks. Project and shell APIs remain version 1. [Fixes, compatibility, and validation](docs/BUGFIX_2_7_1.md).

Forge 2.7 adds optional native Metal on macOS/Apple Silicon, retaining OpenGL and Windows Direct3D 11. GLSL stays portable/editable; native MSL overrides are optional. [Metal setup, custom shaders, and limits](docs/METAL.md).

Forge 2.6 adds an optional Windows Direct3D 11 backend while keeping OpenGL as the default. Existing GLSL shaders remain editable and are translated for Direct3D; optional native HLSL pairs let projects customize individual pipelines. Scripts/scenes retain their APIs. [Backend selection, shader authoring, build switches, and verification limits](docs/DIRECT3D11.md).

Bugfix 2.5.1 corrects editor rollback, saves, path/numeric checks, audio, and PBR preloading. Unchanged text entities are no longer translated every frame. Project format and shell API remain version 1. [Fixes and release validation](docs/BUGFIX_2_5_1.md).

This repository contains source code. .tools, build, dist, and downloaded libraries are not tracked; vendor holds a README and dependency lock. Install dependencies and compile the C++ engine first. Run commands from the repository root.

On macOS, install Command Line Tools and Python 3.10+ from python.org:

```sh
python3 -m venv .tools
.tools/bin/python -m pip install cmake==3.31.6
.tools/bin/python tools/dependencies.py
.tools/bin/python tools/forge.py compile
.tools/bin/python tools/forge.py dev
.tools/bin/python tools/forge.py build --output dist/MyGame
.tools/bin/python tools/forge.py dev --scene advanced.json
.tools/bin/python tools/forge.py edit --scene editor-empty.json
.tools/bin/python tools/forge.py build --output dist/MyGame.app
```

On Windows, install Python x64 and Visual Studio 2022 Build Tools with Desktop development with C++; use Developer PowerShell:

```powershell
python -m venv .tools
.tools\Scripts\python.exe -m pip install cmake==3.31.6
.tools\Scripts\python.exe tools/dependencies.py
.tools\Scripts\python.exe tools/forge.py compile
.tools\Scripts\python.exe tools/forge.py dev
.tools\Scripts\python.exe tools/forge.py build --output dist/MyGame
```

compile creates the engine executable, dev runs content without packaging, and build creates a standalone game in a new dist/MyGame directory. Python/JSON/asset edits do not require recompiling C++. Internet access is needed for initial dependency setup.

A/D moves; Space jumps; Enter advances dialogue; F5 saves/plays audio; 2 opens 3D; **3 opens the UI with menus, history, and slots**; 1 returns to 2D; Escape exits.

[Full guide](GUIDE.md) · [Configuration](engine.json) · [Forge license](LICENSE) · [Attribution](ATTRIBUTION.md) · [Dependency licenses](THIRD_PARTY.md)

Creating a game needs no Core changes. Content directories are configured in JSON. Python API: import forge. Packaged games contain a private Python runtime.

Forge 1.2 supplies Canvas, Row/Column, buttons, sliders, scrolling, and text wrapping. Large text handles HiDPI. Independent modules provide dialogue/auto-reading, history, pause menus, persistent audio preferences, versioned/checksummed/backup save slots, autosave, audio channels, and crossfade. Behaviors allow nested spawning; failed reload restores managed engine state.

Version 1.2.2 strengthened packaging/numeric checks, exposed backup recovery in menus, unified disabled-collider handling for raycast/collisions, and fixed initial Shift+Tab navigation. The guide details limits/save statuses.

C++ localization supports JSON catalogs, parameters/plurals, fallback languages, saved selection, and language switching in the menu. forge.message() binds UI/entities; forge.tr() returns a string. Starter scenes include English/Russian. See [localization](docs/wiki/localization.md); complex scripts/RTL require a text backend extension.

Built-in examples are source-distribution content: [2D](scenes/welcome.json), [3D](scenes/world3d.py), [localized UI/menus](scenes/interface.py), [advanced 3D](scenes/advanced.json), [empty editor scene](scenes/editor-empty.json), [physics/particles](scenes/simulation.json), and [PBR/hierarchy/procedural meshes](scenes/materials.json). They use bundled models, textures, font, and audio; no separate game is needed. Launch with dev --scene interface.py, dev --scene world3d.py, or dev --scene advanced.json.

Forge 2.0 adds render-target cameras, UV/sprite sheets, batching/glyph atlases, postprocessing/Python uniforms, fallback fonts, background asset handles, and memory budgets. It includes action maps, controllers, input recording/replay, UI styles/transitions, AI scheduling, states, and timelines. Audio supports pan/spatial/DSP/ducking/voice limits and playback-cursor subtitles.

The 3D set includes a swept AABB controller, point/directional/spot lights, PCF shadows, OBJ/glTF/GLB/FBX/DAE import, and GPU skeletal animation. edit opens hierarchy, inspector, asset browser, undo/redo, preview, and JSON saving. --scene selects an initial scene without editing configuration.

Forge 2.1 adds native particles and optional Bullet 3D physics: rotating box/sphere/capsule bodies, friction, restitution, forces/torques, CCD, collision masks, and sloped capsule control. Particles support continuous/burst emission, textures, lifetime color/size, following, billboards, and batching. AABB remains the default; choose "physics":{"backend":"bullet"} in a 3D scene. Implementations/shaders are editable; see [physics](docs/wiki/physics.md) and [particles](docs/wiki/particles.md).

```sh
python tools/forge.py dev --scene simulation.json
```

WASD moves the capsule, Space jumps, E emits sparks, R resets the sphere.

Forge 2.3 speeds up particles with cached texture paths, one snapshot per frame, and OpenGL 3.3 instancing instead of six CPU vertices per particle. Existing custom vertex shaders retain a compatible path. Transform setters update affected subtrees; set_positions/raycast_many batch world updates/queries. Bullet synchronizes changed bodies only; Python bridge transfers ordinary data without intermediate JSON strings. Lifecycle, Python API, reload, fonts, media, GPU resources, and particle rendering have separate implementations. [Contracts](docs/wiki/extensions.md), [diagnostics and benchmarks](docs/wiki/development.md) document these paths. One active Python runtime, CPU particle simulation/alpha sorting, and other boundaries remain.

Forge 2.2 adds metallic/roughness PBR with albedo/normal/ORM/AO/emissive maps, transparency, local/world hierarchies, editable procedural meshes/vertex colors, filtering/mipmaps, and user captures. find uses an index; legacy physics filters pairs along X rather than testing every pair. Editor is separate. Deferred nested save metadata is copied independently. Defaults remain; see [materials](docs/wiki/materials.md), [hierarchy](docs/wiki/entities.md), and [geometry](docs/wiki/assets.md).

```sh
python tools/forge.py dev --scene materials.json
python tools/benchmark_world.py build/bin/forge
```

macOS build --output dist/MyGame.app creates an app with private Python, icon, and user directories. Developer ID signing/notarization commands, window settings, and crash reports are available. [2.x capabilities and test coverage](docs/FORGE_2.md) maps subsystems to checks. The guide details rendering/physics/editor/rollback boundaries.

## Source Repository Contents

Git contains C++ source, adaptable Python modules, tools, built-in examples/assets, tests, documentation, licenses, and dependency lock. Builds, environments, downloads, saves, logs, screenshots, and local reports are ignored through [.gitignore](.gitignore). Fonts, OBJ/glTF models, images, and example WAVs are required source assets and remain tracked.

After compilation, python tools/forge.py validate checks resources/syntax and python tools/forge.py test runs integration suites. Full CTest runs eleven base suites plus the optional shader_compiler suite (enabled by default on macOS/Windows), including numeric, packager_paths, rendering, authoring, and project API: ctest --test-dir build -C Release --output-on-failure. macOS/Windows CI builds with/without the builtin editor; Windows additionally runs Direct3D WARP regressions and standalone graphics with silent audio; macOS has Metal-specific/shared GPU and standalone checks. Linux runs four OpenGL GPU suites on Mesa/Xvfb. Software OpenGL does not validate physical GPU/audio hardware. See [validation and test suites](docs/wiki/development.md#validation-and-test-suites).

## License and Modifications

Forge uses the custom **Forge Attribution License 1.0**, with available source and required loading-screen/menu attribution. Your modified Core requires “Built on the Forge engine (modified core)”; unchanged official Core uses “Uses the Forge engine”. Commercial and closed-source games are allowed.

Everything outside the [Core list](CORE.md) may be adapted unless separately licensed, including shaders, physics, graphics, and audio. Core modification is permitted too, but ordinary game developers should prefer configuration/extensions for easier upgrades. Preserve dependency licenses.

This custom license is not OSI-approved. Full terms: [LICENSE](LICENSE).

Forge 2.4 adds prefab hierarchies/independent instances, property clips, layered skeletal animation/transitions/events, state machines, rest-pose retargeting, and morph targets. Editor tools cover clips/bone keys/layers/scrubbing/markers/morph sliders/retarget JSON. Example: python tools/forge.py dev --scene authoring.py; see [animation and prefabs](docs/wiki/animation.md). The background watcher and indexed listener/contact dispatch avoid repeated full traversals; lightweight C++ contracts retain engine.hpp compatibility. CI also verifies standalone packages with tools/verify_package.py.


Forge 2.5 separates editor shells from the open project format/API. Builtin ImGui is optional (compile --without-editor); JSON-lines CLI, Python SDK, shared extension commands, and runtime editing work without it. Saves preserve unknown fields/external changes, detect conflicts, and replace files atomically. [Shell, format, and API guide](docs/PROJECT_API.md) · [JSON Schema](schemas/project.schema.json) · [Alternative shell example](examples/editor/terminal_shell.py).

```sh
python tools/forge.py shell --shell examples/editor/terminal_shell.py --extension examples/editor/labels_extension.py
python tools/forge.py edit --shell examples/editor/viewport_shell.py --scene editor-empty.json
python tools/forge.py project --serve
```
