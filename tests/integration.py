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
    def test_default_scene_and_renamed_layout(self):
        for key, old in self.config['paths'].items():
            new = 'ресурсы/' + key
            target = self.root / new; target.parent.mkdir(exist_ok=True)
            (self.root / old).rename(target); self.config['paths'][key] = new
        self.config['project']['icon'] = self.config['paths']['textures'] + '/icon.png';self.write_config()
        self.assertIn('2D сцена готова',self.run_engine())
        self.assertIn('validated',self.run_engine('validate'))
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
    assert slots.info('1')['status']=='corrupt'
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
    def test_packaged_game(self):
        if sys.platform not in ('darwin','win32'):self.skipTest('packaging OS')
        self.script_scene("import forge, json, math, sqlite3, ssl, zlib, ctypes, pathlib, sys\ndef on_start():\n    assert pathlib.Path(sys.prefix).samefile(forge.project_path('runtime'))\n    forge.save('packaged', {'works':True})\n    forge.log('PACKAGED_OK')\n    forge.log('PACKAGED_VERSION_'+forge.__version__)\n    forge.quit()\n")
        target=self.root/'dist/game'
        self.run_engine('build',extra=('--output',target))
        binary=target/('Game.exe' if os.name=='nt' else 'Game')
        env=os.environ.copy();env['PYTHONHOME']='/does/not/exist';env['PYTHONPATH']='/does/not/exist'
        result=subprocess.run([str(binary),'--headless','--no-open-log'],cwd=tempfile.gettempdir(),env=env,text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('PACKAGED_OK',result.stdout)
        self.assertTrue((target/'saves/packaged.json').exists())
        manifest=json.loads((target/'manifest.json').read_text())
        self.assertIn('PACKAGED_VERSION_'+manifest['engine_version'],result.stdout)
        self.assertIn('START.txt',manifest['files'])
        for relative, checksum in manifest['files'].items():
            self.assertEqual(hashlib.sha256((target/relative).read_bytes()).hexdigest(),checksum,relative)
        for document in ('LICENSE', 'NOTICE', 'CORE.md', 'ATTRIBUTION.md'):
            self.assertEqual((target/document).read_bytes(), (ROOT/document).read_bytes())
        self.assertIn('Output already exists',self.run_engine('build',expected=1,extra=('--output',target)))

if __name__ == '__main__': unittest.main(verbosity=2)
