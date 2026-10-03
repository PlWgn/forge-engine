"""Reusable UI module; all engine features are exposed by native `forge`."""
import forge

def text(id, value, x, y, size=24, color=(1, 1, 1, 1)):
    return forge.spawn({'id': id, 'kind': 'text', 'text': value, 'position': [x, y, 0], 'font_size': size, 'color': list(color), 'screen': True})

def panel(id, x, y, width, height, color):
    return forge.spawn({'id': id, 'kind': 'sprite', 'position': [x, y, 0], 'scale': [width, height, 1], 'color': list(color), 'screen': True})

class Button:
    """Immediate click edge detector; call update once per frame."""
    def __init__(self, bounds, callback):
        self.bounds, self.callback, self.was_down = bounds, callback, False
    def update(self):
        down = forge.mouse_down()
        x, y = forge.mouse_position()
        left, top, width, height = self.bounds
        if down and not self.was_down and left <= x <= left + width and top <= y <= top + height:
            self.callback()
        self.was_down = down
