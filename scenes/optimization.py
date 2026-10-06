"""Optional native visibility/LOD example; no gameplay is frozen by culling.
Run: python tools/forge.py dev --scene optimization.py
O toggles occlusion; L toggles all optimizations; arrow keys move the camera.
"""
import forge

camera_z = 0.0
enabled = True
occlusion = True
frames = 0

def build():
    return {'mode': '3d', 'physics_enabled': False, 'background': [.025, .03, .05, 1],
            'camera': {'position': [0, 1, camera_z], 'target': [0, 1, camera_z - 1]},
            'rendering': {'optimization': {'enabled': enabled, 'occlusion': occlusion}}}

def on_start():
    # Original/low representations are ordinary interchangeable user-authored meshes.
    high = forge.set_mesh('detail', {'positions': [[-1,-1,0],[1,-1,0],[1,1,0],[-1,-1,0],[1,1,0],[-1,1,0]],
                                     'colors': [[1,.65,.15,1]] * 6})
    low = forge.set_mesh('distant', {'positions': [[-1,-1,0],[1,-1,0],[0,1,0]],
                                    'colors': [[.2,.8,.4,1]] * 3})
    forge.spawn({'kind': 'cube', 'position': [0, 1, -5], 'scale': [4, 3, .4],
                 'color': [.35,.4,.6,1], 'material_properties': {'alpha_mode': 'opaque'},
                 'optimization': {'occluder': {'min': [-.5,-.5,-.5], 'max': [.5,.5,.5]}}})
    for row in range(12):
        for column in range(-4, 5):
            forge.spawn({'kind': 'mesh', 'model': high, 'position': [column*2.5, 1, -8-row*3],
                         'optimization': {'levels': [{'distance': 18, 'model': low}], 'max_distance': 80}})
    forge.spawn({'kind': 'text', 'screen': True, 'position': [20,35,0], 'font_size': 20,
                 'text': 'Uses the Forge engine | Arrows: move | O: occlusion | L: optimization'})
    global status
    status = forge.spawn({'kind': 'text', 'screen': True, 'position': [20,70,0], 'font_size': 18})

def on_update(dt):
    global camera_z, enabled, occlusion, frames
    camera_z += ((1 if forge.key_down('DOWN') else 0) - (1 if forge.key_down('UP') else 0)) * dt * 8
    forge.set_camera([0,1,camera_z], [0,1,camera_z-1])
    if forge.key_pressed('O'): occlusion = not occlusion
    if forge.key_pressed('L'): enabled = not enabled
    forge.set_render_optimization({'enabled': enabled, 'occlusion': occlusion})
    frames += 1
    if frames % 10 == 0:
        stats = forge.renderer_stats()
        view = stats.get('optimization', {}).get('passes', {}).get('main', {})
        status.text = f"Draws: {stats.get('draw_calls',0)} | LOD: {view.get('lod_selected',0)} | Frustum: {view.get('frustum_culled',0)} | Occluded: {view.get('occlusion_culled',0)}"
