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

# Additional authoring API; SpriteSheet, SpriteAnimation and Tween remain compatible.
from bisect import bisect_right
from copy import deepcopy


def _number(value, label, low=0, high=1e6):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f'{label} must be finite in {low}..{high}')
    return float(value)


def _value(value):
    if isinstance(value, (list, tuple)):
        return tuple(_value(item) for item in value)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return _number(value, 'keyframe value', -1e30, 1e30)
    if isinstance(value, (str, bool)):
        return value
    raise ValueError('Keyframes support numbers, numeric vectors, strings and bools')


def _mix(a, b, t):
    if isinstance(a, tuple) and isinstance(b, tuple) and len(a) == len(b):
        return tuple(_mix(x, y, t) for x, y in zip(a, b))
    if type(a) is float and type(b) is float:
        return a + (b-a)*t
    raise ValueError('Interpolated keyframes need matching numeric shapes; use step for strings/bools')


class AnimationClip:
    """Reusable property tracks, in seconds. No scene or entity state is retained."""
    def __init__(self, tracks, *, duration=None, events=(), name='clip'):
        if not isinstance(tracks, dict) or not tracks or len(tracks) > 128:
            raise ValueError('tracks must contain 1..128 properties')
        self.name, self.tracks = str(name), {}
        end = 0
        for prop, definition in tracks.items():
            if not isinstance(prop, str) or prop.startswith('_'):
                raise ValueError('Invalid animation property')
            mode = definition.get('interpolation', 'linear') if isinstance(definition, dict) else 'linear'
            keys = definition.get('keys', ()) if isinstance(definition, dict) else definition
            if mode not in ('linear', 'smooth', 'step') or not 1 <= len(keys) <= 8192:
                raise ValueError('Invalid interpolation or key count')
            points = tuple((_number(time, 'keyframe time'), _value(value)) for time, value in keys)
            if any(a[0] >= b[0] for a, b in zip(points, points[1:])):
                raise ValueError('Keyframe times must strictly increase')
            if mode != 'step':
                for a, b in zip(points, points[1:]): _mix(a[1], b[1], .5)
            self.tracks[prop] = (points, tuple(p[0] for p in points), mode)
            end = max(end, points[-1][0])
        self.duration = _number(end if duration is None else duration, 'clip duration', 1e-6)
        if end > self.duration: raise ValueError('Keyframe outside duration')
        self.events = tuple(deepcopy(tuple(events)))
        if len(self.events) > 1024: raise ValueError('At most 1024 markers')
        for event in self.events:
            if not isinstance(event, dict) or not event.get('name'): raise ValueError('Marker needs name/time')
            _number(event['time'], 'event time', 0, self.duration)

    def sample(self, time):
        time = _number(time, 'sample time')
        values = {}
        for prop, (keys, times, mode) in self.tracks.items():
            index = bisect_right(times, time)
            if index == 0: value = keys[0][1]
            elif index == len(keys) or mode == 'step': value = keys[index-1][1]
            else:
                a, b = keys[index-1], keys[index]
                t = (time-a[0])/(b[0]-a[0])
                if mode == 'smooth': t = t*t*(3-2*t)
                value = _mix(a[1], b[1], t)
            values[prop] = value
        return values

    def play(self, entity, *, loop=True, speed=1):
        return Timeline(entity, self, loop=loop, speed=speed)


class Timeline:
    """Explicit update/play/pause/seek; marker queues are drained with events()."""
    def __init__(self, entity, clip, *, loop=True, speed=1):
        self.entity, self.clip, self.loop = entity, clip, bool(loop)
        self.speed = _number(speed, 'playback speed', -1e6, 1e6)
        self.time, self.playing, self.finished, self._events = 0., True, False, []
        self.seek(0)

    def seek(self, time):
        time = _number(time, 'seek time')
        position = time % self.clip.duration if self.loop else min(time, self.clip.duration)
        values = self.clip.sample(position)
        # Public setters perform native validation. Restore successful setters on failure.
        before = {prop: getattr(self.entity, prop) for prop in values}
        applied = []
        try:
            for prop, value in values.items(): setattr(self.entity, prop, value); applied.append(prop)
        except Exception:
            for prop in reversed(applied): setattr(self.entity, prop, before[prop])
            raise
        self.time, self.finished = time, not self.loop and time >= self.clip.duration
        self._events.clear()
        return self

    def pause(self, paused=True): self.playing = not paused; return self
    def play(self): return self.pause(False)
    def events(self): events, self._events = self._events, []; return events

    def update(self, dt):
        dt = _number(dt, 'dt', 0, 10)
        if not self.playing: return not self.finished
        old, next_time = self.time, self.time + dt*self.speed
        if not self.loop: next_time = max(0., next_time)
        _number(next_time, 'clock', -1e6 if self.loop else 0, 1e6)
        low, high = min(old, next_time), max(old, next_time)
        emitted = []
        for marker in self.clip.events:
            t = marker['time']
            first = math.ceil((low-t)/self.clip.duration) if self.loop else 0
            last = math.floor((high-t)/self.clip.duration) if self.loop else 0
            if last-first > 1024: raise ValueError('Marker budget exceeded; use seek')
            for cycle in range(first, last+1):
                stamp = t + cycle*self.clip.duration if self.loop else t
                if (old < stamp <= next_time) if next_time >= old else (next_time <= stamp < old):
                    if len(emitted)+len(self._events) >= 4096: raise ValueError('Animation event queue full')
                    emitted.append((stamp, dict(deepcopy(marker), type='marker', clip=self.clip.name)))
        emitted.sort(key=lambda item: item[0], reverse=next_time < old)
        finished = not self.loop and (next_time >= self.clip.duration if self.speed >= 0 else next_time <= 0)
        events = self._events + [event for _, event in emitted]
        if finished and not self.finished: events.append(dict(type='finished', clip=self.clip.name))
        if len(events) > 4096: raise ValueError('Animation event queue full')
        self.seek(next_time % self.clip.duration if self.loop else next_time)
        self.time = next_time
        self.finished, self._events = finished, events
        return not finished


