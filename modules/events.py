"""A decoupled event bus; unsubscribe callbacks when their owner is destroyed."""
from collections import defaultdict

class EventBus:
    def __init__(self): self._listeners = defaultdict(list)
    def on(self, event, callback):
        self._listeners[event].append(callback)
        return lambda: self._listeners[event].remove(callback)
    def emit(self, event, *args, **kwargs):
        for callback in tuple(self._listeners[event]): callback(*args, **kwargs)
bus = EventBus()
