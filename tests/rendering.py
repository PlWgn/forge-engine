"""Real native contracts for hierarchy, geometry, materials and persistence snapshots."""
import json,subprocess,time,unittest
import integration as base
ROOT,ENGINE=base.ROOT,base.ENGINE
class RenderingTests(unittest.TestCase):
    setUp,tearDown=base.EngineTests.setUp,base.EngineTests.tearDown
    write_config,script_scene,run_engine=base.EngineTests.write_config,base.EngineTests.script_scene,base.EngineTests.run_engine
    def test_render_optimization_configuration_and_entity_roundtrip(self):
        self.script_scene("""import forge
from engine_api import require_api
def on_start():
    require_api(1,'lod','occlusion_culling')
    initial=forge.render_settings()
    for bad in ({'enabled':1},{'occlusion':'yes'},{'grid_width':0},{'grid_height':257},{'max_occluders':1.5}):
        try:forge.set_render_optimization(bad)
        except RuntimeError:pass
        else:raise AssertionError('Invalid optimization accepted')
        assert forge.render_settings()==initial
    definition={'max_distance':50,'bounds':{'min':[-1,-1,-1],'max':[1,1,1]},'vendor':{'quality':'adaptive'}}
    e=forge.spawn({'id':'optimized','kind':'cube','optimization':definition})
    snapshot=e.optimization;snapshot['vendor']['quality']='mutated'
    assert e.optimization==definition
    for bad in ({'max_distance':-1},{'bounds':{'min':[1,0,0],'max':[0,1,1]}},{'occluder':{'min':[0,0,0],'max':[1,1,0]}},{'levels':[{'distance':5,'texture':'missing.png'}]},{'levels':[{'distance':5,'model':'cube.obj'}]}):
        try:e.optimization=bad
        except RuntimeError:pass
        else:raise AssertionError('Invalid entity optimization accepted')
        assert e.optimization==definition
    forge.set_render_optimization({'enabled':True,'occlusion':True,'vendor':{'tag':1}})
    data=forge.scene_data()
    assert next(x for x in data['entities'] if x['id']=='optimized')['optimization']==definition
    assert data['rendering']['optimization']['vendor']=={'tag':1}
    e.optimization={};assert 'optimization' not in next(x for x in forge.scene_data()['entities'] if x['id']=='optimized')
    forge.log('OPTIMIZATION_API_OK');forge.quit()
""")
        self.assertIn('OPTIMIZATION_API_OK',self.run_engine())
        for bad in ({'bounds':{'min':[0,0],'max':[1,1,1]}},{'levels':[{'distance':0,'texture':'icon.png'}]}):
            (self.root/'objects/invalid-optimization.json').write_text(json.dumps({'optimization':bad}),encoding='utf-8')
            self.assertIn('optimization',self.run_engine('validate',expected=1).lower())
        (self.root/'objects/invalid-optimization.json').unlink()
    def test_world_position_rejects_destroyed_children(self):
        self.script_scene("""import forge
def on_start():
    parent=forge.spawn({'id':'parent','kind':'empty'})
    child=forge.spawn({'id':'child','kind':'empty','parent':'parent','position':[1,0,0]})
    parent.destroy()
    before=tuple(child.position)
    try:child.world_position=(2,0,0)
    except RuntimeError as error:assert 'current world' in str(error)
    else:raise AssertionError('Destroyed child accepted a world-position update')
    assert tuple(child.position)==before and not child.alive
    forge.log('RETIRED_CHILD_SAFE');forge.quit()
""")
        self.assertIn('RETIRED_CHILD_SAFE',self.run_engine())
    def test_graphics_backend_configuration_and_capabilities(self):
        self.script_scene("""import forge
def on_start():
    available=forge.graphics_backends()
    assert available[0]=='opengl'
    assert available==forge.capabilities()['graphics_backends']
    assert 'renderer_backends' in forge.capabilities()['features']
    forge.log('BACKEND_API_OK');forge.quit()
""")
        for backend in ('opengl','auto','direct3d11','metal'):
            self.config['renderer']['backend']=backend;self.write_config()
            self.assertIn('BACKEND_API_OK',self.run_engine())  # Headless needs no device.
        original=json.loads(json.dumps(self.config['renderer']))
        invalid=[('backend',None),('backend',True),('backend','direct3d12'),
                 ('direct3d11',[]),('direct3d11',{'driver':'unknown'}),
                 ('direct3d11',{'debug':1}),('direct3d11',{'shaders':[]}),
                 ('direct3d11',{'shaders':{'scene':{'vertex':'missing.hlsl'}}}),
                 ('direct3d11',{'shaders':{'scene':{'vertex':'missing.hlsl','fragment':'missing.hlsl'}}})]
        for field,value in invalid:
            with self.subTest(field=field,value=value):
                self.config['renderer']=dict(original,**{field:value});self.write_config()
                self.assertIn('renderer' if field=='backend' else 'shader' if isinstance(value,dict) and 'shaders' in value else 'direct3d11',
                              self.run_engine('validate',expected=1).lower())
        self.config['renderer']=original
        self.config['renderer']['direct3d11']={'shaders':{'scene':{
            'vertex':'direct3d11/unlit.vert.hlsl','fragment':'direct3d11/unlit.frag.hlsl'}}}
        self.write_config();self.run_engine('validate')
        self.config['renderer']['direct3d11']['shaders']['scene']['vertex']='../../outside.hlsl'
        self.write_config();self.assertIn('escapes',self.run_engine('validate',expected=1))
    def test_metal_shader_configuration_validation(self):
        self.script_scene("def on_start():pass\n")
        self.config['renderer']['metal']={'shaders':{'scene':{
            'vertex':'metal/unlit.vert.metal','fragment':'metal/unlit.frag.metal'}}}
        self.write_config();self.run_engine('validate')
        original=json.loads(json.dumps(self.config['renderer']['metal']))
        for value in ([],{'uniform_budget_bytes':0},{'uniform_budget_bytes':True},{'uniform_budget_bytes':1.5},{'uniform_budget_bytes':536870913},{'shaders':[]},{'shaders':{'scene':{}}},
                      {'shaders':{'scene':{'vertex':'missing.metal','fragment':'missing.metal'}}},
                      {'shaders':{'scene':dict(original['shaders']['scene'],vertex_entry=1)}}):
            self.config['renderer']['metal']=value;self.write_config()
            self.run_engine('validate',expected=1)
        self.config['renderer']['metal']=original
        self.config['renderer']['metal']['shaders']['scene']['vertex']='../../outside.metal'
        self.write_config();self.assertIn('escapes',self.run_engine('validate',expected=1))
    def test_incremental_and_atomic_bulk_transforms(self):
        self.script_scene("""import forge
def build():return {'mode':'3d','physics_enabled':False,'entities':[{'id':'e'+str(i),'kind':'empty'} for i in range(2000)]+[{'id':'child','kind':'empty','parent':'e0','position':[1,0,0]}]}
def on_start():
    items=[forge.find('e'+str(i)) for i in range(2000)]
    before=forge.world_stats()
    for i,e in enumerate(items):e.position=(i,1,0)
    after=forge.world_stats()
    assert after['transform_computations']-before['transform_computations']==2001,(before,after)
    assert after['transform_audits']==before['transform_audits'],(before,after)
    child=forge.find('child');assert tuple(child.world_position)==(1,1,0)
    forge.set_positions([(items[0],(10,0,0)),(child,(2,0,0)),(items[-1],(20,0,0))])
    assert tuple(child.world_position)==(12,0,0) and tuple(items[-1].position)==(20,0,0)
    old=[e.position for e in (items[0],child,items[-1])]
    for updates in ([(items[0],(1,0,0)),(items[0],(2,0,0))],[(items[0],(1,0,0)),(child,(float('nan'),0,0))]):
        try:forge.set_positions(updates)
        except (RuntimeError,ValueError):pass
        else:raise AssertionError('invalid bulk transform accepted')
        assert old==[e.position for e in (items[0],child,items[-1])]
    items[0].scale=(1e30,1,1);old_child=child.position;old_root=items[0].position
    try:forge.set_positions([(items[0],(0,0,0)),(child,(1e30,0,0))])
    except RuntimeError:pass
    else:raise AssertionError('bulk world overflow accepted')
    assert items[0].position==old_root and child.position==old_child
    child.destroy();replacement=forge.spawn({'id':'child','kind':'empty','parent':'e1'})
    items[1].position=(4,0,0);assert tuple(replacement.world_position)==(4,0,0)
    forge.log('INCREMENTAL_OK');forge.quit()
""")
        self.assertIn('INCREMENTAL_OK',self.run_engine())

    def test_direct_python_bridge_preserves_json_contract_and_copies(self):
        self.script_scene("""import forge,math,json
from pathlib import Path
class Named(str):pass
def on_start():
    e=forge.spawn({'kind':'empty'})
    payload={'unicode':'Привет 😀','values':[None,True,False,-42,2**63-1,.25,{'nested':[1,2]}],'tuple':(1,2)}
    e.data=payload;payload['values'][-1]['nested'].append(3)
    snapshot=e.data;assert snapshot['tuple']==[1,2] and snapshot['values'][-1]['nested']==[1,2]
    snapshot['values'][-1]['nested'].append(4);assert e.data['values'][-1]['nested']==[1,2]
    e.data={1:'one',False:'false',None:'null',1.5:'fraction',Named('name'):Named('value')}
    assert e.data=={'1':'one','false':'false','null':'null','1.5':'fraction','name':'value'}
    e.data={'big':2**64-1};assert e.data['big']==2**64-1
    e.data={'big':2**100};assert math.isfinite(e.data['big'])
    before=e.data;cycle=[];cycle.append(cycle)
    for invalid in (cycle,{'v':float('nan')},{'v':float('inf')},{'v':object()},{'v':b'bytes'}):
        try:e.data=invalid
        except (RuntimeError,ValueError,TypeError,OverflowError):pass
        else:raise AssertionError('invalid JSON accepted')
        assert e.data==before
    e.data={'shared':[payload,payload]};assert e.data['shared'][0]==e.data['shared'][1]
    Path(forge.project_path('saves/deep.json')).parent.mkdir(exist_ok=True)
    encoded='['*2000+'0'+']'*2000
    Path(forge.project_path('saves/deep.json')).write_text(encoded)
    try:json.loads(encoded)
    except RecursionError:
        try:forge.load('deep')
        except RecursionError:pass
        else:raise AssertionError('decoder recursion contract changed')
    else:
        decoded=forge.load('deep')
        for _ in range(2000):decoded=decoded[0]
        assert decoded==0
    forge.log('BRIDGE_OK');forge.quit()
""")
        self.assertIn('BRIDGE_OK',self.run_engine())

    def test_hierarchy_local_world_reparent_and_cycles(self):
        self.script_scene('''import forge,math
def build(): return {'mode':'3d','physics_enabled':False,'entities':[
    {'id':'child','kind':'empty','parent':'root','position':[1,0,0]},
    {'id':'root','kind':'empty','position':[10,0,0],'rotation':[0,0,90],'scale':[2,2,2]}]}
def close(a,b):assert all(abs(x-y)<.001 for x,y in zip(a,b)),(a,b)
def on_start():
    root,child=forge.find('root'),forge.find('child')
    close(child.local_position,(1,0,0));close(child.world_position,(10,2,0))
    child.world_position=(10,4,0);close(child.position,(2,0,0))
    root.position=(12,0,0);close(child.world_position,(12,4,0))
    before=child.world_matrix;child.set_parent(None,keep_world=True)
    close(child.world_position,(12,4,0));close(child.world_rotation,(0,0,90))
    for row,old in zip(child.world_matrix,before):close(row,old)
    child.set_parent(root,keep_world=True);close(child.position,(2,0,0))
    try:root.set_parent(child)
    except RuntimeError:pass
    else:raise AssertionError('cycle accepted')
    assert root.parent is None and child.parent is root
    try:child.set_parent('missing')
    except RuntimeError:pass
    else:raise AssertionError('missing parent accepted')
    assert child.parent is root and root.children()==[child]
    saved=forge.scene_data();assert next(e for e in saved['entities'] if e['id']=='child')['parent']=='root'
    root.destroy(children=False);assert child.alive and child.parent is None
    close(child.world_position,(12,4,0));forge.log('HIERARCHY_OK');forge.quit()
''')
        self.assertIn('HIERARCHY_OK',self.run_engine())
    def test_hierarchy_rejects_bad_graphs_and_restores_transform_mutations(self):
        for entities in ([{'id':'a','parent':'missing'}], [{'id':'a','parent':'b'},{'id':'b','parent':'a'}], [{'id':'a','screen':True},{'id':'b','parent':'a'}]):
            (self.root/'scenes/invalid.json').write_text(json.dumps({'entities':entities}))
            self.run_engine('validate',expected=1)
        (self.root/'scenes/invalid.json').unlink()
        self.script_scene("""import forge
def build():return {'mode':'3d','physics_enabled':False,'entities':[{'id':'root','kind':'empty','scale':[2,1,1]}, {'id':'child','kind':'empty','parent':'root','rotation':[0,0,45]}, {'id':'plain','kind':'empty','parent':'root'}]}
def on_start():
    root,child,plain=[forge.find(x) for x in ('root','child','plain')]
    before=child.world_matrix
    try:child.set_parent(None,keep_world=True)
    except RuntimeError:pass
    else:raise AssertionError('sheared TRS accepted')
    assert child.parent is root and child.world_matrix==before
    try:root.destroy(children=False)
    except RuntimeError:pass
    else:raise AssertionError('failed detach destroyed subtree')
    assert root.alive and child.parent is root and plain.parent is root
    try:child.dynamic=True
    except RuntimeError:pass
    else:raise AssertionError('dynamic child setter accepted')
    assert not child.dynamic
    plain.scale=(1e30,1,1);old=root.scale
    try:root.scale=(1e30,1,1)
    except RuntimeError:pass
    else:raise AssertionError('world matrix overflow accepted')
    assert root.scale==old and plain.world_matrix[0][0]<3e30
    root.scale=(0,1,1);position=plain.position
    try:plain.world_position=(1,0,0)
    except RuntimeError:pass
    else:raise AssertionError('singular world position accepted')
    assert plain.position==position
    forge.log('HIERARCHY_ATOMIC_OK');forge.quit()
""")
        self.assertIn('HIERARCHY_ATOMIC_OK',self.run_engine())
    def test_subtree_destroy_calls_behaviors_once(self):
        (self.root/'scripts/tree.py').write_text('''import forge
class Behavior:
    def __init__(self,e,p):self.entity=e
    def on_destroy(self):forge.log('DESTROY_'+self.entity.id)
''')
        self.script_scene('''import forge
frames=0
def build():return {'physics_enabled':False,'entities':[{'id':name,'kind':'empty','parent':parent,'scripts':['tree.py']} for name,parent in [('root',''),('child','root'),('leaf','child')]]}
def on_update(dt):
    global frames
    if frames==0:forge.find('root').destroy()
    if frames==2:
        assert forge.entities()==[] and forge.world_stats()['indexed']==0
        forge.log('SUBTREE_OK');forge.quit()
    frames+=1
''')
        output=self.run_engine();self.assertIn('SUBTREE_OK',output)
        for id in ('root','child','leaf'):self.assertEqual(output.count('DESTROY_'+id),1)
    def test_world_index_and_broadphase_scale_on_separated_colliders(self):
        self.script_scene('''import forge,time
def build():return {'mode':'3d','gravity':[0,0,0],'physics_enabled':False,'entities':[{'id':'e'+str(i),'kind':'empty','position':[i*3,0,0],'collider':[1,1,1]} for i in range(10000)]}
def on_start():
    start=time.perf_counter()
    for _ in range(3):
        for i in range(10000):assert forge.find('e'+str(i)).id=='e'+str(i)
    lookup=time.perf_counter()-start
    forge.physics_step(.01);assert forge.world_stats()['candidate_pairs']==0
    old=forge.find('e123');old.destroy();assert forge.find('e123') is None
    new=forge.spawn({'id':'e123','kind':'empty'});assert forge.find('e123') is new and not old.alive
    forge.log('INDEX_OK lookup_seconds='+str(lookup));forge.quit()
''')
        self.assertIn('INDEX_OK',self.run_engine())
    def test_hierarchy_static_colliders_follow_in_both_backends(self):
        for backend in ('legacy','bullet'):
            with self.subTest(backend=backend):
                self.script_scene('''import forge
def build():return {'mode':'3d','physics':{'backend':BACKEND},'physics_enabled':False,'entities':[{'id':'root','kind':'empty','position':[10,0,0]}, {'id':'child','kind':'empty','parent':'root','position':[2,0,0],'collider':[1,1,1]}]}
def on_start():
    child=forge.find('child');assert forge.raycast((12,0,3),(0,0,-1),5) is child
    forge.find('root').position=(20,0,0)
    assert forge.raycast((12,0,3),(0,0,-1),5) is None
    assert forge.raycast((22,0,3),(0,0,-1),5) is child
    character=forge.spawn({'id':'character','kind':'empty','parent':'root','position':[0,0,0],'collider':[1,1,1]})
    result=forge.move_character(character,(5,0,0));assert 20.9<character.world_position[0]<21.1,result
    try:forge.spawn({'id':'invalid','parent':'root','dynamic':True})
    except RuntimeError:pass
    else:raise AssertionError('dynamic child accepted')
    forge.log('COLLIDER_TREE_OK');forge.quit()
'''.replace('BACKEND',repr(backend)))
                self.assertIn('COLLIDER_TREE_OK',self.run_engine())
    def test_procedural_mesh_validation_version_lifetime_and_budget(self):
        self.script_scene('''import forge
def on_start():
    data={'positions':[[-1,0,0],[1,0,0],[0,1,0]],'colors':[[1,0,0,1],[0,1,0,1],[0,0,1,1]]}
    name=forge.set_mesh('triangle',data);assert name=='@mesh:triangle'
    info=forge.mesh_info(name);revision=info['revision'];assert info['bytes']>0
    body=forge.spawn({'id':'triangle','kind':'mesh','model':name})
    stats=forge.geometry_stats();forge.set_geometry_budget(stats['resident_bytes'])
    try:forge.set_mesh('overflow',data)
    except RuntimeError:pass
    else:raise AssertionError('budget ignored')
    assert forge.geometry_stats()['meshes']==1
    forge.set_geometry_budget(64*1024*1024)
    for bad in [{'positions':[[0,0,float('nan')]]},{'positions':data['positions'],'indices':[0,1,3]}, {'positions':data['positions'],'uvs':[[0,0]]}]:
        try:forge.set_mesh('triangle',bad)
        except RuntimeError:pass
        else:raise AssertionError('bad mesh accepted')
        assert forge.mesh_info(name)['revision']==revision
    data['positions'][2][1]=2;forge.set_mesh('triangle',data)
    assert forge.mesh_info(name)['revision']>revision
    try:forge.remove_mesh(name)
    except RuntimeError:pass
    else:raise AssertionError('live mesh removed')
    body.destroy();forge.remove_mesh(name);assert forge.geometry_stats()['meshes']==0
    forge.log('MESH_OK');forge.quit()
''')
        self.assertIn('MESH_OK',self.run_engine())
    def test_material_validation_is_shared_and_atomic(self):
        for bad in ({'shading':'wrong'},{'shading':'pbr','roughness':-1},{'normal_texture':'../missing.png'},{'metallic':True},{'emissive':[1,2]}):
            with self.subTest(bad=bad):
                (self.root/'materials/invalid.json').write_text(json.dumps(bad))
                self.assertIn('material',self.run_engine('validate',expected=1).lower())
        (self.root/'materials/invalid.json').unlink()
        (self.root/'materials/legacy.json').write_text(json.dumps({'color':[2,-1,.5,1]}))
        self.run_engine('validate')
        self.script_scene('''import forge
def on_start():
    e=forge.spawn({'kind':'cube','material_properties':{'shading':'pbr','metallic':.75,'roughness':.3}})
    before=e.material_properties
    try:e.material_properties={'shading':'pbr','roughness':float('nan')}
    except RuntimeError:pass
    else:raise AssertionError('bad material accepted')
    assert e.material_properties==before
    assert forge.scene_data()['entities'][-1]['material_properties']['shading']=='pbr'
    forge.log('MATERIAL_OK');forge.quit()
''')
        self.assertIn('MATERIAL_OK',self.run_engine())
    def test_reload_snapshots_nested_metadata_and_restores_mesh_hierarchy(self):
        self.config['stage']=0
        self.script_scene('''import forge
from saves import SaveManager
from pathlib import Path
stage=forge.settings()['stage']
reported=False
def build():return {'physics_enabled':False,'entities':[{'id':'root','kind':'empty'},{'id':'child','kind':'empty','parent':'root','position':[2,0,0]}]}
def on_start():
    global mesh_revision
    forge.set_mesh('triangle',{'positions':[[0,0,0],[1,0,0],[0,1,0]]});mesh_revision=forge.mesh_info('triangle')['revision']
    details={'nested':{'tags':['frozen']}};data={'stage':stage,'values':[1]}
    SaveManager().write('snapshot',data,metadata=details)
    details['nested']['tags'].append('mutated');data['values'].append(2)
    if stage==2:raise RuntimeError('rejected replacement')
    forge.log('STAGE_'+str(stage))
def on_reload_failed(error):
    assert forge.mesh_info('triangle')['revision']==mesh_revision
    assert forge.find('child').parent is forge.find('root')
    assert tuple(forge.find('child').world_position)==(2,0,0)
    forge.log('RELOAD_TREE_OK')
def on_update(dt):
    global reported
    if not reported:forge.log('RUN_STAGE_'+str(stage));reported=True
    if Path(forge.project_path('stop')).exists():forge.quit()
''')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(predicate):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                if predicate():return
                if process.poll() is not None:self.fail((self.root/'forge.log').read_text())
                time.sleep(.03)
            self.fail('Timed out waiting for committed reload\n'+(self.root/'forge.log').read_text())
        def log(marker):return (self.root/'forge.log').exists() and marker in (self.root/'forge.log').read_text()
        slot=self.root/'saves/slots/snapshot.json'
        try:
            wait(lambda:log('STAGE_0'));self.config['stage']=1;self.write_config()
            wait(lambda:slot.exists() and json.loads(slot.read_text())['data']['stage']==1)
            saved=json.loads(slot.read_text());self.assertEqual(saved['metadata']['nested']['tags'],['frozen']);self.assertEqual(saved['data']['values'],[1])
            wait(lambda:log('RUN_STAGE_1'));original=slot.read_bytes();self.config['stage']=2;self.write_config();wait(lambda:log('RELOAD_TREE_OK'));self.assertEqual(slot.read_bytes(),original)
            self.config['stage']=3;self.write_config();wait(lambda:log('RUN_STAGE_3'))
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
if __name__=='__main__':unittest.main(verbosity=2)
