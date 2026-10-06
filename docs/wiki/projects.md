# Projects and Configuration

[Forge wiki](../../GUIDE.md) · **Projects and Configuration**

A project is a settings file plus ordinary content files. Use configuration and public modules before changing the Core.

- [Source and game directories](#source-and-game-directories)
- [Project configuration](#project-configuration)
- [Configuration map](#configuration-map)

## Source and game directories

This tree shows the engine checkout. Generated games contain the content directories and documentation, but do not copy `engine/`, `tools/`, `sdk/`, `tests/`, `build/`, or `dist/`.

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

The core is limited to the code identified in [CORE.md](../../CORE.md); other implementations, including physics, graphics, audio, and shaders, can be adapted to your game. Core changes are also permitted, but ordinary game development should first use configuration and extensions to retain compatibility with engine updates.

Paths are relative to the settings file's directory. Texture/model/script/prefab references are relative to their respective `paths` groups. Absolute paths and `..` escapes outside the project root are prohibited, including outward symlinks. Add groups to `paths` and access them through `forge.asset_path(group, file)`. Do not use the project root itself as an asset directory: packaging requires separate resource directories.

Create a separate game while sharing the engine:

```sh
python /path/to/engine/tools/forge.py init --output /path/to/MyGame
python /path/to/engine/tools/forge.py dev --project /path/to/MyGame/engine.json
python /path/to/engine/tools/forge.py build --project /path/to/MyGame/engine.json --output /path/to/releases/MyGame
```

There is one C++ engine installation; game content lives in a separate directory. To include game-specific C++ modules, run `compile --project /path/to/MyGame/engine.json`.

## Project configuration

For example, with `scenes/main.py` from the [first scene tutorial](../../GUIDE.md#first-scene), bundled shaders/icon, and `scripts/startup.py`:

```json
{
  "schema_version": 1,
  "project": {"name": "My Game", "version": "1.0.0", "icon": "textures/icon.png"},
  "entry_scene": "main.py",
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

`schema_version`, `project.name`, `entry_scene`, and the basic `paths` groups are required. Empty asset directories are allowed; missing directories are an error. The icon path is relative to the project root and is used for the window on Windows. macOS ignores the GLFW window icon; a directory build contains the executable, while an output ending in `.app` creates an application bundle ([macos applications and signing](packaging.md)).

`startup_scripts` run once at application startup and survive scene changes. They may define `on_start()`, `on_update(dt)`, and `on_destroy()`. `python_paths` lists additional Python package directories inside the project; they are included in the build. `native_modules` lists additional `.cpp` sources compiled into the runtime.

Custom JSON fields are allowed and accessible through `forge.settings()`. Unknown fields do not automatically create new core features; they are metadata for your modules.

## Configuration map

The complete bundled example is [engine.json](../../engine.json). JSON Schemas in [schemas](../../schemas/project.schema.json) describe the open format; native `validate` additionally checks references, arithmetic, media, and Python syntax. Unknown fields are preserved by the document API and available to game modules; they are not automatically interpreted as engine features.

| Setting | Purpose / default | Reference |
| --- | --- | --- |
| `renderer.backend` | `opengl`; also `metal`, `direct3d11`, `auto` according to compiled support | [Backends](rendering.md#choose-a-graphics-backend) |
| `renderer.font`, `fallback_fonts` | Font files relative to graphics | [Text and shaders](rendering.md#portable-shaders-and-text) |
| `renderer.batching`, `particle_instancing` | Both true by default | [Rendering](rendering.md), [particles](particles.md) |
| `renderer.texture_filter`, `mipmaps` | `linear`, false | [Filtering](rendering.md#texture-filtering-and-captures) |
| `asset_budget_bytes` | 256 MiB CPU cache | [Assets](assets.md) |
| `renderer.gpu_budget_bytes` | 128 MiB accounted GPU resources | [Assets](assets.md) |
| `geometry_budget_bytes` | 64 MiB scene procedural registry | [Geometry](assets.md#procedural-meshes) |
| `physics` | Legacy default; optional Bullet settings | [Physics](physics.md) |
| `rendering` | Ambient, lights/shadows, custom uniforms, optional optimization | [Materials](materials.md), [rendering](rendering.md) |
| `rendering.postprocess` | Optional fullscreen effects/uniforms | [Cameras and postprocessing](rendering.md#cameras-uvs-postprocessing-and-uniforms) |
| `localization` | English/default fallback when absent; bundled project selects Russian | [Localization](localization.md) |
| `storage` / `save_directory` | Project storage / `saves`; user storage recommended for installed games | [Persistence](persistence.md) |
| `audio_settings` | Voice limits, streaming, listener following, ducking | [Audio](audio.md) |
| `development.watch_interval` | 0.3 seconds; range 0.05..10 | [Reload](development.md) |
| `editor` | Optional shell/extension selection; ignored by ordinary run/build | [Editor](editor.md), [document API](../PROJECT_API.md) |

Keep engine settings separate from scene overrides. `physics` settings can be overridden in scenes; scene `rendering` supports lights/ambient/optimization, and scene `rendering.postprocess` configures that scene's effects. `renderer` selects the graphics device and resource/shader configuration for the runtime. Changing backend/device options requires restart, even in dev.

Configuration integers/booleans are checked strictly: fractional dimensions, numeric values in boolean fields, NaN/Infinity, and out-of-range fields are errors. Optional fields receive defaults; remove a setting only when its default is appropriate. Custom content-group keys can be added without renaming the standard contract keys.
