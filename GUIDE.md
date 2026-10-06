# Forge — Game Development Guide

Forge is a C++17 engine with user-defined scenes and behavior written in Python. The core creates the window, runs the game loop, provides modules, validates resources, and packages a portable game directory. Game developers edit configuration and content; this does not require changes to the C++ core.

Features include 2D sprites, Cyrillic text, a perspective 3D camera, cubes, OBJ models, materials and GLSL shaders, keyboard and mouse input, WAV/MP3/FLAC audio, basic AABB physics, triggers, raycasting, saves, and automatic content reload. Forge provides built-in localization (section 10.5), UI widgets, layout and text wrapping, menus, reliable save slots, audio channels, and fades. Platformer, visual-novel, and 3D scenes use the same API.

## 1. Installation

### macOS

Install Command Line Tools (`xcode-select --install`), Python 3.10+ with development headers, and CMake 3.24+. Python from python.org is recommended for portable builds; the engine targets the architecture of the Python used to compile it. Apple's system Python is not recommended for this purpose.

```sh
python3 -m venv .tools
source .tools/bin/activate
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
```

`dependencies.py` downloads pinned libraries and a free font. Internet access is needed only to install dependencies; subsequent compilation works offline. The script verifies TLS certificates. If certificate validation fails, repair the installed Python's certificate setup rather than disabling verification.

A clean checkout contains no `.tools`, `build`, `dist`, or downloaded libraries: Git ignores them. `vendor` contains only its README and lock file. These commands create the local environment, download dependencies, and compile the engine. Built-in scenes and their source assets are tracked; local screenshots, validation reports, saves, and logs are excluded from the source distribution.

### Windows

Install Visual Studio 2022 Build Tools with Desktop development with C++, Windows SDK, CPython x64 from python.org, and CMake. Run the commands in Developer PowerShell or Native Tools Command Prompt:

```powershell
python -m venv .tools
.tools\Scripts\Activate.ps1
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py dev
```

All components must target the same architecture, such as x64. If venv activation is blocked, invoke `.tools\Scripts\python.exe` directly. MSVC receives `/utf-8` to support Unicode strings in source files.

CMake produces `build/bin/forge` on macOS and usually `build/bin/Release/forge.exe` on Windows. The Python launcher locates the appropriate binary automatically. The graphics driver must support OpenGL 3.3 Core; macOS uses a forward-compatible Core context.

## 2. Quick Start

After installing dependencies and compiling as described in section 1, run from the engine root:

```sh
python tools/forge.py validate
python tools/forge.py dev
python tools/forge.py run
python tools/forge.py build --output dist/MyGame
python tools/forge.py test
```

If your macOS shell has not activated `.tools`, use `python3` or `.tools/bin/python`.

- `compile` — configure CMake and compile the C++ engine. Required initially and after changing C++ modules.
- `configure` — generate CMake build files only.
- `validate` — validate configuration, resources, decoded textures/models, JSON objects, and the syntax of all configured Python files.
- `dev` — run the project immediately with scene/resource reload, without packaging a game build.
- `run` — run current content without watching files.
- `build` — validate and produce a standalone game directory containing assets, an executable, and Python.
- `test` — integration checks against the actual C++ runtime.
- `init --output path` — create a new project from the starter content. The destination must be empty.

C++ is compiled: the core must be compiled before its first run. Subsequent runs and Python/JSON/asset edits do not require recompiling C++. A game `build` packages the compiled runtime; Python code is interpreted both in development and in the packaged game.

Run the native executable directly:

```sh
./build/bin/forge dev --project engine.json
./build/bin/forge validate --project engine.json
./build/bin/forge run --headless --frames 120 --no-open-log
./build/bin/forge build --output dist/MyGame
```

`--headless` runs logic and physics without a window or audio device, using a 1/60-second timestep. `--frames N` limits the number of frames. `--no-open-log` disables automatic log opening, while logging stays enabled. Exit status is 0 on success and 1 on failure. A reload error in dev pauses gameplay; fixing the file allows recovery after reload.

Starter scene: A/D moves, Space jumps, Enter advances dialogue, F5 saves and plays a sound, 2 opens 3D, and 3 opens the UI example. In 3D: WASD moves the camera, arrow keys look around, hold M for mouse look, and 1 returns to 2D. Escape exits.

## 3. Structure and Core Independence

```text
engine.json          — shared settings and initial scene
engine/              — C++ core and replaceable subsystem implementations (boundaries: CORE.md)
  include/forge/     — public headers for C++ modules
  src/               — startup, configuration, world, graphics, Python, audio, logging
graphics/            — shaders and font
modules/             — shared Python modules and additional C++ modules
scenes/              — JSON and Python scenes
scripts/             — object behavior and startup scripts
textures/            — PNG/JPEG and other stb_image formats
materials/           — JSON materials
models/              — OBJ, glTF/GLB, FBX, DAE
objects/             — JSON object templates
audio/               — audio files
locales/             — JSON translation dictionaries
saves/               — created when the first save is written
forge.log            — log in the project or user logs directory
build/               — compiled engine output
dist/                — packaged games
```

All content directories can be renamed through `paths`; nested directories are supported. Files can also be renamed by updating references. The public API does not hardcode `scenes/` or `textures/`. The settings filename is unrestricted: select it with `--project`.

The core is limited to the code identified in [CORE.md](CORE.md); other implementations, including physics, graphics, audio, and shaders, can be adapted to your game. Core changes are also permitted, but ordinary game development should first use configuration and extensions to retain compatibility with engine updates.

Paths are relative to the settings file's directory. Texture/model/script/prefab references are relative to their respective `paths` groups. Absolute paths and `..` escapes outside the project root are prohibited, including outward symlinks. Add groups to `paths` and access them through `forge.asset_path(group, file)`. Do not use the project root itself as an asset directory: packaging requires separate resource directories.

Create a separate game while sharing the engine:

```sh
python /path/to/engine/tools/forge.py init --output /path/to/MyGame
python /path/to/engine/tools/forge.py dev --project /path/to/MyGame/engine.json
python /path/to/engine/tools/forge.py build --project /path/to/MyGame/engine.json --output /path/to/releases/MyGame
```

There is one C++ engine installation; game content lives in a separate directory. To include game-specific C++ modules, run `compile --project /path/to/MyGame/engine.json`.

## 4. engine.json

```json
{
  "schema_version": 1,
  "project": {"name": "My Game", "version": "1.0.0", "icon": "textures/icon.png"},
  "entry_scene": "main.json",
  "paths": {
    "graphics": "graphics", "modules": "modules", "scenes": "scenes",
    "scripts": "scripts", "textures": "textures", "materials": "materials",
    "models": "models", "objects": "objects", "audio": "audio", "locales": "locales"
  },
  "window": {"width": 1280, "height": 720, "vsync": true, "resizable": true, "fullscreen": false},
  "renderer": {"vertex_shader": "default.vert", "fragment_shader": "default.frag", "font": "font.ttf"},
  "logging": {"open_on_error": true},
  "startup_scripts": ["startup.py"],
  "python_paths": [],
  "native_modules": [],
  "save_directory": "saves"
}
```

`schema_version`, `project.name`, `entry_scene`, and the basic `paths` groups are required. Empty asset directories are allowed; missing directories are an error. The icon path is relative to the project root and is used for the window on Windows. macOS ignores the GLFW window icon; a directory build contains the executable, while an output ending in `.app` creates an application bundle (section 18.10).

`startup_scripts` run once at application startup and survive scene changes. They may define `on_start()`, `on_update(dt)`, and `on_destroy()`. `python_paths` lists additional Python package directories inside the project; they are included in the build. `native_modules` lists additional `.cpp` sources compiled into the runtime.

Custom JSON fields are allowed and accessible through `forge.settings()`. Unknown fields do not automatically create new core features; they are metadata for your modules.

## 5. JSON Scenes

```json
{
  "mode": "3d",
  "background": [0.03, 0.04, 0.07, 1],
  "gravity": [0, -9.81, 0],
  "camera": {"position": [0, 2, 7], "target": [0, 1, 0], "fov": 60},
  "script": "main.py",
  "entities": [
    {
      "id": "player", "prefab": "player.json", "position": [0, 2, 0],
      "scripts": [{"file": "player.py", "properties": {"speed": 5}}]
    }
  ]
}
```

`script` is an optional Python file relative to `paths.scenes`, with scene events `on_start()`, `on_update(dt)`, and `on_destroy()`. `entities` is the object list. An `id` must be unique within the scene and is used by `forge.find()`. The engine generates an ID when omitted; assign one explicitly for stable references.

2D uses logical window pixels, with X right and Y down. The camera offsets the world in X/Y. Sprites are centered on `position`; `scale` specifies their size. `position.z` controls 2D layer order: larger values draw later. UI with `screen: true` draws after the world, independently of the camera.

3D uses X right, Y up, and arbitrary world units. The camera is perspective; `target` is its look-at point and `fov` is in degrees. Position and target must differ; the renderer selects an alternate up vector for a vertical view. The built-in cube is 1×1×1. `rotation` is in degrees about X/Y/Z. Models retain their source coordinates. Lighting, shadows, and PBR are described in sections 18 and 20.

## 6. Python Scenes

`entry_scene` may point to a `.py` file:

```python
import forge

def build():
    return {
        'mode': '2d',
        'gravity': [0, 980, 0],
        'entities': [
            {'id': 'title', 'kind': 'text', 'text': 'Hello!',
             'position': [40, 40, 0], 'font_size': 30, 'screen': True}
        ]
    }

def on_start():
    forge.log('Scene ready')

def on_update(dt):
    if forge.key_pressed('ENTER'):
        forge.change_scene('level2.json')

def on_destroy():
    forge.log('Scene unloaded')
```

`build()` is optional. It can return a scene dictionary, return None, or create objects through `forge.spawn()`. When returning None, call `set_mode`, `set_background`, `set_gravity`, and `set_camera` directly. A returned description is loaded after build completes; do not create the same objects through both approaches.

A scene change is deferred to the start of the next frame; the last request in a frame takes precedence. The previous scene remains available until the new scene initializes successfully. References to old entities have `alive == False` after the switch. Scene objects are not automatically preserved across scenes; use a startup module or save files for shared state.

## 7. Objects, Templates, and Materials

Supported `kind` values are `sprite`, `cube`, `mesh`, `text`, and `empty`. `empty` is useful for triggers and logical objects.

| Field | Meaning |
| --- | --- |
| id, name | Identifier and display name |
| prefab | JSON template from paths.objects |
| position, rotation, scale | Three-component numeric vectors |
| color | Four-component RGBA, usually 0..1 |
| texture | File from paths.textures |
| model | Model from paths.models; required for mesh |
| material | JSON from paths.materials |
| text, font_size | UTF-8 text and font height in pixels |
| text_key, text_params | Localization key and JSON translation parameters; see 10.5 |
| visible | Rendering visibility; false does not disable scripts or physics |
| screen | UI above the world without the camera or depth test |
| collider | Full dimensions independent of scale; AABB in legacy, selected shape in Bullet |
| dynamic, mass, velocity | Physics integration, positive mass, and velocity |
| angular_velocity, rigid_body | Angular velocity and optional Bullet settings; section 19 |
| trigger | Overlap events without physical resolution |
| scripts | List of filenames or file/properties objects |
| data | User-defined JSON data |

JSON objects, Python build() results, and forge.spawn use the same validator after merging a prefab. Position, rotation/scale/velocity/collider, RGBA, and clip require numeric components that are finite and representable as C++ float. Boolean/null components, NaN/Infinity, and values such as 1e100 are rejected. Mass and font size must remain positive after float conversion. Reciprocal mass must also fit float: mass=1e-40 is rejected in JSON, forge.spawn, and Entity.mass assignment. clip=null disables clipping. Numeric Entity setters, move, and impulse validate before mutation and preserve the previous value on failure.

Template `objects/crate.json`:

```json
{"kind": "cube", "model": "", "scale": [1, 1, 1], "material": "wood.json", "collider": [1, 1, 1]}
```

Material `materials/wood.json`:

```json
{"color": [1, 1, 1, 1], "texture": "wood.png"}
```

A material sets color and texture; explicit entity fields take precedence. Template and local fields use JSON Merge Patch: local values replace base values, arrays are replaced entirely, and null removes a field. Prefab inheritance and reusable hierarchies are supported in 2.4 and later; see section 22.2.

Textures support PNG/JPEG/BMP/TGA and other stb_image formats; V points downward. Model import uses Assimp and supports the formats in section 18.3. OBJ may contain positions, UVs, normals, and negative indices; polygon faces are triangulated. Triangulate complex concave polygons when exporting. Imported material maps and explicit JSON materials are described in sections 18.3 and 20.1.

Materials are read when an entity is created. Change color or texture at runtime through Entity properties. Editing a material JSON in dev recreates the scene.

## 8. Object Behavior

`scripts/player.py`:

```python
import forge

class Behavior:
    def __init__(self, entity, properties):
        self.entity = entity
        self.speed = properties.get('speed', 240)

    def on_start(self):
        forge.log(f'Created {self.entity.name}')

    def on_update(self, dt):
        direction = forge.key_down('D') - forge.key_down('A')
        self.entity.move(direction * self.speed * dt, 0)

    def on_collision(self, other):
        if other: forge.log(f'Contact: {other.id}')

    def on_collision_exit(self, other):
        # other may be None if the other object has already been removed.
        if other: forge.log(f'Contact ended: {other.id}')

    def on_destroy(self):
        forge.log('Object removed')
```

Objects can be created in `Behavior.on_start()`, `on_update()`, and ordinary `on_destroy()`. Attachment processes stable snapshots of references: new objects and their behaviors initialize before the next physics step without invalidating traversal. Each instance receives on_start and on_destroy at most once. Destroyed objects receive no updates; their behavior is removed from the active list before on_destroy. An infinite spawn chain stops with a diagnostic after 8192 scripted objects in one attachment pass; objects without Behavior do not consume this limit.

During scene replacement, the old on_destroy sees its own World and configuration. Objects created there disappear with the old world; pause, transition, listener, and audio changes do not carry into the new scene. Mouse capture and screenshots are ignored during teardown. Final shutdown also uses a stable callback list. Entity.alive remains accessible through retained references after destruction.

Each script attachment creates a separate Behavior instance. An entity may have multiple behaviors. All events are optional. Without a Behavior class, events are invoked directly on the Python module; retain the entity reference yourself, for example through forge.find().

Startup order is startup on_start → scene build → scene on_start → behavior on_start. A frame processes input and pending transitions, frame listeners, startup/scene/object updates, fixed-step physics and contacts, lifecycle attachment/removal, animation/localization, and rendering. Shutdown invokes on_destroy. Scripts and the Python API run on the main thread; do not access the scene or graphics from background threads.

`entity.destroy()` immediately sets alive=false; removal and on_destroy are processed by lifecycle passes. A new entity appears in the world immediately and its behavior attaches during the next attachment pass. A retained reference to a destroyed entity remains a safe Python object but is no longer rendered or updated.

## 9. Python API

`import forge` loads the embedded C++ module. Vectors accept tuples/lists and return sequences. To change a component, read the vector and assign the complete modified vector.

```python
x, y, z = entity.position
entity.position = (x + 1, y, z)
```

