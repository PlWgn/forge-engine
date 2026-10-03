"""Retained UI, layout and input. Optional, editable, independent of game genre.
Canvas owns entities and registers a scene-scoped frame listener automatically.
Coordinates in a Canvas are design pixels; its viewport adapts to window size.
Legacy text(), panel() and Button((x,y,w,h), callback) remain supported.
"""
from functools import lru_cache
import math
import forge

WHITE = (1, 1, 1, 1)
MUTED = (.64, .7, .79, 1)
PANEL = (.075, .10, .15, .98)
ACCENT = (.24, .76, .63, 1)


def text(id, value, x, y, size=24, color=WHITE):
    return forge.spawn(dict(id=id, kind='text', text=value, position=[x, y, 0],
                            font_size=size, color=list(color), screen=True))


def panel(id, x, y, width, height, color):
    return forge.spawn(dict(id=id, kind='sprite', position=[x, y, 0],
                            scale=[width, height, 1], color=list(color), screen=True))


@lru_cache(maxsize=4096)
def measure(value, size=24):
    """(advance width, multiline height, line height), using the renderer's font."""
    return tuple(forge.measure_text(str(value), size))


@lru_cache(maxsize=512)
def wrap(value, width, size=24):
    """Word wrap, explicit paragraphs and character fallback for long words."""
    if not math.isfinite(width) or width <= 0:
        return str(value)
    result = []
    for paragraph in str(value).split('\n'):
        line = ''
        for word in paragraph.split():
            trial = (line + ' ' + word).lstrip()
            if measure(trial, size)[0] <= width:
                line = trial
                continue
            if line:
                result.append(line)
                line = ''
            for char in word:
                if line and measure(line + char, size)[0] > width:
                    result.append(line)
                    line = ''
                line += char
        result.append(line)
    return '\n'.join(result)


def _padding(value):
    if isinstance(value, (float, int)):
        return (value,) * 4
    if len(value) == 2:
        return value[0], value[1], value[0], value[1]
    if len(value) == 4:
        return tuple(value)
    raise ValueError('padding: number, (horizontal, vertical) or (left, top, right, bottom)')


def _contains(rect, x, y):
    a, b, w, h = rect
    return a <= x < a + w and b <= y < b + h


def _intersect(a, b):
    x, y = max(a[0], b[0]), max(a[1], b[1])
    return x, y, max(0, min(a[0]+a[2], b[0]+b[2])-x), max(0, min(a[1]+a[3], b[1]+b[3])-y)


class Widget:
    interactive = False

    def __init__(self, *, width=None, height=None, flex=0, padding=0, background=None,
                 visible=True, enabled=True):
        if width is not None and width < 0 or height is not None and height < 0 or flex < 0:
            raise ValueError('Widget dimensions and flex must be nonnegative')
        self.width, self.height, self.flex = width, height, flex
        self.padding, self.background = _padding(padding), background
        self.visible, self.enabled = visible, enabled
        self.parent, self.canvas = None, None
        self.children, self._entities = [], {}
        self.bounds = self.clip = (0, 0, 0, 0)
        self.hovered = self.focused = self.pressed = False
        self._shown = False

    def add(self, child):
        if not isinstance(child, Widget) or getattr(child, "legacy", False):
            raise TypeError("Add a visual Widget; legacy Button(bounds, callback) is updated manually")
        ancestor = self
        while ancestor is not None:
            if ancestor is child:
                raise ValueError("Widget tree cannot contain cycles")
            ancestor = ancestor.parent
        if child.parent is not None or child.canvas is not None:
            raise ValueError('Widget already belongs to a tree')
        if child is self:
            raise ValueError('Widget cannot contain itself')
        child.parent = self
        self.children.append(child)
        return child

    def remove(self, child):
        self.children.remove(child)
        child.close()
        child.parent = None

    def close(self):
        for child in self.children:
            child.close()
        for entity in self._entities.values():
            entity.destroy()
        self._entities.clear()
        self.canvas = None

    def preferred(self, width):
        return self.width or 0, self.height or 0

    def layout(self, rect, clip, canvas, shown=True, enabled=True):
        self.bounds, self.clip, self.canvas = rect, _intersect(rect, clip), canvas
        self._shown, self._enabled = shown and self.visible, enabled and self.enabled
        canvas._widgets.append(self)
        self.paint()

    def _entity(self, key, kind):
        entity = self._entities.get(key)
        if entity is None or not entity.alive:
            entity = forge.spawn(dict(name=f'ui.{key}', kind=kind, screen=True))
            self._entities[key] = entity
        entity.visible = self._shown and self.clip[2] > 0 and self.clip[3] > 0
        entity.clip = self.canvas.to_screen(self.clip)
        return entity

    def rectangle(self, key, rect, color):
        entity = self._entity(key, 'sprite')
        x, y, w, h = self.canvas.to_screen(rect)
        entity.position = (x + w / 2, y + h / 2, self.canvas._next_z())
        entity.scale, entity.color = (max(w, .001), max(h, .001), 1), color
        return entity

    def caption(self, key, value, x, y, size, color):
        entity = self._entity(key, 'text')
        x, y, _, _ = self.canvas.to_screen((x, y, 0, 0))
        entity.position, entity.scale = (x, y, self.canvas._next_z()), (*self.canvas.scale, 1)
        entity.text, entity.font_size, entity.color = str(value), size, color
        return entity

    def paint(self):
        if self.background is not None:
            self.rectangle('background', self.bounds, self.background)

    def activate(self):
        pass


