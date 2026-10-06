"""A decoupled event bus; unsubscribe callbacks when their owner is destroyed."""
from collections import defaultdict

class EventBus:
    def __init__(self): self._listeners = defaultdict(list)
    def on(self, event, callback):
        if not callable(callback): raise TypeError('callback must be callable')
        listeners = self._listeners[event]
        listeners.append(callback)
        def unsubscribe():
            nonlocal callback
            if callback is None: return
            listeners.remove(callback)
            callback = None
            if not listeners and self._listeners.get(event) is listeners:
                self._listeners.pop(event)
        return unsubscribe
    def emit(self, event, *args, **kwargs):
        for callback in tuple(self._listeners.get(event, ())): callback(*args, **kwargs)
bus = EventBus()
