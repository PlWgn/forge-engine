# UI, Dialogue, and Menus

[Forge wiki](../../GUIDE.md) · **UI, Dialogue, and Menus**

Editable Python modules provide optional widgets and menus over either 2D or 3D. They do not impose a game structure.

- [Widgets, layout, and text](#widgets-layout-and-text)
- [Pause, dialogue, history, and menus](#pause-dialogue-history-and-menus)
- [Themes and screen transitions](#themes-and-screen-transitions)
- [Window preferences](#window-preferences)

## Widgets, layout, and text

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

scale='fit' preserves the designed Canvas proportions and centers it with margins. stretch scales both axes independently. none uses current logical window dimensions without scaling. Window size and Retina/HiDPI density are handled separately. Widgets and text are clipped to container rectangles rather than spilling over neighboring panels. Glyphs are rasterized for the actual font size, scale, and screen density instead of enlarging a fixed 40-pixel bitmap. Raster height is limited to 1..1024 pixels; the product is computed in double and bounded before conversion to int. Larger text scales the maximum raster. The cache distinguishes size and codepoint; atlas/cache accounting follows [GPU budgets](assets.md#loading-and-cache-budgets).

A button activates when released over the same button; releasing outside cancels the click. Tab / Shift+Tab changes focus, Enter / Space activates a button, and Left/Right adjusts the selected Slider. Disabled elements are skipped. The wheel scrolls the ScrollView under the pointer; PageUp/PageDown scroll by its height. canvas.overlay(widget) creates a separate centered panel; canvas.modal(widget) restricts input to it, and canvas.modal(None) removes that restriction. Panel visibility is controlled separately.

Legacy ui.text, ui.panel, and Button((x,y,width,height), callback).update() remain compatible. The old Button is a manually updated input detector; a visual button uses a string label in a Canvas tree. A low-level kind='text' Entity uses explicit \n; Label provides automatic wrapping.

## Pause, dialogue, history, and menus

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

## Themes and screen transitions

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

## Window preferences

settings.WindowSettings stores resolution/fullscreen/vsync; MenuController exposes them. forge.set_window({...}) applies them; window_settings() reports current state. Fullscreen uses the primary monitor mode. Successful user preferences do not modify engine.json. Configured window dimensions are integers 1..16384 and fullscreen/vsync are bool. set_window allows width 320..16384 and height 240..16384, validating the entire request before changing the window. Headless skips physical window updates.