class Box(Widget):
    def __init__(self, *children, direction='column', gap=12, **options):
        super().__init__(**options)
        if direction not in ('row', 'column'):
            raise ValueError('direction must be row or column')
        self.direction, self.gap = direction, gap
        for child in children:
            self.add(child)

    def preferred(self, width):
        l, t, r, b = self.padding
        children = [c for c in self.children if c.visible]
        sizes = [c.preferred(max(0, width-l-r)) for c in children]
        gap = self.gap * max(0, len(sizes)-1)
        w = (sum(s[0] for s in sizes)+gap if self.direction == 'row' else max((s[0] for s in sizes), default=0))+l+r
        h = (max((s[1] for s in sizes), default=0) if self.direction == 'row' else sum(s[1] for s in sizes)+gap)+t+b
        return self.width if self.width is not None else w, self.height if self.height is not None else h

    def layout(self, rect, clip, canvas, shown=True, enabled=True):
        super().layout(rect, clip, canvas, shown, enabled)
        x, y, w, h = rect
        l, t, r, b = self.padding
        x, y, w, h = x+l, y+t, max(0, w-l-r), max(0, h-t-b)
        row = self.direction == 'row'
        visible = [c for c in self.children if c.visible]
        sizes = {c: c.preferred(w) for c in visible}
        fixed = sum(sizes[c][0 if row else 1] for c in visible if not c.flex)
        total_flex = sum(c.flex for c in visible)
        remaining = max(0, (w if row else h)-fixed-self.gap*max(0, len(visible)-1))
        offset = 0
        for child in self.children:
            if child.visible:
                extent = remaining*child.flex/total_flex if child.flex else sizes[child][0 if row else 1]
                cross = child.height if row else child.width
                cross = (h if row else w) if cross is None else cross
                area = (x+offset, y, extent, cross) if row else (x, y+offset, cross, extent)
                offset += extent+self.gap
            else:
                area = (x, y, 0, 0)
            child.layout(area, self.clip, canvas, self._shown, self._enabled)


class Column(Box):
    def __init__(self, *children, **options):
        super().__init__(*children, direction='column', **options)


class Row(Box):
    def __init__(self, *children, **options):
        super().__init__(*children, direction='row', **options)


class Label(Widget):
    def __init__(self, value='', *, size=24, color=WHITE, wrap_text=True, align='left', **options):
        super().__init__(**options)
        if align not in ('left', 'center', 'right'):
            raise ValueError('align must be left, center or right')
        self.value, self.size, self.color = str(value), size, color
        self.wrap_text, self.align = wrap_text, align

    def lines(self, width):
        return (wrap(self.value, width, self.size) if self.wrap_text else self.value).split('\n')

    def preferred(self, width):
        l, t, r, b = self.padding
        effective = (self.width if self.width is not None else width)-l-r
        lines = self.lines(max(1, effective))
        w = max((measure(v, self.size)[0] for v in lines), default=0)+l+r
        h = len(lines)*measure('', self.size)[2]+t+b
        return self.width if self.width is not None else w, self.height if self.height is not None else h

    def paint(self):
        super().paint()
        x, y, w, h = self.bounds
        l, t, r, b = self.padding
        lines = self.lines(max(1, w-l-r)) if self._shown else []
        line_height = measure('', self.size)[2]
        for index, line in enumerate(lines):
            advance = measure(line, self.size)[0]
            offset = 0 if self.align == 'left' else (w-l-r-advance)*(0.5 if self.align == 'center' else 1)
            self.caption(f'line{index}', line, x+l+offset, y+t+index*line_height, self.size, self.color)
        for key, entity in self._entities.items():
            if key.startswith('line') and int(key[4:]) >= len(lines):
                entity.visible = False


