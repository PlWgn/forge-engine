"""Original engine authoring example: prefab instances, fades, retarget and morphs."""
import forge
from animation import Animator,AnimationClip
from prefab_animation_example import AnimatedActor
actors=[]

def build():
    return {'mode':'3d','physics_enabled':False,'background':[.04,.05,.08,1],
            'camera':{'position':[0,1,7],'target':[0,.5,0]}}

def on_start():
    global retarget,property_animation
    actors.extend([AnimatedActor(position=(-2,0,0)),AnimatedActor(position=(0,0,0))])
    actors[1].machine.set('moving',True)
    mesh=forge.spawn({'kind':'mesh','model':'retarget.gltf','position':[2,0,0]})
    retarget=Animator(mesh,layers=[{'model':'authoring.gltf','clip':'Raise','mapping':{'Joint':'TargetJoint'}}])
    title=forge.spawn({'kind':'text','text_key':'animation.title','screen':True,'position':[30,55,0],'font_size':24})
    property_animation=AnimationClip({'color':[(0,(1,1,1,1)),(1,(.4,.8,1,1)),(2,(1,1,1,1))]}).play(title)
    forge.log('Animation example: prefabs, layers, retarget, animated morph targets')

def on_update(dt):
    for actor in actors: actor.update(dt)
    property_animation.update(dt)
    if forge.key_pressed('SPACE'):
        actors[0].machine.trigger('raise')
    if forge.key_pressed('ESCAPE'): forge.change_scene('welcome.json')

def on_destroy(): actors.clear()
