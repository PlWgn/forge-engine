"""Sprite sheets and interpolation. All updates can be driven by a Scheduler."""
import math

class SpriteSheet:
    def __init__(self, texture, columns, rows, *, frames=None):
        if type(columns) is not int or type(rows) is not int or columns < 1 or rows < 1:
            raise ValueError('Sprite sheet dimensions must be positive integers')
        self.texture, self.columns, self.rows = texture, columns, rows
        self.frames = tuple(range(columns*rows)) if frames is None else tuple(frames)
        if not self.frames or any(type(i) is not int or not 0 <= i < columns*rows for i in self.frames):
            raise ValueError('Invalid sprite sheet frames')
    def region(self, index):
        frame = self.frames[index]
        return frame % self.columns / self.columns, frame // self.columns / self.rows, 1/self.columns, 1/self.rows
    def apply(self, entity, index=0):
        entity.texture, entity.uv = self.texture, self.region(index)
    def animate(self, entity, fps=12, loop=True):
        return SpriteAnimation(entity, self, fps, loop)

class SpriteAnimation:
    def __init__(self, entity, frames, fps=12, loop=True):
        if not frames or not math.isfinite(fps) or fps <= 0: raise ValueError('frames and positive fps required')
        self.entity, self.frames, self.fps, self.loop, self.elapsed = entity, frames, fps, loop, 0
    def update(self, dt):
        if not math.isfinite(dt) or dt < 0: raise ValueError('dt must be finite and nonnegative')
        self.elapsed += dt
        index = int(self.elapsed * self.fps)
        frames = self.frames.frames if isinstance(self.frames, SpriteSheet) else self.frames
        index = index % len(frames) if self.loop else min(index, len(frames)-1)
        if isinstance(self.frames, SpriteSheet): self.frames.apply(self.entity, index)
        else: self.entity.texture = frames[index]
        return self.loop or self.elapsed*self.fps < len(frames)

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