class Button(Widget):
    interactive = True

    def __init__(self, label, callback, *, size=24, color=WHITE, **options):
        self.legacy = isinstance(label, (tuple, list))
        if self.legacy:
            self.bounds, self.callback, self.was_down = tuple(label), callback, False
            return
        options.setdefault('padding', (20, 12))
        super().__init__(**options)
        self.label, self.callback, self.size, self.color = str(label), callback, size, color

    def preferred(self, width):
        l, t, r, b = self.padding
        w, h, _ = measure(self.label, self.size)
        return self.width if self.width is not None else w+l+r, self.height if self.height is not None else h+t+b

    def paint(self):
        color = (.13, .18, .24, 1)
        if self.pressed:
            color = (.15, .42, .37, 1)
        elif self.hovered or self.focused:
            color = (.12, .32, .29, 1)
        if not self._enabled:
            color = (.09, .11, .14, 1)
        self.rectangle('background', self.bounds, color)
        x, y, w, h = self.bounds
        tw, th, _ = measure(self.label, self.size)
        self.caption('caption', self.label, x+(w-tw)/2, y+(h-th)/2, self.size, self.color if self._enabled else MUTED)

    def activate(self):
        if self._enabled:
            self.callback()

    def update(self):
        if not self.legacy:
            raise RuntimeError('Add Button to Canvas; Canvas updates it automatically')
        down = forge.mouse_down()
        if down and not self.was_down and _contains(self.bounds, *forge.mouse_position()):
            self.callback()
        self.was_down = down


class Slider(Widget):
    interactive = True

    def __init__(self, value=1, callback=lambda value: None, *, step=.05, **options):
        options.setdefault('height', 32)
        options.setdefault('width', 240)
        super().__init__(**options)
        self.value, self.callback, self.step = max(0, min(1, value)), callback, step

    def preferred(self, width):
        return self.width, self.height

    def set(self, value):
        value = max(0, min(1, value))
        if value != self.value:
            self.value = value
            self.callback(value)

    def drag(self, x):
        left, _, width, _ = self.bounds
        self.set((x-left-7)/max(1, width-14))

    def paint(self):
        x, y, w, h = self.bounds
        self.rectangle('track', (x+7, y+h/2-3, max(.001,w-14), 6), (.2, .25, .32, 1))
        self.rectangle('fill', (x+7, y+h/2-3, max(.001,(w-14)*self.value), 6), ACCENT)
        self.rectangle('knob', (x+max(0,w-14)*self.value, y+h/2-11, 14, 22), WHITE if self.focused or self.hovered else ACCENT)


class ScrollView(Column):
    """Clips content. offset is in design pixels; wheel and PageUp/Down scroll."""
    def __init__(self, *children, **options):
        super().__init__(*children, **options)
        self.offset = 0

    def layout(self, rect, clip, canvas, shown=True, enabled=True):
        # Keep viewport in the input tree, then shift children only.
        Widget.layout(self, rect, clip, canvas, shown, enabled)
        content_height = max(rect[3], Box.preferred(self, rect[2])[1] if self.height is None else self._content_height(rect[2]))
        self.offset = max(0, min(self.offset, content_height-rect[3]))
        viewport = self.clip
        before = len(canvas._widgets)
        Box.layout(self, (rect[0], rect[1]-self.offset, rect[2], content_height), viewport, canvas, shown, enabled)
        del canvas._widgets[before:before+1]  # avoid duplicate self
        self.bounds, self.clip = rect, viewport
        # Background must remain fixed rather than scroll with content.
        if self.background is not None:
            entity = self._entities['background']
            x, y, w, h = canvas.to_screen(rect)
            entity.position = (x+w/2, y+h/2, entity.position[2])
            entity.scale = (max(.001, w), max(.001, h), 1)

    def _content_height(self, width):
        l, t, r, b = self.padding
        visible = [c for c in self.children if c.visible]
        return t+b+sum(c.preferred(max(0, width-l-r))[1] for c in visible)+self.gap*max(0, len(visible)-1)


