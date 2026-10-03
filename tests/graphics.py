"""Optional tests requiring a real desktop, OpenGL 3.3 driver and audio device."""
from pathlib import Path
import json, shutil, subprocess, sys, tempfile, time, unittest
ROOT=Path(__file__).resolve().parents[1]
ENGINE=Path(sys.argv.pop(1)).resolve()

class GraphicsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='forge-graphics-');self.root=Path(self.temp.name)
        self.config=json.loads((ROOT/'engine.json').read_text())
        self.config['startup_scripts']=[];self.config['logging']['open_on_error']=False
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
        for scene in ('welcome.json','world3d.py'):
            self.config['entry_scene']=scene;self.write_config()
            result=subprocess.run([str(ENGINE),'run','--project',str(self.root/'engine.json'),'--frames','20','--no-open-log'],text=True,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            frame=(self.root/'frame.ppm').read_bytes();self.assertTrue(frame.startswith(b'P6\n'));self.assertGreater(len(set(frame[100:])),4)
    def test_shader_error_recovers_without_closing_window(self):
        (self.root/'scenes/control.py').write_text("import forge\ndef on_update(dt):\n    if __import__('pathlib').Path(forge.project_path('stop')).exists(): forge.quit()\n")
        scene=self.root/'scenes/welcome.json';data=json.loads(scene.read_text());data['script']='control.py';scene.write_text(json.dumps(data))
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('Scene loaded',process)
            shader=self.root/'graphics/default.frag';source=shader.read_text();shader.write_text('#version 330 core\ninvalid shader syntax\n')
            self.wait_log('Development paused',process);self.assertIsNone(process.poll())
            shader.write_text(source);self.wait_log('Hot reload complete',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()

if __name__=='__main__':unittest.main(verbosity=2)
