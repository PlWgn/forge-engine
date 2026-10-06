"""Genre-independent timers, cooperative AI work and explicit state transitions."""
import heapq
import math
import forge

def _time(value):
    if not math.isfinite(value) or value < 0: raise ValueError('Time must be finite and nonnegative')
    return value

class Scheduler:
    """Deterministic insertion order, bounded callbacks per frame, pauseable game clock."""
    def __init__(self, *, budget=128, automatic=True, run_paused=False):
        if type(budget) is not int or budget < 1: raise ValueError('budget must be a positive integer')
        self.budget, self.run_paused = budget, run_paused
        self.time, self._next, self._queue, self._tasks = 0., 0, [], {}
        self._listener = forge.on_frame(self.update) if automatic else None
    def call_later(self, delay, callback, *, repeat=0):
        _time(delay); _time(repeat)
        if not callable(callback): raise TypeError('callback must be callable')
        deadline = _time(self.time+delay)
        self._next += 1
        self._tasks[self._next] = (callback, repeat)
        heapq.heappush(self._queue, (deadline, self._next))
        return self._next
    def every(self, interval, callback):
        if interval <= 0: raise ValueError('interval must be positive')
        return self.call_later(interval, callback, repeat=interval)
    def cancel(self, task):
        self._tasks.pop(task, None)
        # Amortized compaction bounds retained tombstones without rebuilding on every cancel.
        if len(self._queue) > max(64, 2*len(self._tasks)):
            self._queue = [entry for entry in self._queue if entry[1] in self._tasks]
            heapq.heapify(self._queue)
    def update(self, dt):
        _time(dt)
        if forge.is_paused() and not self.run_paused: return
        self.time = _time(self.time+dt)
        count = 0
        while self._queue and self._queue[0][0] <= self.time and count < self.budget:
            due, id = heapq.heappop(self._queue)
            task = self._tasks.get(id)
            if task is None: continue
            callback, interval = task
            if not interval: self._tasks.pop(id, None)
            count += 1
            try: callback()
            finally:
                if interval and id in self._tasks:
                    # Missed periods are skipped instead of producing an unbounded catch-up burst.
                    try: deadline = _time(max(due+interval, self.time+interval))
                    except ValueError:
                        self._tasks.pop(id, None)
                        raise
                    heapq.heappush(self._queue, (deadline, id))
        return count
    @property
    def pending(self): return len(self._tasks)
    def close(self):
        if self._listener is not None: forge.remove_listener(self._listener)
        self._queue.clear(); self._tasks.clear(); self._listener = None

class StateMachine:
    """States are dicts or objects with optional enter, update, exit and event callbacks."""
    def __init__(self, states, initial=None):
        self.states, self.state, self._transitioning = dict(states), None, False
        if initial is not None: self.change(initial)
    def _callback(self, state, name, *args):
        if state is None: return
        value = self.states[state]
        fn = value.get(name) if isinstance(value, dict) else getattr(value, name, None)
        if fn is not None: return fn(*args)
    def change(self, state):
        if state not in self.states: raise KeyError(state)
        if self._transitioning: raise RuntimeError('Nested state transitions are not supported')
        if state == self.state: return
        self._transitioning = True
        try:
            previous = self.state
            self._callback(previous, 'exit', state)
            self.state = state
            self._callback(state, 'enter', previous)
        finally: self._transitioning = False
    def update(self, dt): return self._callback(self.state, 'update', _time(dt))
    def event(self, name, *args): return self._callback(self.state, 'event', name, *args)
    def close(self):
        self._callback(self.state, 'exit', None)
        self.state = None

class Timeline:
    """Ordered (seconds, callback) cues, seek and loop; callbacks execute once per pass."""
    def __init__(self, cues, *, duration=None, loop=False):
        self.cues = sorted((_time(t), i, fn) for i, (t, fn) in enumerate(cues))
        if any(not callable(fn) for _, _, fn in self.cues): raise TypeError('Cue must be callable')
        self.duration = _time(duration if duration is not None else (self.cues[-1][0] if self.cues else 0))
        if self.cues and self.cues[-1][0] > self.duration: raise ValueError('Cue exceeds duration')
        if loop and self.duration <= 0: raise ValueError('Loop duration must be positive')
        self.loop, self.time, self.index, self.finished = loop, 0., 0, False
    def seek(self, seconds):
        self.time = min(_time(seconds), self.duration)
        self.index = next((i for i, cue in enumerate(self.cues) if cue[0] >= self.time), len(self.cues))
        self.finished = False
    def update(self, dt):
        _time(dt)
        if self.finished: return False
        # A guard catches accidental huge fast-forward instead of freezing a frame.
        if self.loop and dt/self.duration > 1024: raise ValueError('Timeline update exceeds 1024 loops')
        remaining = dt
        while True:
            end = min(self.duration, self.time+remaining)
            while self.index < len(self.cues) and self.cues[self.index][0] <= end:
                _, _, callback = self.cues[self.index]; self.index += 1; callback()
            remaining -= end-self.time; self.time = end
            if self.time < self.duration: return True
            if not self.loop: self.finished = True; return False
            self.time, self.index = 0., 0
            if remaining <= 0: return True

class Sequence(Timeline):
    """Sequential delays and callbacks: [(delay, callback), ...]."""
    def __init__(self, steps, **options):
        time, cues = 0., []
        for delay, callback in steps:
            time += _time(delay); cues.append((time, callback))
        super().__init__(cues, duration=time, **options)