class Animator:
    """Native layered skeletal/morph playback. Automatic frame updates by default."""
    def __init__(self, entity, clip=None, *, layers=None, auto_update=True, **options):
        import forge
        self.entity = entity
        if clip is not None: layers = [dict(clip=clip, **options)]
        if layers is not None:
            forge.configure_animator(entity, dict(layers=deepcopy(layers), auto_update=auto_update))

    def configure(self, *, layers, playing=True, auto_update=True):
        import forge
        forge.configure_animator(self.entity, dict(layers=layers, playing=playing, auto_update=auto_update))
        return self
    def crossfade(self, clip, seconds=.25, *, speed=1, loop=True):
        import forge
        forge.crossfade_animation(self.entity, clip, seconds, speed, loop); return self
    def pause(self, paused=True):
        import forge
        forge.pause_animation(self.entity, paused); return self
    def play(self): return self.pause(False)
    def seek(self, time):
        import forge
        forge.seek_animation(self.entity, time); return self
    def update(self, dt):
        import forge
        forge.update_animation(self.entity, dt)
    def events(self):
        import forge
        return forge.animation_events(self.entity)
    @property
    def info(self):
        import forge
        return forge.animator_info(self.entity)
    @property
    def pose(self):
        import forge
        return forge.entity_animation_pose(self.entity)
    def morph(self, **weights):
        import forge
        forge.set_morph_weights(self.entity, weights); return self


class AnimationStateMachine:
    """Reusable declarative graph. Call update once per frame; first matching edge wins.

    Conditions: parameter equals a value; {'gt': number}, {'lt': number}, or
    {'trigger': True}. Triggers are consumed only after a successful transition.
    Exit time is seconds in the current state, independent of clip speed.
    """
    def __init__(self, animator, states, transitions=(), *, initial=None):
        import forge
        self.animator = animator
        self.states, self.transitions = deepcopy(states), deepcopy(tuple(transitions))
        if not isinstance(states, dict) or not states: raise ValueError('At least one state required')
        for name, state in self.states.items():
            if not isinstance(name, str) or not name or not isinstance(state, dict): raise ValueError('Invalid state')
            layer = state.get('layer', dict(clip=state.get('clip', ''), speed=state.get('speed', 1), loop=state.get('loop', True)))
            forge.validate_animator(animator.entity, dict(layers=[layer]))
            _number(state.get('fade', .25), 'fade', 0, 60)
        for edge in self.transitions:
            if edge.get('from') not in (*states, '*') or edge.get('to') not in states or not isinstance(edge.get('when', {}), dict):
                raise ValueError('Invalid transition')
            _number(edge.get('exit_time', 0), 'exit time')
            for condition in edge.get('when', {}).values():
                if isinstance(condition, dict) and (len(condition) != 1 or next(iter(condition)) not in ('gt', 'lt', 'trigger')):
                    raise ValueError('Unknown transition condition')
                if isinstance(condition, dict):
                    key, expected = next(iter(condition.items()))
                    if key == 'trigger' and type(expected) is not bool: raise ValueError('trigger condition must be bool')
                    if key in ('gt', 'lt'): _number(expected, 'condition', -1e30, 1e30)
        self.parameters, self.triggers, self._events, self.elapsed = {}, set(), [], 0.
        self.state = None
        self.enter(initial if initial is not None else next(iter(states)))

    def set(self, name, value): self.parameters[name] = value; return self
    def trigger(self, name): self.triggers.add(name); return self
    def enter(self, name):
        if name not in self.states: raise ValueError('Unknown animation state')
        if len(self._events) >= 4096: raise ValueError('State event queue full; consume events')
        settings = self.states[name]
        if 'layer' in settings:
            import forge
            forge.transition_animator(self.animator.entity, dict(layers=[settings['layer']]), settings.get('fade', .25))
        else: self.animator.crossfade(settings['clip'], settings.get('fade', .25), loop=settings.get('loop', True), speed=settings.get('speed', 1))
        self._events.append(dict(type='state', previous=self.state, state=name))
        self.state, self.elapsed = name, 0.
        return self

    def update(self, dt):
        dt = _number(dt, 'dt', 0, 10)
        self.elapsed += dt
        for edge in self.transitions:
            if edge['from'] not in (self.state, '*') or self.elapsed < edge.get('exit_time', 0): continue
            accepted = True
            used = []
            for name, condition in edge.get('when', {}).items():
                value = self.parameters.get(name)
                if isinstance(condition, dict):
                    key, expected = next(iter(condition.items()))
                    if key == 'trigger': ok = name in self.triggers if expected else name not in self.triggers; used.append(name)
                    elif key == 'gt': ok = isinstance(value, (int,float)) and value > expected
                    else: ok = isinstance(value, (int,float)) and value < expected
                else: ok = value == condition
                accepted = accepted and ok
            if accepted:
                self.enter(edge['to'])
                self.triggers.difference_update(used)
                return True
        return False

    def events(self): events, self._events = self._events, []; return events
    def describe(self): return deepcopy(dict(states=self.states, transitions=self.transitions, initial=self.state))
