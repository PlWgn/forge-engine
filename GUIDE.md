# Forge Engine Wiki

The current manual for **Forge 2.9.0**: a modular C++17 runtime with Python scenes and scripts for macOS and Windows. This wiki describes the current APIs and defaults. [Release history and coverage](docs/FORGE_2.md) are maintained separately.

## Start here

1. [Install dependencies and compile the engine](docs/wiki/installation.md). A clean checkout contains source, not a prebuilt runtime.
2. Run the built-in example from the engine source directory:

   ```sh
   python tools/forge.py validate --no-open-log
   python tools/forge.py dev
   ```

   These commands assume the Python environment from installation is activated. Otherwise use `.tools/bin/python` on macOS or `.tools\Scripts\python.exe` on Windows.

3. Create your own content project in an empty directory:

   ```sh
   python tools/forge.py init --output ../MyGame
   python tools/forge.py dev --project ../MyGame/engine.json
   ```

   Keep running the launcher from the **engine checkout**, selecting your game with `--project`. A generated game contains content, examples, schemas, licenses, and this wiki; it does not contain another engine checkout, compiler toolchain, launcher, or SDK.

4. [Write a scene](docs/wiki/scenes.md), then [package the game](docs/wiki/packaging.md). Python, JSON, and asset changes need no C++ recompilation.

## Find a task

| I want to… | Read |
| --- | --- |
| Install, compile, or select optional graphics/editor components | [Installation](docs/wiki/installation.md) |
| Create a game, rename content folders, or edit settings | [Projects and configuration](docs/wiki/projects.md) |
| Add LAN, Valve sockets, or optional Steam services | [Networking and Steam](docs/wiki/networking.md) |
| Create scenes, attach scripts, or understand callback order | [Scenes and lifecycle](docs/wiki/scenes.md) |
| Find an entity, read input, or call everyday engine functions | [Python API](docs/wiki/api.md) |
| Reuse objects or work with parent/local/world transforms | [Entities, hierarchies, and prefabs](docs/wiki/entities.md) |
| Change graphics backends, cameras, shaders, or culling/LOD | [Rendering](docs/wiki/rendering.md) |
| Use PBR maps, transparency, lighting, or imported models | [Materials and lighting](docs/wiki/materials.md) |
| Preload assets, control cache budgets, or generate meshes | [Assets and geometry](docs/wiki/assets.md) |
| Animate sprites, properties, skeletons, or morphs | [Animation](docs/wiki/animation.md) |
| Use collisions, Bullet bodies, forces, or a character controller | [Physics](docs/wiki/physics.md) |
| Create particle effects or replace their shaders | [Particles](docs/wiki/particles.md) |
| Build buttons, layout, dialogue, pause menus, or settings | [UI and menus](docs/wiki/ui.md) |
| Add languages, parameters, plurals, or reactive text | [Localization](docs/wiki/localization.md) |
| Play music, crossfade, add spatial audio/DSP, or subtitles | [Audio](docs/wiki/audio.md) |
| Save slots, recover backups, migrate data, or use user storage | [Persistence](docs/wiki/persistence.md) |
| Rebind actions, use gamepads, or record/replay input | [Input](docs/wiki/input.md) |
| Write reusable Python/C++ modules or detect API capabilities | [Extensions](docs/wiki/extensions.md) |
| Launch the editor or create an alternative shell | [Editor shells](docs/wiki/editor.md) |
| Recover from reload errors, read logs, profile, or run tests | [Development and diagnostics](docs/wiki/development.md) |
| Distribute a standalone game and supply required attribution | [Packaging and licensing](docs/wiki/packaging.md) |

## First scene

In your generated game, create `scenes/main.py`:

```python
import forge


def build():
    return {
        'mode': '2d',
        'background': [0.04, 0.06, 0.09, 1],
        'entities': [
            {'id': 'title', 'kind': 'text', 'text': 'Hello, Forge!',
             'position': [40, 40, 0], 'font_size': 36, 'screen': True}
        ],
    }


def on_start():
    forge.log('My scene is ready')


def on_update(dt):
    if forge.key_pressed('ESCAPE'):
        forge.quit()
```