| Function | Purpose |
| --- | --- |
| spawn(dict) → Entity | Create an entity; supports prefab |
| find(id) → Entity/None | Find a live entity |
| entities() → list | List live entities |
| change_scene(path) | Change scene next frame |
| quit() | End the loop |
| dt(), time() | Frame time and elapsed runtime time in seconds |
| is_dev(), is_headless() | Execution modes |
| key_down(name), key_pressed(name) | Held key / single-frame press |
| mouse_down(button=0) | Mouse button: 0 left, 1 right, 2 middle |
| mouse_scroll() | Wheel X/Y movement this frame |
| mouse_position(), mouse_delta() | Logical pixels / movement this frame |
| capture_mouse(bool) | Capture and hide the cursor |
| window_size() | Logical window dimensions |
| set_camera(position, target=(0,0,0)) | Scene camera |
| camera_position() | Camera position |
| set_mode('2d'/'3d') | World mode; configure gravity separately |
| set_gravity(vector) | Acceleration of dynamic entities |
| set_background(rgba) | Clear color |
| raycast(origin, direction, distance=1000) | Closest collider or None; uses the selected physics backend |
| overlaps(entity_a, entity_b) | Check collider overlap |
| play_sound(file, loop=False, volume=1, channel='sfx', fade=0) → id | Audio from paths.audio; see 10.4 |
| screenshot(file) | Save the next rendered frame as PPM; see storage/capture settings in section 20 |
| stop_sounds() | Stop all sounds |
| log(message, level='INFO') | Write to terminal and log simultaneously |
| open_log() | Open the log with the OS application |
| asset_path(group, file) | Absolute resource path |
| project_path(file='.') | Absolute path inside the project |
| settings() | Configuration copy as dict |
| save(name, value) | JSON save with atomic file replacement |
| load(name, default=None) | Read a save or return the default |

Keys: single Latin letters, digits, SPACE, ESCAPE, ENTER, TAB, BACKSPACE, LEFT/RIGHT/UP/DOWN, PAGEUP/PAGEDOWN, left SHIFT/CTRL/ALT, and F1..F25. An unknown key name raises an exception. key_pressed is true for one frame. Headless input is always false with mouse position (0,0); audio mixes without a device.

Entity properties: id and alive are read-only; name, position, rotation, scale, velocity, collider, mass, color, texture, model, text, text_key, text_params, font_size, clip, visible, dynamic, trigger, and data are mutable. kind, scripts, and material are specified at creation. Methods: move(x,y,z=0), impulse(x,y,z=0) (changes velocity by impulse/mass), destroy(). Additional properties are documented with their subsystems below.

entity.data returns a copy of its JSON dictionary; assign the entire dict back to retain changes. Lifecycle state is held separately in C++; fields prefixed _forge_ are reserved for extensions. Python objects or functions cannot be stored in data: it must serialize to JSON.

Examples:

```python
entity = forge.spawn({'id': 'enemy', 'kind': 'cube', 'position': [0, 1, 0], 'collider': [1, 1, 1]})
forge.log({'stage': 'load', 'progress': 0.75}, 'DEBUG')
forge.save('slot1', {'health': 100, 'scene': 'level.json'})
state = forge.load('slot1', {'health': 100})
path = forge.asset_path('textures', 'portrait.png')
```

## 10. Modules and Extensions

forge.__version__ contains the C++ engine version from CMake; help and the build manifest use the same version. project.version in engine.json is your game's independent version.

Built-in low-level modules are C++ implementations of the world, graphics, input, audio, and saves exported through forge. The modules directory is added to sys.path; import shared Python functionality normally, such as import ui or import animation.

Ready-made modules include ui (Canvas, layout, widgets), dialogue, menus, saves, settings.AudioSettings, audio, math3d.add/mul/normalize, events.EventBus, animation.SpriteAnimation, and animation.Tween. Update Animation/Tween instances and the legacy Button(bounds, callback) yourself from on_update. Canvas and newer widgets update automatically. Modules do not dictate a genre, game class structure, or global manager.

EventBus.on(event, callback) returns an idempotent unsubscribe function: repeated cleanup is safe, and cleanup releases that subscription's callback. Duplicate subscriptions remain independent. emit() snapshots callbacks in registration order and does not store unknown event names. Tween requires matching finite vector endpoints and a finite positive duration. Tween/SpriteAnimation updates require finite nonnegative dt and reject clock/frame overflow before committing elapsed time. A rejected native Tween property setter also leaves elapsed time unchanged.

For example:

```python
from animation import Tween

def on_start():
    global tween
    tween = Tween(forge.find('player'), 'position', (700, 450, 1), 2)

def on_update(dt):
    tween.update(dt)
```

C++ extension without editing the core:

```cpp
#include <forge/engine.hpp>
FORGE_MODULE(my_math) {
    module.def("lerp", [](float a, float b, float t) {
        return a + (b - a) * t;
    });
}
```

Save the file in your module library, add its relative path to native_modules, and run compile --project .... Python then exposes forge.lerp(...). The header contains Config, World, Entity, and Runtime; C++ modules can access the active runtime through forge::active. Check the pointer and use the main thread. Registration happens at application startup; new API names must not conflict with existing ones.

C++ modules compile into the executable: this is source extension, not arbitrary DLL loading. It lets you replace or extend the low-level library without modifying the main script, using the same build process on both OSes.

Install third-party Python packages inside the project:

```sh
python -m pip install --target python_packages package_name
```

Add "python_paths": ["python_packages"]. Binary extensions must match the embedded Python version/architecture and target OS. Check their external DLL/dylib dependencies and licenses separately; automatic packaging of arbitrary third-party binary dependencies is not guaranteed.

## 10.1. Ready-Made UI and Automatic Layout

Press **3** in the starter 2D scene to open scenes/interface.py. The example includes dialogue, automatic reading, history, a pause menu, volume settings, manual slots, and autosave. Escape toggles the menu. These features live in editable Python modules rather than the core.

Minimal scene UI:

```python
import forge
from ui import Canvas, Column, Row, Label, Button

def on_start():
    global canvas
    root = Column(padding=32, gap=16)
    root.add(Label('Game Title', size=64))
    root.add(Label('Text wraps automatically to the container width.', size=26))
    root.add(Row(
        Button('Start', lambda: forge.change_scene('game.py')),
        Button('Exit', forge.quit),
    ))
    canvas = Canvas(root, width=1280, height=720)

def on_destroy():
    canvas.close()
```

Canvas subscribes to frame updates automatically; do not update it again from on_update. For a custom loop, set automatic=False and call canvas.update(dt). close() unsubscribes and destroys its owned entities; repeated calls are safe. A widget belongs to one tree.

| Component | Purpose |
| --- | --- |
| `Column(*children, gap=12, ...)` | Vertical layout |
| `Row(*children, gap=12, ...)` | Horizontal layout |
| `Label(value, size=24, color=..., wrap_text=True, align='left', ...)` | Text; left/center/right alignment |
| `Button(label, callback, ...)` | Button with hover, press, disabled state, and focus |
| `Slider(value=1, callback=..., step=.05, ...)` | Value 0..1; mouse and arrow control |
| `ScrollView(*children, height=..., ...)` | Vertical scrolling and content clipping |
| `Canvas(root, width=1280, height=720, scale='fit')` | Scaling, input, tree and overlay rendering |

Shared options: width, height, flex, padding, background, visible, enabled. Without explicit dimensions, text size or available container width is used. flex=1 receives a share of the remaining space along the container axis; multiple flex children divide space by weight. Explicit dimensions take precedence for non-flex children. padding accepts a number, (horizontal, vertical), or (left, top, right, bottom). background is RGBA; Column(background=..., padding=...) is sufficient for a panel.

widget.add(child) adds an element; remove(child) removes it and destroys its graphics. Change label.value, button.label, or widget.visible/enabled to recalculate layout next frame. Long words split at character boundaries; \n inserts an explicit paragraph. ui.measure(value, size) returns (width, height, line_height) using the renderer's TrueType metrics. ui.wrap(value, width, size) is available independently of widgets.

scale='fit' preserves the designed Canvas proportions and centers it with margins. stretch scales both axes independently. none uses current logical window dimensions without scaling. Window size and Retina/HiDPI density are handled separately. Widgets and text are clipped to container rectangles rather than spilling over neighboring panels. Glyphs are rasterized for the actual font size, scale, and screen density instead of enlarging a fixed 40-pixel bitmap. Raster height is limited to 1..1024 pixels; the product is computed in double and bounded before conversion to int. Larger text scales the maximum raster. The cache distinguishes size and codepoint; atlas/cache eviction and budgets are described in sections 18 and 20.

A button activates when released over the same button; releasing outside cancels the click. Tab / Shift+Tab changes focus, Enter / Space activates a button, and Left/Right adjusts the selected Slider. Disabled elements are skipped. The wheel scrolls the ScrollView under the pointer; PageUp/PageDown scroll by its height. canvas.overlay(widget) creates a separate centered panel; canvas.modal(widget) restricts input to it, and canvas.modal(None) removes that restriction. Panel visibility is controlled separately.

Legacy ui.text, ui.panel, and Button((x,y,width,height), callback).update() remain compatible. The old Button is a manually updated input detector; a visual button uses a string label in a Canvas tree. A low-level kind='text' Entity uses explicit \n; Label provides automatic wrapping.

## 10.2. Pause, Dialogue, History, and Menus

forge.set_paused(True) pauses startup/scene/Behavior on_update and physics. UI, audio, frame time, and forge.on_frame continue. forge.is_paused() reports the state. For background UI services:

```python
listener = forge.on_frame(lambda dt: update_interface(dt))
forge.remove_listener(listener)
```

Ordinary subscriptions belong to the scene and are removed on replacement. persistent=True keeps one until explicit removal or engine shutdown; its callback must manage references to old entities. Callbacks run in registration order before gameplay updates. A callback removed this frame is not invoked again; a newly registered callback starts next frame.

The dialogue module is optional:

```python
from dialogue import Dialogue

dialogue = root.add(Dialogue([
    {'speaker': 'Hero', 'text': 'First line.'},
    {'speaker': 'Friend', 'text': 'Next line.'},
], characters_per_second=35, auto_delay=1.5, on_done=finish_dialogue, flex=1))
```

Strings without speaker are also accepted. advance() first reveals the current line, then moves on. toggle_auto() enables automatic reading: after revealing a line, the delay is auto_delay + len(text)/80 seconds. Pause, a hidden widget, or a modal panel suspends reading. finished reports completion; journal.entries contains history and journal.view(height=...) creates a scrolling view. capture() returns JSON state; restore(state) validates and restores the position and history. Dialogue.close() disables its timer; Canvas invokes it when closing the tree.

Ready-made menu:

```python
from menus import MenuController
menus = MenuController(canvas, saves=save_manager, capture=capture_game,
                       restore=restore_game, journal=dialogue.journal)
menus.show_pause()  # Also triggered by Escape.
```

Save/load/history entries appear only when their corresponding services are supplied. The menu exposes show_settings(), show_slots(save=False), show_journal(), show_about(), close_menu(), and close(). Slots 1..5 and auto are available by default; slots configures the number of manual slots. Closing restores the previous pause state. Load errors appear inside the menu and are logged as WARN. Overwriting a selected manual slot is immediate outside reload/authoring transactions.

settings.AudioSettings stores master/music/sfx/voice/ui gains in saves/preferences/audio.json with validation and backup. The menu creates this service or accepts settings=your_audio_settings. Changes save after 0.25 seconds without further edits; close() flushes them. Corrupt preferences recover from backup or use valid defaults with a warning. Add graphics or game-specific settings through separate widgets and your SaveManager.

Call menus.close(), save_manager.close(), and canvas.close() from the scene's on_destroy(). scenes/interface.py demonstrates the complete setup and cleanup order. UI and menus work over 2D or 3D; dialogue is optional.

## 10.3. Save Slots, Format Versions, and Autosave

forge.save/load remain low-level APIs. Use the independent saves module for game slots:

```python
from saves import SaveManager, SaveError

saves = SaveManager(
    version=2,
    migrations={1: lambda old: dict(old, coins=0)},
    validate=lambda data: isinstance(data, dict) and 'health' in data,
)
saves.write('1', {'health': 100, 'coins': 8}, title='Chapter 2',
            description='Before entering the city', metadata={'scene': 'city.py'})
try:
    state = saves.read('1')
except SaveError as error:
    forge.log(error, 'WARN')

saves.autosave(capture_game, interval=60, slot='auto')
```

The default directory is <save_directory>/slots; directory selects another path within the configured storage root (section 18.8). Slot identifiers contain 1..80 ASCII letters, digits, underscores, or hyphens. The forge.slot/1 envelope version is independent of your game's data version. It records data, version, UTC time, title, description, metadata, and a SHA-256 checksum of the envelope excluding checksum. The default size limit is 16 MiB, configured by max_bytes, which must be a positive integer. The checksum detects corruption, not deliberate tampering.

SaveManager revalidates the storage boundary on every operation, including listing slots and committing deferred writes/deletes. A directory redirected outside storage is rejected with SaveError; after changing the configured storage location, create a new manager. Excessive JSON nesting is treated as corruption, allowing the same backup recovery and status reporting as other damaged data. These checks do not provide a sandbox against arbitrary Python I/O or concurrent external filesystem changes.

Writes use a temporary file, flush/fsync, and atomic replacement. Before overwriting, the last verified primary becomes .json.bak; a corrupt primary never overwrites a valid backup. read(..., recover=True) tries backup when the primary is absent/corrupt and logs a warning. Recovery returns backup data without rewriting the primary. recover=False disables it. read('missing', default=...) returns default only when both primary and backup are absent; it does not hide corruption.

migrations[n] converts data from n to n+1 until the current version is reached. Missing migrations or newer versions raise SaveVersion; corruption raises SaveCorrupt or SaveError, and schema violations raise SaveError. All derive from SaveError. A validator may return False or raise an exception. Migrations do not rewrite files automatically; save the validated result yourself. Validators and restore must check game fields before mutating game state.

saves.info(slot) returns metadata and status empty/ok/recoverable/corrupt: empty means neither file exists, ok means a valid primary, recoverable means a valid backup when the primary is absent/corrupt, and corrupt means files exist without a valid copy. A recoverable result takes metadata/version from backup, source is backup, and error describes the primary problem. The menu enables loading and displays a translated recovery hint. info() and slots() neither write nor run migrations/game validators; read() checks data/version compatibility. saves.slots() lists primary and backup slots without duplicates; delete(slot) removes both, deferred during reload/authoring transactions. info()['version'] exposes newer data versions. Autosave registers a frame callback; its timer normally pauses with gameplay (while_paused=True changes this). close() stops autosave. The game defines how saved data maps to entities; JSON saving does not automatically serialize Python instances or the whole World.

## 10.4. Audio Channels, Gain, and Music Transitions

```python
import audio

audio.master_volume(.8)
audio.music.volume = .5
track = audio.music.crossfade('theme.wav', seconds=1.5)
effect = audio.sfx.play('notify.wav', volume=.3)
effect.volume = .2
effect.fade(0, seconds=.5, stop_after=True)
track.pause()
track.resume()
audio.music.stop(fade=1)
```

Predefined channels are audio.music, sfx, voice, and ui. Create any named channel with audio.Channel('ambience'); master is reserved for overall gain. Effective gain is voice × channel × master, with ducking when configured. Values are 0..1; fade durations are finite and nonnegative. Sound.id is stable, playing is the playback state, and volume is voice gain before channel/master. fade changes it linearly; pause/resume retains playback position. A paused sound's fade is paused too. Pausing gameplay alone does not stop audio.

crossfade loads the replacement first, then fades out the previous voices on that channel and fades in the new one. A replacement-file error preserves the old voices. loop=True and volume=1 configure the new music. Finished one-shot sounds and completed stop-fades release their resources.

The native API is available without the Python wrapper: play_sound(file, loop=False, volume=1, channel='sfx', fade=0) → id, stop_sound(id, fade=0), stop_channel(name, fade=0), channel_sounds(name), set_channel_volume(name, value), channel_volume(name), set_sound_volume(id, value), sound_volume(id), fade_sound(id, volume, seconds, stop_after=False), sound_playing(id), pause_sound(id, paused=True), and stop_sounds(). Control commands safely ignore absent/finished IDs. Headless decodes and mixes without an audio device: useful for API tests, but not proof of audible playback.

