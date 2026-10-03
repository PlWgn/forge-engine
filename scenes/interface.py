"""Complete example using editable modules; the core knows nothing about novels."""
import forge
import ui
import audio
from saves import SaveManager
from dialogue import Dialogue
from menus import MenuController


def on_start():
    global canvas, dialogue, saves, menus
    root = ui.Column(padding=40, gap=18)
    root.add(ui.Label('FORGE / UI', size=20, color=ui.ACCENT))
    root.add(ui.Label(forge.message('example.ui.title'), size=60))
    root.add(ui.Label(forge.message('example.ui.subtitle'), size=24, color=ui.MUTED))
    dialogue = root.add(Dialogue([
        dict(speaker=forge.message('example.ui.developer'), text=forge.message('example.ui.page1')),
        dict(speaker='Forge', text=forge.message('example.ui.page2')),
        dict(speaker='Forge', text=forge.message('example.ui.page3')),
    ], flex=1))
    root.add(ui.Row(ui.Button(forge.message('example.ui.menu'), lambda: menus.show_pause()),
                    ui.Button(forge.message('example.ui.scene2d'), lambda: forge.change_scene('welcome.json')),
                    ui.Button(forge.message('example.ui.scene3d'), lambda: forge.change_scene('world3d.py'))))
    root.add(ui.Label(forge.message('engine.about.attribution'), size=18, color=ui.MUTED))
    canvas = ui.Canvas(root)
    saves = SaveManager(version=1, validate=lambda data: isinstance(data, dict) and 'dialogue' in data)
    saves.autosave(capture, interval=30)
    menus = MenuController(canvas, saves=saves, capture=capture, restore=restore, journal=dialogue.journal)
    audio.ui.play('notify.wav', volume=.15)
    forge.log('UI_READY')


def capture(): return dict(dialogue=dialogue.capture())
def restore(data): dialogue.restore(data['dialogue'])


def on_destroy():
    menus.close()
    saves.close()
    canvas.close()
