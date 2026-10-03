import math
import forge
import ui

yaw, pitch = -math.pi / 2, -.15
position = [0, 2.5, 8]

def build():
    return {
        'mode': '3d', 'background': [.025, .035, .06, 1], 'gravity': [0, -9.81, 0],
        'camera': {'position': position, 'target': [0, 1, 0], 'fov': 60},
        'entities': [
            {'id': 'floor3d', 'kind': 'cube', 'position': [0, -.5, 0], 'scale': [22, 1, 22], 'collider': [22, 1, 22], 'color': [.12, .2, .25, 1]},
            {'id': 'hero', 'kind': 'mesh', 'model': 'crystal.obj', 'position': [0, 1.6, 0], 'scale': [1.3, 1.3, 1.3], 'material': 'teal.json', 'scripts': ['spin.py']},
            *[{'id': f'pillar{i}', 'kind': 'cube', 'position': [x, 1.5, z], 'scale': [.8, 3, .8], 'color': [.25, .35, .5, 1], 'collider': [.8, 3, .8]} for i, (x, z) in enumerate([(-4, -4), (4, -4), (-4, 4), (4, 4)])]
        ]
    }

def on_start():
    ui.text('3d-brand', 'F O R G E   /   3D WORLD', 42, 30, 20, (.4, .86, .73, 1))
    ui.text('3d-help', forge.message('example.world3d.help'), 42, 66, 17)
    forge.log('3D сцена готова: OBJ, материалы, перспектива.')

def on_update(dt):
    global yaw, pitch
    yaw += (forge.key_down('RIGHT') - forge.key_down('LEFT')) * dt * 1.5
    pitch += (forge.key_down('UP') - forge.key_down('DOWN')) * dt
    if forge.key_down('M'):
        forge.capture_mouse(True)
        dx, dy = forge.mouse_delta()
        yaw += dx * .002; pitch -= dy * .002
    else: forge.capture_mouse(False)
    pitch = max(-1.3, min(1.3, pitch))
    forward = [math.cos(yaw), 0, math.sin(yaw)]
    side = [-forward[2], 0, forward[0]]
    a = forge.key_down('W') - forge.key_down('S')
    b = forge.key_down('D') - forge.key_down('A')
    for i in (0, 2): position[i] += (forward[i] * a + side[i] * b) * dt * 4
    target = [position[0] + math.cos(yaw) * math.cos(pitch), position[1] + math.sin(pitch), position[2] + math.sin(yaw) * math.cos(pitch)]
    forge.set_camera(position, target)
    if forge.key_pressed('1'): forge.change_scene('welcome.json')
    if forge.key_pressed('ESCAPE'): forge.quit()
