"""Exercise the real native binary, Python bridge, physics, reload, and packaging."""
from pathlib import Path
import json, os, shutil, subprocess, sys, tempfile, time, unittest
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
    def test_packaged_game(self):
        if sys.platform not in ('darwin','win32'):self.skipTest('packaging OS')
        self.script_scene("import forge, json, math, sqlite3, ssl, zlib, ctypes, pathlib, sys\ndef on_start():\n    assert pathlib.Path(sys.prefix).samefile(forge.project_path('runtime'))\n    forge.save('packaged', {'works':True})\n    forge.log('PACKAGED_OK')\n    forge.quit()\n")
        target=self.root/'dist/game'
        self.run_engine('build',extra=('--output',target))
        binary=target/('Game.exe' if os.name=='nt' else 'Game')
        env=os.environ.copy();env['PYTHONHOME']='/does/not/exist';env['PYTHONPATH']='/does/not/exist'
        result=subprocess.run([str(binary),'--headless','--no-open-log'],cwd=tempfile.gettempdir(),env=env,text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('PACKAGED_OK',result.stdout)
        self.assertTrue((target/'saves/packaged.json').exists())
        self.assertTrue((target/'manifest.json').exists())
        for document in ('LICENSE', 'NOTICE', 'CORE.md', 'ATTRIBUTION.md'):
            self.assertEqual((target/document).read_bytes(), (ROOT/document).read_bytes())
        self.assertIn('Output already exists',self.run_engine('build',expected=1,extra=('--output',target)))

if __name__ == '__main__': unittest.main(verbosity=2)
