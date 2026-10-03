"""Input contracts run inside the real engine; only physical input is simulated."""
import forge
import ui

def verify():
    originals = {name: getattr(forge, name) for name in ('window_size', 'mouse_position', 'mouse_down', 'key_pressed', 'key_down', 'mouse_scroll')}
    state = dict(size=(1280, 720), pos=(0, 0), down=False, keys=set(), held=set(), scroll=(0, 0))
    forge.window_size = lambda: state['size']
    forge.mouse_position = lambda: state['pos']
    forge.mouse_down = lambda button=0: state['down']
    forge.key_pressed = lambda key: key in state['keys']
    forge.key_down = lambda key: key in state['held']
    forge.mouse_scroll = lambda: state['scroll']
    baseline = len(forge.entities())
    calls = []
    canvas = None
    try:
        root = ui.Column(padding=20)
        label = root.add(ui.Label('Автоматический перенос длинного текста без расчётов в игровом коде.', size=30, width=300))
        row = root.add(ui.Row(gap=12))
        first = row.add(ui.Button('Первая', lambda: calls.append('first')))
        disabled = row.add(ui.Button('Отключена', lambda: calls.append('disabled'), enabled=False))
        second = row.add(ui.Button('Вторая', lambda: calls.append('second')))
        values = []
        slider = root.add(ui.Slider(0.5, values.append, width=300))
        scrolling = root.add(ui.ScrollView(*(ui.Label('Строка ' + str(n), size=25) for n in range(30)), height=160))
        canvas = ui.Canvas(root, automatic=False)
        for operation in (lambda: scrolling.add(root), lambda: ui.Canvas(root)):
            try: operation()
            except ValueError: pass
            else: raise AssertionError('UI tree ownership/cycle check failed')
        assert len(label.lines(300)) > 1, 'UI assertion at line 29: len(label.lines(300))>1'
        assert second.bounds[0] > first.bounds[0] + first.bounds[2], 'UI assertion at line 30: second.bounds[0] > first.bounds[0]+first.bounds[2]'

        def tick(**updates):
            state.update(keys=set(), scroll=(0, 0))
            state.update(updates)
            canvas.update(1 / 60)

        def point(widget, fraction=0.5):
            x, y, w, h = canvas.to_screen(widget.bounds)
            return (x + w * fraction, y + h / 2)
        tick(pos=point(first))
        assert first.hovered and (not second.hovered), 'UI assertion at line 39: first.hovered and not second.hovered'
        tick(down=True)
        assert first.pressed and calls == [], 'UI assertion at line 41: first.pressed and calls==[]'
        tick(down=True)
        tick(down=False)
        assert calls == ['first'], "UI assertion at line 44: calls==['first']"
        tick(pos=point(first), down=True)
        tick(pos=point(second), down=False)
        assert calls == ['first'], 'release outside must cancel'
        tick(pos=point(disabled), down=True)
        tick(down=False)
        assert 'disabled' not in calls, "UI assertion at line 49: 'disabled' not in calls"
        canvas.focus = None
        tick(keys={'TAB'}, held={'SHIFT'})
        assert canvas.focus is slider, 'first Shift+Tab must select last focusable widget'
        tick(keys={'TAB'})
        assert canvas.focus is second, 'reverse traversal must skip disabled widgets'
        tick(keys={'TAB'})
        assert canvas.focus is first
        tick(keys={'TAB'})
        assert canvas.focus is slider, 'reverse traversal must wrap'
        canvas.focus = None
        tick(keys={'TAB'}, held=set())
        assert canvas.focus is first, 'UI assertion at line 52: canvas.focus is first'
        tick(keys={'TAB'})
        assert canvas.focus is second, 'disabled button entered focus chain'
        tick(keys={'ENTER'})
        assert calls == ['first', 'second'], "UI assertion at line 56: calls==['first','second']"
        px,py,sw,sh=canvas.to_screen(slider.bounds)
        tick(pos=(px+7*canvas.scale[0]+(sw-14*canvas.scale[0])*.25,py+sh/2), down=True)
        assert abs(slider.value - 0.25) < 0.001, 'UI assertion at line 58: abs(slider.value-.25)<.001'
        tick(pos=point(slider, 1.5), down=True)
        assert slider.value == 1, 'UI assertion at line 60: slider.value==1'
        tick(down=False)
        tick(keys={'LEFT'})
        assert abs(slider.value - 0.95) < 0.001, 'UI assertion at line 63: abs(slider.value-.95)<.001'
        tick(pos=point(scrolling), scroll=(0, -3))
        tick()
        assert scrolling.offset == 120, 'UI assertion at line 66: scrolling.offset==120'
        for child in scrolling.children:
            for entity in child._entities.values():
                if entity.visible:
                    x, y, w, h = entity.clip
                    sx, sy, sw, sh = canvas.to_screen(scrolling.bounds)
                    assert sy <= y and y + h <= sy + sh + 0.01, 'UI assertion at line 72: sy <= y and y+h <= sy+sh+.01'
        overlay = canvas.overlay(ui.Column(width=500, height=200, padding=20, background=ui.PANEL))
        modal_button = overlay.add(ui.Button('Модальная', lambda: calls.append('modal')))
        canvas.modal(overlay)
        tick()
        canvas.focus = None
        tick(keys={'TAB'}, held={'SHIFT'})
        assert canvas.focus is modal_button, 'reverse traversal must respect modal scope'
        tick(held=set())
        tick(pos=point(first), down=True)
        tick(down=False)
        assert calls == ['first', 'second'], "UI assertion at line 77: calls==['first','second']"
        tick(pos=point(modal_button), down=True)
        tick(down=False)
        assert calls[-1] == 'modal', "UI assertion at line 79: calls[-1]=='modal'"
        overlay.visible = False
        canvas.modal(None)
        tick(size=(640, 480))
        assert canvas.scale == (0.5, 0.5) and canvas.origin == (0.0, 60.0), 'UI assertion at line 82: canvas.scale==(.5,.5) and canvas.origin==(0.,60.)'
        x, y, w, h = canvas.to_screen(first.bounds)
        assert abs(first._entities['background'].position[0] - (x + w / 2)) < .001, "UI assertion at line 84: first._entities['background'].position[0]==x+w/2"
        tick(pos=point(first), down=True)
        tick(down=False)
        assert calls[-1] == 'first', "UI assertion at line 86: calls[-1]=='first'"
        canvas.close()
        canvas.close()
        assert len(forge.entities()) == baseline, 'UI close leaked entities'
        forge.log('UI_INPUT_OK')
    finally:
        if canvas is not None:
            canvas.close()
        for name, value in originals.items():
            setattr(forge, name, value)