Set `"entry_scene": "main.py"` in that game's `engine.json`. Run it from the engine checkout with `python tools/forge.py dev --project ../MyGame/engine.json`. To try a scene without changing the entry, use `--scene main.py`; a packaged build always uses the configured entry.

The engine supplies `forge` inside embedded Python. Running `python scenes/main.py` in system Python is not the game execution path. Code fragments on subsystem pages assume the scene's imports, live entities, callbacks, and named resources; replace sample resource names with your own files.

## Commands at a glance

Run launcher commands from the engine checkout. `--project path/to/engine.json` selects another game.

| Command | Result |
| --- | --- |
| `python tools/forge.py compile` | Configure and compile C++; repeat after C++ changes |
| `python tools/forge.py configure` | Generate CMake build files without compiling |
| `python tools/forge.py validate --no-open-log` | Check the whole project's configuration, resources, media, and Python syntax |
| `python tools/forge.py dev` | Run content with watched reload; no distribution build |
| `python tools/forge.py run` | Run current content without watching files |
| `python tools/forge.py run --headless --frames 120 --no-open-log` | Fixed-time logic/physics run without a window/audio device |
| `python tools/forge.py edit --scene editor-empty.json` | Open the optional builtin editor with simulation paused |
| `python tools/forge.py edit --shell none` | Editor viewport/API without the builtin panels |
| `python tools/forge.py build --output dist/MyGame-1.0` | Validate and package into a new directory; never overwrite an existing build |
| `python tools/forge.py init --output ../MyGame` | Copy the starter content and wiki into an empty directory |
| `python tools/forge.py project --serve` | Local JSON-lines document service for any shell |
| `python tools/forge.py test` | Six native integration suites; [full test commands](docs/wiki/development.md#validation-and-test-suites) include additional CTest/GPU checks |

`compile` and game `build` are different operations. Headless execution uses a 1/60-second frame time; automatic physics uses fixed 1/120-second steps. Headless cannot verify graphics pixels or audible playback. `--silent-audio` retains the window and rendering while avoiding an audio device. `--no-open-log` suppresses automatic opening, not logging. Normal success exits 0; command/runtime errors exit 1, while dev can pause for recovery.

## Built-in examples

Use `python tools/forge.py dev --scene <file>` from the engine checkout, adding `--project` for a generated game.

| Scene | Demonstrates |
| --- | --- |
| [welcome.json](scenes/welcome.json) | 2D sprites, text, basic input and physics |
| [world3d.py](scenes/world3d.py) | 3D objects and a free camera |
| [interface.py](scenes/interface.py) | Localized UI, dialogue/history, pause/settings, slots |
| [advanced.json](scenes/advanced.json) | Lighting, cameras, animation, action maps, profiling |
| [simulation.json](scenes/simulation.json) | Bullet shapes/controller and native particles |
| [materials.json](scenes/materials.json) | PBR, hierarchy, procedural geometry |
| [authoring.py](scenes/authoring.py) | Prefab instances, layers, retargeting, morphs, property clips |
| [optimization.py](scenes/optimization.py) | Optional LOD, distance/frustum/occlusion culling |
| [editor-empty.json](scenes/editor-empty.json) | Empty authoring scene; launch with `edit` |

## Compatibility and customization

OpenGL remains default; Metal on macOS and Direct3D 11 on Windows are optional. Default physics is legacy AABB. LOD/visibility optimization is opt-in. Game API, project document API, and JSON schema versions are currently **1**, independent of the engine version, your game's `project.version`, and its save-data version.

Configuration and public APIs keep simple projects small. Python modules, shaders, physics, graphics, audio, and other components outside the exhaustive [Core boundary](CORE.md) may be replaced or extended under their licenses. Core changes are permitted; ordinary game developers should prefer extensions/configuration for easier upgrades. No editor or mandatory online service is required for run/build.

The [Forge Attribution License](LICENSE) is custom source-available licensing, not OSI-approved. Commercial and closed-source games are allowed. Public products require startup/loading **and** menu/About attribution; [exact wording and Core rules](docs/wiki/packaging.md#license-and-required-attribution), [examples](ATTRIBUTION.md), and [dependency notices](THIRD_PARTY.md) are part of the manual.
