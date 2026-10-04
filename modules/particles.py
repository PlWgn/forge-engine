"""Native scene-owned particles. No Python callbacks or Entities per particle."""
import forge


class Emitter:
    def __init__(self, settings=None, **options):
        values = dict(settings or {})
        values.update(options)
        self.id = forge.particle_emitter(values)
        self._closed = False

    @classmethod
    def existing(cls, id):
        forge.particle_info(id)
        emitter = cls.__new__(cls)
        emitter.id, emitter._closed = id, False
        return emitter

    @property
    def info(self):
        return forge.particle_info(self.id)

    def burst(self, count):
        return forge.particle_burst(self.id, count)

    def start(self):
        forge.particle_enabled(self.id, True)

    def stop(self, *, clear=False):
        """Stop emission; existing particles finish naturally. clear removes emitter."""
        if clear:
            self.close()
        else:
            forge.particle_enabled(self.id, False)

    def move(self, position):
        forge.particle_position(self.id, position)

    def snapshot(self, limit=32):
        return forge.particle_snapshot(self.id, limit)

    def close(self):
        if not self._closed:
            try:
                forge.particle_remove(self.id)
            except RuntimeError:
                # A transition may already have released the scene-owned emitter.
                pass
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def stats():
    return forge.particle_stats()
