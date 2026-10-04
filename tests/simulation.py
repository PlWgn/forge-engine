"""Native particle/rigid-body regression tests; no mocks or GPU dependency."""
from pathlib import Path
import json, subprocess, time, unittest
import integration as base
ROOT, ENGINE = base.ROOT, base.ENGINE


class SimulationTests(unittest.TestCase):
    setUp, tearDown = base.EngineTests.setUp, base.EngineTests.tearDown
    write_config, script_scene, run_engine = base.EngineTests.write_config, base.EngineTests.script_scene, base.EngineTests.run_engine

    def native(self, source, *, rigid=True):
        build = "def build(): return {'mode':'3d','gravity':[0,0,0],'physics':{'backend':'bullet'},'physics_enabled':False}\n" if rigid else ""
        self.script_scene(build + source)
        return self.run_engine(frames=150)

    def test_cached_bodies_and_batched_queries_detect_changes(self):
        for backend in ('legacy','bullet'):
            with self.subTest(backend=backend):
                self.script_scene("""import forge
def build():return {'mode':'3d','physics_enabled':False,'physics':{'backend':'BACKEND'},'entities':[{'id':'e'+str(i),'kind':'empty','collider':[1,1,1],'position':[i*3,0,0]} for i in range(500)]}
def on_start():
    target=forge.find('e0');forge.physics_step(0)
    rays=[((0,0,5),(0,0,-1),10),((1.5,0,5),(0,0,-1),10)]*50
    before=forge.world_stats();before_physics=forge.physics_stats()
    hits=forge.raycast_many(rays);after=forge.world_stats();after_physics=forge.physics_stats()
    assert all(e is target if i%2==0 else e is None for i,e in enumerate(hits))
    assert after['transform_computations']==before['transform_computations']
    if before_physics['backend']=='bullet':
        assert after_physics['body_synchronizations']==before_physics['body_synchronizations']
        assert after_physics['sync_audits']-before_physics['sync_audits']==2
    for _ in range(5):assert forge.raycast((0,0,5),(0,0,-1),10) is target
    if before_physics['backend']=='bullet':assert forge.physics_stats()['body_synchronizations']==after_physics['body_synchronizations']
    target.position=(1.5,0,0);assert forge.raycast_many(rays[:2])==[None,target]
    target.collider=(1,1,0);assert forge.raycast_many(rays[:2])==[None,None]
    target.collider=(1,1,1);target.destroy()
    replacement=forge.spawn({'id':'e0','kind':'empty','collider':[1,1,1]})
    assert forge.raycast_many(rays[:2])==[replacement,None]
    try:forge.raycast_many([rays[0],((float('nan'),0,0),(1,0,0),10)])
    except (RuntimeError,ValueError):pass
    else:raise AssertionError('nonfinite batch accepted')
    forge.log('CACHED_QUERIES_OK');forge.quit()
""".replace('BACKEND',backend))
                self.assertIn('CACHED_QUERIES_OK',self.run_engine())

    def test_oriented_boxes_spheres_capsules_and_exact_rays(self):
        self.assertIn('SHAPES_OK', self.native('''import forge
from physics import raycast
def on_start():
    sphere=forge.spawn({'id':'sphere','kind':'empty','collider':[2,2,2],'rigid_body':{'shape':'sphere'}})
    probe=forge.spawn({'id':'probe','kind':'empty','position':[.9,.9,0],'collider':[.2,.2,.2]})
    assert not forge.overlaps(sphere,probe)
    assert raycast((.9,.9,3),(0,0,-1),5,ignore='probe') is None
    probe.position=(.5,.5,0);assert forge.overlaps(sphere,probe);probe.destroy()
    hit=raycast((0,0,3),(0,0,-1),10)
    assert hit['entity'].id=='sphere' and abs(hit['distance']-2)<.01 and hit['normal'][2]>.99
    box=forge.spawn({'id':'rotated','kind':'empty','position':[5,0,0],'rotation':[0,0,45],'collider':[4,.2,.2]})
    probe=forge.spawn({'id':'probe2','kind':'empty','position':[6.2,1.2,0],'collider':[.1,.1,.1]})
    assert forge.overlaps(box,probe)
    capsule=forge.spawn({'id':'capsule','kind':'empty','position':[10,0,0],'collider':[1,2,1],'rigid_body':{'shape':'capsule'}})
    assert forge.raycast((10,0,3),(0,0,-1),10).id=='capsule'
    capsule.collider=(1,2,0);assert raycast((10,0,3),(0,0,-1),10) is None
    forge.log('SHAPES_OK');forge.quit()
'''))

    def test_forces_mass_torque_impulses_rotation_and_serialization(self):
        self.assertIn('FORCES_OK', self.native('''import forge,math
from physics import RigidBody
def on_start():
    a=forge.spawn({'id':'a','kind':'empty','dynamic':True,'collider':[1,1,1]})
    b=forge.spawn({'id':'b','kind':'empty','dynamic':True,'mass':2,'position':[10,0,0],'collider':[1,1,1]})
    body=RigidBody(a,friction=.8,sleep=False)
    body.force((10,0,0));forge.apply_force(b,(10,0,0));forge.physics_step(.1)
    assert abs(a.velocity[0]-1)<1e-4 and abs(b.velocity[0]-.5)<1e-4,(a.velocity,b.velocity)
    body.impulse((0,1,0),point=(a.position[0]+.5,0,0))
    assert a.angular_velocity[2]>2 and a.velocity[1]>.99
    body.torque((0,1,0));forge.physics_step(.1)
    assert abs(a.rotation[2])>10 and abs(a.angular_velocity[1])>.1
    data=forge.scene_data();saved=next(e for e in data['entities'] if e['id']=='a')
    assert saved['rigid_body']['friction']==.8 and saved['angular_velocity'][2]>2
    assert data['physics']['backend']=='bullet' and forge.physics_stats()['precision']=='double'
    forge.log('FORCES_OK');forge.quit()
'''))

    def test_ccd_stops_fast_body_at_thin_wall(self):
        self.assertIn('CCD_OK', self.native('''import forge
def on_start():
    wall=forge.spawn({'id':'wall','kind':'empty','collider':[.1,10,10]})
    bullet=forge.spawn({'id':'fast','kind':'empty','position':[-5,0,0],'velocity':[1200,0,0],'dynamic':True,'collider':[.5,.5,.5],'rigid_body':{'shape':'sphere','ccd':True}})
    forge.physics_step(1/60)
    assert bullet.position[0]<0,bullet.position
    assert bullet.position[0]>-1,bullet.position
    forge.log('CCD_OK');forge.quit()
'''))

    def test_filters_triggers_bounce_and_sleeping(self):
        self.assertIn('CONTACTS_OK', self.native('''import forge
from physics import RigidBody
def on_start():
    a=forge.spawn({'id':'filtered_a','kind':'empty','collider':[2,2,2],'rigid_body':{'group':2,'mask':2}})
    b=forge.spawn({'id':'filtered_b','kind':'empty','collider':[2,2,2],'rigid_body':{'group':4,'mask':4}})
    assert not forge.overlaps(a,b)
    assert forge.raycast_hit((0,0,3),(0,0,-1),10,mask=2)['entity']=='filtered_a'
    a.destroy();b.destroy()
    trigger=forge.spawn({'id':'trigger','kind':'empty','trigger':True,'collider':[2,2,2]})
    body=forge.spawn({'id':'inside','kind':'empty','dynamic':True,'collider':[.5,.5,.5]})
    assert forge.overlaps(trigger,body)
    assert forge.raycast_hit((0,0,3),(0,0,-1),10,ignore='inside',include_triggers=False) is None
    forge.physics_step(.1);assert tuple(body.position)==(0,0,0)
    trigger.destroy();body.destroy()
    floor=forge.spawn({'kind':'empty','position':[0,-.5,0],'collider':[20,1,20],'rigid_body':{'restitution':1,'friction':1}})
    ball=forge.spawn({'kind':'empty','position':[0,3,0],'dynamic':True,'collider':[1,1,1],'rigid_body':{'shape':'sphere','restitution':1}})
    forge.set_gravity((0,-10,0));bounced=False
    for _ in range(100):
        forge.physics_step(.02)
        if ball.velocity[1]>2:bounced=True
    assert bounced
    ball.destroy()
    box=forge.spawn({'kind':'empty','position':[0,2,0],'velocity':[3,0,0],'dynamic':True,'collider':[1,1,1],'rigid_body':{'friction':1}})
    for _ in range(150):forge.physics_step(.05)
    assert abs(box.velocity[0])<.05 and abs(box.position[1]-.5)<.03,(box.position,box.velocity)
    assert RigidBody(box).sleeping
    forge.set_gravity((0,10,0));forge.physics_step(.1)
    assert box.velocity[1]>.5 and not RigidBody(box).sleeping
    RigidBody(box).impulse((1,0,0));assert not RigidBody(box).sleeping
    forge.log('CONTACTS_OK');forge.quit()
'''))

    def test_capsule_character_can_land_and_slide_on_rotated_ramp(self):
        self.assertIn('CAPSULE_OK', self.native('''import forge
from character import CharacterController
def on_start():
    ramp=forge.spawn({'id':'ramp','kind':'empty','rotation':[0,0,30],'collider':[10,.3,10],'rigid_body':{'group':4,'mask':4}})
    body=forge.spawn({'kind':'empty','position':[0,4,0],'collider':[1,2,1],'rigid_body':{'shape':'capsule','group':4,'mask':4}})
    controller=CharacterController(body,automatic=False)
    landed=controller.move((0,-3,0));assert landed['grounded'],landed
    assert landed['hits'][0]['normal'][1]>.8 and body.position[1]>.8,landed
    previous=body.position;result=controller.move((2,-.1,0));assert body.position[0]>previous[0]+1
    assert body.position[1]>previous[1],(previous,body.position,result)
    assert forge.rigid_body_info(body)['kinematic']
    controller.close();forge.log('CAPSULE_OK');forge.quit()
'''))

    def test_invalid_parameters_and_world_step_are_atomic(self):
        self.assertIn('VALIDATION_OK', self.native('''import forge
def reject(callback):
    try:callback()
    except (RuntimeError,TypeError):return
    raise AssertionError('bad data accepted')
def on_start():
    body=forge.spawn({'id':'valid','kind':'empty','dynamic':True,'collider':[1,1,1]})
    for field,value in [('mass',1e-20),('position',(1e8,0,0)),('velocity',(1e8,0,0)),('angular_velocity',(1e8,0,0)),('collider',(.000001,1,1))]:
        before=getattr(body,field);reject(lambda:setattr(body,field,value));assert getattr(body,field)==before
    before=forge.rigid_body_settings(body);reject(lambda:forge.set_rigid_body(body,{'friction':float('nan')}));assert forge.rigid_body_settings(body)==before
    reject(lambda:forge.spawn({'id':'bad','dynamic':True,'mass':1e-20,'collider':[1,1,1]}));assert forge.find('bad') is None
    reject(lambda:forge.configure_physics({'backend':'bullet','iterations':0}));assert forge.physics_settings()['backend']=='bullet'
    weak=forge.spawn({'id':'weak','kind':'empty','position':[10,0,0],'dynamic':True,'mass':1e-6,'collider':[1,1,1]})
    before=[(e.position,e.velocity,e.rotation) for e in (body,weak)]
    forge.apply_force(weak,(1e6,0,0));reject(lambda:forge.physics_step(1))
    assert before==[(e.position,e.velocity,e.rotation) for e in (body,weak)]
    forge.apply_force(weak,(-1e6,0,0));forge.physics_step(.01)
    assert all(abs(v)<1 for v in weak.velocity)
    body.destroy();weak.destroy();assert forge.physics_stats()['bodies']==0
    forge.log('VALIDATION_OK');forge.quit()
'''))

    def test_runtime_force_covers_all_fixed_substeps(self):
        self.assertIn('FRAME_FORCE_OK',self.native('''import forge
frames=0
def on_start():
    global body
    forge.set_physics_enabled(True)
    body=forge.spawn({'id':'pushed','kind':'empty','dynamic':True,'collider':[1,1,1]})
def on_update(dt):
    global frames
    if frames==60:
        assert 9.9<body.velocity[0]<10.1,body.velocity
        forge.log('FRAME_FORCE_OK');forge.quit()
    forge.apply_force(body,(10,0,0));frames+=1
'''))

    def test_validate_checks_declarative_physics_and_emitter_limits(self):
        cases=[
            ({'physics':{'iterations':0}},'iterations'),
            ({'gravity':[1e8,0,0]},'gravity'),
            ({'mode':'2d'},'3D'),
            ({'entities':[{'dynamic':True,'mass':1e-20,'collider':[1,1,1]}]},'mass'),
            ({'entities':[{'collider':[1,1,1],'rigid_body':{'friction':-1}}]},'friction'),
            ({'entities':[{'collider':[2,1,2],'rigid_body':{'shape':'capsule'}}]},'capsule'),
            ({'physics':{'max_bodies':1},'entities':[{'collider':[1,1,1]},{'collider':[1,1,1]}]},'max_bodies'),
            ({'emitters':[{'max_particles':60000},{'max_particles':60000}]},'capacity'),
            ({'emitters':[{'lifetime':[0,1]}]},'lifetime'),
        ]
        for values,error in cases:
            scene={'mode':'3d','physics':{'backend':'bullet'}}
            if 'physics' in values:scene['physics'].update(values['physics'])
            scene.update({k:v for k,v in values.items() if k!='physics'})
            with self.subTest(values=values):
                (self.root/'scenes/invalid.json').write_text(json.dumps(scene))
                self.assertIn(error,self.run_engine('validate',expected=1))

    def test_collision_callback_force_is_queued_for_next_frame(self):
        (self.root/'scripts/contact_force.py').write_text('''import forge
class Behavior:
    def __init__(self, entity, context): self.entity=entity
    def on_collision(self, other):
        if other.id=='sensor':
            forge.apply_force(self.entity,(12,0,0))
            forge.log('CONTACT_FORCE_QUEUED')
''')
        self.assertIn('CONTACT_FORCE_OK',self.native('''import forge
frames=0
def on_start():
    global body
    forge.set_physics_enabled(True)
    forge.spawn({'id':'sensor','kind':'empty','trigger':True,'collider':[10,10,10]})
    body=forge.spawn({'id':'body','kind':'empty','dynamic':True,'collider':[1,1,1],'scripts':['contact_force.py']})
def on_update(dt):
    global frames
    if frames==1: assert abs(body.velocity[0])<1e-5,body.velocity
    if frames==2: assert abs(body.velocity[0]-.2)<.001,body.velocity
    if frames==3:
        assert abs(body.velocity[0]-.2)<.001,body.velocity
        forge.log('CONTACT_FORCE_OK');forge.quit()
    frames+=1
'''))

    def test_particles_are_native_bounded_and_seeded(self):
        self.assertIn('PARTICLE_POOL_OK',self.native('''import forge
from particles import Emitter
def on_start():
    count=len(forge.entities())
    settings={'shape':'sphere','radius':2,'seed':123,'rate':0,'burst':200,'max_particles':128,'velocity_random':[2,3,4],'lifetime':[1,2]}
    a=Emitter(settings);b=Emitter(settings)
    assert a.info['alive']==128 and a.info['dropped']==72
    assert a.snapshot(128)==b.snapshot(128)
    assert len(forge.entities())==count and forge.particle_stats()['alive']==256
    assert a.burst(2)==0 and a.info['dropped']==74
    for p in a.snapshot(128):assert sum(v*v for v in p['position'])<=4.001 and 1<=p['lifetime']<=2
    before=forge.particle_stats()
    try:Emitter(max_particles=100000)
    except RuntimeError:pass
    else:raise AssertionError('unbounded pool')
    assert forge.particle_stats()==before
    a.close();a.close();b.close();assert forge.particle_stats()['alive']==0
    forge.log('PARTICLE_POOL_OK');forge.quit()
''',rigid=False))

    def test_particle_motion_curves_duration_follow_and_lifetime(self):
        self.assertIn('PARTICLE_LIFE_OK',self.native('''import forge
from particles import Emitter
def on_start():
    owner=forge.spawn({'id':'owner','kind':'empty','position':[10,0,0]})
    local=Emitter(rate=0,burst=1,follow='owner',space='local',velocity=[0,0,0],gravity=[0,-2,0],lifetime=[1,1],size=[2,0],color_start=[1,0,0,1],color_end=[0,1,0,0])
    world=Emitter(rate=0,burst=1,follow='owner',velocity=[0,0,0],lifetime=[1,1])
    owner.position=(20,0,0);forge.particle_step(.5)
    p=local.snapshot()[0]
    assert abs(p['world_position'][0]-20)<1e-5 and abs(world.snapshot()[0]['world_position'][0]-10)<1e-5
    assert abs(p['position'][1]+.5)<1e-5 and p['color']==[.5,.5,0,.5] and abs(p['size']-1)<1e-5
    forge.particle_step(.5);assert local.info['alive']==0 and world.info['alive']==0
    finite=Emitter(rate=10,duration=.2,lifetime=[2,2]);loop=Emitter(rate=10,duration=.2,loop=True,lifetime=[2,2])
    for _ in range(5):forge.particle_step(.1)
    assert finite.info['alive']==2 and loop.info['alive']==5,(finite.info,loop.info)
    loop.stop();forge.particle_step(.1);assert loop.info['alive']==5
    loop.start();forge.particle_step(.1);assert loop.info['alive']==6
    finite.close();loop.close();local.close();world.close()
    forge.log('PARTICLE_LIFE_OK');forge.quit()
''',rigid=False))

    def test_particle_validation_scene_serialization_and_transition(self):
        (self.root/'scenes/empty.json').write_text(json.dumps({'mode':'2d','entities':[]}))
        self.assertIn('TRANSITION_OK',self.native('''import forge
from particles import Emitter
old=None
def on_start():
    global old
    for settings in [{'rate':float('nan')},{'lifetime':[0,1]},{'max_particles':0},{'velocity':[1,2]},{'blend':'bad'},{'texture':'../missing.png'}]:
        try:Emitter(settings)
        except RuntimeError:pass
        else:raise AssertionError(settings)
    assert forge.particle_stats()['emitters']==0
    old=Emitter(rate=0,burst=3)
    data=forge.scene_data();assert data['emitters'][0]['burst']==3 and 'live' not in data['emitters'][0]
    forge.change_scene('empty.json')
def on_destroy():
    assert old.info['alive']==3
    forge.log('TRANSITION_OK')
''',rigid=False))

    def test_failed_reload_preserves_bodies_emitters_and_recovers(self):
        self.config['physics']={'backend':'bullet'}
        self.config['stage']=0
        self.script_scene('''import forge
from particles import Emitter
from pathlib import Path
stage=forge.settings()['stage']
def build():return {'mode':'3d','gravity':[0,0,0]}
def on_start():
    global emitter,body
    emitter=Emitter(rate=0,burst=3,lifetime=[100,100],velocity=[0,0,0],seed=3)
    body=forge.spawn({'id':'body','kind':'empty','dynamic':True,'collider':[1,1,1]})
    forge.apply_impulse(body,(0,0,0))
    if stage==1:raise RuntimeError('candidate rejected')
    forge.log('STAGE_'+str(stage))
def on_reload_failed(error):
    assert emitter.info['alive']==3 and forge.physics_stats()['bodies']==1
    assert forge.find('body') is body
    forge.log('ROLLBACK_NATIVE_OK')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
''')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--frames','20000','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                if (self.root/'forge.log').exists() and marker in (self.root/'forge.log').read_text():return
                if process.poll() is not None:self.fail((self.root/'forge.log').read_text())
                time.sleep(.03)
            self.fail('Timeout '+marker)
        try:
            wait('STAGE_0');self.config['stage']=1;self.write_config();wait('ROLLBACK_NATIVE_OK')
            self.config['stage']=2;self.write_config();wait('STAGE_2')
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()


if __name__=='__main__':unittest.main(verbosity=2)
