"""OpenXR room-scale example; desktop preview remains usable with xr.enabled=false."""
import forge
from vr_input import ControllerInput, pose_rotation

controls=None
rig_initialized=False

def build():
    return {
        'mode':'3d','background':[.025,.035,.06,1],
        'camera':{'position':[0,1.6,4],'target':[0,1,0]},
        'entities':[
            {'id':'floor','kind':'cube','position':[0,-.05,0],'scale':[8,.1,8],'color':[.12,.18,.24,1]},
            {'id':'target','kind':'cube','position':[0,1.2,-2],'scale':[.4,.4,.4],'color':[.2,.8,.6,1]},
            *[{'id':hand,'kind':'cube','scale':[.05,.05,.14],'visible':False,'color':color}
              for hand,color in (('left',[.25,.55,1,1]),('right',[1,.5,.25,1]))],
            {'id':'brand','kind':'text','screen':False,'text_key':'example.vr.brand',
             'position':[-.8,2,-3],'font_size':32,'scale':[.005,.005,.005]},
        ]
    }

def on_start():
    global controls
    controls=ControllerInput()

def on_update(dt):
    global rig_initialized
    state=controls.update()
    if state.get('running') and not rig_initialized:
        # Example policy: local space often starts at eye height; stage already has a floor.
        if state.get('reference_space')=='local':
            units=forge.settings().get('xr',{}).get('meters_per_unit',1)
            forge.xr_set_origin([0,1.6/units,0])
        rig_initialized=True
    for hand in ('left','right'):
        entity=forge.find(hand);pose=state.get('hands',{}).get(hand,{}).get('grip',{})
        entity.visible=bool(pose.get('valid'))
        if entity.visible:entity.position=pose['position'];entity.rotation=pose_rotation(pose)
        if controls.pressed(hand):forge.xr_haptic(hand,.35,.06)
    if forge.key_pressed('ESCAPE'):forge.quit()
