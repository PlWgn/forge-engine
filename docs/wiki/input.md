# Input, Actions, and Replay

[Forge wiki](../../GUIDE.md) · **Input, Actions, and Replay**

Forge 2.10 supplies a native C++ action manager with live rebinding. You can use it with any game UI, editor shell, headless runtime or recorded input. Raw input and the existing Python `ActionMap` remain available.

- [Quick start](#quick-start)
- [Bindings and keys](#bindings-and-keys)
- [Action state and contexts](#action-state-and-contexts)
- [In-game rebinding](#in-game-rebinding)
- [Preferences and open format](#preferences-and-open-format)
- [Native use and replacement](#native-use-and-replacement)
- [Raw input and replay](#raw-input-and-replay)
- [Limits and verification](#limits-and-verification)

## Quick start

```python
import forge
from input_actions import InputManager


def on_start():
    global controls
    controls = InputManager({
        'jump': ['key:SPACE', 'pad:any:a'],
        'move': [{'input': 'key:A', 'scale': -1}, 'key:D', 'axis:any:left_x'],
        'interact': ['key:E', 'mouse:1'],
    })
    controls.load()  # False when no preferences exist; defaults remain active.


def on_update(dt):
    player = forge.find('player')  # Your scene supplies this entity.
    player.position = (player.position[0] + controls.value('move') * 200 * dt,
                       player.position[1], player.position[2])
    if controls.pressed('jump'):
        forge.log('Jump requested')


def on_destroy():
    controls.close()
```

`InputManager` from `input_actions` supplies automatic scene-owned frame updates and optional `SaveManager` preferences. Its `native` property is the `forge.InputManager` C++ instance. Direct `forge.InputManager(actions, deadzone=.15)` has no subscriptions or disk access: call `update()` explicitly. Both expose the same action/rebinding methods. `forge.capabilities()['features']` includes `native_input`.

Try the complete localized controls screen:

```sh
python tools/forge.py dev --scene input.py
```

Click **Rebind**, enter a control, then **Save bindings**. This example changes slot 0, retains alternate bindings, reports conflicts, offers defaults, and ignores the confirmation control until release. Escape cancels capture. Its UI is ordinary editable Python, separate from the manager.

## Bindings and keys

An action accepts one string/descriptor, a list of up to 16 descriptors, or `[]` to disable it. Bindings add their scaled values; `value(action)` clamps the sum to `[-1, 1]`. `raw_value(action)` returns the sum before clamping.

| Input | Meaning |
| --- | --- |
| `key:SPACE`, `key:J`, `key:HOME` | Keyboard key |
| `mouse:0` … `mouse:7` | GLFW mouse button; 0 left, 1 right, 2 middle |
| `pad:0:a`, `pad:any:a` | Standard mapped gamepad button; `any` accepts every connected pad |
| `axis:0:left_x`, `axis:any:left_x` | Gamepad axis; `any` selects the strongest absolute value |
| `wheel:up/down/left/right` | One-frame scroll direction, clamped to 0..1 |

Gamepad IDs are 0..15. Buttons: `a`, `b`, `x`, `y`, `left_bumper`, `right_bumper`, `back`, `start`, `guide`, `left_stick`, `right_stick`, `up`, `right`, `down`, `left`. Axes: `left_x`, `left_y`, `right_x`, `right_y`, `left_trigger`, `right_trigger`. Trigger values normalize GLFW's idle -1 to 0 and fully pressed +1 to 1. Missing/disconnected pads are neutral; disconnecting a held control releases its action. Mapping/device recognition comes from GLFW, without an extra engine driver. A device ID is not a persistent hardware identity; use `any` or implement player/device assignment in your game.

Named keys include A–Z, 0–9, punctuation `- = [ ] \\ ; ' , . /` and backtick; `SPACE`, `ESCAPE`, `ENTER`, `TAB`, `BACKSPACE`, `INSERT`, `DELETE`, `PAGEUP`, `PAGEDOWN`, `HOME`, `END`, arrows, F1–F25, `CAPS_LOCK`, `SCROLL_LOCK`, `NUM_LOCK`, `PRINT_SCREEN`, `PAUSE`, `MENU`, `WORLD_1`, `WORLD_2`; `KP_0`–`KP_9` and `KP_DECIMAL/DIVIDE/MULTIPLY/SUBTRACT/ADD/ENTER/EQUAL`. Generic `SHIFT/CTRL/ALT/SUPER` accept either side; `LEFT_SHIFT`, `RIGHT_SHIFT`, `LEFT_CTRL`, `RIGHT_CTRL`, `LEFT_ALT`, `RIGHT_ALT`, `LEFT_SUPER`, `RIGHT_SUPER` distinguish sides. Names are case-insensitive. `RETURN`, `ESC`, `CONTROL`, `CMD`, `COMMAND`, `WIN` normalize to the corresponding canonical names. Text entry uses Unicode text events, not action key names or translated characters.

```python
controls.bind('save', {
    'input': 'key:S',
    'modifiers': ['CTRL'],
    'exact_modifiers': True,
})
controls.bind('look_right', {
    'input': 'axis:any:right_x', 'direction': 1, 'deadzone': .2,
})
```

Descriptor fields:

| Field | Default / rule |
| --- | --- |
| `input` | Required supported control string |
| `scale` | 1; finite -1000..1000, negative reverses the value |
| `deadzone` | Manager default .15; finite 0 ≤ value < 1 |
| `direction` | 0; axes only: 0 signed axis, +1 positive half, -1 negative half (half axes produce positive values before scale) |
| `modifiers` | `[]`; up to four unique generic SHIFT/CTRL/ALT/SUPER names; apply to any input kind |
| `exact_modifiers` | False; true rejects additional modifiers, excluding the primary modifier key itself |

Axes inside the deadzone yield 0; outside it, magnitude remaps continuously from 0 to 1. Each binding freezes its deadzone when defined. Editing a profile's default deadzone affects shorthand overrides and future definitions, while explicit binding values remain. Exported descriptors include all effective fields. Unknown descriptor metadata survives edits/exports so your extensions can use it; the builtin evaluator ignores it.

`define(action, bindings, context='default')` defines both current bindings and reset defaults. `bind(action, bindings, context='default', conflict='share')` changes only current bindings. Constructor actions define the default context. There is no mandatory input section in `engine.json`; scenes or modules can load definitions from their own JSON files.

## Action state and contexts

| Method/property | Result |
| --- | --- |
| `value(action)`, `raw_value(action)` | Current scalar value / sum |
| `down(action)` | Absolute clamped value > .001 |
| `pressed(action)`, `released(action)` | Transition from inactive / to inactive since the preceding update |
| `held(action)` | Seconds since initial activation; initial press is 0 |
| `repeat(action, delay=.4, interval=.08)` | Initial press and repeat pulses after delay; delay 0..3600, interval .001..60 seconds |
| `context` | Top context name |
| `bindings`, `defaults` | Independent copies grouped by context/action |
| `reset(action='', context='')` | All defaults when empty; one action (default context if unspecified), or one context |

Unknown/inactive action names return neutral state. Switching from a positive value to negative without crossing zero keeps the action held; use separate half-axis actions if you need direction-specific edges. Repeat returns a boolean even when a long frame crosses multiple intervals. Call update once per input frame; repeated reads do not consume edges.

```python
controls.define('ui_accept', ['key:ENTER', 'pad:any:a'], context='menu')
controls.push_context('menu')
# Only menu actions evaluate; the input manager doesn't pause gameplay itself.
controls.pop_context()
```

The top context is exclusive; it does not fall through to lower contexts. Unknown contexts and popping the base context are errors. Bind/define/reset/profile import/context changes clear derived action state and cancel capture; held controls can produce a new press on the next update. Failed operations preserve bindings, context, capture and evaluated state. Conflicts only inspect the selected context and other actions, never the same action's alternate slots.

The facade's frame listener continues during ordinary `forge.set_paused(True)`, allowing controls/settings UIs to work. A development error pause suspends ordinary listeners until recovery, and the builtin editor's paused simulation does not dispatch game listeners. Editor shells can drive their own manager explicitly. `automatic=False` gives full ordering control:

```python
controls = InputManager(definitions, automatic=False)
# Your frame callback:
# replay.update(); controls.update(); game_logic(); canvas.update(); recorder.update()
```

## In-game rebinding

```python
controls.begin_rebind('jump', slot=0, conflict='reject', timeout=10,
                      devices=['keyboard', 'mouse', 'gamepad', 'wheel'],
                      threshold=.6, chords=True, escape_cancels=True)
# Poll during your UI update:
result = controls.capture_state()
if result['status'] == 'bound':
    controls.save()  # Explicit; capture never writes preferences by itself.
# Optional UI cancellation:
controls.cancel_rebind()
```

`context` defaults to the active context. `slot` replaces an existing slot, or appends when equal to the current binding count (0 for an unbound action). Controls held at capture start, including the click/keyboard press that opened your settings, must release/return below threshold before capture. Scroll present at capture start is ignored on the opening frame. Other actions are suppressed while listening and on the completion frame; the captured primary control is then suppressed until released/neutral. Raw input remains accessible to your UI. Axis suppression uses the smaller of the slot deadzone and half the capture threshold, so a large deadzone cannot treat a still-deflected stick as released.

Capture selects fresh keyboard keys/chords, mouse/gamepad buttons, gamepad half axes crossing `threshold` (.001..1), or a wheel direction. A modifier alone is captured upon release, allowing Ctrl+J to be captured as a chord first. Required modifiers use generic names; set explicit side-specific primary keys through `bind` when needed. Existing slot scale/deadzone/extension metadata survive capture. If several controls arrive together, priority is keyboard, mouse, gamepad, wheel, then key/device/control index; capture does not infer player intent from simultaneous inputs.

Timeout is .001..300 seconds of supplied input frame time. Escape cancels by default even with keyboard capture disabled; `escape_cancels=False` allows binding it. Gamepad threshold concerns the normalized axis before deadzone, and idle triggers remain neutral.

| Conflict policy | Behavior |
| --- | --- |
| `share` | Allow the same control on multiple actions |
| `reject` | Direct bind throws; capture reports conflict and keeps listening without changing bindings |
| `replace` | Remove only conflicting bindings from other actions; retain their unrelated slots |

`conflicts(action, bindings, context='default')` returns copies of `{action, context, slot, binding}` records. Wildcard gamepads overlap specific IDs; opposite half axes are distinct; chords overlap when their modifier requirements can hold simultaneously. Rebinding one slot checks/removes conflicts only for the replacement; unchanged shared slots remain.

`capture_state()` returns a copy with `status`: `idle`, `listening`, `conflict`, `bound`, `canceled`, `timeout`. Listening includes action/context; conflict/bound also include slot, candidate binding and conflicts. Conflict remains visible while awaiting a new candidate. When embedding capture in a UI, prevent UI keyboard shortcuts from also activating a focused button with the newly captured key; `input.py` clears focus during capture/completion. Handle any cancel-button hit before evaluating mouse capture, or use Escape. No menu/widget design is mandatory.

## Preferences and open format

The facade's `save()`/`load()` use slot `native_input` under the configured `save_directory` + `/preferences`, resolved through `forge.storage_path`. Override `directory` and `slot` for different players/profiles:

```python
controls = InputManager(defaults, directory='preferences/controls', slot='player1')
controls.save()
```

SaveManager provides version 1 envelopes, checksums, atomic replacement, verified backup recovery and managed reload/authoring deferral. Loading a backup-only slot works. A malformed profile raises without changing the current manager; your UI can report it or explicitly restore defaults. Loading missing preferences returns False. Saving requires an explicit call; pending capture or unsaved edits are not automatically persisted. Ordinary initial-start saves keep SaveManager's existing behavior; rejected reload/authoring candidates do not commit their deferred writes. This does not roll back arbitrary game I/O or mutations to a manager deliberately shared outside scene ownership.

The native manager performs no disk I/O. `export_profile()`/`import_profile(profile)` interchange plain JSON documented by [input-profile.schema.json](../../schemas/input-profile.schema.json):

```json
{
  "format": "forge.input.bindings/1",
  "deadzone": 0.15,
  "contexts": {
    "default": {
      "jump": [{"input": "key:J", "scale": 1, "deadzone": 0.15,
                "direction": 0, "modifiers": [], "exact_modifiers": false}]
    }
  }
}
```

Import merges saved action overrides onto game defaults so new actions added in a game update survive. Explicit `[]` remains unbound. Unknown contexts/actions/top-level metadata and binding metadata survive; effective binding fields normalize to canonical forms. Defaults themselves never change on import. Unknown format identifiers and invalid definitions are rejected before mutation. Derivative state, repeat clocks, devices, capture and context stack are not serialized. Import returns to the default context. Games can use another storage layer or transform this public format before import; engine/project API versions remain 1.

## Native use and replacement

`engine/include/forge/input_manager.hpp` supplies independent `forge::InputManager` and typed `InputFrame`, with no Runtime, Python or GPU ownership. The `forge_input` CMake library contains `input_manager.cpp` and shared `input_keys.cpp`; the Python adapter is `input_api.cpp`. These implementations and the facade are adaptable outside the Core list. `python_api.cpp` only registers the additive public class/capability; no new Core restriction is introduced.

```cpp
#include <forge/input_manager.hpp>
forge::InputManager controls({{"jump", "key:SPACE"}});
forge::InputFrame frame;
frame.keys.set(forge::inputKey("SPACE"));
controls.update(frame, 1.0 / 60.0);
if (controls.pressed("jump")) { /* dispatch your action */ }
```

Typed producers must populate `modifiers` consistently with their held/pressed keys (the JSON parser does this automatically). Inputs validate finite axes/scroll/time before mutation. Bindings compile once; evaluation uses bitsets, fixed gamepad arrays and compiled controls. `update()` reads the runtime JSON frame directly in C++, without copying a Python snapshot or passing binding lists through JSON each frame. Configuration/profile edits use JSON off the evaluation path; the existing window frame itself remains JSON. Multiple independent managers are possible; Forge still has one active Python runtime.

Replace the Python facade, native implementation or typed frame producer with your own system; there is no mandatory singleton, menu, file layout, editor or network service. Keep the public contracts needed by your content. Your UI can consume any object with the existing `value/down/pressed/released` interface, including `ui.Canvas(actions=controls)` for its optional `ui_next/ui_previous/ui_accept` actions. Raw Canvas keyboard shortcuts remain available.

## Raw input and replay

`forge.key_down/key_pressed`, mouse functions and `input_snapshot()` remain compatible. Native keyboard actions accept held keys or one-frame explicit `pressed` pulses. Snapshots include keys/pressed/released, mouse buttons/position/delta/scroll, gamepads, key/mouse_button/text/scroll events, dt/time/frame. Physical modifier events now coexist with the legacy generic aliases; event consumers should choose one representation to avoid handling a modifier twice. `forge.input_events()`/`gamepads()` expose corresponding arrays. Current input polling does not guarantee recording a complete press-and-release occurring between OS polls; raw snapshots are not a high-frequency event recorder.

The existing Python `ActionMap` retains its prior behavior and saved `input_actions` format. It supports key/mouse/fixed-pad/axis actions and contexts; new capture/chords/wildcards/repeat/profile APIs belong to `InputManager`. Existing games do not migrate silently.

```python
from input_actions import InputRecorder, InputReplay
replay = InputReplay('replay/test.json')  # Register before automatic consumers.
controls = InputManager({'jump': 'key:SPACE'})
recorder = InputRecorder(max_frames=360000)
# recorder.save('replay/session.json'); close each object on scene teardown.
```

Recorder atomically stores full frames with format `forge.input/1`, separate from binding profile format `forge.input.bindings/1`. Replay injects input and dt through `forge.inject_input`; consumers registered later see it. `automatic=False` supplies explicit order; `finished` marks completion. Only input/dt are replayed, not world/RNG/network/asynchronous results.

## Limits and verification

Native managers allow 64 contexts, 1024 actions per context, 4096 total actions, 16 bindings per action, 32 context stack entries, and nonempty names up to 128 UTF-8 bytes. Snapshot limits are 512 key names, 8 mouse-button entries, 16 unique gamepads with 15 button entries/6 finite axes each. Unknown extension key names are ignored in snapshots; unknown binding keys are errors. dt is finite 0..1 seconds. These bounds concern manager operations, not a whole-process memory cap. Native action evaluation is on the caller's thread; managers are not synchronized for concurrent mutation.

```sh
ctest --test-dir build -C Release --output-on-failure -R '^input_native$'
python tests/features.py build/bin/forge
python tests/features_graphics.py build/bin/forge FeatureGraphicsTests.test_native_controls_ui_rebinding_and_preference_pixels
python tools/benchmark_input.py build/bin/forge --actions 256 --frames 2000
```

Native tests cover compiled actions, numeric validation, chords/axes, defaults/profile copies/merging, capture/conflicts/timeout and physical key snapshots. Embedded-Python tests check managed preferences, backup-only loading, invalid-state preservation, paused/replay order and rejected reload. The shared GPU regression exercises the shipped controls screen and pixels. `benchmark_input.py` measures native/Python evaluation in the same headless runtime without timing gates or an FPS promise. Target-platform compilation/device tests are required before claiming hardware behavior.