## 10.5. Built-In Localization

Forge 1.2 introduced C++ localization shared by Python and UI modules. Translations are separate from scenes; language changes need no restart. Standard English and Russian menu strings are embedded and available without translation files. Projects may override any key and add languages.

Add the optional "locales": "locales" group to paths and configure:

```json
"localization": {
  "default_language": "ru",
  "fallback_language": "en",
  "auto_detect": false,
  "save_selection": true,
  "warn_missing": true,
  "languages": {
    "ru": {"name": "Russian", "file": "ru.json"},
    "en": {"name": "English", "file": "en.json"},
    "fr": {"name": "Français", "file": "fr.json"}
  }
}
```

file is relative to paths.locales; directories and filenames may be changed. A language may use shorthand "fr": "fr.json". An object without file registers a language using its parent/fallback dictionary. name appears in settings. ru and en are always registered; translate engine.* keys for other languages to localize the entire menu. Embedded keys are listed in engine/include/forge/localization_data.hpp.

Without localization, existing projects work with English as the default and fallback; translation files are unnecessary. The supplied project defaults to Russian. default_language and fallback_language must be registered.

Example locales/ru.json (Russian text intentionally retained to demonstrate plural forms):

```json
{
  "menu": {"start": "Начать игру"},
  "hello": "Привет, {name}!",
  "inventory": {
    "one": "{count} предмет",
    "few": "{count} предмета",
    "many": "{count} предметов",
    "other": "{count} предмета"
  },
  "literal": "Запись {{name}} содержит фигурные скобки"
}
```

Example locales/en.json:

```json
{
  "menu.start": "Start game",
  "hello": "Hello, {name}!",
  "inventory": {"one": "{count} item", "other": "{count} items"},
  "literal": "The entry {{name}} contains braces"
}
```

Nested keys and dotted keys are equivalent; duplicates after flattening are rejected. Values may be strings, key groups, or plural objects zero/one/two/few/many/other; other is mandatory. Arrays and numeric dictionary values are rejected.

```python
import forge, ui

forge.log(forge.tr('hello', name='Anya'))
forge.log(forge.tr('inventory', count=21))  # Uses the Russian singular category for 21.

# A translation reference: Label/Button automatically update text and layout.
heading = ui.Label(forge.message('hello', name='Anya'), size=40)
button = ui.Button(forge.message('menu.start'), start_game)
forge.set_language('en')
assert heading.value == 'Hello, Anya!'

# tr() returns a plain string containing the translation at the time of the call.
fixed = forge.tr('menu.start')
```

Substitution accepts named JSON parameters such as {name}; names contain ASCII letters, digits, or underscores and cannot start with a digit. {{ and }} produce literal braces. Missing parameters raise an exception; extra parameters are accepted. Strings insert directly; other JSON values insert as JSON. Regional date/currency/number formatting, ICU MessageFormat, nested conditions, and printf specifiers are not implemented: prepare the parameter in a game module.

