# Scenes and Script Lifecycle

[Forge wiki](../../GUIDE.md) · **Scenes and Script Lifecycle**

Choose JSON for declarative scenes, Python for generated scenes, or combine JSON with a scene script.

- [JSON scenes](#json-scenes)
- [Python scenes](#python-scenes)
- [Object behaviors](#object-behaviors)

## JSON scenes

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

3D uses X right, Y up, and arbitrary world units. The camera is perspective; `target` is its look-at point and `fov` is in degrees. Position and target must differ; the renderer selects an alternate up vector for a vertical view. The built-in cube is 1×1×1. `rotation` is in degrees about X/Y/Z. Models retain their source coordinates. Lighting, shadows, and PBR are described in [rendering, cameras, and shaders](rendering.md) and [materials and lighting](materials.md).

## Python scenes

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

## Object behaviors

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
