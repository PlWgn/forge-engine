# Python API Reference

[Forge wiki](../../GUIDE.md) · **Python API Reference**

The embedded forge module is available inside the native runtime. Ordinary system Python cannot import it. Subsystem pages document the larger APIs.

- [Everyday functions and entities](#everyday-functions-and-entities)
- [Discover supported APIs](#discover-supported-apis)

## Everyday functions and entities

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
| play_sound(file, loop=False, volume=1, channel='sfx', fade=0) → id | Audio from paths.audio; see [audio](audio.md) |
| screenshot(file) | Save the next rendered frame as PPM; see [captures](rendering.md#texture-filtering-and-captures) |
| stop_sounds() | Stop all sounds |
| log(message, level='INFO') | Write to terminal and log simultaneously |
| open_log() | Open the log with the OS application |
| asset_path(group, file) | Absolute resource path |
| project_path(file='.') | Absolute path inside the project |
| settings() | Configuration copy as dict |
| save(name, value) | JSON save with atomic file replacement |
| load(name, default=None) | Read a save or return the default |

Keys: single Latin letters, digits, SPACE, ESCAPE, ENTER, TAB, BACKSPACE, LEFT/RIGHT/UP/DOWN, PAGEUP/PAGEDOWN, left SHIFT/CTRL/ALT, and F1..F25. An unknown key name raises an exception. key_pressed is true for one frame. Headless has no hardware keyboard/mouse input; recorded or injected input remains available. Audio mixes without a device.

Entity properties: id and alive are read-only; name, position, rotation, scale, velocity, collider, mass, color, texture, model, text, text_key, text_params, font_size, clip, visible, dynamic, trigger, and data are mutable. kind, scripts, and material are specified at creation. Methods: move(x,y,z=0), impulse(x,y,z=0) (changes velocity by impulse/mass), destroy(). Additional properties are documented in [entities](entities.md), [materials](materials.md), [animation](animation.md), and [rendering](rendering.md).

entity.data returns a copy of its JSON dictionary; assign the entire dict back to retain changes. Lifecycle state is held separately in C++; fields prefixed _forge_ are reserved for extensions. Python objects or functions cannot be stored in data: it must serialize to JSON.

Examples:

```python
entity = forge.spawn({'id': 'enemy', 'kind': 'cube', 'position': [0, 1, 0], 'collider': [1, 1, 1]})
forge.log({'stage': 'load', 'progress': 0.75}, 'DEBUG')
forge.save('slot1', {'health': 100, 'scene': 'level.json'})
state = forge.load('slot1', {'health': 100})
path = forge.asset_path('textures', 'portrait.png')
```

## Discover supported APIs

Use `forge.__version__`, `forge.api_version`, and `forge.capabilities()` inside the runtime. Feature checks through [engine_api.require_api](extensions.md#api-compatibility-and-capabilities) let a module state its requirements explicitly. `forge.settings()` and returned entity data are copies; mutating them does not change configuration or entities until the appropriate checked setter is called.

| Subsystem | Main entry points | Reference |
| --- | --- | --- |
| Lifecycle/pause | `on_frame`, `remove_listener`, `set_paused`, `is_paused` | [Scenes](scenes.md), [UI](ui.md#pause-dialogue-history-and-menus) |
| Hierarchy/prefabs | Entity local/world properties, `set_positions`, `load_prefab`, `instantiate_prefab` | [Entities](entities.md) |
| Graphics | `graphics_backends`, `set_render_target`, `set_lights`, `set_postprocess`, `set_shader_uniform`, `renderer_stats` | [Rendering](rendering.md), [materials](materials.md) |
| Optimization | `set_render_optimization`, Entity.optimization | [LOD/culling](../RENDER_OPTIMIZATION.md) |
| Geometry/assets | `set_mesh`, `mesh_info`, `asset_request`, `asset_info`, `asset_stats` | [Assets](assets.md) |
| Animation | `configure_animator`, `seek_animation`, `animation_events`, `set_morph_weights` | [Animation](animation.md) |
| Physics | `configure_physics`, `physics_step`, `raycast_many`, `raycast_hit`, `move_character` | [Physics](physics.md) |
| Particles | `particle_emitter`, `particle_burst`, `particle_stats`, `particle_step` | [Particles](particles.md) |
| Audio | `play_sound`, `sound_info`, `set_audio_listener`, `configure_audio`, `audio_stats` | [Audio](audio.md) |
| Text | `tr`, `message`, `set_language`, `measure_text` | [Localization](localization.md), [UI](ui.md) |
| Storage | `user_path`, `storage_path`, `save`, `load`, `defer_persistence` | [Persistence](persistence.md) |
| Input | `input_snapshot`, `input_events`, `gamepads`, `inject_input` | [Input](input.md) |
| Editor | `scene_data`, `editor_command`, `project_request`, `project_response` | [Editor](editor.md), [document API](../PROJECT_API.md) |
| Diagnostics | `log`, `profile`, `world_stats`, `crash_report`, `open_log` | [Development](development.md) |

Use these APIs on the runtime main thread. A Python background worker may prepare ordinary data; it must not call scene, input, audio, or GPU APIs concurrently. Retain entity IDs across reloads and look them up again rather than using retired Entity references.
