"""Optional pause, settings, journal and save/load screens built from generic UI."""
import forge
from settings import AudioSettings, WindowSettings
from saves import SaveError
from ui import Column, Row, Label, Button, Slider, ScrollView, PANEL, MUTED


class MenuController:
    def __init__(self, canvas, *, saves=None, capture=None, restore=None, journal=None, slots=5, settings=None):
        self.canvas, self.saves, self.capture, self.restore, self.journal = canvas, saves, capture, restore, journal
        self.settings = settings if settings is not None else AudioSettings()
        self._owns_settings = settings is None
        self.window = WindowSettings()
        self.slots, self.opened, self._previous_pause = slots, False, False
        self.panel = canvas.overlay(Column(width=760, height=620, padding=28, background=PANEL, visible=False))
        self._revision = forge.localization_revision()
        self._view = None
        self._listener = forge.on_frame(self.update)

    def update(self, dt):
        revision = forge.localization_revision()
        if revision != self._revision:
            self._revision = revision
            if self.opened and self._view is not None: self._view()
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
        self._view = self.show_pause
        self._screen(forge.message('engine.menu.pause'))
        self.panel.add(Button(forge.message('engine.menu.resume'), self.close_menu))
        self.panel.add(Button(forge.message('engine.menu.settings'), self.show_settings))
        if self.saves is not None and self.capture is not None:
            self.panel.add(Button(forge.message('engine.menu.save'), lambda: self.show_slots(save=True)))
        if self.saves is not None and self.restore is not None:
            self.panel.add(Button(forge.message('engine.menu.load'), self.show_slots))
        if self.journal is not None:
            self.panel.add(Button(forge.message('engine.menu.journal'), self.show_journal))
        self.panel.add(Button(forge.message('engine.menu.about'), self.show_about))

    def show_settings(self):
        self._view = self.show_settings
        self._screen(forge.message('engine.menu.settings'))
        content = self.panel.add(ScrollView(flex=1, gap=8))
        content.add(Label(forge.message('engine.settings.display'), size=20))
        for width, height in ((1280,720),(1600,900),(1920,1080)):
            content.add(Button(f'{width} × {height}', lambda w=width,h=height: self.window.set(width=w,height=h), size=20))
        content.add(Button(forge.message('engine.settings.fullscreen'), lambda: self.window.set(fullscreen=not self.window.values['fullscreen']), size=20))
        content.add(Button('VSync', lambda: self.window.set(vsync=not self.window.values['vsync']), size=20))
        content.add(Label(forge.message('engine.settings.language'), size=20))
        for language in forge.available_languages():
            content.add(Button(language['name'], lambda code=language['code']: forge.set_language(code), size=20))
        for name, title in [('master', forge.message('engine.settings.master')), ('music', forge.message('engine.settings.music')), ('sfx', forge.message('engine.settings.sfx')), ('voice', forge.message('engine.settings.voice')), ('ui', forge.message('engine.settings.ui'))]:
            content.add(Label(title, size=20))
            content.add(Slider(forge.channel_volume(name), lambda value, key=name: self.settings.set(key, value), width=640))
        self.panel.add(Button(forge.message('engine.menu.back'), self.show_pause))

    def show_slots(self, save=False):
        self._view = lambda: self.show_slots(save)
        self._screen(forge.message('engine.slots.save') if save else forge.message('engine.slots.load'))
        if self.saves is None: return
        for slot in ['auto']+[str(n) for n in range(1, self.slots+1)]:
            if save and slot == 'auto': continue
            info = self.saves.info(slot)
            title = forge.tr('engine.slots.auto') if slot == 'auto' else forge.tr('engine.slots.title', slot=slot)
            detail = forge.tr('engine.slots.'+info['status']) if info['status'] in ('empty', 'corrupt', 'recoverable') else info.get('title', '')
            label = f"{title} — {detail} {info.get('saved_at', '')[:19]}"
            button = self.panel.add(Button(label, lambda key=slot: self._slot(key, save), size=18))
            button.enabled = save or info['status'] != 'empty'
        self.panel.add(Button(forge.message('engine.menu.back'), self.show_pause))

    def _slot(self, slot, save):
        try:
            if save:
                self.saves.write(slot, self.capture(), title=forge.tr('engine.slots.title', slot=slot))
                self.show_slots(save=True)
                self.status.value = forge.message('engine.slots.saved')
            else:
                data = self.saves.read(slot)
                self.restore(data)
                self.close_menu()
        except (SaveError, ValueError, TypeError, KeyError) as error:
            self.status.value = str(error)
            forge.log(error, 'WARN')

    def show_journal(self):
        self._view = self.show_journal
        self._screen(forge.message('engine.menu.journal'))
        if self.journal is not None: self.panel.add(self.journal.view(height=420))
        self.panel.add(Button(forge.message('engine.menu.back'), self.show_pause))

    def show_about(self):
        self._view = self.show_about
        self._screen(forge.message('engine.menu.about'))
        self.panel.add(Label(forge.message('engine.about.attribution'), size=28))
        self.panel.add(Label(forge.message('engine.about.description'), size=22))
        self.panel.add(Button(forge.message('engine.menu.back'), self.show_pause))

    def close_menu(self):
        self.panel.visible, self.opened = False, False
        self.canvas.modal(None)
        forge.set_paused(self._previous_pause)

    def close(self):
        forge.remove_listener(self._listener)
        if self._owns_settings: self.settings.close()
        if self.opened: self.close_menu()