Pass a numeric count for plural selection; strings, bool, NaN, and infinity are unsupported. The engine embeds Unicode CLDR 48 **cardinal** rules for 224 language/region entries. Selection uses the rule of the dictionary supplying the translation, including fallback. Integer 1 and numeric 1.0 can select different forms: JSON preserves this distinction, but not arbitrary trailing zeros such as 1.00. Negative count is classified by magnitude while the original value is inserted. Compact forms such as '1 million' and ordinals are unsupported; CLDR operands c/e are zero. For a custom language code, set "plural_language": "ru" or "other" (one form). An unknown rule with plural messages fails validation. Rule source: [Unicode CLDR](https://www.unicode.org/cldr/charts/48/supplemental/language_plural_rules.html); data and license ship in the repository and build.

| API | Purpose |
| --- | --- |
| `tr(key, **params)` | Return a string in the current language |
| `message(key, **params)` | Create a reactive LocalizedText reference |
| `LocalizedText(key, params={})` | Same with an explicit parameter dict; str(value) translates when called |
| `language()` | Current normalized language code |
| `set_language(code, persist=True)` | Switch language and linked entities; unknown language raises without changing state |
| `available_languages()` | List of {'code': ..., 'name': ...} for a selector |
| `has_translation(key, language='', fallback=True)` | Check without warning; empty language means current |
| `localization_revision()` | Language/dictionary change counter for custom components |

Codes normalize to lowercase with underscores replaced by hyphens: EN_gb → en-gb. Selecting ru-RU falls back to registered ru when no separate regional catalog exists. Lookup follows selected code → parents (en-gb → en) → fallback and parents → default and parents. An absent translation returns its key; warn_missing logs WARN once per language/key until catalogs reload. Fallback use is also diagnosed; using a parent catalog is not an error.

auto_detect uses the first preferred macOS language or Windows user language, choosing a registered code or its parent. With no match, default_language remains. When save_selection is enabled, a saved selection overrides system/default choices. MenuController.show_settings() automatically adds a language selector. Selection is stored in <save_directory>/preferences/language.json; corrupt/outdated preferences are ignored with WARN. persist=False changes the current language but does not cancel an earlier pending persistent selection. save_selection=False disables preference reads/writes.

Preferences use a temporary file and atomic replacement after successful scene/frame processing or at shutdown. Failed scene initialization restores language and pending selection without writing the rejected scene's choice.

JSON entity text can be linked directly:

```json
{"id": "greeting", "kind": "text", "text_key": "hello", "text_params": {"name": "Anya"}}
```

Python entities support entity.text = forge.message('hello', name='Anya') and entity.set_localized_text('hello', {'name': 'Anya'}). text_key and text_params are separately accessible; parameters return a copy that must be reassigned after editing. text returns the current translation. Assigning a plain string clears its translation binding. Use set_localized_text to validate a key/parameter pair atomically; a manually created invalid pair raises when read or refreshed. LocalizedText.params also returns a copy.

Dialogue accepts forge.message(...) for lines and speakers, retaining keys/parameters in history. Changing language keeps a fully revealed line revealed; a partial line retains its revealed character count within the new length. capture/restore preserves history keys and the fully-revealed flag; plain string dialogue remains supported. Free text, player names, and titles of old saves do not translate automatically. For custom serialized data, store a key and JSON parameters rather than a LocalizedText instance.

C++ modules can call forge::active->localization.translate(key, params). A standalone forge::Localization loads with load(config) and provides translate, select, languages, has, and flush. After manually changing active C++ localization, refresh entities through forge::active->refreshLocalizedEntities(); Python set_language handles this atomically.

validate checks dictionaries, plural categories, braces, paths, and matching named-parameter sets for shared keys across languages. Complete translation coverage is not required because fallback applies. Editing translation JSON in dev reloads the scene and catalogs; failure preserves language, dictionaries, objects, and pending selection. build and init copy the configured locale directory; the game never downloads CLDR.

Language selection does not add glyphs to a font: choose renderer.font and fallback_fonts with the required characters (section 18.1). The renderer supports fallback fonts but lacks bidi/RTL shaping, ligatures, and complex-script layout. Arabic/Hebrew dictionaries can be stored and queried, but correct display requires a text backend extension. Localization does not automatically switch images, voice-over, or other assets; a game module can do so using forge.language().

## 11. Physics

In the default legacy backend, collider specifies full dimensions of an axis-aligned rectangle/box centered on the entity's world position. 2D uses X/Y; 3D requires positive X/Y/Z. Collider dimensions do not inherit visual scale or rotation. Update collider yourself when scaling an entity. Bullet's shapes/rotation are described in section 19.

Dynamic entities receive gravity, integrate at a fixed 1/120-second step, and resolve penetration along the smallest overlap axis. Mass distributes correction and impulse. Static bodies do not move through integration. Triggers generate contact enter/exit without correction. Nonpositive width/height disables a collider; 3D also requires positive depth. Contacts, overlaps, and raycast share this rule and exclude destroyed entities. Only active colliders enter pair selection, so UI without colliders is excluded. Legacy intermediates use double; positions/velocities are range-checked before float writes. In 2.5.1, arithmetic failure reports an entity ID and restores the entire legacy step rather than leaving partial updates or NaN/Infinity. Dev pauses on errors.

The default legacy backend is basic AABB physics without rotating bodies, friction, restitution, joints, mesh colliders, or continuous collision detection. Small fast objects may tunnel. Enable Bullet for rotating bodies, friction, and more accurate 3D collision (section 19), or replace the physics module for other models. The starter 3D free camera is intentionally not a physical character controller.

## 12. Logs and Diagnostics

Each session appends to forge.log in the configured project/user log directory (section 18.8). Messages include time and INFO/WARN/ERROR/DEBUG levels. forge.log() and print() write to terminal and file; sys.stderr is also redirected. A partial print line is emitted on flush or shutdown.

```python
import forge
forge.log('Starting load')
forge.log('Optional resource missing', 'WARN')
print('Progress:', 50, '%')
```

A user forge.log(..., 'ERROR') records a message but does not itself exit or open the log. An unhandled script/core exception records diagnostics, opens the log through the OS application, and makes run exit with status 1. In dev, on_update exceptions pause execution until a watched edit triggers recovery. Automatic opening occurs once per session; failed opening leaves a path and warning in the terminal. Open manually with forge.open_log().

build/validate errors, missing files, malformed JSON, and SyntaxError appear in terminal/log with exit status 1; these commands do not open the log automatically. The launcher retains full CMake/MSVC/Clang output, including filenames and line numbers.

Use forge.quit() for controlled exit. Do not call sys.exit() in game scripts: SystemExit is treated as a script error.

The configured log directory must be writable. Native faults such as SIGSEGV, forced termination, or OS failure cannot guarantee log opening. Built-in crash reports and the minimal native fatal handler are described in section 18.8; OS crash diagnostics remain necessary for native stacks.

## 13. Development Without Packaging and Hot Reload

Dev watches settings, paths, and python_paths in a background worker with a default 0.3-second wall-clock interval; development.watch_interval changes it (section 22.7). Python bytecode/cache metadata is excluded. Scene, Behavior, imported module, object, material, model, texture, shader, font, or locale changes reload the current scene. Changing entry_scene loads the new initial scene unless --scene overrides it.

Reload recreates entities and Behavior instances, resetting local scene state. Preserve progress with save/load. Startup scripts live for the whole session; restart dev to change their code. Renderer/window reload staging and rollback follow section 18.8 and the runtime transaction contract. C++ changes require compilation and restart.

Invalid Python, JSON, or shaders during reload record the error and pause gameplay. Fix the file to retry at the next watch check. Errors from on_update use the same recovery mechanism. Failure on the initial load exits; fix and relaunch. A successful replacement invokes the previous scene's on_destroy.

Reload prepares resources and configuration before switching. GPU resources, entities, localization, imports, audio, and managed input/configuration are restored on candidate failure. Initialization commands apply after success. The old scene/Behavior/startup may implement on_reload_failed(message) to inspect restored state; gameplay updates remain paused until recovery.

Transactions cover managed engine state. In Forge 2.5.1, forge.save, SaveManager.write, and SaveManager.delete defer disk changes during hot reload and JSON candidate preparation through editor apply/patch/undo/redo; rejection cancels the queue. Ordinary initial startup or scene loading outside these transactions retains immediate save/write/delete behavior. Use forge.defer_persistence(callable) for custom persistence. Arbitrary open()/Path.write_text(), network calls, external startup-object mutations, and elapsed playback time are not automatically rolled back. Avoid irreversible operations in candidate build()/on_start without explicit deferral. Files edited on disk are not restored from memory. The previous scene remains rendered after a rejected reload.


Reload replaces Forge-managed sys.path entries without accumulating duplicates, removes retired directories, and preserves other paths added by game code. Failure restores sys.path and the engine's managed-path list.

Imports from modules/scripts/scenes are invalidated during reload. Retired python_paths roots also invalidate cached modules and namespace packages; unchanged extra paths retain cached packages, including native extensions. Changed files are selectively invalidated by the watcher. Startup modules can retain imported references; restart to refresh those references. Additional python_paths are watched in 2.4 and later.

## 14. Builds and Distribution

```sh
python tools/forge.py build --output dist/MyGame
```

All copied directories in paths and python_paths are checked before packaging. Output cannot be inside any of them or replace the source project/parent. Paths normalize relative to the root: a valid ../Project/modules alias becomes modules in both copy plan and game.json. Each resource/icon destination is independently checked against the temporary staging directory. Packaged paths/python_paths, project.icon, and save_directory use normalized names. Nested symlinks must stay within the project; cycles and aliases containing output are rejected.

A directory build contains Game / Game.exe, game.json, configured asset directories, extra python_paths, private runtime, licenses, manifest.json with SHA-256 file hashes, and START.txt. Players do not need C++ sources or CMake. Game Python sources are included; packaging is not intellectual-property protection.

On macOS:

```sh
cd dist/MyGame
./Game
```

On Windows, run Game.exe; it opens the game window without an explicit run command. The executable locates game.json beside it regardless of the working directory. Move all build files together. Players need no installed Python; private interpreter startup ignores PYTHONHOME/PYTHONPATH.

Packaging first writes to a temporary sibling directory and moves it to the destination only after success. Existing output is never overwritten. Choose a new versioned name or explicitly remove the old build. Output cannot be the project root, an ancestor, or a directory inside copied assets.

On macOS, Python and native extensions are copied, Mach-O dependencies are relocated to relative paths, and modified binaries receive ad-hoc signatures for local execution. Public distribution requires your Developer ID signature/notarization as appropriate. Output ending in .app creates an application bundle; a directory build runs its executable from the terminal. See section 18.10 for signing/notarization; DMG creation is separate.

On Windows, packaging copies the Python DLL, standard library, DLLs, and runtime DLLs present alongside Python. If the selected Python distribution lacks MSVC runtime dependencies, install Microsoft Visual C++ Redistributable on the player's machine or provide an authorized app-local distribution.

Ordinary build cannot produce a Windows executable on macOS or vice versa. Compile and package separately on each target OS/architecture. .github/workflows/build.yml supplies macOS/Windows build tests and artifacts, plus Linux GPU checks. A workflow file's presence does not establish local Windows validation.

The package includes the selected CPython standard library, potentially tens of megabytes. storage.mode='project' requires the game directory to be writable for logs/saves. For Program Files or another protected directory, use storage.mode='user' to keep player data separately. .app packaging selects user storage automatically. See section 18.8.

## 15. Shaders

Defaults are graphics/default.vert and default.frag. Removing vertex_shader/fragment_shader selects embedded equivalents. Custom shaders use GLSL 330 Core and this interface:

- Attributes: location 0 — vec3 position, 1 — vec2 UV, 2 — vec3 normal; later subsystem attributes are described in sections 18–22.
- uniform mat4 u_mvp, u_model;
- uniform vec4 u_color;
- uniform sampler2D u_texture, int u_textured;
- uniform float u_lit (0 for 2D/UI, 1 for 3D).

Unused uniforms removed by the compiler are allowed. The default shader handles sprites/meshes/text; specialized particle/post passes have their own contracts. GLSL errors include the filename and OpenGL compiler message. Avoid zero scale on rendered objects: the standard shader uses inverse(u_model).

Text decodes from UTF-8; stb_truetype rasterizes requested glyphs and caches them. Noto Sans covers Latin and Cyrillic; other coverage depends on fonts. Ligatures, bidi/RTL shaping, and color emoji are unsupported. Text position is its top-left; \n inserts a line break. ui.Label handles wrapping/alignment. Rasterization accounts for size, scale, and HiDPI. entity.clip=(left, top, width, height) restricts a screen entity to a logical rectangle; None removes clipping.

## 16. Validation and Implementation Boundaries

python tools/forge.py test runs six native integration suites: integration, features, authoring, project_api, rendering, and simulation. They cover JSON/syntax, configurable structure, Python API, contacts/lifecycle, saves/transitions, tracebacks/reload, packaging, nested Behavior creation/destruction, teardown isolation, rollback, pause/listeners/UI, migrations/backup/autosave, audio settings/dialogue, and localization/fallback/preferences. CTest adds numeric and packager_paths for eight base suites, plus shader_compiler when the optional translator is enabled: ctest --test-dir build -C Release --output-on-failure. Regressions cover backup-only menu slots, reverse focus, collider consistency, small masses, physics/raster overflow, path normalization, and standalone execution. Coverage details are in section 20.6 and docs/FORGE_2.md.

Additional checks cover windows, 2D/3D rendering, audio, UI at 640×480/1280×720/1920×1080, GLSL recovery, and preserving old shaders after a rejected scene: python tests/graphics.py build/bin/forge (Windows: use the actual forge.exe path). These require a desktop, graphics driver, and audio device.

OpenGL requires separate windowed tests: headless validates logic rather than the driver. After graphics changes, run the relevant example scenes and GPU suites on target hardware.

Forge 2.0 adds the scene editor, skeletal animation, glTF/GLB/FBX/DAE, lights, and shadows. 2.2 adds PBR (section 20); 2.4 adds morph targets and retargeting (section 22). Navmesh and network multiplayer remain absent. Default physics is axis-aligned AABB; optional Bullet supports rotation, sphere/capsule, and convex sweep (section 19). Detailed boundaries are documented with each subsystem; modules can replace implementations.

For errors, read forge.log first. Fix missing-resource references; SyntaxError identifies file/line; GL failures concern shaders/context/driver; incompatible Python architecture requires matching Python/toolchain. Fixing game content does not require changing the core.

## 17. License, Core, and Required Attribution

The project uses the custom [Forge Attribution License 1.0](LICENSE). Commercial use and closed-source games are allowed. The license is not OSI-approved.

Distributed, published, or publicly demonstrated products must show **“Uses the Forge engine”** on the startup/loading screen and in the main menu or an About section accessible directly from it. With your own core changes outside an official release or documented approval by the affected copyright holders, show **“Built on the Forge engine (modified core)”** in both places. Internal tests are exempt from these screens.

The exhaustive core list is defined in LICENSE and explained in [CORE.md](CORE.md). Everything outside the core may be changed/extended for a game unless separately licensed: scenes, scripts, modules, settings, shaders, physics (engine/src/world.cpp), graphics, and audio. Changing only these does not require modified-core attribution. Dependencies/fonts retain their licenses.

Core changes are also permitted with appropriate origin attribution. Renaming/moving core code does not remove the requirement. Ordinary compilation of unchanged source for another OS/architecture does not count as a core modification.

Keep LICENSE and NOTICE in the distribution; build and init copy them automatically. Game developers place the required UI notices themselves; the engine does not automatically check menus/loading screens. Examples and translated notices: [ATTRIBUTION.md](ATTRIBUTION.md).

## 18. Forge 2.0: Additional Subsystems

All features below use public forge APIs or replaceable Python modules. Directories remain configured through JSON. Run the example:

```sh
python tools/forge.py dev --scene advanced.json
python tools/forge.py edit --scene editor-empty.json
```

--scene selects the initial scene for run/dev/edit; dev retains that override on reload. build uses entry_scene from configuration. advanced.json holds authored entities, lighting, and a separate camera; advanced.py attaches input, menus, and profiling. models/animated_triangle.gltf is an original editable example asset.

### 18.1. Cameras, UVs, Postprocessing, and Uniforms

```python
monitor = forge.set_render_target('door', {
    'width': 640, 'height': 360, 'mode': '3d',
    'position': [5, 3, 5], 'target': [0, 1, 0], 'fov': 60,
    'include_ui': False, 'layers': 1,
})
forge.spawn({'texture': monitor, 'screen': True,
             'position': [900, 180, 0], 'scale': [480, 270, 1]})
forge.remove_render_target('door')  # First remove/change textures referencing it.
```

Each camera has color/depth framebuffers and renders the world every frame. Dimensions are integers 1..4096, fov is 1..178 degrees, and mode is 2d or 3d. 2D uses its texture's coordinates with position as an offset. Entity.layer is a 32-bit mask; visibility requires intersection with the camera mask. Cameras render in name order; a camera's own texture is excluded from its pass, while others can show the previous frame. Cyclic camera graphs are not evaluated recursively. @target:name references are textures, never filesystem paths.

entity.uv=(u,v,width,height) selects a normalized 0..1 region from the top-left without exceeding the texture bounds. It works for sprites, meshes, and screen entities. Batching merges adjacent compatible sprites/glyphs while preserving order; texture, color, clipping, depth, camera, and uniforms define batch boundaries. Glyphs occupy GPU atlases and rasterize for their actual size/HiDPI up to 1024 pixels.

```python
from animation import SpriteSheet
sheet = SpriteSheet('actor.png', columns=8, rows=4, frames=range(8))
animation = sheet.animate(actor, fps=12, loop=True)
def on_update(dt): animation.update(dt)

forge.set_postprocess({'grain': .12, 'bloom': .2, 'aberration': 2,
                      'scanlines': .3, 'vignette': .25, 'fade': 0, 'gamma': 1})
forge.set_shader_uniform('custom_tint', [.2, .8, .5, 1.])
actor.uniforms = {'custom_amount': .4}
```

Postprocess is a separate fullscreen pass using UV/u_texture, u_time, and u_resolution; replace its shader through renderer.post_shader. Grain, scanlines, vignette, and fade: 0..1; bloom: 0..4; aberration: 0..32 pixels; gamma: .1..8. enabled=False disables it. Bloom is a threshold filter of neighboring bright pixels, not an HDR pyramid. Shader uniforms support bool, signed int32, float, and 1..4 float vectors; types must match GLSL (1.0 for float, 1 for int). Global uniforms are in rendering.uniforms, entity uniforms in Entity.uniforms, and post uniforms in postprocess.uniforms. u_* names are engine-owned; choose different names for custom parameters.

Uniform overrides are scoped to their draw. Removing an entity override restores the global value when present, otherwise zero; removed postprocess overrides also reset to zero. Set explicit global defaults for custom shader parameters that need nonzero values. Previous objects/frames must not supply implicit defaults. The same behavior applies to OpenGL, Metal, and Direct3D.

renderer.fallback_fonts in engine.json lists additional TTF files from graphics. Glyph lookup tries the primary font then fallbacks; absent coverage logs Missing glyph U+... once to terminal/file. Wrapping and measurement use the same font selection. Complex shaping, bidi/RTL, and color emoji require another text backend. Include each added font's license in the distribution.

### 18.2. Assets and Budgets

```python
from assets import AssetHandle, ScenePreloader
loader = ScenePreloader.scene('level.json')  # JSON scene, prefab, material, and model textures.
# Do not block the loading screen: poll loader.progress/ready from on_frame.
if loader.ready: forge.change_scene('level.json')
# loader.close() releases pins; the cache may continue using the resource.

with AssetHandle('audio', 'intro.srt') as handle:
    data = handle.wait(timeout=10).bytes()
```

ScenePreloader also accepts an explicit list of (group,file); use this for Python scenes. Single-entity JSON prefabs resolve inheritance/overrides and include PBR maps from material/material_properties. Virtual @mesh:/@target: references do not enter the file loader; the game creates them. Hierarchy prefabs use instantiate and require an explicit preload list. A closed loader can be populated again. JSON supports preload: [{"group":"audio","file":"intro.wav"}]. AssetHandle.info contains queued/loading/ready/failed, bytes, error, path, and group; images add width/height and models add a model metadata object under model (also returned by forge.model_info()). wait() is for setup/testing, not every frame. Four workers read/decode without OpenGL; GPU upload remains on the renderer's main thread. No additional Python packages are needed.

The CPU cache identifies file generations by canonical path, asset group, modification time, and size. New requests see size changes even if a tool preserves modification time; existing pinned handles keep their previous generation. This is metadata caching, not content hashing: for edits that preserve both time and size, change the timestamp or restart the runtime. A reload alone does not guarantee a new CPU cache generation. Imported models also track files read by Assimp, such as glTF binary buffers and OBJ material files; changed/missing dependencies invalidate subsequent requests while pinned handles keep their earlier model. A new request retries a failed model load after sidecar repair without requiring an edit to the main file.

asset_budget_bytes (default 256 MiB) limits resident CPU cache; renderer.gpu_budget_bytes (128 MiB) limits accounted GPU resources. forge.set_asset_budget(bytes) changes the CPU budget; forge.asset_stats() reports handles, entries, workers, loading, and resident_bytes. A live handle pins an asset against LRU eviction; close/context managers release it predictably. Insufficient budget produces an explicit error. These budgets exclude some CPython/Assimp/driver allocations and transient decoding peaks. validate/build fully decode and check content. run/dev/edit check structure/references and prepare current-scene resources; future scenes may preload in workers. Reload prepares models/textures/glyphs/framebuffers before commit, preserving the old scene on failure. Preload does not replace file validation.

### 18.3. Lighting, Models, and Skeletal Animation

```python
forge.set_lights([
    {'type':'directional', 'direction':[-.5,-1,-.3], 'color':[1,.9,.8],
     'intensity':1, 'shadows':True, 'shadow_extent':20},
    {'type':'point', 'position':[3,2,0], 'color':[1,.2,.1], 'range':10, 'intensity':2},
    {'type':'spot', 'position':[0,4,0], 'direction':[0,-1,0], 'range':20, 'cone':30}
])
info = forge.model_info('actor.glb')
actor = forge.spawn({'kind':'mesh', 'model':'actor.glb'})
forge.play_animation(actor, info['animations'][0]['name'], speed=1, loop=True)
forge.pause_animation(actor)
actor.animation_time = .5
```

Assimp imports OBJ, glTF 2/GLB, FBX, and COLLADA/DAE: node hierarchies, base color, diffuse/base-color maps, embedded textures, skin weights, and TRS clips. Unknown clips raise. model_info returns parts, vertex/bone counts, clip durations, external textures, and size. animation_pose(file,clip,seconds,loop) provides global node matrices for tools; Entity.animation/animation_playing are readable. Limits are 128 bones per mesh, four weights per vertex, and 2M source vertices. GPU vertex shaders perform skinning. Imported TRS keys interpolate without complete glTF cubic-spline tangent reproduction. Layered blending, retargeting, authored tracks, and morph targets are described in section 22; full blend-tree graphs are absent.

All external model dependencies must stay within the project. FBX/material support depends on the importer; errors include the path and Assimp message. Forge 2.2 imports major PBR/normal/metallic-roughness/AO/emissive maps (section 20). Forge 2.4 adds blending and rest-pose retargeting with the boundaries in section 22; adapt the model module for other requirements.

Up to 16 point/directional/spot lights are supported; rendering.ambient sets ambient RGB. One PCF shadow map is available for the first directional light with shadows=True. rendering.shadow_size is 64..4096; shadow_extent controls orthographic coverage around the camera target. Entity.casts_shadow=False excludes an object from the depth pass. Opaque/masked geometry casts shadows; text/screen UI and blended objects are excluded. Masked cutout shadows are supported in 2.2 and later; multiple maps and cascades are absent. Empty lights retain the earlier basic lighting.

### 18.4. Character Controller and AI

```python
from character import CharacterController
from ai import Scheduler, StateMachine, Timeline, Sequence
controller = CharacterController(player, speed=5, jump_speed=6, automatic=False)
controller.walk(horizontal, forward)  # Normalizes diagonal movement.
controller.jump()                    # Only when grounded.
controller.update(dt)

scheduler = Scheduler(budget=64)      # Automatic on_frame by default.
scheduler.every(.2, update_enemy)
scheduler.call_later(2, open_door)
state = StateMachine({'idle':{'update':idle_update}, 'chase':{'update':chase_update}}, 'idle')
state.change('chase'); state.update(dt)
sequence = Sequence([(1, show_title), (2, hide_title)])
sequence.update(dt)
```

forge.move_character(entity,delta,skin=.001) performs swept AABB movement along X/Z/Y, limits movement at walls, slides along free axes, and returns position/grounded/hits with IDs/normals. CharacterController makes the body kinematic and adds gravity/speed/jumping. For 2D, give the collider positive Z; collisions ignore that axis. Legacy is an AABB controller without capsule, slope/step climbing, rotating bodies, or automatic recovery from deep initial penetration. Bullet uses capsule/box/sphere convex sweep, slope normals, and bounded penetration recovery; see section 19. Spawn outside obstacles. forge.set_physics_enabled(False) disables physics independently of UI/scripts; physics_enabled()/gravity() report state.

Scheduler uses deterministic timer order and a per-frame callback budget. Missed repeating periods do not generate an unbounded catch-up queue. Canceled heap entries are compacted periodically, bounding retained tombstones and skipped work. Clock/deadline overflow raises ValueError before advancing the clock or scheduling a new task. If a repeating callback has already run and its next deadline overflows, that timer is removed and ValueError is raised; completed callbacks are not rolled back. Pause stops it unless run_paused=True; automatic=False gives explicit clock control. StateMachine invokes enter(previous), exit(next), update(dt), and event(name,...); recursive transitions are rejected. Timeline accepts (time,callback), duration/loop, and seek(seconds); Sequence uses sequential (delay,callback) entries. Loop fast-forward is capped at 1024 cycles per update. These modules impose no genre or pathfinding algorithm. Close long-lived components when retiring a scene.

### 18.5. Input, Rebinding, and Replay

```python
from input_actions import ActionMap, InputRecorder, InputReplay
replay = InputReplay('replay/test.json')  # Create before consumers if replay is needed.
actions = ActionMap({'jump':['key:SPACE','pad:0:a'],
    'move':[{'input':'key:A','scale':-1}, 'key:D', 'axis:0:left_x']})
if actions.pressed('jump'): controller.jump()
actions.bind('jump','key:J'); actions.save(); actions.load()
```

Bindings support key:NAME, mouse:0..7, pad:0..15:a/b/x/y/..., and axis:0..15:left_x/left_y/right_x/right_y/left_trigger/right_trigger. GLFW supplies standard gamepad mappings without a custom engine driver. ActionMap.value is -1..1; trigger axes normalize GLFW -1..1 to 0..1 so a released trigger is inactive. down/pressed/released track transitions; deadzone defaults to .15. bind(...,context='menu') and push_context/pop_context select the top context. SaveManager stores bindings.

forge.input_snapshot() copies the real frame: keys/pressed/released, mouse buttons/position/delta/scroll, gamepads, and key/mouse_button/text/scroll events. input_events()/gamepads() expose corresponding arrays. Recorder subscribes to on_frame; record.save('replay/session.json') writes versioned JSON with default max_frames=360000. Replay injects recorded input and dt through forge.inject_input; automatic consumers registered later see it. automatic=False provides explicit replay → actions → game logic → recorder order. finished=True marks completion; close unsubscribes. Only input/dt are replayed, not the entire world: control RNG seeds, initial state, callback order, and asynchronous results for reproducible regressions.

### 18.6. UI Styles, Transitions, and State

```python
from ui import Canvas, Theme, Column, Button, Label, ScreenStack
canvas = Canvas(Column(), theme=Theme({'Button':{
    'background':(.1,.15,.2,1), 'hover':(.2,.4,.3,1), 'transition':.15}}))
views = ScreenStack(canvas, {
    'home':Column(Button('Start', start_game), width=700),
    'settings':Column(Label('Settings'), width=700)}, duration=.25)
views.state['chapter'] = 1
views.show('home'); views.show('settings'); views.back()
```

Theme sets class colors; Widget.style overrides individual properties. Button uses background/hover/pressed/disabled, color/disabled_color, and transition time. Widget opacity multiplies parent opacity. ScreenStack retains trees, state dictionaries, and history; transitions change opacity, while the active screen becomes modal and blocks underlying input. close releases screens. Canvas(actions=actions) also accepts ui_next/ui_previous/ui_accept for gamepad/rebound navigation; Tab/Shift+Tab/Enter remain available. Layout still uses width/height/flex/padding rather than theme.

### 18.7. Spatial Audio, DSP, and Subtitles

```python
from audio import voice, music
from subtitles import Subtitles
sound = voice.play('intro.wav', pan=-.5, position=(-2,1,0),
                   lowpass=2000, highpass=100, echo_seconds=.12,
                   echo_decay=.3, echo_wet=.25, stream=True)
sound.pan=.5; sound.position=(2,1,0); sound.configure(pitch=1.1)
caption = Subtitles(sound, 'intro.srt', label)
forge.set_audio_listener((0,1,0),(0,0,-1))
```

Audio supports pan -1..1, pitch .01..8, spatial/position, min_distance/max_distance, lowpass/highpass (0 off, 20..23999 Hz), echo_seconds 0..5, echo_decay 0...95, and echo_wet 0..1. DSP uses actual miniaudio nodes: second-order filters and delay. Non-spatial sources retain normal stereo. sound.info/forge.sound_info report options, cursor/duration, volume, paused/playing, channel, and streaming. audio_settings.follow_camera enables camera-following listener updates.

audio_settings in engine.json configures max_voices (integer 1..1024), channel_limits, overflow (steal_oldest/reject), streaming (auto/stream/decode), and stream_threshold_bytes. Individual stream overrides policy. At capacity, the oldest lowest-priority voice is selected; lower-priority requests cannot displace higher-priority voices. Refusal returns ID 0. Stop/release unwanted looping sounds. configure_audio({...}) updates policy; audio_stats() reports voices/streams/DSP/stolen/dropped/duck_gains.

Ducking rules such as {'source':'voice','target':'music','gain':.3,'attack':.05,'release':.5} lower music during speech and restore it smoothly. Subtitles accept SRT or JSON [{'start':0,'end':2,'text':'...'}]; JSON supports localization key/params. They follow the actual playback cursor, remain during pause, and clear on stop/end. Streaming stalls also stall subtitle timing. InputReplay does not reproduce the audio device byte-for-byte.

### 18.8. Persistence, Directories, and Errors

storage:{"mode":"user","application_id":"com.example.game"} directs forge.save/SaveManager/preferences/localization to user directories. Compatibility default is project. macOS data/config/saves use ~/Library/Application Support/<id>, cache uses Library/Caches, and logs use Library/Logs. Windows uses LOCALAPPDATA, falling back to APPDATA. user_path(kind,relative='') always returns a user path regardless of mode; kind=data/saves/config/cache/logs. storage_path(relative) follows mode. Relative paths stay within the selected root; ../ and symlink escapes are rejected. The API returns a path; create parents for your own writes. FORGE_USER_ROOT overrides the root for tests. Use a stable, unique application_id.

During hot reload and editor apply/patch/undo/redo, forge.save and SaveManager.write freeze data/metadata; SaveManager.delete defers removal of primary/backup. Candidate rejection cancels the queue. New disk saves cannot be read before commit. Ordinary startup/scene loading outside these transactions retains immediate save/write/delete. forge.defer_persistence(callable,include_initialization=True) also defers during ordinary scene initialization by default and invokes immediately outside loading. False excludes ordinary initialization only, not reload/authoring transactions. Callbacks must provide their own atomic writes; copy mutable captured data. Commit callbacks run sequentially: failures are logged, completed writes are not undone, and later callbacks continue. Arbitrary Python filesystem/network operations remain the game's responsibility.

settings.WindowSettings stores resolution/fullscreen/vsync; MenuController exposes them. forge.set_window({...}) applies them; window_settings() reports current state. Fullscreen uses the primary monitor mode. Successful user preferences do not modify engine.json. Configured window dimensions are integers 1..16384 and fullscreen/vsync are bool. set_window allows width 320..16384 and height 240..16384, validating the entire request before changing the window. Headless skips physical window updates.

forge.profile() reports callbacks/audio/scripts/physics/frame timings and assets/renderer statistics. renderer_stats() includes draw_calls, batches, triangles, gpu_bytes/budget, glyphs/pages, render_targets, and render_ms. Timings are CPU wall time, not GPU timestamps; frame_ms excludes waiting for the next frame. In 2.5.1, profile()['localization_translations'] counts translations in the last refresh; unchanged text_key/params at the same localization revision are cached. Language/catalog/key/parameter changes invalidate it. Assigning a literal Entity.text clears its binding; assigning text_key again requests a refreshed translation.

Unhandled runtime errors create JSON in crash-reports beside the current log, with version, scene, time, profile, and message/Python traceback; log-opening behavior remains. forge.crash_report(message) creates a report manually. Fatal native signals/exceptions write native-last.txt; consult OS crash reports/Windows Error Reporting for native stacks. The C++ fatal handler uses a separate stack and minimal safe writes without Python/GL. Reports may contain game paths/messages; the developer/user must separately arrange any transmission.

### 18.9. Scene Editor

edit --scene editor-empty.json opens the windowed editor with simulation initially paused. Hierarchy selects entities; Inspector edits transforms, collider, color, visibility, screen/dynamic/trigger/casts_shadow, and text. Add/Duplicate/Delete, resource browsing, lights/camera/gravity, Undo/Redo (100 snapshots), Play preview, and JSON saving are available. Middle mouse moves the camera; WASD moves; Ctrl+S/Cmd+S saves the selected JSON name inside scenes. Dev/edit supports reload; since 2.5, an unsaved editor draft blocks automatic world replacement (section 23).

scene_data()/save_scene(file) serialize declarative data including scripts/data and JSON scene.script. In 2.5, updates to original JSON preserve unknown fields and project authored changes onto the source with external-edit merging; exporting to another file creates a complete snapshot. Conflicts do not overwrite the file. Python source, closures, and arbitrary state are not serialized. Runtime-created objects, including UI, enter snapshots; start authoring with editor-empty.json and attach Behavior through JSON scripts/data or code. Preview scripts may change the world; undo restores scene data while simulation is paused, not arbitrary Python state. Stopping preview does not reset; Reload reads the source. The editor has no visual scripting, shader/animation graph, or transform gizmos.

### 18.10. macOS Applications and Signing

```sh
python tools/forge.py build --output dist/MyGame.app
open dist/MyGame.app
python tools/forge.py sign --output dist/MyGame.app --identity 'Developer ID Application: Your Name (TEAMID)'
python tools/forge.py notarize --output dist/MyGame.app --keychain-profile your-stored-profile
```

.app includes Contents/MacOS/Game, Info.plist/PkgInfo, Resources/game.json, runtime, content, and ICNS converted from project.icon. Conversion uses sips/iconutil and needs macOS system services. The wrapper does not depend on a shell or cwd. Apps automatically use storage.mode=user. Directory packaging remains supported on macOS/Windows; Windows Game.exe uses app-local CPython DLLs.

Local packaging applies ad-hoc signatures for integrity, not Developer ID/Gatekeeper notarization. sign signs nested Mach-O binaries inside-out, enables hardened runtime/Python-library entitlements, updates the manifest, and seals the bundle. notarize creates a ZIP with ditto, invokes notarytool --wait, and staples it. Store credentials in Apple's Keychain beforehand, not engine.json. A real certificate/Apple Developer account is required; the commands do not create them. This release validates local ad-hoc signing and standalone execution only.

Directory manifest.json holds file SHA-256 hashes. In .app it resides in Resources, with paths relative to the bundle root. Game and CodeResources appear in signature_managed_files because external signing changes/verifies them. Also run codesign --verify --deep --strict MyGame.app. Do not remove CPython files or licenses from the bundle.

### 18.11. Testing the 2.0 Subsystems

```sh
python tests/features.py build/bin/forge
python tests/features_graphics.py build/bin/forge
ctest --test-dir build --output-on-failure
```

features.py checks actual native APIs: action maps/replay, AI/controller, asset handles/budgets, glTF/FBX/pose, pan/spatial/DSP/ducking/subtitles/voice limits, UI themes/navigation/UV/preferences, user storage/crash JSON, rendering, serialization, and persistence rollback. macOS checks manifest/signature and .app launch from another cwd with invalid PYTHONHOME/PYTHONPATH. features_graphics.py needs desktop/OpenGL and compares UV pixels, draw calls for 100 sprites/200 glyphs, skinning/shadow/postprocess frames, uniforms, window controls, and editor. integration.py/graphics.py retain compatibility checks. On Windows, use the current forge.exe and -C Release for multi-config CTest.

## 19. Forge 2.1: Particles and 3D Rigid Bodies

Both subsystems are native C++ with independent particles/physics Python modules. Their implementations, headers, modules, and particle shaders are outside the exhaustive core list and may be adapted/replaced. Core changes add entity/world contracts, bindings, configuration validation, and lifecycle resources; simulation logic resides in independent physics.cpp/particles.cpp. Older scenes retain their defaults.

```sh
python tools/forge.py dev --scene simulation.json
```

The example includes falling/rotating boxes, a bouncy sphere, a sloped platform, a capsule controller, steam, and sparks. WASD moves; Space jumps; E bursts sparks; R resets/impulses the sphere; Escape exits. English/Russian translations are in locales.

### 19.1. Enabling Physics

In a JSON scene or Python build() result:

```json
{
  "mode": "3d",
  "gravity": [0, -9.81, 0],
  "physics": {"backend": "bullet", "iterations": 20, "max_bodies": 10000},
  "entities": [
    {"id": "floor", "kind": "cube", "position": [0, -0.5, 0],
     "scale": [20, 1, 20], "collider": [20, 1, 20],
     "rigid_body": {"friction": 0.8}},
    {"id": "box", "kind": "cube", "position": [0, 3, 0],
     "collider": [1, 1, 1], "dynamic": true, "mass": 2,
     "angular_velocity": [0, 1, 0],
     "rigid_body": {"shape": "box", "friction": 0.8, "restitution": 0.2}}
  ]
}
```

physics is also accepted at engine.json's top level; scenes override individual fields. Default backend is legacy. Global Bullet applies to 3D; 2D without its own selection remains legacy. Explicit Bullet in 2D raises. At runtime, call physics.enable_3d(iterations=20,max_bodies=10000) or forge.configure_physics({...}); this replaces backend settings, so validate bodies before switching. physics_settings()/physics_stats() return configuration/diagnostics including backend, bodies/sleeping bodies, Bullet version, and precision.

Bullet 3.25 is statically compiled with double precision; the game's private Python needs no PyBullet. Dynamic bodies translate/rotate with contact impulses, friction, restitution, and sleeping. Static bodies have dynamic:false and rigid_body.kinematic:false. Kinematic bodies have dynamic:false, kinematic:true, with game-controlled pose affecting dynamics. dynamic:true and kinematic:true together are rejected. Triggers participate in queries and Behavior callbacks without pushing bodies.

collider is full world-unit dimensions independent of visual scale. The shape rotates with Entity.rotation:

| shape | Dimensions |
| --- | --- |
| box (default) | X/Y/Z lengths of the rotating box |
| sphere | Diameter is min(X,Y,Z) |
| capsule | Y axis; radius is min(X,Z)/2; Y is total height including hemispheres, at least the diameter |

Any nonpositive axis disables a 3D collider consistently for solver, raycast, and controller. Objects without active colliders retain free fall without collision/rotational simulation.

| rigid_body | Default / Range |
| --- | --- |
| friction | 0.5 / 0..10 |
| rolling_friction, spinning_friction | 0 / 0..10 |
| restitution | 0 / 0..1; both bodies' coefficients affect Bullet response |
| linear_damping, angular_damping | 0 / 0..1 |
| sleep | true; false disables automatic sleeping |
| ccd | true for dynamic bodies; enables swept-sphere protection for fast motion |
| kinematic | false |
| group, mask | 1 and 65535 / bit masks 1..65535 and 0..65535 |
| max_slope | 45 / 0..89 degrees for controller grounded detection |

Bodies collide when each body's group is allowed by the other's mask. mask=0 disables contacts. iterations is 1..100 (default 20); max_bodies is 1..100000 (default 10000), including statics/triggers. Raise budgets deliberately; limits do not guarantee a fixed frame time.

### 19.2. Forces, Impulses, and Queries

```python
import forge
from physics import RigidBody, raycast

body = RigidBody(forge.find('box'), friction=.8, angular_damping=.1)
body.force((10, 0, 0))                    # Newtons: acceleration depends on mass.
body.torque((0, 1, 0))                    # Torque.
body.impulse((0, 2, 0), point=(.5, 3, 0))  # Impulse at a world point, causing rotation.
body.wake()
hit = raycast((0, 10, 0), (0, -1, 0), 100, ignore='box')
if hit:
    forge.log(f"{hit['entity'].id}: distance={hit['distance']}, normal={hit['normal']}")
```

Native equivalents: forge.apply_force(entity,vector,point=None), apply_torque(entity,vector), apply_impulse(entity,vector,point=None), and wake_body(entity). point is a world-space point; omission means center of mass. Impulse requires a dynamic body with an active Bullet collider. Force/torque accumulate; automatic physics applies on_update force to every fixed step of that frame, then consumes it. Collision-callback force waits until the next frame. Add continuous force each on_update; with physics disabled it stays queued. Legacy also handles central force, but not torque.

Entity.angular_velocity is radians/second, rotation is degrees, linear velocity is world units/second, and mass is nominal kilograms for metric coordinates. Entity.impulse(x,y,z) retains the central linear impulse divided by mass; RigidBody.impulse additionally handles the world application point and Bullet rotation.

forge.set_rigid_body(entity,settings) replaces body settings; RigidBody(entity,**settings) merges with current settings. rigid_body_settings() reports configured fields; rigid_body_info() reports shape/dynamic/kinematic/sleeping. Changes synchronize before the next step/query. forge.overlaps() and forge.raycast() use the scene backend automatically. raycast_hit(origin,direction,distance=1000,mask=65535,ignore='',include_triggers=True) requires Bullet and returns None or a dict with entity ID, position, normal, distance, and fraction; physics.raycast substitutes the Entity for its ID.

forge.physics_step(dt) advances manually regardless of pause/physics_enabled. dt is 0..1 seconds; larger steps subdivide to 1/120. Positive dt consumes queued forces/torques. It invokes no Behavior contact callbacks and does not advance game time. Disable automatic physics first to avoid double stepping. dt=0 updates contacts only.

Active Bullet limits: mass 1e-6..1e6, collider dimensions 1e-4..1e4, absolute position/velocity/force/torque ≤1e6, rotation ≤1e7 degrees, and angular_velocity ≤1e4 rad/s. Gravity is also ≤1e6 per component. Setters/spawn/configuration reject NaN/Infinity and invalid vectors. The complete physics result is checked before writing Entity poses; failure preserves Entity state, logs context, and pauses dev. Legacy retains its own numerical ranges.

### 19.3. Controller and Physics Boundaries

CharacterController works with both backends. For Bullet use rigid_body.shape:'capsule' and collider:[1,2,1]. It makes the body kinematic, uses convex sweep/normal sliding, and max_slope for grounded state. Slopes are no longer treated as axis-aligned boxes. Bounded penetration recovery precedes sweep; spawn in free space because recovery cannot guarantee escape from every deep/enclosed overlap.

The bridge supports box/sphere/capsule only: no mesh/convex-hull colliders, joints, ragdolls, soft bodies, navmesh, or automatic step climbing. The controller does not fully handle moving platforms or force reactions against dynamics. CCD uses an inscribed swept sphere, reducing translational tunneling without guaranteeing protection for every thin rotating body. Backend recreation preserves Entity fields but resets internal contacts/sleep. scene_data() stores configuration/angular_velocity, not a complete Bullet solver snapshot. Cross-platform determinism and network rollback are not promised.

### 19.4. Particle Emitters

Define emitters in scene JSON/build() or create an effect from Python:

```python
from particles import Emitter

sparks = Emitter(texture='particle.png', rate=0, max_particles=300,
                 velocity=[0, 3, 0], velocity_random=[3, 2, 3],
                 gravity=[0, -5, 0], lifetime=[.5, 1.5], size=[.15, 0],
                 color_start=[1, .6, .1, 1], color_end=[1, .1, 0, 0],
                 blend='additive', seed=18)
sparks.move((0, 2, 0))
born = sparks.burst(100)
# sparks.stop() stops emission; existing particles finish their lives.
# sparks.start() resumes emission.
# sparks.close() immediately removes the emitter and its particles.
```

Particles occupy native pools rather than Entity/Behavior instances. Emitter needs no per-frame Python update. rate controls continuous emission, burst the initial burst; .burst(count) works even when continuous emission is disabled. It returns the actual spawn count; excess particles are dropped and counted in dropped.

| Field | Meaning / Default |
| --- | --- |
| position | Emitter position [0,0,0] |
| velocity, velocity_random | Initial velocity [0,1,0], symmetric spread [0,0,0] |
| gravity, drag | Acceleration [0,0,0], exponential velocity damping 0 |
| shape | point; also box (extent is half-size) and sphere (radius) |
| lifetime | Random seconds range [1,1], minimum .001, maximum 3600 |
| size | Size **at birth and death** [.1,.1], linearly interpolated |
| size_random | Random size multiplier [1,1] |
| color_start, color_end | RGBA [1,1,1,1] → [1,1,1,0], linearly interpolated |
| rotation, angular_speed | Initial angle range [0,0] and speed 0; degrees |
| rate, burst | 30 particles/second, 0 initial particles |
| duration, loop | 0 means unlimited emission; finite duration/loop=false stops emission; loop=true repeats the continuous-emission clock |
| max_particles, seed | 1000; seed=1 gives repeatable random initial parameters |
| texture, uv | '' means white square; file relative to textures; UV [x,y,w,h]=[0,0,1,1] |
| blend | alpha or additive |
| screen, layer | false; layer=1, participates in camera masks |
| follow, space | '' means no owner; otherwise Entity ID; world or local |
| enabled, name | true; optional name '' |

space:'world' leaves emitted particles in world space; local moves them with the emitter/owner. Following inherits position only, not rotation/scale. Owner destruction stops automatic emission; existing particles retain the last owner position. Local gravity/velocity still use world axes. 3D particles are camera-facing billboards; 2D particles lie in XY. screen:true uses screen pixels and pixel size, drawn above UI. The world particle pass follows geometry and precedes UI; alpha particles sort within that pass and do not write depth. They are not jointly sorted with transparent entities. Render-target cameras draw world particles by layer mask and screen particles when include_ui is enabled.

A scene permits 256 emitters and **100000 total reserved** particles. max_particles is 1..100000 per emitter; rate/burst are at most 100000. Limits apply even to empty pools; lower defaults for many effects. Color/UV: 0..1; size: 0..10000; size_random: 0..10; velocity_random/extent: ≤10000; absolute position/velocity/gravity: ≤1e6; drag: 0..100; duration: ≤86400; radius: ≤10000. Seed/layer are uint32. Invalid parameters fail without adding an emitter.

Emitter.info includes settings/alive/enabled/dropped/elapsed. snapshot(limit=32) returns live-particle diagnostics, not save configuration. close() is idempotent; stop(clear=True) also removes the emitter. Emitter.existing(id) wraps a JSON emitter; particle_emitters() lists IDs. Native APIs: particle_emitter(dict), particle_info(id), particle_burst(id,count), particle_remove(id), particle_enabled(id,bool), particle_position(id,vector), particle_snapshot(id,limit), particle_stats(), and particle_step(dt).

Gameplay pause stops automatic updates; disabling physics does not. particle_step(0..1) allows manual control; do not combine it with automatic stepping unless double advancement is intended. Emitters belong to scenes: transitions release them, old IDs expire, and failed reload restores previous pools/bodies. scene_data() stores emitter definitions rather than age, RNG state, or live particles; a restored scene restarts the effect.

graphics/particle.vert and particle.frag are editable. renderer.particle_vertex_shader/particle_fragment_shader resolve relative to graphics. Older projects without default files use embedded shaders; explicitly configured missing files are errors. Candidate GLSL failure preserves old programs/pools. Profiles contain particles_ms/pool stats; renderer diagnostics include particle_quads and particle_draw_calls across cameras. GPU buffers count toward the GPU budget. Particles lack geometry collisions, lighting/shadows, and GPU compute; atlas UV is static, not per-frame sprite animation.

### 19.5. Testing the 2.1 Subsystems

```sh
python tests/simulation.py build/bin/forge
python tests/simulation_graphics.py build/bin/forge
ctest --test-dir build -C Release --output-on-failure
python tools/forge.py run --headless --frames 180 --scene simulation.json --no-open-log
```

simulation.py uses the actual native bridge: shapes/rotation/queries, filters/triggers, CCD at a thin wall, forces/mass/torque/impulse, friction/restitution/sleep/gravity changes, sloped capsule control, ranges/atomic stepping, force across fixed substeps/callback queues, bounded/seeded particles, lifetime/curves/follow, teardown, and rejected/recovered reload.

simulation_graphics.py requires desktop/OpenGL: 5000 particles with ≤3 draw calls, pixel-checked UV/color/size, billboards/depth/render-target cameras, alpha sorting/additive blending, and retained frames/pools after particle GLSL failure. It complements graphics/features_graphics. On Windows use forge.exe; macOS results do not establish Windows binary behavior.

## 20. Forge 2.2: Materials, Hierarchy, and Procedural Geometry

These subsystems are optional. Older scenes without parent/PBR retain their coordinates/shading; filtering defaults to linear without mipmaps. Start with the built-in example:

```sh
python tools/forge.py dev --scene materials.json
```

The example demonstrates metal/rough surfaces, texture maps, transparency, a rotating parent, and a procedural triangle. P captures a PPM in the user captures directory; Escape exits. Text uses built-in localization.

### 20.1. Native PBR

A material is JSON in paths.materials or entity inline material_properties. material names the file; inline properties replace its rendering property set. Legacy color/texture remain supported; color retains finite values outside 0..1, while PBR base_color has separate range checks. Entity.material_properties returns an independent dict; reassign it to apply edits.

```json
{
  "shading": "pbr",
  "base_color": [1, 1, 1, 1],
  "metallic": 0.7,
  "roughness": 0.35,
  "normal_scale": 1,
  "occlusion_strength": 1,
  "emissive": [0, 0, 0],
  "alpha_mode": "opaque",
  "alpha_cutoff": 0.5,
  "albedo_texture": "surface/base.png",
  "normal_texture": "surface/normal.png",
  "metallic_roughness_texture": "surface/orm.png",
  "occlusion_texture": "surface/orm.png",
  "emissive_texture": "surface/emissive.png"
}
```

All maps resolve relative to paths.textures, checking root containment and existence. An empty name means no map. validate/build, spawn, and property assignment use the same material checks; failed assignment preserves the old material. Decode/GPU upload occurs during scene preparation/rendering; failed reload does not replace the working renderer.

| Field | Meaning / Default |
| --- | --- |
| shading | legacy by default; also pbr, unlit |
| base_color | RGBA factor 0..1, [1,1,1,1]; multiplied by entity/vertex/model-part color |
| `metallic` | 0..1, default 0 |
| roughness | 0..1, default 1; shader clamps the result to at least 0.045 |
| normal_scale | 0..10, default 1; scales normal-map XY |
| occlusion_strength | 0..1, default 1; affects ambient |
| emissive | Linear RGB 0..1000, default [0,0,0]; map multiplies the factor |
| alpha_mode | opaque, mask, blend; default blend for legacy, opaque for pbr/unlit |
| alpha_cutoff | 0..1, default 0.5; used by mask |
| texture, albedo_texture | sRGB color map; explicit Entity.texture takes precedence |
| normal_texture | Linear RGB tangent-space normal; TBN computed from UV derivatives |
| metallic_roughness_texture | Linear map: G roughness, B metallic |
| metallic_texture, roughness_texture | Additional linear R channels; multiply factors/packed MR when combined |
| occlusion_texture | Linear R; an ORM map can serve both MR and AO |
| `emissive_texture` | sRGB RGB |

PBR factors/vertex colors are linear; albedo/emissive textures decode from sRGB and output is encoded for display. The shader uses GGX, Smith approximation, and Schlick Fresnel. Directional/point/spot lights, shadows, and camera position affect PBR, including render targets. Empty lights retain built-in lighting. shading:unlit displays color independently of lighting, useful for procedural data.

Assimp imports major metallic/roughness factors, external/embedded base/normal/MR/AO/emissive maps, and the first vertex-color set. Shared metallic/roughness maps are recognized as glTF packed MR. validate decodes all external model maps. Explicit entity material maps take precedence; unset channels may use model-part maps.

This is direct-light PBR with approximate ambient: no IBL/environment reflections, HDR pipeline, transmission, parallax, glTF material extensions, multiple sampler/UV sets, or texture transforms. It uses the first UV set and shared filtering. double_sided accepts bool, but culling is currently off for all materials. Public channels are fixed; arbitrary vertex layouts require changing the available graphics module. Custom GLSL must support its required uniforms/attributes; old shaders remain valid but only compatible shaders display PBR maps.

### 20.2. Transparency Pass

In 3D, opaque writes depth, mask discards fragments below cutoff and supports cutout shadows, and blend draws after opaque geometry in far-to-near object-center order with depth testing but no depth writes. Screen/UI retains a separate order; 2D keeps its existing Z order.

Center sorting is not OIT; intersecting surfaces and large models with mixed alpha categories may need splitting. If any model part blends, the entire model enters the transparent pass. Blended entities do not cast shadows. Particles have a separately sorted pass, not joint ordering with transparent meshes. opaque forces alpha=1; use legacy/blend or explicit mask for older transparent textures.

### 20.3. Parents, Local Coordinates, and World Coordinates

```json
{
  "entities": [
    {"id": "root", "kind": "empty", "position": [5,0,0], "rotation": [0,45,0]},
    {"id": "child", "kind": "cube", "parent": "root", "position": [2,0,0]}
  ]
}
```

parent is an ID in the same scene; empty/absent means a root. JSON/build() may declare parents after children; runtime spawn requires an existing parent. Cycles, missing parents, mixed screen/world spaces, and matrix overflow are rejected before committing the graph.

position/rotation/scale and aliases local_position/local_rotation/local_scale define local TRS; a root's position is also world position. Local matrix order is T × Rx × Ry × Rz × S; world matrix is parent.world_matrix × local. Screen entities may have parents if the complete link remains in screen space.

```python
root = forge.find('root')
child = forge.find('child')
child.local_position = (2, 1, 0)
print(child.world_position)       # XYZ list.
print(child.world_matrix)         # Four rows of four components.
print(child.world_rotation)       # Accumulated XYZ degrees, without scale.
child.world_position = (10, 2, 0)  # Inverse parent transform -> local.
child.set_parent(root)            # Keeps local, changes world.
child.set_parent(None, keep_world=True)
for item in root.children(recursive=True):
    forge.log(item.id)
root.destroy()                   # Destroys subtree; on_destroy once.
root.destroy(children=False)     # Children become roots preserving world TRS.
```

parent is read-only Entity/None; world_matrix/world_rotation are read-only. set_parent accepts Entity, ID, or None. children() returns immediate children by default. keep_world retains the full matrix only if representable as local TRS; shear/singular matrices fail and roll back. Ordinary inheritance can display shear. Failed destroy(children=False) keeps the entire subtree alive/attached. world_position rejects inverse conversion through a singular parent.

**Physics contract:** dynamic bodies must be roots, with visual children allowed. Static/kinematic colliders may be children. Collider dimensions are explicit world units, not multiplied by visual scale. Bullet uses world pose/orientation; legacy remains axis-aligned. Raycast, overlaps, and character control use world coordinates. Parent scale/rotation may move a child without resizing its collider. Particle follow tracks owner world position; offsets still do not inherit rotation/scale.

Hierarchy does not inherit visibility, scripts, or data; it manages transforms/lifetime. Scene JSON records parent and local TRS. Editor Parent reparents with keep_world, World displays coordinates, and PBR/Metallic/Roughness edit materials. Other maps use JSON/API. Undo/redo stores scene JSON, not arbitrary Python callbacks.

### 20.4. Procedural Meshes

```python
import forge
from geometry import Mesh
mesh = Mesh('triangle', [[-1,0,0],[1,0,0],[0,1,0]],
            colors=[[1,0,0,1],[0,1,0,1],[0,0,1,1]])
entity = forge.spawn({'kind':'mesh', 'model':mesh.model})
mesh.update([[-2,0,0],[2,0,0],[0,2,0]])
entity.destroy()
mesh.close()
```

forge.set_mesh(name,data) returns @mesh:name. Data contains positions XYZ, optional triangle indices, normals XYZ, uvs XY, and colors RGBA 0..1. Optional arrays must match the position count. Without indices, positions form triples; without normals the engine calculates flat triangle normals. Values must be finite, indices in range, with at most 2M positions/2M indices. Names contain 1..128 ASCII letters/digits/spaces or -_., optionally prefixed @mesh:.

Updates replace data entirely and increment revision; invalid input/CPU budget preserves the old mesh. GPU buffers update on the OpenGL thread during preparation/rendering. Budgets are checked before removing the old GPU mesh, but driver allocation errors have no general rollback guarantee. remove_mesh(name) rejects live Entity references and tolerates an absent name. A later render releases unused removed procedural GPU meshes.

mesh_info(name) reports model/vertices/bytes/revision; geometry_stats() reports meshes/resident_bytes/budget_bytes/revision. geometry_budget_bytes defaults to 64 MiB, range 1..1 GiB. set_geometry_budget(bytes) changes the scene limit only if it covers resident meshes. It is independent of CPU asset/GPU budgets. Input JSON/Python, temporary arrays, old shared copies, and driver memory are excluded; this is not a process-wide memory limit.

The registry belongs to the scene: transitions/reload create a new one and rejection restores the old one. Reusing a name/revision in another scene does not reuse its old GPU mesh. Create declarative @mesh data in scene on_start before readiness checks. scene_data/save_scene store references, not vertices; your generator recreates them on startup. Editor undo retains the current registry, not generation history; failed authoring candidates use an isolated registry.

Native providers use forge/geometry.hpp and geometry(world).set/remove with the common data format; each generator needs no validator modification. Custom-format loading belongs to a provider registering @mesh results. There is no general arbitrary URI/custom-attribute registry. Chunks, terrain, and water remain game modules.

### 20.5. Filtering, Captures, and Scaling

```json
{"renderer": {"texture_filter": "nearest", "mipmaps": true}}
```

texture_filter is nearest or linear (default); mipmaps is bool (default false). Options cover file/imported/embedded textures; font atlases/render targets filter separately. GPU-generated mipmap chains count fully toward GPU budgets. nearest selects nearest mip levels; linear is trilinear. Anisotropic filtering and per-texture samplers are absent.

forge.user_screenshot('review.ppm') writes P6 PPM under forge.user_path('captures',...), outside the game bundle even in project storage. forge.screenshot writes inside the project for tests/development. Capture occurs on the next render, is unavailable headless, and remains inside the selected root.

forge.find uses a hash index of live entities, supporting removal/pruning/ID reuse. Legacy broadphase sorts X bounds and sends only candidate pairs to the solver in original entity-index order. Pairs are fixed before resolving each substep; a new overlap caused by correction may appear next substep. Dense scenes still have quadratic candidate counts. Raycast/controller/descendant queries remain linear or tree-dependent; this is not an all-purpose spatial index. world_stats() reports entities/indexed/candidate_pairs; the last is legacy-step statistics, not Bullet broadphase.

```sh
python tools/benchmark_world.py build/bin/forge
python tools/benchmark_world.py build/bin/forge --entities 1000 5000 --backend bullet
```

The benchmark creates isolated scenes of separated static boxes, warms bodies, measures 10N find calls and ten physics_step(1/120) calls. Results include Python bridge/transform checks, not GUI or dense dynamic scenes. It supplies no timing gate/FPS promise. Measure your game's distribution and workloads. Scriptless objects do not consume the 8192-per-pass scripted lifecycle limit; physics/memory budgets are independent.

Editor, hierarchy, geometry, and materials have separate implementations. Renderer receives Runtime explicitly. Shared scene/model headers do not pull in the Python API; moved engine.hpp contracts retain core origin (CORE.md). A process still supports one active Python runtime; a second reports an error. Window/input/GPU remain Renderer responsibilities; splitting files does not fully decouple subsystems.

### 20.6. Testing and CI

```sh
ctest --test-dir build -C Release --output-on-failure
python tests/graphics.py build/bin/forge
python tests/features_graphics.py build/bin/forge
python tests/simulation_graphics.py build/bin/forge
python tests/rendering_graphics.py build/bin/forge
python tools/forge.py validate --no-open-log
```

Current CTest suites: numeric, packager_paths, integration, features, project_api, authoring, rendering, and simulation. Launcher test runs the six Python integration suites without numeric/packager_paths. rendering checks JSON/Python hierarchy, TRS/reparent/cycles, shear/overflow/detach rollback, subtree lifecycle, indexed lookup/broadphase, both backends, procedural validation/budgets/release, materials, and nested metadata/data during accepted/rejected reload. Deferred metadata uses a deep JSON copy; tests wait for save commit rather than early on_start logging.

rendering_graphics compares actual pixels: PBR maps/factors, embedded glTF emission, alpha/mask passes, vertex colors/live GPU update/release, mesh names reused across scenes, child transforms, mip budgets, and user capture. The four GPU suites need desktop/OpenGL and audio devices for audio tests. Headless CTest does not establish GPU behavior.

The macOS/Windows workflow runs full CTest and directory packaging with editor ON/OFF. A separate Linux job uses Mesa software OpenGL/Xvfb and a virtual audio sink for all four GPU suites. This expands coverage but does not claim an unexecuted CI result; software rendering does not certify physical macOS/Windows GPU/audio hardware. Windows needs the actual forge.exe path and -C Release for multi-config. Developer ID signing/notarization needs your credentials.


## 21. Forge 2.3: Performance and Module Boundaries

New APIs are optional. Existing setters, raycast, scenes, materials, and emitter settings remain. C++ changes require compile; Python content still reloads in dev.

### 21.1. Particles: GPU Instancing and Textures

Default particles use OpenGL 3.3 instancing: position, UV rectangle, color, size, and angle require 52 bytes per particle rather than six 36-byte vertices (216 bytes). GLSL expands/rotates quads. The static quad adds 48 bytes; instance buffers grow to batch capacity, count toward the GPU budget, and release with renderer resources. Budgets do not limit transient driver memory when orphaning buffers.

Renderer takes one particle-pool snapshot per frame for all cameras/UI passes. Simulation, property interpolation, filtering, and alpha sorting remain CPU-side. Already ordered passes skip sorting; additive particles group by texture without depth sorting. Existing transparency/UI order remains; no GPU compute, collisions, or particle lighting are added.

Resolved texture paths cache across frames. GPU handles cache within a frame and mark textures for LRU; eviction cannot leave active references to removed resources. Successful reload/resource replacement clears both caches. Directory renames, symlink retargets, and replacements apply through reload rather than per-particle canonicalization. Paths are checked for project containment before caching.

Disable the optimization in engine.json:

```json
"renderer": {"particle_instancing": false}
```

particle_instancing defaults to true. If particle_vertex_shader or graphics/particle.vert differs from the embedded source, the engine selects the legacy six-vertex layout automatically. particle_fragment_shader works in both modes. Change the instanced layout through renderer.particle_instance_shader (default particle-instance.vert). Retain the default legacy vertex shader when you want instancing.

Instanced attributes: location 0 quad corner vec2; 1 position vec3; 2 UV rectangle vec4; 3 color vec4; 4 size/angle vec2. Uniforms: u_view mat4, u_right/u_up vec3; fragment texture u_texture. Outputs v_uv/v_color are retained. A new shader failure rejects reload and keeps the previous game.

forge.renderer_stats() adds:

| Field | Meaning |
| --- | --- |
| particle_instancing | Whether the instanced vertex layout is selected |
| particle_upload_bytes | Total live stream bytes across particle passes in the last frame |
| particle_sort_ms | CPU filtering/depth/order time across particle passes, excluding simulation |
| texture_path_resolutions | Newly resolved logical texture names this frame; a warm cache returns 0 |

Extra cameras increase upload bytes/quads/draw calls because each needs its own pass/order. render_ms is CPU rendering wall time, including submission/driver waits, not GPU timer queries.

### 21.2. Transforms and Batch Updates

position/local_position, rotation/local_rotation, scale/local_scale, world_position setters and move() update only the affected entity/subtree. An independent root no longer rebuilds the whole world. Cached parent poses use double precision; overflow/physics limits are checked before commit. Invalid descendant matrices preserve previous local/world transforms.

For a coordinated update of several objects:

```python
forge.set_positions([
    (player, (10, 0, 0)),
    (child, (2, 0, 0)),        # Local position relative to parent.
    (enemy, (20, 0, 0)),
])
```

Accepts a sequence of (Entity,(x,y,z)) pairs. Entities must be distinct live members of the current world. Updates are atomic: invalid objects, duplicates, NaN/Infinity, or final-tree overflow preserve old positions. Parent and child can change together; the final graph is validated. No JSON serialization is involved. Bulk updates can outperform repeated overlapping subtree updates; ordinary setters remain effective for a few independent objects.

world_stats() includes cumulative transform_audits (public-field scans) and transform_computations (computed world matrices). An unchanged world is audited without recomputing matrices. Python world_position/world_matrix/world_rotation reads retain audits for native-extension compatibility.

C++ modules use World::setLocalTransform, setPositions, and setWorldPosition. After direct public Entity writes or structural changes, call world.syncTransforms() before fast setters/cached pose reads. Explicit sync, physics, and rendering audit public fields. The cache cannot intercept arbitrary C++ writes; extensions retain this responsibility.

### 21.3. Physics and Batch Queries

Bullet caches shape/body settings, world poses, and velocities. Repeated queries on unchanged worlds do not build JSON signatures, reconstruct bodies, reapply identical transforms/velocities, or update AABBs. Collider/mass/shape/pose/force/torque changes, destruction, and ID reuse are checked; changes commit after all active bodies pass validation.

For many rays, use a batch:

```python
hits = forge.raycast_many([
    ((0, 1, 5), (0, 0, -1), 10),
    ((3, 1, 5), (0, 0, -1), 10),
])
for hit in hits:
    if hit is not None:
        forge.log(hit.name)
```

Each item is (origin,direction,distance). Results preserve input order and contain Entity/None like forge.raycast. Both legacy/Bullet and triggers are supported. All input numbers validate before querying. Use raycast_hit/physics for hit position/normal/fraction and mask/ignore.

A batch synchronizes once and queries that state without Python callbacks between rays. physics_stats() adds cumulative sync_audits/body_synchronizations; calling stats itself audits and increments the first. The second counts body creation/pose/velocity updates, not every solver integration.

Public C++ field audits remain linear; legacy rays inspect active colliders while Bullet uses broadphase. Caching does not make arbitrary scalar queries O(1). Batching reduces repeated audits. Collider dimensions/backend limits from section 19 remain unchanged.

### 21.4. Python Bridge and Architecture

Ordinary None/bool/int/float/str/dict/list/tuple convert directly between CPython and Json. Returned collections are independent copies. Tuple retains JSON array/list semantics; entity data is not a live view. Deep nesting, non-string dictionary keys, subclasses, and large integers retain the standard JSON fallback and its existing coercions/errors. NaN/Infinity, cycles, and unsupported types are rejected.

Implementations are separated:

| File | Responsibility |
| --- | --- |
| runtime.cpp | Startup, loop, shutdown, persistence orchestration |
| python_api.cpp | Base embedded Python API |
| python_bridge.cpp | Python/Json conversion |
| scene_runtime.cpp | Scene loading, Behavior lifecycle, scene transactions |
| reload.cpp | Change polling, managed sys.path/imports, reload transactions |
| particle_render.cpp | Particle sorting/batching and GPU stream |
| gpu_resources.cpp | Mesh/texture upload, GLSL compilation/linking, uniforms |
| text.cpp | Font cache/fallback, Unicode decoding, measurement |
| media.cpp | CPU image/model/font validation |

Moved core implementations retain origin under CORE.md; graphics/physics/text remain adaptable. One process supports one active Python runtime. active remains for existing native modules; file separation does not isolate sys.modules, Python streams, or GLFW for parallel games. Window/input/GPU orchestration remains in Renderer.

This stage does not add IBL/HDR, cascaded shadows, mesh colliders, joints, navmesh, or RTL/shaping. Terrain, chunks, voxel water, and genre-specific logic belong to game modules.

### 21.5. Reproducible Checks and Measurements

```sh
python tools/forge.py compile
ctest --test-dir build -C Release --output-on-failure
python tests/graphics.py build/bin/forge
python tests/features_graphics.py build/bin/forge
python tests/simulation_graphics.py build/bin/forge
python tests/rendering_graphics.py build/bin/forge
python tools/benchmark_hotpaths.py build/bin/forge --entities 1000
python tools/benchmark_hotpaths.py build/bin/forge --entities 50 --backend legacy --particles 10000
```

On Windows, select an existing forge.exe. The benchmark creates a temporary project and measures 1000 scalar setters, 100 individual raycasts, and 100 nested Python/Json roundtrips. --particles opens an actual window and averages CPU render_ms after warm-up. --entities sets count; backend is legacy/bullet/both. This measures your machine without timing gates/FPS promises. Compare identical parameters/scenes/window dimensions and workload.

rendering checks incremental counters, parent/child bulk commit/rejection, copies/Unicode/legacy coercion. simulation checks cache/batch in both backends and body updates. simulation_graphics checks instanced bytes, cached texture paths, UV/color/depth, legacy custom shaders/explicit fallback, and shader rollback. Current CI includes eight base CTest suites, the optional shader_compiler suite, and four shared GPU suites; local reports/screenshots stay out of the source distribution.

## 22. Forge 2.4: Reusable Objects and Animation

### 22.1. API Stability and Extensions

forge.api_version is the public contract version, currently 1, distinct from forge.__version__ and JSON/save/replay versions. capabilities() reports api_version, features, and python_runtimes_per_process. New functionality extends existing APIs; SpriteSheet, SpriteAnimation, Tween, play_animation, and pause_animation remain supported. Games need no core changes.

```python
from engine_api import require_api
require_api(1, 'prefabs', 'animation_layers', 'morph_targets')
```

modules/prefab_animation_example.py demonstrates AnimatedActor using public APIs; replace it freely. modules/native_example.cpp demonstrates FORGE_MODULE; engine.hpp remains compatible. Independent code can include lightweight scene.hpp/config.hpp/model.hpp/assets.hpp/audio.hpp/localization.hpp/renderer.hpp/logger.hpp contracts without a Python runtime dependency between them; aggregate engine.hpp includes bridge/orchestration.

One embedded Python runtime is active per process. active preserves native-module compatibility; header separation does not isolate sys.modules, output streams, or GLFW. An independent C++ World needs no global runtime; a host may provide prepareEntity for resource validation before publication. Moved contracts retain core origin under CORE.md.

### 22.2. Prefabs: Independent Instances and Inheritance

Existing single-entity prefab remains valid. A new JSON prefab can hold an entire hierarchy:

```json
{
  "entities": [
    {"id": "root", "kind": "empty"},
    {"id": "mesh", "parent": "root", "kind": "mesh", "model": "authoring.gltf"}
  ]
}
```

Prefab IDs are local. Exactly one root is required, all parents belong to the prefab, and cycles are forbidden. Instance IDs receive a prefix; empty prefix makes the engine select a unique one. The limit is 8192 entities. A root may attach to an existing entity; position offsets its local root position while preserving child positions.

```python
from prefabs import Prefab
actor = Prefab('animated-actor.json')
a = actor.instantiate(prefix='player_', position=(2, 0, 0))
b = actor.instantiate(overrides={'mesh': {'color': [.2, .8, 1, 1]}})
a['mesh'].visible = False      # b is unchanged.
print(a.root, actor.describe())
a.destroy()                   # Removes the instance tree.
```

forge.load_prefab(file) returns a normalized document copy. instantiate_prefab(file,prefix='',overrides={},parent=None,position=(0,0,0)) returns local-name → Entity dict. parent is a current Entity, its ID, or None. Overrides use local names and cannot change id/parent; use hierarchy methods afterward. Mutable JSON/data/playback are independent; imported models and immutable CPU/GPU resources are shared.

{"extends":"base.json",...} inherits through JSON Merge Patch: objects merge, arrays replace, null removes fields. Inheritance depth is 64; cycles fail. Hierarchies use instantiate; single entities also support existing spawn/prefab. Creation validates before publishing the tree; resource/hierarchy/overflow failure removes partial entities. Behavior attaches in the next lifecycle pass. Arbitrary external Behavior side effects are not part of the creation transaction.

### 22.3. 2D/3D property clips

animation.AnimationClip stores reusable property tracks of (seconds,value) pairs with strictly increasing times. Numbers/vectors support linear/smooth; use step for strings/bools/discrete UV frames. Endpoint values hold before/after the key range. Property animation is independent of 2D/3D mode, font, or renderer.

```python
from animation import AnimationClip
clip = AnimationClip({
    'position': [(0, (0, 0, 0)), (1, (100, 30, 0))],
    'visible': {'interpolation': 'step', 'keys': [(0, True), (1, False)]}
}, duration=1, events=[{'name': 'half', 'time': .5}], name='enter')
player = clip.play(entity, loop=False)
player.update(dt)             # The game calls this once per frame.
player.pause().seek(.3)
player.play()
for event in player.events(): print(event)
```

Timeline exposes time, speed, playing, finished, seek, pause, play, update, and events. Seek clears the queue without generating events. Marker crossings support loops/reverse speed; finished fires once at a non-looping clip's end. Limits: 128 tracks, 8192 keys/track, 1024 markers, 4096 queued events, dt 0..10 seconds, clocks/seek ≤1e6 seconds. Setters validate; failed track application restores already changed properties. Custom setters with external effects need a game-defined strategy. Property clips are Python modules, not GPU compute or physical root motion.

### 22.4. Skeletal Layers, Playback, and Transitions

Assimp imports glTF/GLB/FBX/DAE. model_info additionally reports skeleton name/parent and morph_targets part/name/weight. Existing animation_pose(model,clip,time,loop=True) remains. entity_animation_pose(entity) reports the actual global blended/retargeted/faded pose, with each node matrix flattened into 16 row-major values.

```python
from animation import Animator
animator = Animator(entity, 'Move')
animator.crossfade('Idle', .25)
animator.pause().seek(.5).play()
animator.configure(layers=[
    {'clip': 'Idle', 'weight': .4},
    {'clip': 'Move', 'weight': .6, 'mask': ['Joint'],
     'events': [{'name': 'step', 'time': .5, 'foot': 'left'}]}
])
for event in animator.events(): handle(event)
print(animator.info)
```

An animator has 1..16 layers; a layer includes clip, optional model, weight 0..1, time seconds (default 0), speed (default 1, ±1e6), loop (default true), mask of target-node names, events, mapping, and translation_scale. Weights above one normalize; missing weight uses rest pose. Local TRS blend with quaternion hemisphere alignment; crossfades use slerp. Transitions last 0..60 seconds. Interruptions begin from the current blended pose. Masks affect named nodes without implicit descendant expansion. Global matrices follow local blending.

Native APIs: validate_animator(entity,settings), configure_animator, transition_animator(entity,settings,seconds=.25), crossfade_animation(entity,clip,seconds=.25,speed=1,loop=True), animator_info, seek_animation, update_animation, and animation_events. Configuration is {"layers":[...],"playing":true,"auto_update":true}. Defaults update automatically; set auto_update false for manual stepping. Crossfade preserves the previous auto_update setting. Gameplay pause stops automatic clocks; legacy pause_animation also controls the new animator. Use seek_animation/Animator.seek for the new system; animation_time belongs to legacy playback.

Marker events contain type:marker, clip, and original name/time/parameters; completion events have type:finished and clip. Both sides of a transition can emit markers. Sampling does not invoke user code; games consume the queue. A time-zero marker does not fire initially but is crossed on the next loop. Huge intervals cannot generate millions of events: exceeding budgets rejects the step and preserves clocks/pose. Use seek for jumps. Failed configuration/stepping preserves the entity's previous animation state.

Entity JSON supports animator and morph_weights, validated at spawn/load, saved in scene_data/save_scene, and checked with declarative assets during validate/build. Live Python animators use the same checks. Scene JSON stores configuration/initial time, not a serialized Python state machine or intermediate fade.

### 22.5. Retargeting and Morph Targets

To transfer a clip, specify the source model and source → target name pairs:

```python
Animator(target, layers=[{
    'model': 'authoring.gltf', 'clip': 'Move',
    'mapping': {'Joint': 'TargetJoint'}, 'translation_scale': 2
}])
```

Empty mapping matches identical names. Target nodes must be unique; missing names, an empty resolved mapping, and singular source rest scales fail. Retargeting transfers **local rest-pose differences**: scaled translation, scale relative to source rest scale, and quaternion relative to source rest rotation applied to target rest TRS. Unmapped nodes keep rest pose. This suits compatible skeletons; anatomy detection, IK, foot locking, and automatic axis/proportion correction are absent. Prepare mappings/skeletons in DCC or replace the animation module.

Models import position/normal morph deltas, default weights, and animated weight channels, up to 32 targets per mesh part. Weights range −10..10; inputs/results must be finite. Weights animate/blend/crossfade and deform vertices before GPU skinning. set_morph_weights(entity,{'Smile':.7}) overrides corresponding animated weights; an empty dict returns control to clips. morph_weights(entity) copies overrides; morph_vertices(model,part=0,weights={}) returns deformed positions for tools/testing.

Morph deformation runs on CPU and uploads a GPU mesh stream. Instances do not share weights. Skinning remains GPU-based with 128 bones/part and four influences/vertex. This is not GPU morph compute, vertex-cache animation, or arbitrary topology animation. Tangent morph channels are not imported; exact glTF cubic-spline interpolation is not promised because sampling uses imported keys and linear/slerp.

### 22.6. State Machines and the Visual Editor

AnimationStateMachine(animator,states,transitions,initial=None) validates states/edges/clips. A state has clip/loop/speed/fade or a layer with retarget/mask/events. Transitions have from (name or *), to, optional exit_time (state seconds), and when parameters. Conditions are equality, {"gt":number}, {"lt":number}, or {"trigger":true}. First matching transition wins, at most one per update. set/trigger/enter/update/events/describe control the graph and return description copies. Triggers are consumed after a successful transition; game callbacks are not embedded in sampling.

```python
machine = AnimationStateMachine(animator,
    {'idle': {'clip':'Idle'}, 'walk': {'clip':'Move', 'fade':.2}},
    [{'from':'idle', 'to':'walk', 'when':{'moving':True}},
     {'from':'walk', 'to':'idle', 'when':{'moving':False}}])
machine.set('moving', True)
machine.update(dt)
```

Run python tools/forge.py edit --scene editor-empty.json and select/import a mesh. Inspector's **Open animation editor** opens a movable **Forge Animation Editor** window:

- Layer clip selection, weights/speed/loop, adding/removing layers;
- Play/Pause, independent preview while the scene is paused, time scrubbing;
- Transition duration and transactionally validated Apply;
- Visual marker timeline, event names/positions, adding/removing markers;
- New authored bone clip: node/time/local position/Euler rotation/scale and insert/replace/load/delete TRS keys;
- Morph sliders and return to animated weights;
- JSON draft for source models, retarget mappings, and masks with validation;
- Export animation prefab into objects as new JSON without overwriting existing files.

Draft and JSON synchronize explicitly through Draft → JSON / Validate JSON → draft. Apply applies the draft. Morph overrides/applied settings persist in scene JSON with authoring undo/redo. Preview/seek is temporary playback; arbitrary Python state-machine graphs are not serialized. This is a visual playback/layer/bone-key/marker/morph/retarget editor without a state-machine node graph. forge.editor_select(entity) lets extensions select the current entity.

Run the built-in demo with python tools/forge.py dev --scene authoring.py. It shows two prefab instances, separate retargeting, and a property clip; Space transitions the first object, Escape returns to welcome. authoring/retarget models are original Forge resources generated by tests/model_fixture.py and may be changed/replaced.

### 22.7. Loop, Hot Reload, and Reproducible Packaging

Frame listeners use hash membership and deferred removal: O(L) traversal/compaction instead of repeated linear searches. Removed listeners are skipped; new listeners start next frame. Contacts index changed pairs by entity ID then traverse scripts, retaining script order and enter-before-exit. Profile fields: listener_checks, listener_calls, contact_pairs_scanned, contact_callbacks.

The filesystem watcher recursively scans in a worker. The main thread reads prepared results and still performs reload/loading. development.watch_interval is 0.05..10 seconds, default .3, using wall-clock time. It checks paths/python_paths and creation/removal/mtime/size, excluding .git/__pycache__/pyc. It calls no Python/OpenGL and captures no mutable Config. Scan errors preserve the previous snapshot and report diagnostics. profile()['watcher'] includes scans/files/scan_ms/error.

Initial/reconfigured baselines are synchronous; shutdown waits for an active scan. Steady frames do no recursive scanning. Snapshots are metadata, not content hashes; unchanged size/timestamps can hide edits and intervals are not exact reload deadlines. Unchanged extra packages retain state; changed python_paths files invalidate imports, and retired directories/namespaces become unavailable. More than 8192 queued changed paths bounds the queue and invalidates managed roots wholesale. Candidate failure retains old imports/paths/world/playback/watcher. New watcher baselines precede scene reads so edits during preparation are not lost.

Window/input live in window.cpp, frame orchestration in game_loop.cpp, and dispatch/watcher in separate implementations. Renderer still combines GPU orchestration and scene passes; this does not promise fully independent concurrent renderers/runtimes.

```sh
python tools/forge.py compile
ctest --test-dir build -C Release --output-on-failure
python tests/rendering_graphics.py build/bin/forge
python tools/forge.py build --output dist/ReviewGame
python tools/verify_package.py dist/ReviewGame
```

Current CTest has nine base suites plus optional shader_compiler and launcher test has six integration suites; numeric/packager_paths are CTest-only. macOS/Windows CI runs CTest, builds a package, and checks hashes/licenses/three standalone scenes from another cwd with invalid PYTHONHOME/PYTHONPATH. Linux runs four GPU suites; rendering_graphics includes morph/retarget/editor checks. CMake links Threads explicitly and tracks embedded GLSL sources for reconfiguration. Each platform/environment validates itself; success on one does not establish another.

### 22.8. Authored Skeletal Clips

Create bone animation without an external clip: a layer with duration (1e-6..1e6 seconds) uses model rest pose and tracks. clip becomes the authored name; duration distinguishes it from imported clips. Without duration, tracks can override imported nodes. Node names refer to the source model before retargeting.

```python
Animator(entity, layers=[{
    'clip': 'Wave', 'duration': 2, 'loop': False,
    'tracks': [{'node': 'Joint',
        'position': [{'time': 0, 'value': [0,0,0]},
                     {'time': 2, 'value': [0,1,0]}],
        'rotation': [{'time': 0, 'value': [0,0,0]},
                     {'time': 2, 'value': [0,0,90]}]}]
}])
```

Position/scale are local vec3; rotation uses Euler degrees converted to quaternion/slerp. Times increase within duration; endpoint values hold. Limits are 128 node tracks, 8192 keys/property, and 65536 keys/layer. Existing blend/mask/event/retarget/fade contracts apply. Export preserves authored tracks/timeline. Create/edit tracks in the animation window without DCC/glTF export. This is not a full DCC: curve tangents, IK, graph nodes, and export of new glTF animations are absent.

Negative looping animator/timeline speed crosses loops backward, including starting at zero; info may report negative internal time. Sampling normalizes to positive clip time. Initial time/explicit seek remain nonnegative. Native/property timeline tests cover reverse looping and markers.

Standalone verification uses a temporary package copy so logs/preferences do not contaminate dist. CI sets PYTHONUTF8=1 for consistent Unicode fixture reads on macOS/Windows; verifier explicitly decodes native output as UTF-8.


## 23. Forge 2.5: Independent Editor Shells

The open format, API, and shell-development instructions are in [docs/PROJECT_API.md](docs/PROJECT_API.md): a public contract for builtin ImGui, external GUIs, and terminal tools. All project fields remain manually editable; unknown nested entity/camera/extension metadata is preserved. JSON Schema is in schemas/; builds/runs need no editor.

- python tools/forge.py project --serve — JSON-lines document API, JSON-only stdout, stderr/log diagnostics.
- sdk/forge_editor — standard-library Client/Document/ProjectError, patch/check/history/merge/save, and validate/dev/build launch. data/base return deep snapshots; use replace/patch to edit.
- python tools/forge.py shell --shell examples/editor/terminal_shell.py — independent shell host; --extension examples/editor/labels_extension.py loads a shared plugin.
- python tools/forge.py edit --shell none or --shell examples/editor/viewport_shell.py — viewport without builtin UI; callbacks run while gameplay is paused.
- forge.editor_command — snapshot/select/preview/apply/patch/undo/redo/load/save and extension commands. Mutations require pause; apply uses lifecycle transactions.
- editor.extensions or explicit edit --extension plugin.py — the same public commands in builtin/SDK shells. Builtin UI exposes Extension commands with JSON arguments.
- python tools/forge.py compile --without-editor and bootstrap --without-editor exclude ImGui; CMake FORGE_WITH_EDITOR=OFF. Ordinary compile restores the builtin shell.

Save As does not overwrite an existing unopened file. A new export becomes the current edit document and subsequent saves use its baseline. Saving original JSON does not expand unchanged defaults/prefab values. Document API base:null creates a file. Conflicts retain disk and draft; ProjectError.paths reports conflict paths. Automatic reload does not erase editor drafts: save/undo or explicitly reload and retry a watched edit. Undo apply may re-run callbacks; it does not restore arbitrary Python state/external actions.

project_api.py <binary> belongs to the nine base CTest suites and six-suite launcher checks. CI builds native ON/OFF on macOS/Windows; GPU suites run separately with builtin UI. Structural schemas do not replace native validate, media/GPU checks, or Python execution. API 1 offers an external document service and in-process paused editor API, without Qt/GTK viewport embedding, remote live-game editing, arbitrary ImGui widgets, or a network service.

## 24. Forge 2.5.1: Fixes and Compatibility

Schema/project API/save-envelope versions are unchanged. Strict checks reject fractional schema_version/window dimensions/audio limits, invalid booleans, and out-of-range values; silent truncation was a bug. Full validate/build also checks python_paths syntax. Packaging checks nested symlinks/cycles/output against all copied directories while retaining game folders named test/tests/tkinter. Save/backup symlinks cannot escape storage; core saves do not overwrite existing temporary files.

Legacy arithmetic failure restores positions/velocities/contacts for the whole step; collision corrections update child transforms. Bullet rejects 2D switching/out-of-range gravity before mutation. Text-metric overflow raises instead of returning Infinity. Large finite camera/light/listener directions use double-precision intermediates; unrepresentable matrices are rejected. Failed audio replacement preserves the old voice at capacity; removing a ducking rule releases its gain.

Editor candidates isolate procedural registries/listener membership; remaining retired-scene callbacks do not run after synchronous on_frame apply. Custom viewport shells continue on_update after rejected reload and may fix files or request editor load. If the shell itself fails in this mode, repeated updates are suppressed until successful loading/restart.


## 25. Forge 2.6: Direct3D 11 and OpenGL

OpenGL remains the default. Windows builds additionally include optional Direct3D 11; renderer.backend selects opengl/direct3d11/auto. Existing GLSL shaders keep their file/uniform API and are compiled through glslang/SPIRV-Cross on Direct3D. Native HLSL stage-pair overrides are optional; the two backends can share one project without platform rules in scenes.

See [docs/DIRECT3D11.md](docs/DIRECT3D11.md) for backend defaults, optional dependencies/build switches, all editable shader pipelines, native HLSL coordinates/semantics, budgets, and Windows verification limits. Backend/driver/debug changes require restart; rejected development changes retain the current device. forge.graphics_backends() and project capabilities expose compiled support; renderer_stats() exposes the actual backend after rendering. --silent-audio is independent of graphics and headless mode. Project/shell API stays version 1.


## 26. Forge 2.7: Native Metal on Apple Silicon

OpenGL remains default; macOS builds now include optional Metal alongside it. renderer.backend accepts metal/opengl/direct3d11/auto. Auto prefers Metal on macOS when compiled; explicit opengl retains the previous path. GLSL remains portable/editable, with native MSL stage pairs and configurable entry points available per pipeline. Game/shell API remains version 1. Backend changes require restart; shaders retain transactional reload.

See [docs/METAL.md](docs/METAL.md) for build switches, shader examples/contracts, coordinates, bounded uniform uploads, command synchronization, and actual verification limits. --without-metal excludes the backend; --without-editor is independent. The same project can preserve GLSL/HLSL/MSL settings and unknown fields across shells.

## 28. Native LOD and Visibility Optimization (2.8)

Enable optional native rendering optimization in `rendering.optimization` or call `forge.set_render_optimization({'enabled': True, 'occlusion': True})`. Defaults preserve existing projects. Each entity's JSON/Python `optimization` supports static model/texture `levels` at increasing distances, hysteresis, a maximum drawing distance, conservative local bounds, and explicit solid-box occluders. Culling affects draws only, leaving scripts, physics, audio, and animation clocks active. Screen UI/text remain outside these tests.

```python
forge.set_render_optimization({'enabled': True})
prop.optimization = {'levels': [{'distance': 40, 'model': 'low-detail.obj'}],
                     'hysteresis': .1, 'max_distance': 200}
# Opt an entity out, or disable the whole system:
prop.optimization = {'culling': False, 'lod': False}
```

Static mesh bounds enclose all model levels and inherit parent transforms. Skeletal/morph bounds require an explicit envelope for every pose; model LOD does not replace a skeleton. Occlusion needs authored boxes wholly inside opaque solid geometry for every LOD. Custom shader displacement needs enclosing bounds; cutouts must never be covered by a solid proxy. The conservative CPU grid runs independently per color camera, with no GPU readback. Distance/occlusion do not remove shadow casters. These systems provide no automatic mesh decimation/GPU Hi-Z/gameplay streaming.

Run `python tools/forge.py dev --scene optimization.py` for a visible example. Inspect `forge.renderer_stats()['optimization']` for per-pass culling/LOD/cache/CPU counters. `RenderOptimizationPolicy` and `Renderer::setOptimizationPolicy` are public C++ contracts; `modules/render_optimization_example.cpp` demonstrates replacement/composition. All shader/backend and editor-shell customization remains available. [Complete settings, contracts, schema, examples, and tests](docs/RENDER_OPTIMIZATION.md).