class Canvas:
    def __init__(self, root=None, *, width=1280, height=720, scale='fit', automatic=True):
        if scale not in ('fit', 'stretch', 'none') or width <= 0 or height <= 0:
            raise ValueError('Canvas needs positive dimensions and fit/stretch/none scale')
        self.root = root if root is not None else Column(padding=32)
        if self.root.parent is not None or self.root.canvas is not None:
            raise ValueError("Canvas root already belongs to a tree")
        self.width, self.height, self.mode = width, height, scale
        self.scale, self.origin = (1, 1), (0, 0)
        self.overlays, self.modal_widget, self._widgets = [], None, []
        self.focus, self.capture, self.was_down = None, None, False
        self.closed, self._z = False, 100
        self._listener = forge.on_frame(self.update) if automatic else None
        self._layout()

    def overlay(self, widget):
        if widget.canvas is not None or widget.parent is not None:
            raise ValueError('Overlay already belongs to a tree')
        self.overlays.append(widget)
        return widget

    def modal(self, widget):
        if widget is not None and widget not in self.overlays:
            raise ValueError('Modal must be an overlay of this Canvas')
        self.modal_widget = widget
        self.focus = self.capture = None
        if widget is not None:
            widget.visible = True

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self._listener is not None:
            forge.remove_listener(self._listener)
        self.root.close()
        for overlay in self.overlays:
            overlay.close()

    def _next_z(self):
        self._z += .01
        return self._z

    def to_screen(self, rect):
        x, y, w, h = rect
        return self.origin[0]+x*self.scale[0], self.origin[1]+y*self.scale[1], w*self.scale[0], h*self.scale[1]

    def _layout(self):
        self._widgets, self._z = [], 100
        ww, wh = forge.window_size()
        if self.mode == 'none':
            self.scale, self.origin = (1, 1), (0, 0)
            width, height = ww, wh
        else:
            sx, sy = ww/self.width, wh/self.height
            self.scale = (min(sx, sy),)*2 if self.mode == 'fit' else (sx, sy)
            self.origin = ((ww-self.width*self.scale[0])/2, (wh-self.height*self.scale[1])/2)
            width, height = self.width, self.height
        clip = (0, 0, width, height)
        self.root.layout(clip, clip, self)
        for overlay in self.overlays:
            w = min(width, overlay.width or width*.7)
            h = min(height, overlay.height or overlay.preferred(w)[1])
            overlay.layout(((width-w)/2, (height-h)/2, w, h), clip, self)

    def _allowed(self, widget):
        if not widget._shown or not widget._enabled:
            return False
        if self.modal_widget is not None and self.modal_widget.visible:
            parent = widget
            while parent is not None:
                if parent is self.modal_widget:
                    return True
                parent = parent.parent
            return False
        return True

    def update(self, dt=0):
        if self.closed:
            return
        self._layout()
        mx, my = forge.mouse_position()
        sx, sy = self.scale
        x, y = (mx-self.origin[0])/max(.001, sx), (my-self.origin[1])/max(.001, sy)
        targets = [w for w in self._widgets if w.interactive and self._allowed(w)]
        hit = next((w for w in reversed(targets) if _contains(w.clip, x, y)), None)
        for widget in self._widgets:
            widget.hovered = widget is hit
            widget.focused = widget is self.focus and widget in targets
            widget.pressed = widget is self.capture
        if self.focus not in targets:
            self.focus = None
        if self.capture not in targets:
            self.capture = None
        down = forge.mouse_down()
        if down and not self.was_down:
            self.capture = self.focus = hit
        if down and isinstance(self.capture, Slider):
            self.capture.drag(x)
        activate = self.capture if not down and self.was_down and self.capture is hit else None
        if not down:
            self.capture = None
        self.was_down = down
        if forge.key_pressed('TAB') and targets:
            index = targets.index(self.focus) if self.focus in targets else -1
            self.focus = targets[(index+(-1 if forge.key_down('SHIFT') else 1)) % len(targets)]
        if self.focus is not None:
            if forge.key_pressed('ENTER') or forge.key_pressed('SPACE'):
                activate = self.focus
            if isinstance(self.focus, Slider):
                if forge.key_pressed('LEFT'): self.focus.set(self.focus.value-self.focus.step)
                if forge.key_pressed('RIGHT'): self.focus.set(self.focus.value+self.focus.step)
        for widget in reversed(self._widgets):
            if isinstance(widget, ScrollView) and self._allowed(widget) and _contains(widget.clip, x, y):
                widget.offset -= forge.mouse_scroll()[1]*40
                if forge.key_pressed('PAGEDOWN'): widget.offset += widget.bounds[3]*.8
                if forge.key_pressed('PAGEUP'): widget.offset -= widget.bounds[3]*.8
                break
        if activate is not None:
            activate.activate()
        for widget in self._widgets:
            widget.focused = widget is self.focus and widget in targets
            widget.pressed = widget is self.capture
        # Paint after input, so hover/pressed/focus have no one-frame delay.
        if not self.closed:
            self._layout()
