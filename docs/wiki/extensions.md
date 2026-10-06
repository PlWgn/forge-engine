# Modules and Extension APIs

[Forge wiki](../../GUIDE.md) · **Modules and Extension APIs**

Python modules and compiled C++ modules extend the shared public API. Independent implementations remain adaptable; Core changes are permitted but usually unnecessary for a game.

- [API compatibility and capabilities](#api-compatibility-and-capabilities)
- [Python and native modules](#python-and-native-modules)
- [Contracts and architectural boundaries](#contracts-and-architectural-boundaries)
- [Events, scheduling, and state machines](#events-scheduling-and-state-machines)

## API compatibility and capabilities

forge.api_version is the public contract version, currently 1, distinct from forge.__version__ and JSON/save/replay versions. capabilities() reports api_version, features, and python_runtimes_per_process. New functionality extends existing APIs; SpriteSheet, SpriteAnimation, Tween, play_animation, and pause_animation remain supported. Games need no core changes.

```python
from engine_api import require_api
require_api(1, 'prefabs', 'animation_layers', 'morph_targets')
```

modules/prefab_animation_example.py demonstrates AnimatedActor using public APIs; replace it freely. modules/native_example.cpp demonstrates FORGE_MODULE; engine.hpp remains compatible. Independent code can include lightweight scene.hpp/config.hpp/model.hpp/assets.hpp/audio.hpp/localization.hpp/renderer.hpp/logger.hpp contracts without a Python runtime dependency between them; aggregate engine.hpp includes bridge/orchestration.

One embedded Python runtime is active per process. active preserves native-module compatibility; header separation does not isolate sys.modules, output streams, or GLFW. An independent C++ World needs no global runtime; a host may provide prepareEntity for resource validation before publication. Moved contracts retain core origin under CORE.md.

## Python and native modules

forge.__version__ contains the C++ engine version from CMake; help and the build manifest use the same version. project.version in engine.json is your game's independent version.

Built-in low-level modules are C++ implementations of the world, graphics, input, audio, and saves exported through forge. The modules directory is added to sys.path; import shared Python functionality normally, such as import ui or import animation.

Ready-made modules include ui (Canvas, layout, widgets), dialogue, menus, saves, settings.AudioSettings, audio, math3d.add/mul/normalize, events.EventBus, animation.SpriteAnimation, and animation.Tween. Update SpriteAnimation/Tween instances and the legacy Button(bounds, callback) yourself from on_update. Canvas and newer widgets update automatically. Modules do not dictate a genre, game class structure, or global manager.

EventBus.on(event, callback) returns an idempotent unsubscribe function: repeated cleanup is safe, and cleanup releases that subscription's callback. Duplicate subscriptions remain independent. emit() snapshots callbacks in registration order and does not store unknown event names. Tween requires matching finite vector endpoints and a finite positive duration. Tween/SpriteAnimation updates require finite nonnegative dt and reject clock/frame overflow before committing elapsed time. A rejected native Tween property setter also leaves elapsed time unchanged.

For example:

```python
import forge
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

## Contracts and architectural boundaries

Ordinary None/bool/int/float/str/dict/list/tuple convert directly between CPython and Json. Returned collections are independent copies. Tuple retains JSON array/list semantics; entity data is not a live view. Deep nesting, non-string dictionary keys, subclasses, and large integers retain the standard JSON fallback and its existing coercions/errors. NaN/Infinity, cycles, and unsupported types are rejected.

Implementations are separated:

| File | Responsibility |
| --- | --- |
| runtime.cpp / game_loop.cpp | Startup, frame orchestration, shutdown, persistence |
| python_api.cpp | Base embedded Python API |
| python_bridge.cpp | Python/Json conversion |
| scene_runtime.cpp | Scene loading, Behavior lifecycle, scene transactions |
| reload.cpp | Change polling, managed sys.path/imports, reload transactions |
| particle_render.cpp | Particle sorting/batching and GPU stream |
| gpu_resources.cpp / graphics_device.cpp | Mesh/texture resources and graphics-device dispatch |
| shader_compiler.cpp / direct3d11.cpp / metal.mm | Portable shader translation and optional native backends |
| window.cpp | Window and input |
| render_optimization.cpp | Replaceable LOD/visibility policy |
| text.cpp | Font cache/fallback, Unicode decoding, measurement |
| media.cpp | CPU image/model/font validation |

Moved core implementations retain origin under CORE.md; graphics/physics/text remain adaptable. One process supports one active Python runtime. active remains for existing native modules; file separation does not isolate sys.modules, Python streams, or GLFW for parallel games. Window/input/GPU orchestration remains in Renderer.

Forge does not implement IBL/HDR, cascaded shadows, mesh colliders, joints, navmesh, or RTL/shaping. Terrain, chunks, voxel water, and genre-specific logic belong to game modules.

## Events, scheduling, and state machines

```python
from ai import Scheduler, StateMachine, Timeline, Sequence
from events import EventBus

# Supply your game's callbacks; scheduler subscribes to on_frame automatically.
scheduler = Scheduler(budget=64)
scheduler.every(.2, update_enemy)
scheduler.call_later(2, open_door)
state = StateMachine({'idle': {'update': idle_update},
                      'chase': {'update': chase_update}}, 'idle')
state.change('chase')
state.update(dt)  # StateMachine itself is manually updated.
sequence = Sequence([(1, show_title), (2, hide_title)])
sequence.update(dt)

bus = EventBus()
unsubscribe = bus.on('door.open', handle_open)
bus.emit('door.open')
unsubscribe()  # Idempotent; releases this subscription's callback.
scheduler.close()
```

Scheduler uses deterministic timer order and a per-frame callback budget. Missed repeating periods do not generate an unbounded catch-up queue. Canceled heap entries are compacted periodically, bounding retained tombstones and skipped work. Clock/deadline overflow raises ValueError before advancing the clock or scheduling a new task. If a repeating callback has already run and its next deadline overflows, that timer is removed and ValueError is raised; completed callbacks are not rolled back. Pause stops it unless run_paused=True; automatic=False gives explicit clock control. StateMachine invokes enter(previous), exit(next), update(dt), and event(name,...); recursive transitions are rejected. Timeline accepts (time,callback), duration/loop, and seek(seconds); Sequence uses sequential (delay,callback) entries. Loop fast-forward is capped at 1024 cycles per update. These modules impose no genre or pathfinding algorithm. Close long-lived components when retiring a scene.
