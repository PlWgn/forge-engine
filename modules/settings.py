"""Persistent audio preferences, separate from project/engine configuration."""
import math
import forge
from saves import SaveManager, SaveError


class AudioSettings:
    channels = ('master', 'music', 'sfx', 'voice', 'ui')

    def __init__(self, *, delay=.25):
        if not math.isfinite(delay) or delay < 0: raise ValueError('Settings delay must be nonnegative')
        self.store = SaveManager(1, directory=forge.settings().get('save_directory','saves')+'/preferences', validate=self._valid)
        defaults = {name: forge.channel_volume(name) for name in self.channels}
        try: self.values = self.store.read('audio', default=defaults)
        except SaveError as error:
            forge.log(f'Audio settings: {error}; using defaults', 'WARN')
            self.values = defaults
        for name, value in self.values.items(): forge.set_channel_volume(name, value)
        self.delay, self.elapsed, self.dirty = delay, 0., False
        self._listener = forge.on_frame(self.update)

    @classmethod
    def _valid(cls, value):
        return isinstance(value, dict) and set(value)==set(cls.channels) and all(type(v) in (float,int) and math.isfinite(v) and 0<=v<=1 for v in value.values())

    def set(self, channel, value):
        if channel not in self.channels: raise ValueError('Unknown settings channel')
        forge.set_channel_volume(channel, value)
        self.values[channel], self.dirty, self.elapsed = value, True, 0.

    def update(self, dt):
        if self.dirty:
            self.elapsed += dt
            if self.elapsed >= self.delay: self.flush()

    def flush(self):
        if self.dirty:
            self.store.write('audio', self.values, title='Настройки звука')
            self.dirty = False

    def close(self):
        forge.remove_listener(self._listener)
        self.flush()

class WindowSettings:
    """Persistent player preferences for resolution, fullscreen and vsync."""
    def __init__(self):
        self.store = SaveManager(1, directory=forge.settings().get('save_directory','saves')+'/preferences', validate=self._valid)
        defaults = forge.window_settings()
        defaults = {k: defaults.get(k, v) for k,v in {'width':1280,'height':720,'fullscreen':False,'vsync':True}.items()}
        try: self.values = self.store.read('window', default=defaults)
        except SaveError as error:
            forge.log(f'Window settings: {error}; using defaults', 'WARN'); self.values = defaults
        forge.set_window(self.values)
    @staticmethod
    def _valid(value):
        return (isinstance(value, dict) and set(value)=={'width','height','fullscreen','vsync'}
                and type(value['width']) is int and 320 <= value['width'] <= 16384
                and type(value['height']) is int and 240 <= value['height'] <= 16384
                and type(value['fullscreen']) is bool and type(value['vsync']) is bool)
    def set(self, **options):
        values = dict(self.values, **options)
        if not self._valid(values): raise ValueError('Invalid window preferences')
        forge.set_window(values)
        self.values = values
        forge.defer_persistence(lambda data=dict(values): self.store.write('window', data, title='Window preferences'))
