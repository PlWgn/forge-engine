"""Native rebinding with an optional, replaceable localized controls screen."""
import forge
import ui
from input_actions import InputManager
from saves import SaveError


def build():
    return {'mode': '2d', 'physics_enabled': False, 'background': [.025, .04, .06, 1]}


def on_start():
    global actions, canvas, status, meters, rows, controls, jumps, previous_status
    jumps, previous_status = 0, 'idle'
    actions = InputManager({
        'move': [{'input': 'key:A', 'scale': -1}, 'key:D', 'axis:any:left_x'],
        'jump': ['key:SPACE', 'pad:any:a'],
        'interact': ['key:E', 'mouse:1'],
    })
    try:
        actions.load()
    except (SaveError, ValueError, RuntimeError) as error:
        forge.log(f'Input preferences: {error}', 'WARN')
    root = ui.Column(padding=36, gap=14)
    root.add(ui.Label(forge.message('input.title'), size=42))
    root.add(ui.Label(forge.message('input.help'), size=20, color=ui.MUTED))
    rows, controls = {}, []
    for action in ('move', 'jump', 'interact'):
        label = ui.Label('', size=21, flex=1)
        button = ui.Button(forge.message('input.rebind'), lambda name=action: capture(name), size=20)
        rows[action] = label
        controls.append(button)
        root.add(ui.Row(label, button, gap=16))
    status = root.add(ui.Label(forge.message('input.ready'), size=20, color=ui.ACCENT))
    meters = root.add(ui.Label('', size=22, flex=1))
    reset = ui.Button(forge.message('input.reset'), restore_defaults, size=20)
    save = ui.Button(forge.message('input.save'), persist, size=20)
    controls.extend((reset, save))
    root.add(ui.Row(reset, save, gap=12))
    root.add(ui.Label(forge.message('engine.about.attribution'), size=16, color=ui.MUTED))
    # An explicit update order prevents Canvas keyboard shortcuts from consuming
    # the same key used to complete a rebind. Any other shell can use this API.
    canvas = ui.Canvas(root, automatic=False)
    forge.on_frame(update)
    forge.log('NATIVE_INPUT_READY')


def capture(action):
    # Slot 0 only; alternate keys/pads remain. The full API can edit every slot.
    actions.begin_rebind(action, slot=0, conflict='reject')


def persist():
    try:
        actions.save()
        status.value = forge.message('input.saved')
    except SaveError as error:
        status.value = str(error)
        forge.log(f'Input preferences: {error}', 'WARN')


def restore_defaults():
    actions.reset()
    status.value = forge.message('input.defaults')


def update(dt):
    global jumps, previous_status
    state = actions.capture_state()
    just_bound = state['status'] == 'bound' and previous_status != 'bound'
    if state['status'] != previous_status or state['status'] == 'conflict':
        previous_status = state['status']
        if previous_status == 'conflict':
            status.value = forge.message('input.conflict', actions=', '.join(hit['action'] for hit in state['conflicts']))
        elif previous_status == 'bound':
            status.value = forge.message('input.bound')
        elif previous_status in ('listening', 'canceled', 'timeout'):
            status.value = forge.message('input.'+previous_status)
    if actions.pressed('jump'):
        jumps += 1
    meters.value = forge.message('input.values', move=f'{actions.value("move"):.2f}', jumps=jumps,
                                interact=str(actions.down('interact')))
    bindings = actions.bindings['default']
    for action, label in rows.items():
        values = bindings[action]
        # Required modifiers and scaled/half axes remain visible in the binding label.
        labels = []
        for binding in values:
            text = '+'.join(binding.get('modifiers', [])+[binding['input']])
            if binding.get('scale', 1) != 1:
                text += f' × {binding["scale"]}'
            if binding.get('direction', 0):
                text += f' ({binding["direction"]:+d})'
            labels.append(text)
        label.value = forge.message('input.bindings', action=forge.tr('input.action.'+action),
                                   bindings=' / '.join(labels) or forge.tr('input.unbound'))
    # Canvas already handles mouse release; disabling its keyboard focus while
    # listening also keeps Enter/Space capture from reopening a focused button.
    for control in controls:
        control.enabled = not actions.capturing and not just_bound
    if actions.capturing or just_bound:
        canvas.focus = None
    canvas.update(dt)


def on_destroy():
    actions.close()
    canvas.close()
