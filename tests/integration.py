"""Exercise the real native binary, Python bridge, physics, reload, and packaging."""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, sys, tempfile, time, unittest
ROOT = Path(__file__).resolve().parents[1]
ENGINE = Path(sys.argv.pop(1)).resolve()

class EngineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='forge тест ')
        self.root = Path(self.temp.name)
        self.config = json.loads((ROOT / 'engine.json').read_text())
        self.config['logging']['open_on_error'] = False
        self.config['startup_scripts'] = []
        for group, relative in self.config['paths'].items():
            shutil.copytree(ROOT / relative, self.root / relative, ignore=shutil.ignore_patterns('__pycache__'))
        self.write_config()
    def tearDown(self): self.temp.cleanup()
    def write_config(self):
        (self.root / 'engine.json').write_text(json.dumps(self.config, ensure_ascii=False), encoding='utf-8')
    def run_engine(self, command='run', frames=10, expected=0, extra=()):
        result = subprocess.run([str(ENGINE), command, '--project', str(self.root / 'engine.json'), '--headless', '--frames', str(frames), '--no-open-log', *map(str,extra)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result.stdout + result.stderr
    def script_scene(self, text):
        (self.root / 'scenes/test.py').write_text(text, encoding='utf-8')
        self.config['entry_scene'] = 'test.py'; self.write_config()
    def test_frame_limit_requires_complete_positive_integer(self):
        self.script_scene("import forge\ndef on_start():forge.log('FRAME_LIMIT_OK')\n")
        for limit in ('2junk','2.5','1e3','0','-1','2147483648','not-a-number'):
            with self.subTest(limit=limit):
                result=subprocess.run([str(ENGINE),'run','--project',str(self.root/'engine.json'),'--headless','--frames',limit,'--no-open-log'],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,1,result.stdout+result.stderr)
                self.assertIn('--frames must be a positive integer',result.stdout+result.stderr)
        self.assertIn('FRAME_LIMIT_OK',self.run_engine(frames=2))
    def test_default_scene_and_renamed_layout(self):
        for key, old in self.config['paths'].items():
            new = 'ресурсы/' + key
            target = self.root / new; target.parent.mkdir(exist_ok=True)
            (self.root / old).rename(target); self.config['paths'][key] = new
        self.config['project']['icon'] = self.config['paths']['textures'] + '/icon.png';self.write_config()
        self.assertIn('2D сцена готова',self.run_engine())
        self.assertIn('validated',self.run_engine('validate'))
    def test_entity_validation_is_shared_by_json_and_python(self):
        cases=[]
        for field in ('position','rotation','scale','velocity','collider'):
            for bad in (1e100,True,None): cases.append((field,{field:[0,bad,0]}))
        cases += [('position',{'position':[0,1]}),('position',{'position':None})]
        for field in ('color','clip'):
            for bad in (1e100,True,None):cases.append((field,{field:[0,1,bad,1]}))
        for field in ('mass','font_size'):
            for bad in (1e100,0,-1,1e-100,True):cases.append((field,{field:bad}))
        cases.append(('mass',{'mass':1e-40}))
        cases += [('scripts',{'scripts':{}}),('mesh',{'kind':'mesh'}),('Missing file',{'texture':'absent.png'})]
        cases += [(field,{field:123}) for field in ('id','name','kind','model','texture','material','text','text_key','dynamic','trigger','visible','screen')]
        for field,data in cases:
            with self.subTest(field=field,data=data):
                (self.root/'objects/invalid.json').write_text(json.dumps(data),encoding='utf-8')
                self.assertIn(field,self.run_engine('validate',expected=1))
        (self.root/'objects/invalid.json').unlink()
        (self.root/'cases.json').write_text(json.dumps(cases),encoding='utf-8')
        self.script_scene("""import forge,json,math
from pathlib import Path
def on_start():
    for field,data in json.loads(Path(forge.project_path('cases.json')).read_text()):
        before=len(forge.entities())
        try: forge.spawn(dict({'id':'rejected'},**data))
        except RuntimeError as error: assert field in str(error), (field,str(error))
        else: raise AssertionError('invalid object accepted '+str(data))
        assert forge.find('rejected') is None and len(forge.entities())==before
    for number in (float('nan'),float('inf'),-float('inf')):
        try: forge.spawn({'id':'nonfinite','position':[number,0,0]})
        except (RuntimeError,ValueError): pass
        else: raise AssertionError('nonfinite input accepted')
        assert forge.find('nonfinite') is None
    prefab=Path(forge.asset_path('objects','live.json'))
    prefab.write_text(json.dumps({'position':[1e100,0,0]}))
    try: forge.spawn({'id':'from_prefab','prefab':'live.json'})
    except RuntimeError as error: assert 'position' in str(error)
    else: raise AssertionError('invalid prefab accepted')
    assert forge.find('from_prefab') is None
    prefab.write_text(json.dumps({'position':[20,30,0],'mass':2}))
    e=forge.spawn({'id':'from_prefab','prefab':'live.json','position':[1,2,0],'clip':None})
    assert tuple(e.position)==(1,2,0) and e.mass==2 and e.clip is None
    forge.log('SHARED_ENTITY_VALIDATION_OK');forge.quit()
""")
        self.assertIn('SHARED_ENTITY_VALIDATION_OK',self.run_engine())
    def test_python_scene_and_mutations_reject_invalid_numbers(self):
        self.script_scene("def build(): return {'entities':[{'position':[1e100,0,0]}]}\n")
        self.assertIn('position must be finite',self.run_engine(expected=1))
        self.script_scene("""import forge,math
def on_start():
    e=forge.spawn({'id':'safe','kind':'empty'})
    e.clip=(1,2,30,40)
    for field in ('position','rotation','scale','velocity','collider','color','clip','mass','font_size'):
        before=getattr(e,field)
        for number in (float('nan'),float('inf'),-float('inf'),1e100):
            value=[number,0,0] if field in ('position','rotation','scale','velocity','collider') else [number,0,0,1] if field in ('color','clip') else number
            try: setattr(e,field,value)
            except RuntimeError as error: assert field in str(error), str(error)
            else: raise AssertionError('invalid setter '+field)
            assert getattr(e,field)==before,(field,before,getattr(e,field))
    e.position=(2e38,0,0);before=e.position
    try: e.move(2e38,0)
    except RuntimeError: pass
    else: raise AssertionError('overflowing move accepted')
    assert e.position==before;e.position=(0,0,0)
    e.mass=1e-38;before=e.velocity
    try: e.impulse(2e38,0)
    except RuntimeError: pass
    else: raise AssertionError('overflowing impulse accepted')
    assert e.velocity==before;e.mass=1
    try: e.mass=1e-40
    except RuntimeError: pass
    else: raise AssertionError('overflowing inverse mass accepted')
    assert e.mass==1
    camera=forge.camera_position()
    try: forge.set_camera((1,2,3),(float('nan'),0,0))
    except RuntimeError: pass
    else: raise AssertionError('invalid camera accepted')
    assert forge.camera_position()==camera
    e.clip=None;assert e.clip is None
    e.move(1,2,3);e.impulse(2,3,4)
    assert tuple(e.position)==(1,2,3) and tuple(e.velocity)==(2,3,4)
    forge.log('NUMERIC_MUTATION_OK');forge.quit()
""")
        self.assertIn('NUMERIC_MUTATION_OK',self.run_engine())
    def test_physics_handles_small_masses_and_large_finite_geometry(self):
        self.script_scene("""import forge, math
frames=0
def build(): return {'mode':'2d','gravity':[0,0,0]}
def on_start():
    global a,b
    a=forge.spawn({'id':'a','kind':'empty','dynamic':True,'mass':4e-39,'collider':[2,2,0]})
    b=forge.spawn({'id':'b','kind':'empty','dynamic':True,'mass':4e-39,'position':[1,0,0],'collider':[2,2,0]})
    # Opposite coordinates and summed extents would overflow float intermediates.
    large_a=forge.spawn({'id':'large_a','kind':'empty','position':[-2e38,0,0],'collider':[3e38,2,0]})
    large_b=forge.spawn({'id':'large_b','kind':'empty','position':[2e38,0,0],'collider':[3e38,2,0]})
    assert not forge.overlaps(large_a,large_b)
    assert forge.raycast((0,0,0),(3e38,0,0),3e38) is not None
    large_a.collider=(0,0,0);large_b.collider=(0,0,0)
def on_update(dt):
    global frames
    frames+=1
    if frames>2:
        assert b.position[0]-a.position[0]>=1.99,(a.position,b.position)
        assert abs(a.position[0]+.5)<.001 and abs(b.position[0]-1.5)<.001
        assert all(math.isfinite(n) for e in (a,b) for n in (*e.position,*e.velocity))
        forge.log('SMALL_MASS_PHYSICS_OK');forge.quit()
""")
        self.assertIn('SMALL_MASS_PHYSICS_OK',self.run_engine())
    def test_physics_reports_overflow_without_committing_nonfinite_state(self):
        self.script_scene("""import forge
def build(): return {'gravity':[3e38,0,0]}
def on_start():
    global body,initial
    body=forge.spawn({'id':'overflowing','kind':'empty','dynamic':True,'velocity':[3.4e38,0,0]})
    initial=(body.position,body.velocity)
def on_destroy():
    assert (body.position,body.velocity)==initial
    forge.log('PHYSICS_OPERATION_UNCHANGED')
""")
        output=self.run_engine(expected=1)
        self.assertIn("Physics entity 'overflowing' velocity must be finite",output)
        self.assertIn('PHYSICS_OPERATION_UNCHANGED',output)
        self.assertIn("Physics entity 'overflowing'",(self.root/'forge.log').read_text())
    def test_raycast_and_overlap_share_collider_activation(self):
        self.script_scene("""import forge
def on_start():
    forge.set_mode('3d')
    disabled=forge.spawn({'id':'disabled','kind':'empty','collider':[2,2,0]})
    target=forge.spawn({'id':'target','kind':'empty','position':[0,0,-4],'collider':[2,2,2]})
    probe=forge.spawn({'id':'probe','kind':'empty','collider':[2,2,2]})
    assert not forge.overlaps(disabled,probe)
    probe.collider=(0,0,0)
    assert forge.raycast((0,0,5),(0,0,-1),20).id=='target'
    for size in ((0,2,2),(2,0,2),(2,2,-1),(2,2,0)):
        target.collider=size
        assert forge.raycast((0,0,5),(0,0,-1),20) is None
    # Zero depth remains a valid 2D collider.
    target.collider=(0,0,0)
    forge.set_mode('2d')
    assert forge.raycast((-5,0,0),(1,0,0),20).id=='disabled'
    probe.collider=(2,2,0)
    assert forge.overlaps(disabled,probe)
    disabled.destroy();probe.destroy();target.destroy()
    assert forge.raycast((-5,0,0),(1,0,0),20) is None
    forge.log('COLLIDER_ACTIVATION_OK');forge.quit()
""")
        self.assertIn('COLLIDER_ACTIVATION_OK',self.run_engine())
    def test_hot_reload_replaces_search_paths_and_removed_packages(self):
        extras=self.root/'extra';extras.mkdir()
        (extras/'removed_extra.py').write_text('VALUE=1\n')
        (extras/'kept_extra.py').write_text('VALUE=2\n')
        ns=extras/'retired_namespace';ns.mkdir();(ns/'part.py').write_text('VALUE=3\n')
        (self.root/'modules/removed_module.py').write_text('VALUE=4\n')
        custom=self.root/'custom';custom.mkdir();(custom/'custom_module.py').write_text('VALUE=5\n')
        self.config['python_paths']=['extra','modules','extra'];self.config['path_stage']=0
        self.script_scene("""import forge,sys,importlib
from pathlib import Path
def on_start():
    global old_paths
    stage=forge.settings()['path_stage']
    custom=forge.project_path('custom')
    if custom not in sys.path: sys.path.append(custom)
    import custom_module
    assert custom_module.VALUE==5
    if stage==42: raise RuntimeError('PATH_CANDIDATE_FAILED')
    if stage < 10:
        import removed_module,removed_extra,retired_namespace.part,kept_extra
        assert kept_extra.VALUE==(2 if stage==0 else 99)
        kept_extra.VALUE=99 # surviving extra package must remain cached
    else:
        for name in ('removed_module','removed_extra','retired_namespace.part'):
            assert name not in sys.modules,name
            try: importlib.import_module(name)
            except ImportError: pass
            else: raise AssertionError('retired import still accessible '+name)
        import fresh_extra
        assert fresh_extra.VALUE==6
    old_paths=list(sys.path)
    forge.save('path_report',{'stage':stage,'paths':old_paths})
    forge.log('PATH_STAGE_'+str(stage))
def on_reload_failed(error):
    assert list(sys.path)==old_paths
    forge.log('PATH_ROLLBACK_OK')
def on_update(dt):
    if Path(forge.project_path('stop')).exists(): forge.quit()
""")
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and marker in log.read_text():return
                if process.poll() is not None:self.fail(log.read_text())
                time.sleep(.03)
            self.fail('Missing '+marker)
        def paths():return json.loads((self.root/'saves/path_report.json').read_text())['paths']
        def layout(prefix):
            for group in ('modules','scripts','scenes'):
                relative=prefix+'/'+group;shutil.copytree(self.root/self.config['paths'][group],self.root/relative)
                self.config['paths'][group]=relative
            relative=prefix+'/extra';shutil.copytree(extras,self.root/relative)
            (self.root/relative/'removed_extra.py').unlink();shutil.rmtree(self.root/relative/'retired_namespace')
            (self.root/self.config['paths']['modules']/'removed_module.py').unlink(missing_ok=True)
            (self.root/relative/'fresh_extra.py').write_text('VALUE=6\n')
            self.config['python_paths']=[relative,self.config['paths']['modules'],relative]
        try:
            wait('PATH_STAGE_0');baseline=paths()
            for relative in ('modules','scripts','scenes','extra'):self.assertEqual(baseline.count(str((self.root/relative).resolve())),1)
            for stage in range(1,6):
                self.config['path_stage']=stage;self.write_config();wait('PATH_STAGE_'+str(stage))
                self.assertEqual(paths(),baseline)
            layout('second');self.config['path_stage']=10;self.write_config();wait('PATH_STAGE_10')
            stable=paths();self.assertEqual(len(stable),len(baseline))
            for relative in ('modules','scripts','scenes','extra'):self.assertNotIn(str((self.root/relative).resolve()),stable)
            for relative in ('second/modules','second/scripts','second/scenes','second/extra'):self.assertEqual(stable.count(str((self.root/relative).resolve())),1)
            second_config=json.loads(json.dumps(self.config))
            layout('failed');self.config['path_stage']=42;self.write_config();wait('PATH_ROLLBACK_OK')
            # Recover into another layout, exposing an incorrect rollback of managed paths.
            self.config=second_config;layout('third');self.config['path_stage']=43;self.write_config();wait('PATH_STAGE_43')
            current=paths();self.assertEqual(len(current),len(baseline))
            for prefix in ('second','failed'):
                for group in ('modules','scripts','scenes','extra'):self.assertNotIn(str((self.root/prefix/group).resolve()),current)
            self.assertIn(str(custom.resolve()),current)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
            self.assertNotIn('[ERROR] AssertionError',(self.root/'forge.log').read_text())
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_hot_reload_retries_packages_changed_during_failed_candidate(self):
        extras=self.root/'extra';extras.mkdir();(extras/'lib_value.py').write_text('VALUE=1\n')
        self.config['python_paths']=['extra']
        good='''import forge,lib_value
from pathlib import Path
def on_start(): forge.log('LIB_VALUE_'+str(lib_value.VALUE))
def on_reload_failed(error): forge.log('LIB_ROLLBACK')
def on_update(dt):
    if Path(forge.project_path('stop')).exists(): forge.quit()
'''
        self.script_scene(good)
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker,count=1):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and log.read_text().count(marker)>=count:return
                if process.poll() is not None:self.fail(log.read_text())
                time.sleep(.03)
            self.fail('Missing '+marker)
        try:
            wait('LIB_VALUE_1')
            (self.root/'scenes/test.py').write_text(good+"raise RuntimeError('CANDIDATE_FAILED')\n",encoding='utf-8');wait('LIB_ROLLBACK')
            # The package changes while the scene still fails; rollback restores its old module.
            (extras/'lib_value.py').write_text('VALUE=22\n');wait('LIB_ROLLBACK',2)
            (self.root/'scenes/test.py').write_text(good,encoding='utf-8');wait('LIB_VALUE_22')
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_physics_lifecycle_saves_and_bridge(self):
        (self.root / 'scripts/body.py').write_text('''import forge
class Behavior:
    def __init__(self, entity, props): self.entity = entity; assert props['test'] == 42
    def on_start(self): forge.log('BODY_STARTED')
    def on_collision(self, other):
        assert other.id == 'floor'
        forge.log('COLLISION')
    def on_destroy(self): forge.log('BODY_DESTROYED')
''')
        self.script_scene('''import forge
frames = 0
def build():
    return {'mode':'2d', 'gravity':[0, 60, 0], 'entities':[
        {'id':'floor','kind':'empty','position':[0,100,0],'collider':[200,10,1]},
        {'id':'body','kind':'empty','position':[0,80,0],'collider':[10,10,1],'dynamic':True,'scripts':[{'file':'body.py','properties':{'test':42}}]}
    ]}
def on_start():
    assert forge.window_size() == [1280,720] or tuple(forge.window_size()) == (1280,720)
    assert forge.raycast([0,0,0],[0,1,0]).id == 'body'
    forge.save('test', {'unicode':'Привет', 'number':42})
    assert forge.load('test')['number'] == 42
    assert forge.load('missing', 'fallback') == 'fallback'
    print('PRINT_CAPTURED')
    try: forge.project_path('../escape')
    except RuntimeError: pass
    else: raise AssertionError('path traversal accepted')
def on_update(dt):
    global frames
    frames += 1
    if frames == 120:
        body=forge.find('body'); assert 89.9 <= body.position[1] <= 90.1, body.position
        body.destroy(); assert not body.alive
        forge.spawn({'id':'replacement','kind':'empty'})
        assert forge.find('replacement').alive
        forge.log('BRIDGE_OK')
''')
        output=self.run_engine(frames=125)
        for marker in ['COLLISION','BODY_STARTED','BODY_DESTROYED','PRINT_CAPTURED','BRIDGE_OK']: self.assertIn(marker,output)
        self.assertIn('PRINT_CAPTURED',(self.root/'forge.log').read_text())
    def test_scene_transition(self):
        self.script_scene("import forge\ndef on_update(dt): forge.change_scene('second.py')\ndef on_destroy(): forge.log('OLD_DESTROYED')\n")
        (self.root/'scenes/second.py').write_text("import forge\ndef on_start(): forge.log('SECOND_SCENE'); forge.quit()\n")
        output=self.run_engine();self.assertIn('SECOND_SCENE',output);self.assertIn('OLD_DESTROYED',output)
    def test_runtime_error_traceback(self):
        self.script_scene("def on_update(dt):\n    raise ValueError('intentional runtime failure')\n")
        output=self.run_engine(expected=1)
        self.assertIn('intentional runtime failure',output);self.assertIn('test.py',output)
        self.assertIn('intentional runtime failure',(self.root/'forge.log').read_text())
    def test_shutdown_error_is_failure(self):
        self.script_scene("def on_destroy(): raise RuntimeError('shutdown failure')\n")
        self.assertIn('shutdown failure',self.run_engine(expected=1))
    def test_syntax_error_before_start(self):
        (self.root/'scripts/bad.py').write_text('def broken(:\n')
        output=self.run_engine('validate',expected=1);self.assertIn('bad.py',output);self.assertIn('SyntaxError',output)
    def test_invalid_json_assets_and_escape(self):
        self.config['paths']['textures']='../outside';self.write_config()
        self.assertIn('escapes',self.run_engine('validate',expected=1))
        self.config['paths']['textures']='textures';self.write_config()
        (self.root/'objects/bad.json').write_text('{"texture":"missing.png"}')
        self.assertIn('missing.png',self.run_engine('validate',expected=1))
    def test_corrupt_texture_and_obj(self):
        (self.root/'textures/broken.png').write_bytes(b'not an image')
        self.assertIn('Invalid texture',self.run_engine('validate',expected=1))
        (self.root/'textures/broken.png').unlink()
        (self.root/'models/broken.obj').write_text('v 0 0 0\nf 1 2 3\n')
        self.assertIn('OBJ',self.run_engine('validate',expected=1))
    def test_hot_reload_and_error_recovery(self):
        self.script_scene("import forge\ndef on_start(): forge.log('FIRST_READY')\ndef on_update(dt): raise ValueError('reload failure')\n")
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                file=self.root/'forge.log'
                if file.exists() and 'reload failure' in file.read_text():break
                time.sleep(.03)
            else:self.fail('Runtime did not start')
            (self.root/'scenes/test.py').write_text("import forge\ndef on_start(): forge.log('RECOVERED'); forge.quit()\n")
            self.assertEqual(process.wait(timeout=15),0)
            self.assertIn('RECOVERED',(self.root/'forge.log').read_text())
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_reentrant_object_lifecycle(self):
        (self.root/'scripts/tree.py').write_text("""import forge
class Behavior:
    def __init__(self, entity, props): self.entity, self.depth = entity, props.get('depth', 0)
    def on_start(self):
        forge.log('START '+self.entity.id)
        if self.depth < 4:
            for n in range(3):
                forge.spawn({'id':self.entity.id+str(n), 'kind':'empty', 'data':[], 'scripts':[{'file':'tree.py','properties':{'depth':self.depth+1}}]})
    def on_destroy(self):
        forge.log('DESTROY '+self.entity.id)
        if self.entity.id == 'root':
            forge.spawn({'id':'after_destroy', 'kind':'empty', 'scripts':['after.py']})
""")
        (self.root/'scripts/after.py').write_text("import forge\ndef on_start(): forge.log('AFTER_START')\ndef on_destroy(): forge.log('AFTER_DESTROY')\n")
        self.script_scene("""import forge
frames=0
def build(): return {'entities':[{'id':'root','kind':'empty','scripts':['tree.py']}]}
def on_update(dt):
    global frames
    frames+=1
    if frames==1:
        assert len(forge.entities())==121, len(forge.entities())
        for e in forge.entities(): e.destroy()
    if frames==2:
        assert forge.find('after_destroy').alive
        forge.find('after_destroy').destroy()
    if frames==3: forge.quit()
""")
        output=self.run_engine()
        self.assertEqual(sum('] [INFO] START root' in l for l in output.splitlines()),121)
        self.assertEqual(sum('] [INFO] DESTROY root' in l for l in output.splitlines()),121)
        self.assertEqual(output.count('AFTER_START'),1);self.assertEqual(output.count('AFTER_DESTROY'),1)
    def test_old_scene_teardown_is_isolated(self):
        self.script_scene("""import forge
def build(): return {'entities':[{'id':'same','kind':'empty'}]}
def on_update(dt): forge.change_scene('second.py')
def on_destroy():
    forge.find('same').destroy()
    forge.spawn({'id':'leak','kind':'empty'})
    forge.set_paused(True)
    forge.quit()
    forge.change_scene('welcome.json')
    forge.stop_sounds()
""")
        (self.root/'scenes/second.py').write_text("""import forge
frames=0
def build(): return {'entities':[{'id':'same','kind':'empty'}]}
def on_update(dt):
    assert forge.find('same').alive
    assert forge.find('leak') is None
    assert not forge.is_paused()
    forge.log('ISOLATED_OK');forge.quit()
""")
        self.assertIn('ISOLATED_OK',self.run_engine())
    def test_pause_and_frame_listener_removal(self):
        self.script_scene("""import forge
calls=updates=0
second=None
def tick(dt):
    global calls
    calls+=1
    if calls==1:
        forge.remove_listener(second)
        forge.set_paused(True)
    if calls==3: forge.set_paused(False)
    if calls==5:
        assert updates==2, updates
        forge.log('PAUSE_OK');forge.quit()
def forbidden(dt): raise AssertionError('removed listener was called')
def on_start():
    global second
    forge.on_frame(tick)
    second=forge.on_frame(forbidden)
def on_update(dt):
    global updates
    updates+=1
""")
        self.assertIn('PAUSE_OK',self.run_engine())
    def test_backup_only_slots_are_listed_and_loadable_from_menu(self):
        self.script_scene("""import forge, ui
from pathlib import Path
from saves import SaveManager, SaveError, SaveVersion
from menus import MenuController
def on_start():
    saves=SaveManager()
    saves.write('1',{'n':1},title='First',description='Backup metadata')
    saves.write('1',{'n':2},title='Second')
    path=Path(forge.project_path('saves/slots/1.json'));path.unlink()
    before=path.with_suffix('.json.bak').read_bytes()
    info=saves.info('1')
    assert info['status']=='recoverable' and info['source']=='backup'
    assert info['title']=='First' and info['description']=='Backup metadata' and info['version']==1
    assert [i['slot'] for i in saves.slots()]==['1']
    # Unrelated files are not slots and must not break the listing.
    path.with_name('notes and drafts.json').write_text('{}')
    assert [i['slot'] for i in saves.slots()]==['1']
    path.with_name('notes and drafts.json').unlink()
    assert not path.exists() and before==path.with_suffix('.json.bak').read_bytes()
    assert saves.info('missing')['status']=='empty'
    try: saves.read('1',recover=False)
    except SaveError: pass
    else: raise AssertionError('recovery-disabled slot loaded')
    loaded=[];canvas=ui.Canvas(ui.Column(),automatic=False)
    menu=MenuController(canvas,saves=saves,restore=loaded.append,slots=1)
    try:
        for language in ('ru','en'):
            forge.set_language(language,persist=False);menu.show_slots();canvas.update()
            buttons=[w for w in menu.panel.children if isinstance(w,ui.Button)]
            assert not buttons[0].enabled
            assert buttons[1].enabled and forge.tr('engine.slots.recoverable') in buttons[1].label
            buttons[1].activate()
            assert loaded[-1]=={'n':1} and not menu.opened
    finally: menu.close();canvas.close()
    assert not path.exists() # listing/loading must not rewrite files
    path.with_suffix('.json.bak').write_text('{broken')
    assert saves.info('1')['status']=='corrupt'
    try: saves.read('1',default={})
    except SaveError: pass
    else: raise AssertionError('damaged backup hidden by default')
    saves.delete('1');assert saves.slots()==[]
    newer=SaveManager(2);newer.write('future',{'n':3});newer.write('future',{'n':4})
    Path(forge.project_path('saves/slots/future.json')).unlink()
    assert saves.info('future')['version']==2
    try: saves.read('future')
    except SaveVersion: pass
    else: raise AssertionError('newer backup version accepted')
    forge.log('BACKUP_MENU_OK');forge.quit()
""")
        self.assertIn('BACKUP_MENU_OK',self.run_engine())
    def test_text_slots_audio_and_dialogue(self):
        self.script_scene(r"""import forge, ui, audio, pathlib
from saves import SaveManager, SaveError, SaveVersion
from dialogue import Dialogue
from menus import MenuController
from settings import AudioSettings
frames=0
def on_start():
    global slots, canvas, dialogue, music
    w,h,line=forge.measure_text('Привет AV', 32)
    assert w>0 and h>0 and line>0
    assert abs(forge.measure_text('Привет AV',64)[0]-w*2)<.01
    wrapped=ui.wrap('Привет мир оченьдлинноеслово12345',80,24)
    assert '\n' in wrapped
    assert all(forge.measure_text(l,24)[0]<=80.01 for l in wrapped.split('\n'))
    slots=SaveManager(1, validate=lambda v: isinstance(v,dict) and 'n' in v)
    slots.write('1',{'n':1},title='Первый')
    slots.write('1',{'n':2},title='Второй')
    assert slots.info('1')['title']=='Второй'
    path=pathlib.Path(forge.project_path('saves/slots/1.json'))
    path.write_text('{broken',encoding='utf-8')
    assert slots.info('1')['status']=='recoverable'
    assert slots.read('1')['n']==1
    try: slots.read('1',recover=False)
    except SaveError: pass
    else: raise AssertionError('corruption accepted')
    slots.write('1',{'n':3})
    newer=SaveManager(2,migrations={1:lambda d:{'n':d['n'],'migrated':True}})
    assert newer.read('1')['migrated']
    newer.write('future',{'n':4})
    try: slots.read('future')
    except SaveVersion: pass
    else: raise AssertionError('future format accepted')
    try: slots.write('../escape',{'n':0})
    except SaveError: pass
    else: raise AssertionError('slot traversal accepted')
    slots.autosave(lambda:{'n':99},interval=.02)
    root=ui.Column(padding=24)
    dialogue=root.add(Dialogue(['Текст для автоматического чтения','Вторая страница'],characters_per_second=10000,auto_delay=0))
    dialogue.toggle_auto()
    root.add(ui.Row(ui.Button('Кнопка',lambda:None),ui.Slider(.4)))
    canvas=ui.Canvas(root)
    menus=MenuController(canvas,saves=slots,capture=lambda:{'n':8},restore=lambda d:None,journal=dialogue.journal)
    menus.show_settings();assert forge.is_paused();menus.close_menu()
    menus.settings.set('voice',.37);menus.settings.flush()
    preferences=AudioSettings();assert abs(preferences.values['voice']-.37)<.001;preferences.close()
    menus.show_slots();menus._slot('2',True);assert slots.read('2')['n']==8;menus.close_menu()
    menus.show_journal();menus.close_menu()
    audio.music.volume=.3;audio.master_volume(.5)
    assert abs(forge.channel_volume('music')-.3)<.001
    effect=audio.sfx.play('notify.wav',loop=True,volume=.25)
    assert abs(effect.volume-.25)<.001
    effect.volume=.4;assert abs(effect.volume-.4)<.001
    effect.fade(.2,.02)
    music=audio.music.crossfade('notify.wav',.02)
    audio.music.crossfade('notify.wav',.02)
    assert len(forge.channel_sounds('music'))==2
    music.pause();assert not music.playing;music.resume()
    try: forge.set_channel_volume('sfx',float('nan'))
    except RuntimeError: pass
    else: raise AssertionError('invalid volume')
def on_update(dt):
    global frames
    frames+=1
    if frames==5:
        assert slots.read('auto')['n']==99
        assert abs(forge.sound_volume(forge.channel_sounds('sfx')[0])-.2)<.001
        assert len(forge.channel_sounds('music'))==1
    if frames==100:
        assert dialogue.finished
        assert len(dialogue.journal.entries)==2
        checkpoint=dialogue.capture();dialogue.restore(checkpoint)
        canvas.close();forge.log('HIGH_LEVEL_OK');forge.quit()
""")
        self.assertIn('HIGH_LEVEL_OK',self.run_engine(frames=120))
    def test_ui_input_layout_resize_and_cleanup(self):
        shutil.copy2(ROOT/'tests/ui_components.py',self.root/'modules/ui_contract.py')
        self.script_scene("import forge\nfrom ui_contract import verify\ndef on_start(): verify();forge.quit()\n")
        self.assertIn('UI_INPUT_OK',self.run_engine())
    def test_interface_example(self):
        self.config['entry_scene']='interface.py';self.write_config()
        self.assertIn('UI_READY',self.run_engine(frames=10))
    def test_hot_reload_rolls_back_settings_imports_and_audio(self):
        (self.root/'modules/helper.py').write_text('VALUE=1\n')
        self.script_scene("""import forge, helper
from pathlib import Path
old_root=forge.settings()['project']['name']
old_paths=list(__import__('sys').path)
def on_reload_failed(message):
    assert forge.settings()['project']['name']==old_root
    assert __import__('helper').VALUE==1
    assert __import__('sys').path==old_paths
    assert forge.find('original').alive
    assert forge.find('candidate') is None
    assert abs(forge.channel_volume('music')-.4)<.001
    assert forge.channel_sounds('music')==[original_sound]
    assert not forge.channel_sounds('voice')
    assert forge.sound_playing(original_sound)
    forge.log('ROLLBACK_OK')
def on_start():
    global original_sound
    original_sound=forge.play_sound('notify.wav',loop=True,channel='music')
    forge.spawn({'id':'original','kind':'empty'})
    forge.set_channel_volume('music',.4)
    forge.log('READY_FOR_RELOAD')
def on_destroy():
    assert forge.settings()['project']['name']==old_root
    assert forge.find('original').alive
""")
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                file=self.root/'forge.log'
                if file.exists() and marker in file.read_text(): return
                if process.poll() is not None:self.fail(file.read_text())
                time.sleep(.03)
            self.fail('Missing '+marker)
        try:
            wait('READY_FOR_RELOAD')
            (self.root/'modules/helper.py').write_text('VALUE=2\n')
            (self.root/'scenes/test.py').write_text("import forge, helper\ndef on_start():\n    assert helper.VALUE==2\n    forge.spawn({'id':'candidate','kind':'empty'})\n    forge.set_channel_volume('music',.9)\n    forge.stop_sounds()\n    forge.play_sound('notify.wav',loop=True,channel='voice')\n    forge.on_frame(lambda dt: (_ for _ in ()).throw(RuntimeError('LEAKED_LISTENER')),persistent=True)\n    raise ValueError('CANDIDATE_FAILED')\n")
            self.config['project']['name']='Changed';self.write_config();wait('ROLLBACK_OK')
            # The previous on_reload_failed callback audited state before recovery.
            # An unsuccessful candidate must not leak helper, config, audio or listener state.
            (self.root/'scenes/test.py').write_text("import forge, helper\ndef on_start():\n    assert helper.VALUE==2\n    forge.log('RECOVERY_SETTINGS_'+forge.settings()['project']['name'])\ndef on_update(dt): forge.quit()\n")
            wait('Hot reload complete');self.assertEqual(process.wait(timeout=15),0)
            output=(self.root/'forge.log').read_text();self.assertIn('CANDIDATE_FAILED',output);self.assertIn('ROLLBACK_OK',output);self.assertIn('RECOVERY_SETTINGS_Changed',output)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def write_language(self, code, messages):
        (self.root/'locales'/f'{code}.json').write_text(json.dumps(messages,ensure_ascii=False),encoding='utf-8')
        self.config['localization']['languages'][code]={'name':code,'file':code+'.json'}
        self.write_config()
    def test_localization_plural_parameters_and_fallback(self):
        forms={key:'{count} '+key for key in ('zero','one','two','few','many','other')}
        for code in ('ru','en','ar','pl','cs','fr','ja','cy','sl','pt-PT'):
            self.write_language(code, {'items':forms,'greeting':{'hello':'Hello {name}!'},'escaped':'{{{name}}}'})
        english=json.loads((self.root/'locales/en.json').read_text());english['fallback_items']=forms
        self.write_language('en',english)
        self.write_language('en-GB',{'greeting':{'hello':'Cheers {name}!'}})
        self.script_scene("""import forge
def on_start():
    assert forge.language()=='ru'
    assert len(forge.available_languages())==11
    assert forge.tr('greeting.hello',name='Мир')=='Hello Мир!'
    assert forge.tr('escaped',name='x')=='{x}'
    samples={'ru':[(1,'one'),(2,'few'),(5,'many'),(11,'many'),(21,'one'),(1.5,'other'),(1.0,'other'),(-22,'few'),(1000000000000000001,'one')],
        'en':[(1,'one'),(1.0,'other'),(2,'other')], 'ar':[(0,'zero'),(1.0,'one'),(2,'two'),(3,'few'),(11,'many'),(102,'other')],
        'pl':[(1,'one'),(2,'few'),(5,'many'),(12,'many'),(22,'few'),(1.5,'other')], 'cs':[(1,'one'),(4,'few'),(5,'other'),(1.5,'many')],
        'fr':[(0,'one'),(1.5,'one'),(2,'other'),(1000000,'many')], 'ja':[(1,'other')],
        'cy':[(0,'zero'),(1,'one'),(2,'two'),(3,'few'),(6,'many'),(4,'other')], 'sl':[(1,'one'),(2,'two'),(3,'few'),(1.5,'few')],
        'pt-pt':[(0,'other'),(1,'one'),(1.0,'other')]}
    for language, tests in samples.items():
        forge.set_language(language,persist=False)
        for n,form in tests:
            assert forge.tr('items',count=n).endswith(' '+form), (language,n,forge.tr('items',count=n))
    forge.set_language('RU_ru',persist=False);assert forge.language()=='ru'
    assert forge.tr('fallback_items',count=21)=='21 other' # supplying catalog's grammar
    assert forge.has_translation('fallback_items') and not forge.has_translation('fallback_items',fallback=False)
    forge.set_language('EN_gb',persist=False)
    assert forge.language()=='en-gb' and forge.tr('greeting.hello',name='Jo')=='Cheers Jo!'
    assert forge.tr('items',count=1)=='1 one' # parent catalog
    assert forge.has_translation('items','en-GB')
    for bad in ['unregistered','../ru','ru--RU']:
        try: forge.set_language(bad)
        except RuntimeError: pass
        else: raise AssertionError('accepted language '+bad)
    assert forge.language()=='en-gb'
    for call in [lambda:forge.tr('items'),lambda:forge.tr('items',count='1'),lambda:forge.tr('greeting.hello')]:
        try: call()
        except RuntimeError: pass
        else: raise AssertionError('accepted bad parameters')
    for _ in range(3): assert forge.tr('unknown.key')=='unknown.key'
    forge.log('LOCALIZATION_RULES_OK');forge.quit()
""")
        output=self.run_engine()
        self.assertIn('LOCALIZATION_RULES_OK',output)
        self.assertEqual(output.count('Localization key not found [en-gb]: unknown.key'),1)
    def test_localization_validation_and_old_project(self):
        file=self.root/'locales/ru.json'
        for value, marker in [({'bad':{'one':'one'}},'needs other'),({'bad':'{name'},'Unmatched'),({'bad':['text']},'must be string'),({'a.b':'one','a':{'b':'two'}},'duplicate')]:
            file.write_text(json.dumps(value),encoding='utf-8')
            self.assertIn(marker.lower(),self.run_engine('validate',expected=1).lower())
        self.write_language('ru',{'test':'{name}'})
        self.write_language('en',{'test':'{other}'})
        self.assertIn('placeholder mismatch',self.run_engine('validate',expected=1))
        self.write_language('en',{'test':'{name}'})
        self.write_language('zz',{'test':{'other':'{name}'}})
        self.assertIn('No CLDR plural rule',self.run_engine('validate',expected=1))
        self.config['localization']['languages']['zz']['plural_language']='other';self.write_config()
        self.assertIn('validated',self.run_engine('validate'))
        self.config.pop('localization');self.config['paths'].pop('locales');self.write_config()
        self.script_scene("import forge\ndef on_start():\n    assert forge.language()=='en'\n    assert forge.tr('engine.menu.pause')=='Paused'\n    forge.log('OLD_PROJECT_OK');forge.quit()\n")
        self.assertIn('OLD_PROJECT_OK',self.run_engine())
    def test_localization_reactive_entities_ui_and_journal(self):
        self.write_language('ru',{'hello':'Привет {name}','page':'Коротко','speaker':'Автор'})
        self.write_language('en',{'hello':'Hello {name}','page':'A longer translated page','speaker':'Author'})
        self.script_scene("""import forge,ui,json
from dialogue import Dialogue,Journal
from menus import MenuController
from saves import SaveManager
def build(): return {'entities':[{'id':'caption','kind':'text','text_key':'hello','text_params':{'name':'Jo'}}]}
def on_start():
    e=forge.find('caption');assert e.text=='Привет Jo'
    message=forge.message('hello',name='Jo')
    label=ui.Label(message);button=ui.Button(message,lambda:None)
    old=ui.text('legacy',message,0,0)
    d=Dialogue([dict(speaker=forge.message('speaker'),text=forge.message('page'))])
    root=ui.Column(label,button,d);canvas=ui.Canvas(root)
    menus=MenuController(canvas,journal=d.journal)
    menus.show_settings();d.advance();assert d.body.value=='Коротко'
    journal_view=d.journal.view()
    saved=d.capture();slots=SaveManager();slots.write('loc',saved)
    forge.set_language('en',persist=False);d.update(0);menus.update(0);canvas.update(0)
    assert e.text=='Hello Jo' and old.text=='Hello Jo'
    assert label.value=='Hello Jo' and button.label=='Hello Jo'
    assert d.body.value=='A longer translated page' and d.speaker.value=='Author'
    assert d.next_button.label=='Next'
    assert menus.panel.children[0].value=='Settings'
    assert journal_view.children[0].value=='Author\\nA longer translated page'
    d.restore(slots.read('loc'));assert Journal.resolve(d.journal.entries[0],'text')=='A longer translated page'
    assert d.body.value=='A longer translated page'
    e.text=forge.message('hello',name='Ana');assert e.text=='Hello Ana'
    e.text_params={'name':'Pat'};assert e.text=='Hello Pat'
    e.text='literal';forge.set_language('ru',persist=False);assert e.text=='literal' and e.text_key==''
    revision=forge.localization_revision()
    forge.spawn({'id':'invalid','kind':'text','text_key':'hello','text_params':{'name':'valid'}}).text_params={}
    try: forge.set_language('en')
    except RuntimeError: pass
    else: raise AssertionError('accepted invalid localized entity')
    assert forge.language()=='ru' and forge.localization_revision()==revision
    forge.find('invalid').destroy()
    menus.close();canvas.close();forge.log('LOCALIZED_UI_OK');forge.quit()
""")
        self.assertIn('LOCALIZED_UI_OK',self.run_engine())
    def test_language_preference_restart_corruption_and_failed_start(self):
        self.script_scene("import forge\ndef on_start():\n    forge.set_language('en');forge.quit()\n")
        self.run_engine()
        path=self.root/'saves/preferences/language.json'
        self.assertEqual(json.loads(path.read_text())['language'],'en')
        self.script_scene("import forge\ndef on_start():\n    assert forge.language()=='en'\n    forge.set_language('ru',persist=False);forge.quit()\n")
        self.run_engine();self.assertEqual(json.loads(path.read_text())['language'],'en')
        path.write_text('{broken')
        self.script_scene("import forge\ndef on_start():\n    assert forge.language()=='ru'\n    forge.quit()\n")
        self.assertIn('Ignoring language preference',self.run_engine())
        path.unlink()
        self.script_scene("import forge\ndef on_start():\n    forge.set_language('en')\n    raise RuntimeError('FAIL_AFTER_LANGUAGE')\n")
        self.assertIn('FAIL_AFTER_LANGUAGE',self.run_engine(expected=1));self.assertFalse(path.exists())
        self.config['localization']['save_selection']=False;self.write_config()
        self.script_scene("import forge\ndef on_start(): forge.set_language('en');forge.quit()\n")
        self.run_engine();self.assertFalse(path.exists())
    def test_localization_hot_reload_transaction(self):
        self.write_language('ru',{'version':'original'})
        self.script_scene("""import forge,json
from pathlib import Path
def on_start():
    global revision
    revision=forge.localization_revision()
    forge.set_language('ru')
    forge.spawn({'id':'stable','kind':'text','text_key':'version'})
    forge.log('LOCALE_READY')
def on_reload_failed(error):
    assert forge.language()=='ru' and forge.localization_revision()==revision
    assert forge.tr('version')=='original' and forge.find('stable').text=='original'
    assert json.loads(Path(forge.project_path('saves/preferences/language.json')).read_text())['language']=='ru'
    forge.log('LOCALE_ROLLBACK_OK')
def on_destroy():
    assert forge.tr('version')=='original' and forge.language()=='ru'
    forge.set_language('en') # isolated teardown must not persist
""")
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and marker in log.read_text():return
                if process.poll() is not None:self.fail(log.read_text())
                time.sleep(.03)
            self.fail('Missing '+marker)
        try:
            wait('LOCALE_READY')
            # Wait for initial deferred preference commit, then fail initialization.
            deadline=time.monotonic()+5
            while not (self.root/'saves/preferences/language.json').exists():
                self.assertLess(time.monotonic(),deadline);time.sleep(.02)
            (self.root/'scenes/test.py').write_text("import forge\ndef on_start():\n    forge.set_language('en')\n    raise RuntimeError('LOCALE_CANDIDATE_FAILURE')\n")
            self.write_language('ru',{'version':'updated'})
            wait('LOCALE_ROLLBACK_OK')
            (self.root/'scenes/test.py').write_text("import forge\ndef on_start():\n    assert forge.language()=='ru' and forge.tr('version')=='updated'\n    forge.log('LOCALE_RECOVERED')\ndef on_update(dt): forge.quit()\n")
            wait('LOCALE_RECOVERED');self.assertEqual(process.wait(timeout=15),0)
            log=(self.root/'forge.log').read_text();self.assertNotIn('[ERROR] AssertionError',log)
            self.assertEqual(json.loads((self.root/'saves/preferences/language.json').read_text())['language'],'ru')
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_starter_project_includes_docs_and_localization(self):
        target=self.root/'new-project'
        result=subprocess.run([sys.executable,str(ROOT/'tools/forge.py'),'init','--output',str(target)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertTrue((target/'locales/en.json').exists())
        self.assertFalse((target/'Инструкция.md').exists())
        for relative in [Path('GUIDE.md'), *(file.relative_to(ROOT) for file in (ROOT/'docs/wiki').glob('*.md'))]:
            self.assertEqual((target/relative).read_bytes(), (ROOT/relative).read_bytes())
        self.assertEqual((target/'licenses/Unicode.txt').read_bytes(),(ROOT/'engine/resources/Unicode-LICENSE.txt').read_bytes())
        output=self.run_engine('validate',extra=('--project',target/'engine.json'))
        self.assertIn('validated',output)
    def test_packager_rejects_output_in_python_paths_before_copy(self):
        (self.root/'extra').mkdir()
        self.config['python_paths']=['extra'];self.write_config()
        for relative in ('extra/new/Game','modules/new/Game'):
            target=self.root/relative
            self.assertIn('Output cannot be inside an asset directory',self.run_engine('build',expected=1,extra=('--output',target)))
            self.assertFalse(target.parent.exists())
        self.assertFalse(list(self.root.rglob('.forge-build-*')))
    def test_packaged_game(self):
        if sys.platform not in ('darwin','win32'):self.skipTest('packaging OS')
        self.script_scene("import forge, json, math, sqlite3, ssl, zlib, ctypes, pathlib, sys\ndef on_start():\n    assert pathlib.Path(sys.prefix).samefile(forge.project_path('runtime'))\n    assert forge.tr('engine.menu.pause')=='Пауза'\n    forge.set_language('en')\n    assert forge.tr('example.welcome.title')=='Create your worlds.'\n    forge.save('packaged', {'works':True})\n    forge.log('PACKAGED_OK')\n    forge.log('PACKAGED_VERSION_'+forge.__version__)\n    forge.quit()\n")
        (self.root/'extra').mkdir();(self.root/'extra/package_extra.py').write_text('VALUE=42\n')
        script=self.root/'scenes/test.py'
        script.write_text(script.read_text().replace('import forge, json', 'import package_extra; assert package_extra.VALUE==42\nimport forge, json'))
        alias='../'+self.root.name+'/'
        self.config['paths']={k:alias+v for k,v in self.config['paths'].items()}
        self.config['python_paths']=[alias+'extra']
        self.config['project']['icon']=alias+'textures/icon.png'
        self.config['save_directory']=alias+'saves';self.write_config()
        target=self.root/'dist/game'
        self.run_engine('build',extra=('--output',target))
        binary=target/('Game.exe' if os.name=='nt' else 'Game')
        env=os.environ.copy();env['PYTHONHOME']='/does/not/exist';env['PYTHONPATH']='/does/not/exist'
        result=subprocess.run([str(binary),'--headless','--no-open-log'],cwd=tempfile.gettempdir(),env=env,text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('PACKAGED_OK',result.stdout)
        self.assertTrue((target/'saves/packaged.json').exists())
        packaged=json.loads((target/'game.json').read_text())
        self.assertEqual(packaged['python_paths'],['extra'])
        self.assertEqual(packaged['paths']['modules'],'modules')
        self.assertEqual(packaged['project']['icon'],'textures/icon.png')
        self.assertEqual(packaged['save_directory'],'saves')
        self.assertEqual((target/'licenses/Unicode.txt').read_bytes(),(ROOT/'engine/resources/Unicode-LICENSE.txt').read_bytes())
        manifest=json.loads((target/'manifest.json').read_text())
        self.assertIn('PACKAGED_VERSION_'+manifest['engine_version'],result.stdout)
        self.assertIn('START.txt',manifest['files'])
        for relative, checksum in manifest['files'].items():
            self.assertEqual(hashlib.sha256((target/relative).read_bytes()).hexdigest(),checksum,relative)
        for document in ('LICENSE', 'NOTICE', 'CORE.md', 'ATTRIBUTION.md'):
            self.assertEqual((target/document).read_bytes(), (ROOT/document).read_bytes())
        self.assertIn('Output already exists',self.run_engine('build',expected=1,extra=('--output',target)))

    def test_scene_ids_reserve_explicit_forward_declarations(self):
        self.script_scene("""import forge
def build():return {'entities':[{'kind':'empty','name':'Anonymous'},{'id':'entity_0','kind':'empty','name':'Explicit'}]}
def on_start():
    assert len(forge.entities())==2 and forge.find('entity_0').name=='Explicit'
    assert len({e.id for e in forge.entities()})==2
    forge.log('FORWARD_IDS_OK');forge.quit()
""")
        self.assertIn('FORWARD_IDS_OK',self.run_engine())

    def test_legacy_physics_failure_restores_whole_world(self):
        self.script_scene("""import forge
def build():return {'gravity':[3e38,0,0],'physics_enabled':False}
def on_start():
    first=forge.spawn({'id':'first','kind':'empty','dynamic':True,'velocity':[1,0,0]})
    forge.spawn({'id':'child','kind':'empty','parent':'first','position':[2,0,0]})
    last=forge.spawn({'id':'last','kind':'empty','dynamic':True,'velocity':[3.4e38,0,0]})
    before=forge.scene_data();matrix=forge.find('child').world_matrix
    try:forge.physics_step(.1)
    except RuntimeError:pass
    else:raise AssertionError('overflow accepted')
    assert forge.scene_data()==before and forge.find('child').world_matrix==matrix
    forge.log('PHYSICS_TRANSACTION_OK');forge.quit()
""")
        self.assertIn('PHYSICS_TRANSACTION_OK',self.run_engine())

    def test_text_measurement_overflow_and_integer_uniform_limits(self):
        self.script_scene("""import forge,math
def on_start():
    for text in ('WWWWWWWW','W\\nW\\nW'):
        try:result=forge.measure_text(text,3e38)
        except RuntimeError:pass
        else:assert all(math.isfinite(v) for v in result),result
    for value in (-2147483648,2147483647):forge.set_shader_uniform('valid',value)
    forge.set_render_target('mask',{'layers':4294967295})
    forge.log('MEASUREMENT_LIMITS_OK');forge.quit()
""")
        self.assertIn('MEASUREMENT_LIMITS_OK',self.run_engine())

    def test_extra_python_paths_are_checked_for_syntax(self):
        (self.root/'extra').mkdir();(self.root/'extra/broken.py').write_text('def broken(\n')
        self.config['python_paths']=['extra'];self.write_config()
        self.assertIn('SyntaxError',self.run_engine('validate',expected=1))

    def test_localization_refresh_skips_unchanged_entities(self):
        for code in ('en','ru'):
            path=self.root/'locales'/(code+'.json');messages=json.loads(path.read_text());messages['cache.test']='Hello {name}' if code=='en' else 'Привет {name}'
            path.write_text(json.dumps(messages,ensure_ascii=False),encoding='utf-8')
        self.script_scene("""import forge
frames=0
def on_start():
    forge.set_language('en',persist=False)
    for i in range(1000):forge.spawn({'id':'label'+str(i),'kind':'text','text_key':'cache.test','text_params':{'name':'A'}})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        assert forge.profile()['localization_translations']==0
        forge.find('label0').text_params={'name':'B'}
    if frames==3:
        assert forge.profile()['localization_translations']==1
        e=forge.find('label0');assert e.text=='Hello B'
        e.text='Literal';e.text_key='cache.test';e.text_params={'name':'B'}
    if frames==4:
        assert forge.profile()['localization_translations']==1
        forge.set_language('ru',persist=False)
        assert forge.profile()['localization_translations']==1000
        assert forge.scene_data()['entities'][0]['text']=='Привет B'
    if frames==5:
        assert forge.profile()['localization_translations']==0
        forge.log('LOCALIZATION_CACHE_OK');forge.quit()
""")
        self.assertIn('LOCALIZATION_CACHE_OK',self.run_engine())

    def test_save_recovers_from_stale_regular_temp_file(self):
        (self.root/'saves').mkdir(exist_ok=True)
        (self.root/'saves/crashed.json.tmp').write_text('partial')
        self.script_scene("""import forge
def on_start():
    forge.save('crashed',{'ok':True})
    assert forge.load('crashed')=={'ok':True}
    forge.log('STALE_TEMP_SAVE_OK');forge.quit()
""")
        self.assertIn('STALE_TEMP_SAVE_OK',self.run_engine())
        self.assertFalse((self.root/'saves/crashed.json.tmp').exists())
    def test_save_symlinks_and_existing_temp_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix='forge-external-') as folder:
            outside=Path(folder)/'protected.json';outside.write_text('{"private":true}')
            (self.root/'saves').mkdir(exist_ok=True);(self.root/'slots').mkdir()
            try:
                (self.root/'saves/escape.json').symlink_to(outside)
                (self.root/'saves/temporary.json.tmp').symlink_to(outside)
                (self.root/'slots/escape.json.bak').symlink_to(outside)
            except OSError as error:self.skipTest(f'Symlinks unavailable: {error}')
            self.script_scene("""import forge
from saves import SaveManager,SaveError
def on_start():
    for operation in (lambda:forge.save('escape',{}),lambda:forge.load('escape'),lambda:forge.save('temporary',{})):
        try:operation()
        except RuntimeError:pass
        else:raise AssertionError('unsafe native save path accepted')
    store=SaveManager(directory='slots')
    for operation in (lambda:store.read('escape'),lambda:store.write('escape',{}),lambda:store.delete('escape')):
        try:operation()
        except SaveError:pass
        else:raise AssertionError('unsafe slot backup accepted')
    forge.log('SAFE_SAVE_PATHS_OK');forge.quit()
""")
            self.assertIn('SAFE_SAVE_PATHS_OK',self.run_engine())
            self.assertEqual(outside.read_text(),'{"private":true}')

if __name__ == '__main__': unittest.main(verbosity=2)
