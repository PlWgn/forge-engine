"""Sound handles and named buses. All native controls also work without this module."""
import forge


class Sound:
    def __init__(self, id): self.id = id
    @property
    def playing(self): return forge.sound_playing(self.id)
    @property
    def volume(self): return forge.sound_volume(self.id)
    @volume.setter
    def volume(self, value): forge.set_sound_volume(self.id, value)
    def stop(self, fade=0): forge.stop_sound(self.id, fade)
    def pause(self): forge.pause_sound(self.id, True)
    def resume(self): forge.pause_sound(self.id, False)
    def fade(self, volume, seconds, stop_after=False): forge.fade_sound(self.id, volume, seconds, stop_after)
    @property
    def info(self): return forge.sound_info(self.id)
    @property
    def cursor(self): return forge.sound_position(self.id)
    def configure(self, **options):
        forge.set_sound_options(self.id, options)
        return self
    @property
    def pan(self): return self.info.get('pan', 0)
    @pan.setter
    def pan(self, value): self.configure(pan=value)
    @property
    def position(self): return self.info.get('position', (0, 0, 0))
    @position.setter
    def position(self, value): self.configure(position=list(value), spatial=True)


class Channel:
    def __init__(self, name):
        if not name or name == 'master': raise ValueError('Channel needs a nonempty name other than master')
        self.name = name
    @property
    def volume(self): return forge.channel_volume(self.name)
    @volume.setter
    def volume(self, value): forge.set_channel_volume(self.name, value)
    def play(self, file, *, loop=False, volume=1, fade=0, **options):
        return Sound(forge.play_sound(file, loop, volume, self.name, fade, options))
    def stop(self, fade=0): forge.stop_channel(self.name, fade)
    def crossfade(self, file, seconds=1, *, loop=True, volume=1, **options):
        # Prepare replacement first: a missing/invalid track leaves the old music playing.
        previous = forge.channel_sounds(self.name)
        sound = self.play(file, loop=loop, volume=volume, fade=seconds, **options)
        for id in previous: forge.stop_sound(id, seconds)
        return sound


def master_volume(value=None):
    if value is not None: forge.set_channel_volume('master', value)
    return forge.channel_volume('master')


music, sfx, voice, ui = (Channel(name) for name in ('music', 'sfx', 'voice', 'ui'))
