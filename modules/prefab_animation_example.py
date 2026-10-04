"""Game-side extension: reusable animated actors, using only public Forge APIs."""
from engine_api import require_api
from prefabs import Prefab
from animation import Animator,AnimationStateMachine

class AnimatedActor:
    def __init__(self, file='animated-actor.json', **instance_options):
        require_api(1,'prefabs','animation_layers')
        self.instance=Prefab(file).instantiate(**instance_options)
        self.animator=Animator(self.instance['mesh'])
        self.machine=AnimationStateMachine(self.animator,
            {'idle':{'clip':'Idle'},'move':{'clip':'Move'},'raise':{'clip':'Raise'}},
            [{'from':'idle','to':'move','when':{'moving':True}},
             {'from':'move','to':'idle','when':{'moving':False}},
             {'from':'*','to':'raise','when':{'raise':{'trigger':True}}}])
    def update(self, dt): self.machine.update(dt)
    def destroy(self): self.instance.destroy()
