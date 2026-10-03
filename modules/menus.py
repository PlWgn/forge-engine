"""Optional pause, settings, journal and save/load screens built from generic UI."""
import forge
from settings import AudioSettings
from saves import SaveError
from ui import Column, Row, Label, Button, Slider, ScrollView, PANEL, MUTED


class MenuController:
    def __init__(self, canvas, *, saves=None, capture=None, restore=None, journal=None, slots=5, settings=None):
        self.canvas, self.saves, self.capture, self.restore, self.journal = canvas, saves, capture, restore, journal
        self.settings = settings if settings is not None else AudioSettings()
        self._owns_settings = settings is None
        self.slots, self.opened, self._previous_pause = slots, False, False
        self.panel = canvas.overlay(Column(width=760, height=620, padding=28, background=PANEL, visible=False))
        self._listener = forge.on_frame(self.update)

    def update(self, dt):
        if forge.key_pressed('ESCAPE'):
            if self.opened: self.close_menu()
            else: self.show_pause()

    def _screen(self, title):
        if not self.opened:
            self._previous_pause = forge.is_paused()
        self.opened = True
        forge.set_paused(True)
        for child in list(self.panel.children): self.panel.remove(child)
        self.panel.add(Label(title, size=36))
        self.status = self.panel.add(Label('', size=18, color=MUTED))
        self.canvas.modal(self.panel)

    def show_pause(self):
        self._screen('Пауза')
        self.panel.add(Button('Продолжить', self.close_menu))
        self.panel.add(Button('Настройки', self.show_settings))
        if self.saves is not None and self.capture is not None:
            self.panel.add(Button('Сохранить игру', lambda: self.show_slots(save=True)))
        if self.saves is not None and self.restore is not None:
            self.panel.add(Button('Загрузить игру', self.show_slots))
        if self.journal is not None:
            self.panel.add(Button('Журнал', self.show_journal))
        self.panel.add(Button('О движке', self.show_about))

    def show_settings(self):
        self._screen('Настройки звука')
        content = self.panel.add(ScrollView(flex=1, gap=8))
        for name, title in [('master', 'Общая громкость'), ('music', 'Музыка'), ('sfx', 'Эффекты'), ('voice', 'Речь'), ('ui', 'Интерфейс')]:
            content.add(Label(title, size=20))
            content.add(Slider(forge.channel_volume(name), lambda value, key=name: self.settings.set(key, value), width=640))
        self.panel.add(Button('Назад', self.show_pause))

    def show_slots(self, save=False):
        self._screen('Сохранение игры' if save else 'Загрузка игры')
        if self.saves is None: return
        for slot in ['auto']+[str(n) for n in range(1, self.slots+1)]:
            if save and slot == 'auto': continue
            info = self.saves.info(slot)
            label = f'{slot} — {info.get("title", "Сохранение")} {info.get("saved_at", "")[:19]}'
            button = self.panel.add(Button(label, lambda key=slot: self._slot(key, save), size=18))
            button.enabled = save or info['status'] != 'empty'
        self.panel.add(Button('Назад', self.show_pause))

    def _slot(self, slot, save):
        try:
            if save:
                self.saves.write(slot, self.capture(), title=f'Слот {slot}')
                self.show_slots(save=True)
                self.status.value = 'Сохранено'
            else:
                data = self.saves.read(slot)
                self.restore(data)
                self.close_menu()
        except (SaveError, ValueError, TypeError, KeyError) as error:
            self.status.value = str(error)
            forge.log(error, 'WARN')

    def show_journal(self):
        self._screen('Журнал')
        if self.journal is not None: self.panel.add(self.journal.view(height=420))
        self.panel.add(Button('Назад', self.show_pause))

    def show_about(self):
        self._screen('О движке')
        self.panel.add(Label('Используется движок Forge', size=28))
        self.panel.add(Label('C++ · Python · 2D / 3D\nНастраиваемые модули интерфейса, звука и сохранений.', size=22))
        self.panel.add(Button('Назад', self.show_pause))

    def close_menu(self):
        self.panel.visible, self.opened = False, False
        self.canvas.modal(None)
        forge.set_paused(self._previous_pause)

    def close(self):
        forge.remove_listener(self._listener)
        if self._owns_settings: self.settings.close()
        if self.opened: self.close_menu()
