"""PBR materials, local transforms and scene-owned procedural geometry."""
import math
import forge
from geometry import Mesh
from ui import text


def on_start():
    global pedestal, triangle
    pedestal = forge.find('turntable')
    triangle = Mesh('color-triangle', [[-1, 0, 0], [1, 0, 0], [0, 1.4, 0]],
                    colors=[[1, .15, .1, 1], [.1, 1, .2, 1], [.15, .2, 1, 1]])
    forge.spawn({'id': 'procedural', 'kind': 'mesh', 'model': triangle.model,
                 'position': [-3, 1.4, 0],
                 'material_properties': {'shading': 'unlit', 'alpha_mode': 'opaque'}})
    text('materials-title', forge.message('materials.title'), 30, 30, 30)
    text('materials-help', forge.message('materials.help'), 30, 76, 18)
    forge.log('Forge 2.2 PBR + hierarchy + procedural geometry ready')


def on_update(dt):
    angle = math.sin(forge.time() * .5) * 30
    pedestal.rotation = (0, angle, 0)
    if forge.key_pressed('P'):
        forge.user_screenshot('materials.ppm')
    if forge.key_pressed('ESCAPE'):
        forge.quit()
