"""Public native regression coverage for dispatch, prefabs and animation authoring."""
import json, subprocess, time, unittest
import integration as base
from model_fixture import authoring_triangle
ROOT,ENGINE=base.ROOT,base.ENGINE
class AuthoringTests(unittest.TestCase):
    setUp,tearDown=base.EngineTests.setUp,base.EngineTests.tearDown
    write_config,script_scene,run_engine=base.EngineTests.write_config,base.EngineTests.script_scene,base.EngineTests.run_engine
    def test_builtin_authoring_example_uses_public_api(self):
        self.assertIn('Animation example:',self.run_engine(frames=12,extra=('--scene','authoring.py')))

    def model(self):
        authoring_triangle(self.root/'models/authoring.gltf')
        authoring_triangle(self.root/'models/target.gltf',joint='TargetJoint',rest=2)
    def test_linear_listener_dispatch_and_mutation(self):
        self.script_scene('''import forge
calls=[];frame=0
def on_start():
    global ids,first,second
    first=forge.on_frame(remove)
    second=forge.on_frame(lambda dt:calls.append('removed'))
    ids=[forge.on_frame(lambda dt,i=i:calls.append(i)) for i in range(5000)]
def remove(dt):
    forge.remove_listener(first);forge.remove_listener(second)
    forge.on_frame(lambda dt:calls.append('new'))
def on_update(dt):
    global frame
    frame+=1
    assert 'removed' not in calls
    if frame==1:
        assert 'new' not in calls and len(calls)==5000
        stats=forge.profile();assert stats['listener_checks']==5002 and stats['listener_calls']==5001,stats
        calls.clear()
    else:
        assert len(calls)==5001 and calls[-1]=='new';forge.log('DISPATCH_OK');forge.quit()
''')
        self.assertIn('DISPATCH_OK',self.run_engine())
    def test_prefabs_inheritance_independence_and_atomic_failure(self):
        prefab={'entities':[{'id':'root','kind':'empty','position':[1,0,0],'data':{'items':[1]}},{'id':'child','parent':'root','kind':'empty','position':[2,0,0]}]}
        (self.root/'objects/group.json').write_text(json.dumps(prefab))
        (self.root/'objects/derived.json').write_text(json.dumps({'extends':'group.json','label':'derived'}))
        (self.root/'objects/bad.json').write_text(json.dumps({'entities':[{'id':'root','kind':'empty'},{'id':'child','parent':'root','kind':'empty','position':[1e30,0,0]}]}))
        self.script_scene('''import forge
from prefabs import Prefab
def build():return {'mode':'3d','physics_enabled':False}
def on_start():
    from pathlib import Path
    Path(forge.project_path('objects/cycle.json')).write_text('{"extends":"cycle.json"}')
    a=Prefab('derived.json').instantiate(prefix='a_',position=(10,0,0));b=Prefab('group.json').instantiate()
    assert a['child'].parent==a.root and tuple(a['child'].world_position)==(13,0,0)
    data=a.root.data;data['items'].append(2);a.root.data=data;assert b.root.data['items']==[1]
    attached=Prefab('group.json').instantiate(parent=a['child']);assert attached.root.parent==a['child']
    before=forge.world_stats()['entities']
    for file,options in [('cycle.json',{}),('group.json',{'prefix':'a_'}),('group.json',{'overrides':{'child':{'position':[float('nan'),0,0]}}}),('bad.json',{'overrides':{'root':{'scale':[1e30,1,1]}}})]:
        try:Prefab(file).instantiate(**options)
        except (RuntimeError,ValueError):pass
        else:raise AssertionError('bad prefab accepted')
        assert forge.world_stats()['entities']==before
    a.destroy();assert not a['child'].alive and not attached.root.alive and b.root.alive
    forge.log('PREFABS_OK');forge.quit()
''')
        self.assertIn('PREFABS_OK',self.run_engine())
    def test_animation_blend_transition_events_pause_seek_and_failure(self):
        self.model()
        self.script_scene('''import forge,math
from animation import Animator
def build():return {'mode':'3d','physics_enabled':False}
def x(a):return a.pose[-1][3]
def close(a,b):assert abs(a-b)<.001,(a,b)
def on_start():
    e=forge.spawn({'kind':'mesh','model':'authoring.gltf'})
    a=Animator(e,layers=[{'clip':'Move','weight':.5,'time':.5},{'clip':'Idle','weight':.5}],auto_update=False)
    close(x(a),.25)
    a.configure(layers=[{'clip':'Move','loop':False,'events':[{'name':'mid','time':.5},{'name':'end','time':1}]}],auto_update=False)
    a.update(.6);close(x(a),.6);assert [e['name'] for e in a.events()]==['mid']
    a.update(.5);assert [e.get('name',e['type']) for e in a.events()]==['end','finished'];close(x(a),1)
    a.seek(.2).pause();before=a.pose;a.update(.5);assert a.pose==before
    a.play().crossfade('Idle',1,loop=False);before=a.pose
    a.update(.25);close(x(a),.45*.75)
    current=a.pose;a.crossfade('Raise',1);assert a.pose==current
    a.update(.5);assert 0<x(a)<.4
    before=a.info;pose=a.pose
    for settings in ({'layers':[{'clip':'missing'}]},{'layers':[{'clip':'Move','speed':float('inf')}]},{'layers':[{'clip':'Move','events':[{'name':'bad','time':2}]}]}):
        try:forge.configure_animator(e,settings)
        except (RuntimeError,ValueError):pass
        else:raise AssertionError('bad animation accepted')
        assert a.info==before and a.pose==pose
    a.configure(layers=[{'clip':'Move','loop':True,'events':[{'name':'mid','time':.5}]}],auto_update=False)
    a.update(2.6);assert len(a.events())==3
    a.configure(layers=[{'clip':'Move','speed':-1,'events':[{'name':'reverse','time':.5}]}],auto_update=False)
    a.update(.6);close(x(a),.4);assert a.events()[0]['name']=='reverse'
    a.update(1);close(x(a),.4);assert len(a.events())==1
    a.configure(layers=[{'clip':'Move','time':1e6,'speed':1e6,'events':[{'name':'mid','time':.5}]}],auto_update=False)
    before=a.info
    try:a.update(10)
    except RuntimeError:pass
    else:raise AssertionError('event budget accepted')
    assert a.info==before
    forge.log('ANIMATION_OK');forge.quit()
''')
        self.assertIn('ANIMATION_OK',self.run_engine())
    def test_retarget_and_morph_contracts(self):
        self.model()
        self.script_scene('''import forge
from animation import Animator
def build():return {'mode':'3d','physics_enabled':False}
def on_start():
    target=forge.spawn({'kind':'mesh','model':'target.gltf'})
    a=Animator(target,layers=[{'model':'authoring.gltf','clip':'Move','time':.5,'mapping':{'Joint':'TargetJoint'},'translation_scale':2}],auto_update=False)
    assert abs(a.pose[-1][3]-3)<.001,a.pose
    model=forge.model_info('authoring.gltf');assert model['morph_targets'][0]['name']=='Smile',model
    zero=forge.morph_vertices('authoring.gltf',weights={'Smile':0});one=forge.morph_vertices('authoring.gltf',weights={'Smile':1})
    assert any(abs(a[1]-b[1]-1)<.001 for a,b in zip(one,zero)),(zero,one)
    other=forge.spawn({'kind':'mesh','model':'authoring.gltf','morph_weights':{'Smile':.7},'animator':{'layers':[{'clip':'Move','time':.5}],'auto_update':False}})
    assert forge.morph_weights(other)=={'Smile':.7} and forge.animator_info(other)['configured']
    before=forge.morph_weights(other)
    for weights in ({'missing':1},{'Smile':float('nan')},{'Smile':11}):
        try:forge.set_morph_weights(other,weights)
        except (RuntimeError,ValueError):pass
        else:raise AssertionError('bad morph accepted')
        assert forge.morph_weights(other)==before
    state=forge.scene_data();data=next(e for e in state['entities'] if e['id']==other.id)
    assert data['morph_weights']==before and data['animator']['layers'][0]['clip']=='Move'
    forge.log('RETARGET_MORPH_OK');forge.quit()
''')
        self.assertIn('RETARGET_MORPH_OK',self.run_engine())
    def test_authored_bone_clips_and_invalid_track_rollback(self):
        self.model()
        self.script_scene("""import forge
from animation import Animator
def on_start():
    e=forge.spawn({'kind':'mesh','model':'authoring.gltf'})
    layer={'clip':'Custom','duration':2,'loop':False,'tracks':[{'node':'Joint','position':[{'time':0,'value':[0,0,0]},{'time':2,'value':[2,0,0]}],'rotation':[{'time':0,'value':[0,0,0]},{'time':2,'value':[0,0,90]}]}]}
    a=Animator(e,layers=[layer],auto_update=False);a.update(1)
    pose=a.pose[-1];assert abs(pose[3]-1)<.001 and abs(pose[0]-.7071)<.001,pose
    a.seek(2);assert abs(a.pose[-1][3]-2)<.001
    before=a.pose
    bad={'clip':'Broken','duration':2,'tracks':[{'node':'Joint','position':[{'time':2,'value':[0,0,0]},{'time':1,'value':[0,0,0]}]}]}
    try:a.configure(layers=[bad])
    except RuntimeError:pass
    else:raise AssertionError('unordered bone keys accepted')
    assert a.pose==before
    forge.log('AUTHORED_CLIP_OK');forge.quit()
""")
        self.assertIn('AUTHORED_CLIP_OK',self.run_engine())

    def test_property_timeline_and_animation_state_machine(self):
        self.model()
        self.script_scene('''import forge
from animation import AnimationClip,Animator,AnimationStateMachine
def on_start():
    e=forge.spawn({'kind':'empty'})
    clip=AnimationClip({'position':[(0,(0,0,0)),(1,(10,20,0))],'visible':{'interpolation':'step','keys':[(0,True),(1,False)]}},events=[{'name':'half','time':.5}])
    t=clip.play(e,loop=False);t.update(.5);assert tuple(e.position)==(5,10,0) and e.visible
    assert t.events()[0]['name']=='half'
    t.pause().update(.25);assert tuple(e.position)==(5,10,0)
    t.play().update(.5);assert not e.visible and t.finished and t.events()[0]['type']=='finished'
    t.seek(.2);assert tuple(e.position)==(2,4,0) and e.visible
    reverse=clip.play(e,speed=-1);reverse.update(.6)
    assert all(abs(x-y)<.001 for x,y in zip(e.position,(4,8,0))) and reverse.events()[0]['name']=='half'
    e=forge.spawn({'kind':'mesh','model':'authoring.gltf'})
    a=Animator(e,'Idle',auto_update=False)
    sm=AnimationStateMachine(a,{'idle':{'clip':'Idle'},'walk':{'clip':'Move'},'retarget':{'layer':{'model':'target.gltf','clip':'Raise','mapping':{'TargetJoint':'Joint'}}}},[{'from':'idle','to':'walk','when':{'walking':True}},{'from':'walk','to':'retarget','when':{'jump':{'trigger':True}},'exit_time':.5}])
    sm.set('walking',True);assert sm.update(.1) and sm.state=='walk'
    sm.trigger('jump');assert not sm.update(.1);assert sm.update(.4) and sm.state=='retarget' and not sm.triggers
    assert len(sm.events())==3
    forge.log('TIMELINE_MACHINE_OK');forge.quit()
''')
        self.assertIn('TIMELINE_MACHINE_OK',self.run_engine())
    def test_background_watch_includes_changed_python_path_imports(self):
        extra=self.root/'extra';extra.mkdir();(extra/'outside.py').write_text("value='old'\n")
        self.config['python_paths']=['extra'];self.config['development']={'watch_interval':.05}
        self.script_scene('''import forge,outside
from pathlib import Path
def on_start():forge.log('WATCH_'+outside.value)
def on_update(dt):
    if outside.value=='new':forge.quit()
''')
        p=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--frames','10000','--no-open-log'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        try:
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and 'WATCH_old' in log.read_text():break
                time.sleep(.05)
            else:self.fail('watch startup missing')
            (extra/'outside.py').write_text("value='new'\n")
            output=p.communicate(timeout=15)[0]
            self.assertEqual(p.returncode,0,output);self.assertIn('WATCH_new',output)
        finally:
            if p.poll() is None:p.kill();p.communicate()

if __name__=='__main__':unittest.main()
