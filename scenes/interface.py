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
    root.add(ui.Label('Интерфейс под вашу игру', size=60))
    root.add(ui.Label('Автоматическая раскладка, чёткий текст, сохранения и плавное управление звуком.', size=24, color=ui.MUTED))
    dialogue = root.add(Dialogue([
        dict(speaker='Разработчик', text='Этот интерфейс составлен из готовых компонентов. Размер окна может меняться: раскладка, перенос строк и масштаб адаптируются автоматически.'),
        dict(speaker='Forge', text='Модули можно изменять для новеллы, платформера или 3D игры. Меню паузы, журнал и слоты сохранений используют одну библиотеку UI.'),
        dict(speaker='Forge', text='Включите автоматическое чтение или нажмите «Далее». Tab переключает фокус; Enter активирует кнопку. Escape открывает меню.'),
    ], flex=1))
    root.add(ui.Row(ui.Button('Меню', lambda: menus.show_pause()),
                    ui.Button('2D сцена', lambda: forge.change_scene('welcome.json')),
                    ui.Button('3D сцена', lambda: forge.change_scene('world3d.py'))))
    root.add(ui.Label('Используется движок Forge', size=18, color=ui.MUTED))
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
