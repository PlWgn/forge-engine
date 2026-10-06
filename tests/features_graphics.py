"""Real GPU coverage for cameras, UV, postprocess, lights, skinning and scene editor."""
from pathlib import Path
import json,struct,subprocess,unittest
import graphics as base
from model_fixture import animated_triangle
ROOT,ENGINE=base.ROOT,base.ENGINE
engine_command=base.engine_command

def sheet(path):
    # Original uncompressed BMP: red and green horizontal regions.
    pixels=(bytes([0,0,255])*2+bytes([0,255,0])*2)*2
    path.write_bytes(b'BM'+struct.pack('<IHHI',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,4,2,1,24,0,len(pixels),0,0,0,0)+pixels)

def ppm(path):
    head,size,depth,pixels=path.read_bytes().split(b'\n',3)
    w,h=map(int,size.split());assert head==b'P6' and depth==b'255'
    return w,h,pixels

def point(path,x,y):
    w,h,data=ppm(path);x=int(x*w/640);y=int(y*h/480);offset=(y*w+x)*3
    return tuple(data[offset:offset+3])

class FeatureGraphicsTests(unittest.TestCase):
    setUp,tearDown,write_config=base.GraphicsTests.setUp,base.GraphicsTests.tearDown,base.GraphicsTests.write_config
    wait_log=base.GraphicsTests.wait_log
    def run_scene(self,source,*,command='run',frames=8,extra=()):
        (self.root/'scenes/gpu.py').write_text(source,encoding='utf-8');self.config['entry_scene']='gpu.py'
        self.config['window'].update(width=640,height=480,vsync=False);self.write_config()
        result=subprocess.run([*base.engine_command(command),'--project',str(self.root/'engine.json'),'--frames',str(frames),'--no-open-log',*extra],capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result.stdout+result.stderr
    def test_batching_and_uv_regions(self):
        sheet(self.root/'textures/sheet.bmp')
        self.run_scene('''import forge
from animation import SpriteSheet
frames=0
def on_start():
    sheet=SpriteSheet('sheet.bmp',2,1)
    for x in range(100):forge.spawn({'kind':'sprite','screen':True,'position':[x*6,300,0],'scale':[5,5,1]})
    a=forge.spawn({'texture':'sheet.bmp','position':[100,100,0],'scale':[100,100,1],'screen':True});sheet.apply(a,0)
    b=forge.spawn({'texture':'sheet.bmp','position':[250,100,0],'scale':[100,100,1],'screen':True});sheet.apply(b,1)
    forge.spawn({'kind':'text','screen':True,'text':'a'*200,'position':[0,400,0],'font_size':12})
def on_update(dt):
    global frames
    frames+=1
    if frames==4:
        stats=forge.renderer_stats();assert stats['draw_calls']<=4,stats
        assert stats['glyph_pages']==1;forge.screenshot('regions.ppm')
''')
        a,b=point(self.root/'regions.ppm',100,100),point(self.root/'regions.ppm',250,100)
        self.assertGreater(a[0],200);self.assertLess(a[1],30);self.assertGreater(b[1],200);self.assertLess(b[0],30)
    def test_parallel_cameras_postprocess_lights_shadows_and_skinning(self):
        animated_triangle(self.root/'models/animated.gltf')
        self.run_scene('''import forge
frames=0
def build():return {'mode':'3d','physics_enabled':False,'camera':{'position':[0,2,5],'target':[0,0,0]}}
def on_start():
    global model
    forge.set_lights([{'type':'directional','direction':[-.5,-1,-.5],'intensity':1,'shadows':True,'shadow_extent':8}])
    forge.spawn({'kind':'cube','position':[0,-1,0],'scale':[10,.2,10],'color':[.6,.6,.6,1]})
    forge.spawn({'kind':'cube','position':[-1,0,0],'color':[.9,.2,.2,1]})
    model=forge.spawn({'kind':'mesh','model':'animated.gltf','position':[0,0,0]});forge.play_animation(model,'Move')
    camera=forge.set_render_target('monitor',{'width':200,'height':150,'position':[3,2,5],'target':[0,0,0],'mode':'3d'})
    forge.spawn({'texture':camera,'screen':True,'position':[520,90,0],'scale':[200,150,1]})
    forge.pause_animation(model);model.animation_time=0
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('before.ppm')
    if frames==3:model.animation_time=.8
    if frames==4:forge.screenshot('animated.ppm')
    if frames==5:forge.set_lights([{'type':'directional','direction':[-.5,-1,-.5],'intensity':1,'shadows':False}])
    if frames==6:forge.screenshot('no-shadow.ppm')
    if frames==7:forge.set_postprocess({'grain':.15,'bloom':.2,'aberration':2,'scanlines':.4,'vignette':.3})
    if frames==8:
        assert forge.renderer_stats()['render_targets']==3 # camera + retained shadow + postprocess
        forge.screenshot('post.ppm')
''',frames=11)
        before,animated,post=[(self.root/file).read_bytes() for file in ('before.ppm','animated.ppm','post.ppm')]
        self.assertNotEqual(before,animated);self.assertNotEqual(animated,post)
        self.assertNotEqual(animated,(self.root/'no-shadow.ppm').read_bytes())
        self.assertGreater(len(set(post[100:])),30)
    def test_custom_python_uniforms_and_window_controls(self):
        fragment=self.root/'graphics/default.frag'
        fragment.write_text('#version 330 core\nout vec4 out_color;\nuniform vec4 custom_tint;\nvoid main(){out_color=custom_tint;}')
        self.run_scene('''import forge
frames=0
def on_start():
    forge.spawn({'kind':'sprite','screen':True,'position':[100,100,0],'scale':[100,100,1]})
    forge.set_shader_uniform('custom_tint',[1,0,0,1]);forge.set_window({'vsync':False})
    assert not forge.window_settings()['vsync']
    before=forge.window_settings()
    for options in ({'width':4294967616},{'height':480.5},{'width':800,'vsync':'invalid'},{'fullscreen':1}):
        try:forge.set_window(options)
        except RuntimeError:pass
        else:raise AssertionError('invalid window accepted')
        assert forge.window_settings()==before

def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('red.ppm')
    if frames==3:forge.set_shader_uniform('custom_tint',[0,1,0,1])
    if frames==4:forge.screenshot('green.ppm')
''')
        self.assertGreater(point(self.root/'red.ppm',100,100)[0],240)
        self.assertGreater(point(self.root/'green.ppm',100,100)[1],240)
    def test_scene_editor_opens_and_renders_inspector(self):
        self.run_scene('''import forge
frames=0
def on_start():
    assert forge.editor_enabled()
    forge.spawn({'kind':'cube','id':'editable','color':[.2,.8,.5,1]})
    forge.screenshot('editor.ppm')
    forge.save_scene('editable.json')
''',command='edit',frames=3)
        self.assertTrue((self.root/'editable.json').exists() or (self.root/'scenes/editable.json').exists())
        self.assertGreater(len(set((self.root/'editor.ppm').read_bytes()[100:])),30)
    def test_alternative_viewport_shell_without_builtin_interface(self):
        (self.root/'shell.py').write_text("""import forge,json
from pathlib import Path
API_VERSION=1
frames=0
def on_update(dt):
    global frames
    frames+=1
    if frames==1:
        assert forge.editor_enabled() and not forge.editor_command({'op':'snapshot'})['preview']
        forge.editor_command({'op':'patch','patch':[{'op':'add','path':'/entities/-','value':{'id':'custom','kind':'cube','color':[.2,.8,.4,1],'extensions':{'shell':'kept'}}}]})
        forge.editor_command({'op':'select','id':'custom'})
    if frames==3:
        assert forge.editor_command({'op':'snapshot'})['selected']=='custom'
        forge.editor_command({'op':'save','file':'custom-shell.json'})
        forge.find('custom').name='Saved twice'
        forge.editor_command({'op':'save','file':'custom-shell.json'})
        saved=json.loads(Path(forge.asset_path('scenes','custom-shell.json')).read_text())
        assert saved['entities'][0]['name']=='Saved twice' and saved['entities'][0]['extensions']=={'shell':'kept'}
        forge.screenshot('custom-viewport.ppm')
        forge.log('CUSTOM_VIEWPORT_OK');forge.quit()
""",encoding='utf-8')
        output=self.run_scene("def build():return {'mode':'3d','physics_enabled':False}\n",command='edit',frames=8,extra=('--shell','shell.py'))
        self.assertIn('CUSTOM_VIEWPORT_OK',output)
        center=point(self.root/'custom-viewport.ppm',320,240)
        corner=point(self.root/'custom-viewport.ppm',20,20)
        self.assertNotEqual(center,corner)
        self.assertGreater(center[1],center[0]+20)

    def test_failed_reload_restores_window_preferences(self):
        source='''import forge
from pathlib import Path
def on_start():
    forge.spawn({'kind':'sprite','screen':True,'position':[100,100,0],'scale':[100,100,1]})
    forge.log('WINDOW_READY')
def on_reload_failed(error):
    options=forge.window_settings()
    assert options['width']==640 and options['height']==480 and options['vsync'], options
    forge.log('WINDOW_RESTORED')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
'''
        file=self.root/'scenes/window.py';file.write_text(source)
        self.config['entry_scene']='window.py';self.config['window'].update(width=640,height=480,vsync=True);self.write_config()
        process=subprocess.Popen([*base.engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('WINDOW_READY',process)
            file.write_text("import forge\ndef on_start():\n    forge.set_window({'width':800,'height':600,'vsync':False})\n    raise RuntimeError('WINDOW_CANDIDATE_FAILED')\n")
            self.wait_log('WINDOW_RESTORED',process)
            file.write_text(source.replace('WINDOW_READY','WINDOW_RECOVERED'))
            self.wait_log('WINDOW_RECOVERED',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_model_material_texture_failure_rolls_back_before_commit(self):
        (self.root/'models/material.obj').write_text('mtllib material.mtl\nv -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0.5 1\nusemtl Test\nf 1/1 2/2 3/3\n')
        (self.root/'models/material.mtl').write_text('newmtl Test\nKd 1 1 1\nmap_Kd ../textures/nested.bmp\n')
        texture=self.root/'textures/nested.bmp';sheet(texture)
        source='''import forge
from pathlib import Path
def build():return {'mode':'3d','camera':{'position':[0,0,5],'target':[0,0,0]},'entities':[{'id':'stable','kind':'mesh','model':'material.obj'}]}
def on_start():forge.screenshot('material-before.ppm')
def on_reload_failed(error):
    assert forge.find('stable').alive
    forge.screenshot('material-rollback.ppm')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
'''
        file=self.root/'scenes/material.py';file.write_text(source)
        self.config['entry_scene']='material.py';self.write_config()
        process=subprocess.Popen([*base.engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('material-before.ppm',process);texture.write_bytes(b'invalid image')
            self.wait_log('material-rollback.ppm',process)
            self.assertEqual((self.root/'material-before.ppm').read_bytes(),(self.root/'material-rollback.ppm').read_bytes())
            sheet(texture);file.write_text(source.replace('material-before.ppm','material-recovered.ppm'))
            self.wait_log('material-recovered.ppm',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()

if __name__=='__main__':unittest.main(verbosity=2)
