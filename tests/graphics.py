"""Optional tests requiring a real desktop, OpenGL 3.3 driver and audio device."""
from pathlib import Path
import json, os, shutil, subprocess, sys, tempfile, time, unittest
ROOT=Path(__file__).resolve().parents[1]
ENGINE=Path(sys.argv.pop(1)).resolve()
def engine_command(command):
    return [str(ENGINE), command, *(['--silent-audio'] if os.environ.get('FORGE_TEST_SILENT_AUDIO') == '1' else [])]

class GraphicsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='forge-graphics-');self.root=Path(self.temp.name)
        self.config=json.loads((ROOT/'engine.json').read_text())
        self.config['startup_scripts']=[];self.config['logging']['open_on_error']=False
        if os.environ.get('FORGE_TEST_BACKEND'):
            self.config['renderer']['backend']=os.environ['FORGE_TEST_BACKEND']
        if self.config['renderer'].get('backend')=='direct3d11':
            self.config['renderer'].setdefault('direct3d11',{})['driver']=os.environ.get('FORGE_TEST_DIRECT3D_DRIVER','warp')
        for folder in self.config['paths'].values():shutil.copytree(ROOT/folder,self.root/folder,ignore=shutil.ignore_patterns('__pycache__'))
        self.write_config()
    def tearDown(self):self.temp.cleanup()
    def write_config(self):(self.root/'engine.json').write_text(json.dumps(self.config),encoding='utf-8')
    def wait_log(self,marker,process):
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            file=self.root/'forge.log'
            if file.exists() and marker in file.read_text():return
            if process.poll() is not None:self.fail(file.read_text() if file.exists() else 'Runtime stopped')
            time.sleep(.03)
        self.fail('Timed out waiting for '+marker)
    def test_both_renderers_and_audio(self):
        (self.root/'scripts/capture.py').write_text("import forge\ndef on_start():\n    forge.screenshot('frame.ppm')\n    forge.play_sound('notify.wav', volume=.03)\n")
        self.config['startup_scripts']=['capture.py']
        for scene in ('welcome.json','world3d.py','interface.py'):
            self.config['entry_scene']=scene;self.write_config()
            result=subprocess.run([*engine_command('run'),'--project',str(self.root/'engine.json'),'--frames','20','--no-open-log'],text=True,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            frame=(self.root/'frame.ppm').read_bytes();self.assertTrue(frame.startswith(b'P6\n'));self.assertGreater(len(set(frame[100:])),4)
    def test_extreme_glyph_dimensions_are_bounded_before_integer_conversion(self):
        (self.root/'scenes/glyph_limits.py').write_text("""import forge
frames=0
def on_start():
    forge.spawn({'kind':'text','screen':True,'text':'Glyph limits','font_size':40,'position':[20,20,0]})
    for size,scale in ((1e10,1),(3e38,3e38)):
        forge.spawn({'kind':'text','screen':True,'text':'A','font_size':size,
                     'scale':[scale,scale,1],'position':[-3e38,-3e38,0]})
def on_update(dt):
    global frames
    frames+=1
    if frames==3: forge.screenshot('glyph-limits.ppm')
""")
        self.config['entry_scene']='glyph_limits.py';self.write_config()
        result=subprocess.run([*engine_command('run'),'--project',str(self.root/'engine.json'),'--frames','5','--no-open-log'],text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        frame=(self.root/'glyph-limits.ppm').read_bytes()
        self.assertTrue(frame.startswith(b'P6\n'));self.assertGreater(len(set(frame[100:])),20)
    def test_responsive_ui_and_large_text(self):
        (self.root/'scenes/capture_ui.py').write_text("""import forge, interface
frames=0
def on_start():
    interface.on_start()
    forge.on_frame(tick)
def tick(dt):
    global frames
    frames+=1
    if frames==3: forge.screenshot('ui.ppm')
    if frames==5: interface.menus.show_settings()
    if frames==7: forge.screenshot('settings.ppm')
    if frames==9: interface.menus.show_slots(save=True)
    if frames==11: forge.screenshot('slots.ppm')
def on_destroy(): interface.on_destroy()
""")
        self.config['entry_scene']='capture_ui.py'
        for width,height in [(640,480),(1280,720),(1920,1080)]:
            self.config['window'].update(width=width,height=height);self.write_config()
            result=subprocess.run([*engine_command('run'),'--project',str(self.root/'engine.json'),'--frames','14','--no-open-log'],text=True,capture_output=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for file in ('ui.ppm','settings.ppm','slots.ppm'):
                frame=(self.root/file).read_bytes();self.assertTrue(frame.startswith(b'P6\n'));self.assertGreater(len(set(frame[100:])),20)
    def test_live_language_switch_updates_real_frame(self):
        (self.root/'scenes/language.py').write_text("""import forge,ui
from menus import MenuController
frames=0
def on_start():
    global canvas,menus
    canvas=ui.Canvas(ui.Column(ui.Label(forge.message('example.ui.title'),size=60),
        ui.Button(forge.message('engine.menu.resume'),lambda:None),padding=40))
    menus=MenuController(canvas)
    forge.on_frame(tick)
def tick(dt):
    global frames
    frames+=1
    if frames==3: forge.screenshot('ru.ppm')
    if frames==5: forge.set_language('en',persist=False)
    if frames==7:
        assert canvas.root.children[0].value=='An interface for your game'
        forge.screenshot('en.ppm')
    if frames==9: menus.show_settings()
    if frames==11: forge.screenshot('en-settings.ppm')
def on_destroy(): menus.close();canvas.close()
""")
        self.config['entry_scene']='language.py';self.write_config()
        result=subprocess.run([*engine_command('run'),'--project',str(self.root/'engine.json'),'--frames','14','--no-open-log'],text=True,capture_output=True,timeout=45)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        ru,en=[(self.root/file).read_bytes() for file in ('ru.ppm','en.ppm')]
        self.assertNotEqual(ru,en)
        self.assertGreater(len(set((self.root/'en-settings.ppm').read_bytes()[100:])),20)
    def test_scene_failure_keeps_previous_shader_resources(self):
        source="""import forge
from pathlib import Path
frames=0
def on_start():
    forge.spawn({'id':'stable','kind':'sprite','position':[100,100,0],'scale':[100,100,1],'color':[.2,.8,.3,1]})
    forge.screenshot('before.ppm')
def on_reload_failed(error):
    assert forge.find('stable').alive
    forge.screenshot('rollback.ppm')
def on_update(dt):
    if Path(forge.project_path('stop')).exists(): forge.quit()
"""
        scene=self.root/'scenes/stable.py';scene.write_text(source);self.config['entry_scene']='stable.py';self.write_config()
        process=subprocess.Popen([*engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('before.ppm',process)
            fragment=self.root/'graphics/default.frag'
            fragment.write_text('#version 330 core\nout vec4 out_color;\nvoid main(){out_color=vec4(1,0,0,1);}')
            scene.write_text("import forge\ndef on_start(): forge.spawn({'kind':'sprite','texture':'missing.png'})\n")
            self.wait_log('rollback.ppm',process)
            self.assertEqual((self.root/'before.ppm').read_bytes(),(self.root/'rollback.ppm').read_bytes())
            scene.write_text(source.replace("'before.ppm'","'after.ppm'"));self.wait_log('after.ppm',process)
            self.assertNotEqual((self.root/'before.ppm').read_bytes(),(self.root/'after.ppm').read_bytes())
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_backend_change_rejects_reload_and_preserves_working_device(self):
        source="""import forge
from pathlib import Path
def on_start():
    forge.spawn({'id':'stable','kind':'sprite','position':[100,100,0],'scale':[100,100,1],'color':[0,1,0,1]})
    forge.screenshot('device-before.ppm')
def on_reload_failed(error):
    assert 'restart' in error or 'unavailable' in error,error
    assert forge.find('stable').alive
    forge.screenshot('device-rollback.ppm')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
"""
        (self.root/'scenes/device.py').write_text(source,encoding='utf-8')
        self.config['entry_scene']='device.py';self.write_config()
        original=json.loads(json.dumps(self.config))
        process=subprocess.Popen([*engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('device-before.ppm',process)
            active=self.config['renderer'].get('backend','opengl')
            self.config['renderer']['backend']='opengl' if active=='direct3d11' else 'direct3d11'
            self.write_config();self.wait_log('device-rollback.ppm',process)
            self.assertEqual((self.root/'device-before.ppm').read_bytes(),(self.root/'device-rollback.ppm').read_bytes())
            self.config=original;self.write_config();self.wait_log('Hot reload complete',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_invalid_font_reload_preserves_frame_and_recovers(self):
        source="""import forge
from pathlib import Path
def on_start():
    forge.spawn({'id':'stable-text','kind':'text','screen':True,'text':'Stable font',
                 'font_size':60,'position':[40,40,0]})
    forge.screenshot('font-before.ppm')
def on_reload_failed(error):
    assert 'TrueType font' in error,error
    assert forge.find('stable-text').alive
    forge.screenshot('font-rollback.ppm')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
"""
        (self.root/'scenes/font_reload.py').write_text(source,encoding='utf-8')
        self.config['entry_scene']='font_reload.py';self.write_config()
        font=self.root/self.config['paths']['graphics']/self.config['renderer']['font']
        original=font.read_bytes()
        process=subprocess.Popen([*engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('font-before.ppm',process)
            font.write_bytes(b'')
            self.wait_log('font-rollback.ppm',process)
            self.assertEqual((self.root/'font-before.ppm').read_bytes(),(self.root/'font-rollback.ppm').read_bytes())
            font.write_bytes(original);self.wait_log('Hot reload complete',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()

    def test_shader_error_recovers_without_closing_window(self):
        (self.root/'scenes/control.py').write_text("import forge\ndef on_update(dt):\n    if __import__('pathlib').Path(forge.project_path('stop')).exists(): forge.quit()\n")
        scene=self.root/'scenes/welcome.json';data=json.loads(scene.read_text());data['script']='control.py';scene.write_text(json.dumps(data))
        process=subprocess.Popen([*engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('Scene loaded',process)
            shader=self.root/'graphics/default.frag';source=shader.read_text();shader.write_text('#version 330 core\ninvalid shader syntax\n')
            self.wait_log('Development paused',process);self.assertIsNone(process.poll())
            shader.write_text(source);self.wait_log('Hot reload complete',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()

if __name__=='__main__':unittest.main(verbosity=2)
