"""Reusable sprite frames and transform interpolation, independent of the core."""
class SpriteAnimation:
    def __init__(self, entity, frames, fps=12, loop=True):
        if not frames or fps <= 0: raise ValueError('frames and positive fps required')
        self.entity, self.frames, self.fps, self.loop, self.elapsed = entity, frames, fps, loop, 0
    def update(self, dt):
        self.elapsed += dt
        index = int(self.elapsed * self.fps)
        self.entity.texture = self.frames[index % len(self.frames) if self.loop else min(index, len(self.frames)-1)]
        return self.loop or index < len(self.frames)

class Tween:
    def __init__(self, entity, property, target, duration):
        if duration <= 0: raise ValueError('duration must be positive')
        self.entity, self.property, self.start, self.target = entity, property, getattr(entity, property), target
        self.duration, self.elapsed = duration, 0
    def update(self, dt):
        self.elapsed += dt
        t = min(self.elapsed / self.duration, 1)
        t = t*t*(3-2*t)
        value = tuple(a + (b-a)*t for a,b in zip(self.start,self.target))
        setattr(self.entity, self.property, value)
        return self.elapsed < self.duration
