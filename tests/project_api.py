"""Editor-neutral document, concurrency, SDK and paused-runtime regressions."""
from copy import deepcopy
from pathlib import Path
import json, subprocess, sys, unittest, time
import integration as base
ROOT, ENGINE = base.ROOT, base.ENGINE
sys.path.insert(0,str(ROOT/'sdk'))
from forge_editor import Client, ProjectError

class ProjectTests(unittest.TestCase):
    setUp,tearDown=base.EngineTests.setUp,base.EngineTests.tearDown
    write_config,script_scene,run_engine=base.EngineTests.write_config,base.EngineTests.script_scene,base.EngineTests.run_engine

    def client(self): return Client(ENGINE,self.root/'engine.json')
    def scene(self, name='editable.json'):
        value={'mode':'3d','physics_enabled':False,'camera':{'position':[0,0,5],'extensions':{'other':{'lens':'custom'}}},
               'extensions':{'shellA':{'panel':True}},'entities':[{'id':'a','kind':'empty','name':'A','position':[0,0,0],'custom':{'future':[1,{'x':2}]}},{'id':'b','kind':'empty','name':'B'}]}
        (self.root/'scenes'/name).write_text(json.dumps(value,ensure_ascii=False),encoding='utf-8')
        return value

    def test_two_shells_merge_manual_fields_and_preserve_unknown_data(self):
        original=self.scene()
        with self.client() as a,self.client() as b:
            da=a.document('editable.json','scenes');db=b.document('editable.json','scenes')
            da.patch([{'op':'replace','path':'/entities/0/name','value':'Changed A'}]).save()
            db.patch([{'op':'add','path':'/entities/1/position','value':[3,0,0]}]).save()
            path=self.root/'scenes/editable.json';disk=json.loads(path.read_text())
            disk['extensions']['manual']={'language':'日本語'};disk['entities'][0]['custom']['added']='human'
            path.write_text(json.dumps(disk,ensure_ascii=False),encoding='utf-8')
            db.patch([{'op':'replace','path':'/entities/1/name','value':'Changed B'}]).save()
            result=json.loads(path.read_text())
            self.assertEqual(result['entities'][0]['name'],'Changed A')
            self.assertEqual(result['entities'][1]['position'],[3,0,0])
            self.assertEqual(result['entities'][0]['custom']['future'],original['entities'][0]['custom']['future'])
            self.assertEqual(result['extensions']['manual'],{'language':'日本語'})
            self.assertEqual(result['camera'],original['camera'])
            self.assertEqual(db.data,result)

    def test_conflict_keeps_disk_and_draft_and_service_remains_usable(self):
        self.scene()
        with self.client() as a,self.client() as b:
            da=a.document('editable.json','scenes');db=b.document('editable.json','scenes')
            da.patch([{'op':'replace','path':'/entities/0/name','value':'A shell'}]).save()
            db.patch([{'op':'replace','path':'/entities/0/name','value':'B shell'}])
            path=self.root/'scenes/editable.json';before=path.read_bytes()
            with self.assertRaises(ProjectError) as error:db.save()
            self.assertEqual(error.exception.code,'conflict');self.assertIn('/entities/a/name',error.exception.paths)
            self.assertEqual(path.read_bytes(),before);self.assertEqual(db.data['entities'][0]['name'],'B shell')
            self.assertEqual(b.request('capabilities')['api_version'],1)
            self.assertFalse(list(path.parent.glob('.*.forge-*')))

    def test_record_removal_order_and_atomic_numeric_failure(self):
        self.scene()
        with self.client() as client:
            document=client.document('editable.json','scenes');before=deepcopy(document.data)
            with self.assertRaises(ProjectError):document.patch([{'op':'replace','path':'/entities/0/position','value':[1e100,0,0]}])
            self.assertEqual(document.data,before)
            document.patch([{'op':'remove','path':'/entities/1'}]);self.assertTrue(document.undo());self.assertEqual(document.data,before)
            self.assertTrue(document.redo());document.save();self.assertEqual(len(document.data['entities']),1)
            b={'entities':[{'id':i,'name':i} for i in ('a','b','c')]}
            l=deepcopy(b);l['entities'].reverse();d=deepcopy(b);d['entities']=[d['entities'][1],d['entities'][0],d['entities'][2]]
            with self.assertRaises(ProjectError) as error:client.request('merge',base=b,local=l,disk=d)
            self.assertIn('/entities/@order',error.exception.paths)
            b={'entities':[{'id':'a'},{'id':'b'}]};l={'entities':[{'id':'a'},{'id':'local'},{'id':'b'}]};d={'entities':[{'id':'a'},{'id':'disk'},{'id':'b'}]}
            merged=client.request('merge',base=b,local=l,disk=d)
            self.assertEqual([e['id'] for e in merged['entities']],['a','disk','local','b'])

    def test_unicode_renamed_paths_confinement_and_new_file_no_overwrite(self):
        old=self.root/'scenes';new=self.root/'сцены автора';old.rename(new)
        self.config['paths']['scenes']='сцены автора';self.write_config()
        with self.client() as client:
            document=client.document('日本語.json','scenes');self.assertIsNone(document.data)
            document.replace({'entities':[],'future':{'color':'紫'}}).save()
            self.assertTrue((new/'日本語.json').exists())
            with self.assertRaises(ProjectError):client.request('commit',group='scenes',file='日本語.json',base=None,data={'entities':[]})
            for path in ('../../outside.json',str(self.root.parent/'outside.json')):
                with self.assertRaises(ProjectError):client.request('read',file=path)
            outside=self.root.parent/'forge-outside-link.json'
            (self.root/'escape.json').symlink_to(outside)
            with self.assertRaises(ProjectError):client.request('commit',file='escape.json',base=None,data={})
            self.assertFalse(outside.exists())

    def test_public_extensions_and_launcher_machine_protocol(self):
        self.scene()
        with self.client() as client:
            client.extension(ROOT/'examples/editor/labels_extension.py')
            result=client.command('example.label',file='editable.json',entity='a',value='Label')
            self.assertEqual(result['entities'][0]['extensions']['example']['label'],'Label')
            self.assertEqual(client.request('inspect')['config']['project'],self.config['project'])
        process=subprocess.run([sys.executable,str(ROOT/'tools/forge.py'),'project','--project',str(self.root/'engine.json')],input='{"op":"read","group":"scenes","file":"editable.json"}',capture_output=True,text=True,encoding='utf-8')
        self.assertEqual(process.returncode,0,process.stderr);self.assertTrue(json.loads(process.stdout)['ok'])
        process=subprocess.run([sys.executable,str(ROOT/'tools/forge.py'),'project','--project',str(self.root/'engine.json')],input='{"op":"nope","file":"engine.json"}',capture_output=True,text=True)
        self.assertEqual(process.returncode,1);self.assertFalse(json.loads(process.stdout)['ok'])

    def test_same_extension_runs_inside_builtin_runtime_adapter(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        self.config['editor']={'extensions':['labels.py']};self.write_config()
        (self.root/'labels.py').write_text((ROOT/'examples/editor/labels_extension.py').read_text())
        (self.root/'shell.py').write_text("""import forge
API_VERSION=1
def on_update(dt):
    assert forge.editor_command({'op':'commands'})==['example.label']
    result=forge.editor_command({'op':'command','name':'example.label','arguments':{'file':'editable.json','entity':'a','value':'Runtime label'}})
    assert result['entities'][0]['extensions']['example']['label']=='Runtime label'
    from forge_editor import RuntimeClient,ProjectError
    client=RuntimeClient('',forge.project_path('engine.json'))
    try:client.request('read',file='../outside.json')
    except ProjectError:pass
    else:raise AssertionError('native adapter has a different error contract')
    forge.log('EXTENSION_ADAPTER_OK');forge.quit()
""",encoding='utf-8')
        self.assertIn('EXTENSION_ADAPTER_OK',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_config_material_and_prefab_documents_use_native_validation(self):
        with self.client() as client:
            config=client.document('engine.json');original=deepcopy(config.data)
            config.patch([{'op':'add','path':'/extensions','value':{'third_party':{'dock':True}}}]).save()
            self.assertEqual(config.data['paths'],original['paths'])
            before=(self.root/'engine.json').read_bytes()
            with self.assertRaises(ProjectError):config.patch([{'op':'replace','path':'/paths/scenes','value':'../escape'}])
            self.assertEqual((self.root/'engine.json').read_bytes(),before)
            material=client.document('native.json','materials')
            with self.assertRaises(ProjectError):material.replace({'roughness':2})
            material.replace({'roughness':.4,'future':{'map':'custom'}}).save()
            prefab=client.document('native.json','objects')
            with self.assertRaises(ProjectError):prefab.replace({'entities':[{'id':'a','parent':'missing'}]})
            with self.assertRaises(ProjectError):prefab.replace({'extends':'native.json'})
            prefab.replace({'entities':[{'id':'a','kind':'empty'},{'id':'b','kind':'empty','parent':'a'}],'extensions':{'tag':'kept'}}).save()
            self.assertEqual(prefab.data['extensions'],{'tag':'kept'})
            single=client.document('single.json','objects').replace({'kind':'empty'}).save()
            with self.assertRaises(ProjectError):single.patch([{'op':'add','path':'/prefab','value':'single.json'}])
            self.assertEqual(json.loads((self.root/'objects/single.json').read_text()),{'kind':'empty'})
            # Validation also applies to paths without the group shorthand.
            with self.assertRaises(ProjectError):client.request('check',file='scenes/bad.json',data={'entities':[{'position':[1e100,0,0]}]})

    def test_dirty_hot_reload_preserves_editor_world_and_manual_file(self):
        scene=self.scene();scene['script']='dirty_callbacks.py'
        path=self.root/'scenes/editable.json';path.write_text(json.dumps(scene))
        self.config['entry_scene']='editable.json';self.config['development']={'watch_interval':.05};self.write_config()
        (self.root/'scenes/dirty_callbacks.py').write_text("""import forge
def on_start():forge.log('SCENE_INITIALIZED')
def on_reload_failed(error):
    assert 'unsaved changes' in error
    assert forge.find('a').name=='Unsaved editor' and forge.scene_data()['extensions']['shellA']['panel']
    forge.log('DIRTY_WORLD_PRESERVED');forge.quit()
""")
        (self.root/'shell.py').write_text("""import forge
API_VERSION=1
ready=False
def on_update(dt):
    global ready
    if not ready:
        forge.find('a').name='Unsaved editor';forge.log('DIRTY_READY');ready=True
""")
        process=subprocess.Popen([str(ENGINE),'edit','--project',str(self.root/'engine.json'),'--shell','shell.py','--headless','--frames','10000','--no-open-log'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        try:
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and 'DIRTY_READY' in log.read_text():break
                time.sleep(.02)
            else:self.fail('Editor did not start')
            scene['entities'][1]['name']='Manual external';path.write_text(json.dumps(scene))
            output=process.communicate(timeout=15)[0]
            self.assertEqual(process.returncode,0,output);self.assertIn('DIRTY_WORLD_PRESERVED',output)
            self.assertEqual(output.count('SCENE_INITIALIZED'),1)
            self.assertEqual(json.loads(path.read_text())['entities'][1]['name'],'Manual external')
        finally:
            if process.poll() is None:process.kill();process.communicate()

    def test_stale_lock_and_temporary_symlink_do_not_replace_files(self):
        self.scene();path=self.root/'scenes/editable.json';before=path.read_bytes()
        with self.client() as client:
            document=client.document('editable.json','scenes')
            document.patch([{'op':'replace','path':'/entities/0/name','value':'Changed'}])
            lock=path.parent/'.editable.json.forge-lock';lock.mkdir()
            with self.assertRaises(ProjectError):document.save()
            self.assertEqual(path.read_bytes(),before);lock.rmdir()
            target=self.root/'untouched.txt';target.write_text('Keep')
            temp=path.parent/'.editable.json.forge-tmp';temp.symlink_to(target)
            with self.assertRaises(ProjectError):document.save()
            self.assertEqual(target.read_text(),'Keep');self.assertEqual(path.read_bytes(),before)
            temp.unlink()
            # A regular temp file is a crashed writer's leftover; the lock makes replacing it safe.
            temp.write_text('partial');document.save();self.assertEqual(document.data['entities'][0]['name'],'Changed')
            self.assertFalse(temp.exists())

    def test_builtin_save_preserves_manual_and_unknown_fields(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'shell.py').write_text('''import forge,json
from pathlib import Path
API_VERSION=1
def on_update(dt):
    path=Path(forge.asset_path('scenes','editable.json'))
    original=json.loads(path.read_text())
    assert forge.scene_data()['entities'][0]['custom']==original['entities'][0]['custom']
    forge.find('a').name='Editor changed'
    disk=json.loads(path.read_text());disk['entities'][1]['name']='Human changed';disk['camera']['manual']=42
    path.write_text(json.dumps(disk))
    forge.save_scene('editable.json')
    saved=json.loads(path.read_text());assert saved['entities'][0]['name']=='Editor changed' and saved['entities'][1]['name']=='Human changed'
    assert saved['camera']['manual']==42 and saved['camera']['extensions']==original['camera']['extensions']
    assert 'scale' not in saved['entities'][0] and saved['extensions']==original['extensions']
    forge.find('a').name='Conflicting editor'
    disk=json.loads(path.read_text());disk['entities'][0]['name']='Conflicting human';path.write_text(json.dumps(disk))
    before=path.read_bytes()
    try:forge.save_scene('editable.json')
    except RuntimeError as e:assert 'Document changed' in str(e)
    else:raise AssertionError('Conflict overwritten')
    assert path.read_bytes()==before
    forge.log('SAVE_MERGE_OK');forge.quit()
''',encoding='utf-8')
        self.assertIn('SAVE_MERGE_OK',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_rejected_scene_load_does_not_queue_a_failed_next_frame(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'shell.py').write_text("""import forge
API_VERSION=1
frame=0
def on_start():forge.editor_command({'op':'preview','enabled':True})
def on_update(dt):
    global frame
    frame+=1
    if frame==1:
        assert forge.editor_command({'op':'snapshot'})['preview']
        forge.editor_command({'op':'preview','enabled':False})
        for name in ('../../outside.json','missing.json',''):
            try:forge.editor_command({'op':'load','file':name})
            except RuntimeError:pass
            else:raise AssertionError('invalid load accepted')
        assert not forge.project_response({'op':1})['ok']
    else:
        assert forge.find('a').name=='A'
        forge.log('LOAD_QUEUE_PRESERVED');forge.quit()
""",encoding='utf-8')
        self.assertIn('LOAD_QUEUE_PRESERVED',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_public_paused_edit_history_and_failed_lifecycle_rollback(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'scenes/bad_edit.py').write_text("def on_start():raise RuntimeError('candidate refused')\n")
        (self.root/'shell.py').write_text('''import forge
API_VERSION=1
def on_update(dt):
    assert forge.editor_enabled() and not forge.editor_command({'op':'snapshot'})['preview']
    e=forge.find('a');before=forge.scene_data()
    forge.editor_command({'op':'patch','patch':[{'op':'replace','path':'/entities/0/name','value':'New name'}]})
    assert not e.alive and forge.find('a').name=='New name'
    assert forge.editor_command({'op':'undo'}) and forge.find('a').name=='A'
    assert forge.editor_command({'op':'redo'}) and forge.find('a').name=='New name'
    current=forge.find('a');before=forge.scene_data();history=forge.editor_command({'op':'snapshot'})['undo']
    for changes in ([{'op':'replace','path':'/entities/0/position','value':[1e100,0,0]}],[{'op':'add','path':'/script','value':'bad_edit.py'}]):
        try:forge.editor_command({'op':'patch','patch':changes})
        except RuntimeError:pass
        else:raise AssertionError('invalid transaction accepted')
        assert current.alive and forge.find('a') is current and forge.scene_data()==before
        assert forge.editor_command({'op':'snapshot'})['undo']==history
    forge.editor_command({'op':'select','id':'a'});assert forge.editor_command({'op':'snapshot'})['selected']=='a'
    assert forge.project_request({'op':'capabilities'})['api_version']==1
    forge.log('EDITOR_API_OK');forge.quit()
''',encoding='utf-8')
        self.assertIn('EDITOR_API_OK',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_configuration_rejects_lossy_integer_and_boolean_coercion(self):
        with self.client() as client:
            doc=client.document('engine.json')
            before=(self.root/'engine.json').read_bytes()
            for path,value in (('/schema_version',1.5),('/window/width',4294968576),('/window/height',720.5),('/window/fullscreen',1),('/window/vsync','yes')):
                with self.subTest(path=path,value=value),self.assertRaises(ProjectError):
                    doc.patch([{'op':'add','path':path,'value':value}])
                self.assertEqual((self.root/'engine.json').read_bytes(),before)

    def test_failed_scene_initialization_does_not_write_or_delete_saves(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'scenes/persistence.py').write_text("""import forge
from saves import SaveManager
def on_start():
    store=SaveManager(directory='slots')
    store.write('existing',{'value':'candidate'})
    store.delete('removed')
    forge.save('native',{'value':'candidate'})
    if not forge.find('a').data.get('accept'):raise RuntimeError('candidate refused')
""")
        (self.root/'shell.py').write_text("""import forge
from saves import SaveManager
API_VERSION=1
def on_update(dt):
    store=SaveManager(directory='slots')
    store.write('existing',{'value':'original'});store.write('removed',{'value':'original'})
    forge.save('native',{'value':'original'})
    changes=[{'op':'add','path':'/script','value':'persistence.py'}]
    try:forge.editor_command({'op':'patch','patch':changes})
    except RuntimeError:pass
    else:raise AssertionError('candidate accepted')
    assert store.read('existing')=={'value':'original'}
    assert store.read('removed')=={'value':'original'}
    assert forge.load('native')=={'value':'original'}
    changes.append({'op':'add','path':'/entities/0/data/accept','value':True})
    forge.editor_command({'op':'patch','patch':changes})
    assert store.read('existing')=={'value':'candidate'} and store.info('removed')['status']=='empty'
    assert forge.load('native')=={'value':'candidate'}
    forge.log('PERSISTENCE_TRANSACTION_OK');forge.quit()
""")
        self.assertIn('PERSISTENCE_TRANSACTION_OK',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_scene_replacement_during_frame_dispatch_retires_old_callbacks(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'scenes/dispatch.py').write_text("""import forge
def on_start():
    if forge.find('a').data.get('new'):
        forge.on_frame(lambda dt:forge.log('NEW_CALLBACK'))
    else:
        forge.on_frame(replace)
        forge.on_frame(lambda dt:forge.log('RETIRED_CALLBACK'))
def replace(dt):
    forge.set_paused(True)
    forge.editor_command({'op':'patch','patch':[{'op':'add','path':'/entities/0/data/new','value':True}]})
    forge.set_paused(False)
""")
        data=json.loads((self.root/'scenes/editable.json').read_text());data['script']='dispatch.py'
        (self.root/'scenes/editable.json').write_text(json.dumps(data))
        (self.root/'shell.py').write_text("""import forge
API_VERSION=1
frames=0
def on_start():forge.set_paused(False)
def on_update(dt):
    global frames
    frames+=1
    if frames==3:forge.quit()
""")
        output=self.run_engine('edit',extra=('--shell','shell.py'))
        self.assertNotIn('RETIRED_CALLBACK',output);self.assertIn('NEW_CALLBACK',output)

    def test_failed_editor_scene_keeps_procedural_geometry(self):
        self.scene();self.config['entry_scene']='editable.json';self.write_config()
        (self.root/'scenes/geometry.py').write_text("""import forge
def on_start():
    forge.set_mesh('shared',{'positions':[[0,0,0],[2,0,0],[0,2,0]]})
    if not forge.find('a').data.get('accept'):raise RuntimeError('candidate refused')
""")
        (self.root/'shell.py').write_text("""import forge
API_VERSION=1
def on_update(dt):
    forge.set_mesh('shared',{'positions':[[0,0,0],[1,0,0],[0,1,0]]})
    before=forge.mesh_info('shared');stats=forge.geometry_stats()
    changes=[{'op':'add','path':'/script','value':'geometry.py'}]
    try:forge.editor_command({'op':'patch','patch':changes})
    except RuntimeError:pass
    else:raise AssertionError('candidate accepted')
    assert forge.mesh_info('shared')==before and forge.geometry_stats()==stats
    changes.append({'op':'add','path':'/entities/0/data/accept','value':True})
    forge.editor_command({'op':'patch','patch':changes})
    assert forge.mesh_info('shared')['revision']>before['revision']
    forge.log('GEOMETRY_ROLLBACK_OK');forge.quit()
""")
        self.assertIn('GEOMETRY_ROLLBACK_OK',self.run_engine('edit',extra=('--shell','shell.py')))

    def test_custom_shell_keeps_working_after_failed_reload(self):
        self.scene();self.config['entry_scene']='editable.json';self.config['development']={'watch_interval':.05};self.write_config()
        path=self.root/'scenes/editable.json';scene=json.loads(path.read_text());scene['script']='recovery.py';path.write_text(json.dumps(scene))
        (self.root/'scenes/recovery.py').write_text("""import forge
def on_start():
    forge.log('SCENE_READY')
def on_reload_failed(error):
    import builtins;builtins._forge_recover=True
""")
        (self.root/'shell.py').write_text("""import forge
from pathlib import Path
API_VERSION=1
started=False
restored=False
def on_update(dt):
    global started,restored
    path=Path(forge.asset_path('scenes','recovery.py'))
    if not started:
        started=True;path.write_text("def on_start():raise RuntimeError('bad candidate')\\n")
    elif getattr(__import__('builtins'),'_forge_recover',False) and not restored:
        restored=True
        path.write_text("import forge\\ndef on_start():forge.log('SHELL_RECOVERED');forge.quit()\\n")
""")
        self.assertIn('SHELL_RECOVERED',self.run_engine('edit',frames=300,extra=('--shell','shell.py')))

if __name__=='__main__':unittest.main()
