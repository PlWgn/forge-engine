"""XR defaults and validation through the real embedded Python/runtime bridge."""
import unittest
import integration as base

class XrTests(unittest.TestCase):
    setUp, tearDown = base.EngineTests.setUp, base.EngineTests.tearDown
    write_config, run_engine, script_scene = base.EngineTests.write_config, base.EngineTests.run_engine, base.EngineTests.script_scene

    def test_disabled_and_headless_contract(self):
        self.script_scene('''import forge

def on_start():
    assert isinstance(forge.capabilities()['openxr'], bool)
    assert forge.xr_state() == {'enabled': False}
    for call in (lambda:forge.xr_set_origin([0,0,0]), lambda:forge.xr_haptic('left'),
                 lambda:forge.xr_set_origin([True,0,0]),lambda:forge.xr_set_origin([0,0]),
                 lambda:forge.xr_haptic('left',True)):
        try: call()
        except RuntimeError: pass
        else: raise AssertionError('Headless XR side effect accepted')

def on_update(dt):
    assert forge.input_snapshot()['xr'] == {'enabled':False}
    forge.log('XR_DISABLED_OK');forge.quit()
''')
        self.assertIn('XR_DISABLED_OK', self.run_engine())

    def test_editable_vr_helpers(self):
        self.script_scene('''import forge, math
from vr_input import ControllerInput, aim_ray, pose_rotation
def on_start():
    controls=ControllerInput()
    def frame(trigger,focused=True):return {'focused':focused,'hands':{'right':{'trigger':trigger}}}
    controls.update(frame(1));assert not controls.pressed('right')
    controls.update(frame(0));controls.update(frame(1));assert controls.pressed('right')
    controls.update(frame(1));assert controls.down('right') and not controls.pressed('right')
    controls.update(frame(0));assert controls.released('right')
    controls.update(frame(1,False));assert not controls.released('right')
    controls.update(frame(1));assert not controls.down('right')
    controls.update(frame(0));controls.update(frame(1));assert controls.pressed('right')
    pose={'valid':True,'position':[1,2,3],'orientation':[0,0,0,1]}
    assert aim_ray(pose)==((1,2,3),(0,0,-1)) and pose_rotation(pose)==(0,0,0)
    pose['orientation']=[0,math.sin(math.pi/4),0,math.cos(math.pi/4)]
    assert abs(aim_ray(pose)[1][0]+1)<1e-6
    assert abs(pose_rotation(pose)[1]-90)<1e-6
    forge.log('VR_HELPERS_OK')
''')
        self.assertIn('VR_HELPERS_OK',self.run_engine())

    def test_pose_rotation_matches_native_xyz_transforms(self):
        self.script_scene('''import forge,math
from vr_input import pose_rotation,aim_ray
def multiply(a,b):
    x,y,z,w=a;X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def on_start():
    entity=forge.spawn({'kind':'empty'})
    for angles in ((25,40,65),(-40,30,-80),(30,90,40),(-20,-90,15),(100,120,-140)):
        x,y,z=(math.radians(a)/2 for a in angles)
        q=multiply(multiply((math.sin(x),0,0,math.cos(x)),(0,math.sin(y),0,math.cos(y))),(0,0,math.sin(z),math.cos(z)))
        pose={'valid':True,'position':[0,0,0],'orientation':q}
        entity.rotation=angles;expected=entity.world_matrix
        entity.rotation=pose_rotation(pose);actual=entity.world_matrix
        assert max(abs(a-b) for row1,row2 in zip(expected,actual) for a,b in zip(row1,row2))<1e-5,(angles,pose_rotation(pose))
        direction=aim_ray(pose)[1]
        assert max(abs(direction[i]+actual[i][2]) for i in range(3))<1e-5
    forge.log('VR_XYZ_ROTATION_OK');forge.quit()
''')
        self.assertIn('VR_XYZ_ROTATION_OK',self.run_engine())

    def test_invalid_configuration_is_rejected(self):
        for value in (None, [], {'enabled':1}, {'reference_space':'floor'}, {'meters_per_unit':0}, {'near':5,'far':1}, {'far':1e100}):
            with self.subTest(value=value):
                self.config['xr']=value;self.write_config()
                self.assertIn('xr', self.run_engine('validate', expected=1))

    def test_portable_validation_and_headless_authoring(self):
        self.config['xr']={'enabled':True,'reference_space':'stage','game_metadata':{'unknown':1}}
        self.write_config()
        self.assertIn('validated',self.run_engine('validate'))
        # Headless simulations intentionally retain their existing behavior even for VR projects.
        self.assertIn('Runtime stopped', self.run_engine())

if __name__ == '__main__': unittest.main(verbosity=2)
