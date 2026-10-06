# Input, Actions, and Replay

[Forge wiki](../../GUIDE.md) · **Input, Actions, and Replay**

Raw input is available through forge. Action maps add rebinding and contexts; replay supplies recorded input and frame time.

- [Actions and recording](#actions-and-recording)

## Actions and recording

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
