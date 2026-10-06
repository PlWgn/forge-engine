"""Windows Direct3D 11 WARP pixels, native HLSL and shader transaction checks.

The shared four GPU suites also accept FORGE_TEST_BACKEND=direct3d11.
WARP validates Direct3D behavior, not physical GPU performance or audio hardware.
"""
import subprocess,sys,unittest
import features_graphics as base
ROOT,ENGINE=base.ROOT,base.ENGINE

@unittest.skipUnless(sys.platform=='win32','Direct3D 11 runs on Windows')
class Direct3DGraphicsTests(unittest.TestCase):
    tearDown=base.FeatureGraphicsTests.tearDown
    run_scene,write_config,wait_log=base.FeatureGraphicsTests.run_scene,base.FeatureGraphicsTests.write_config,base.FeatureGraphicsTests.wait_log
    def setUp(self):
        base.FeatureGraphicsTests.setUp(self)
        self.config['renderer']['backend']='direct3d11'
        self.config['renderer']['direct3d11']={'driver':'warp'}
        self.write_config()
    def native_scene(self,fragment='direct3d11/unlit.frag.hlsl'):
        self.config['renderer']['direct3d11']['shaders']={'scene':{
            'vertex':'direct3d11/unlit.vert.hlsl','fragment':fragment}}
        self.write_config()
    def test_native_hlsl_uniforms_textures_clipping_and_screen_orientation(self):
        base.sheet(self.root/'textures/sheet.bmp')
        fragment=(self.root/'graphics/direct3d11/unlit.frag.hlsl').read_text(encoding='utf-8')
        fragment=fragment.replace('float4 u_color;','float4 u_color;\n    float4 u_custom_tint;')
        fragment=fragment.replace('u_color * vertex_color;','u_color * vertex_color * u_custom_tint;')
        (self.root/'graphics/direct3d11/custom.frag.hlsl').write_text(fragment,encoding='utf-8')
        self.native_scene('direct3d11/custom.frag.hlsl')
        self.run_scene("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    forge.set_shader_uniform('u_custom_tint',[0,1,0,1])
    forge.spawn({'kind':'sprite','screen':True,'position':[100,80,0],'scale':[80,60,1]})
    forge.spawn({'kind':'sprite','screen':True,'texture':'sheet.bmp','uv':[.5,0,.5,1],
                 'position':[300,80,0],'scale':[80,60,1],'clip':[280,60,20,20]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        info=forge.renderer_stats()
        assert info['backend']=='direct3d11' and info['driver']=='warp' and info['native_programs']>=1,info
        forge.screenshot('native-green.ppm')
    if frames==3:forge.set_shader_uniform('u_custom_tint',[1,0,0,1])
    if frames==4:forge.screenshot('native-red.ppm')
""",frames=6,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'native-green.ppm',100,80)[1],240)
        self.assertGreater(base.point(self.root/'native-red.ppm',100,80)[0],240)
        self.assertGreater(base.point(self.root/'native-green.ppm',285,65)[1],240)
        self.assertEqual(base.point(self.root/'native-green.ppm',315,80),(0,0,0))
        self.assertEqual(base.point(self.root/'native-green.ppm',100,400),(0,0,0))
    def test_native_hlsl_error_rolls_back_then_recovers(self):
        self.native_scene()
        source="""import forge
from pathlib import Path
def on_start():
    forge.spawn({'id':'stable','kind':'sprite','position':[100,100,0],'scale':[80,80,1],'color':[0,1,0,1]})
    forge.screenshot('native-before.ppm')
def on_reload_failed(error):
    assert 'Shader' in error,error
    assert forge.find('stable').alive
    forge.screenshot('native-rollback.ppm')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
"""
        (self.root/'scenes/native.py').write_text(source,encoding='utf-8')
        self.config['entry_scene']='native.py';self.write_config()
        fragment=self.root/'graphics/direct3d11/unlit.frag.hlsl';original=fragment.read_text(encoding='utf-8')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--silent-audio','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('native-before.ppm',process)
            fragment.write_text('This is not HLSL',encoding='utf-8');self.wait_log('native-rollback.ppm',process)
            self.assertEqual((self.root/'native-before.ppm').read_bytes(),(self.root/'native-rollback.ppm').read_bytes())
            fragment.write_text(original.replace('return color;','return float4(1,0,0,1);'),encoding='utf-8')
            self.wait_log('Hot reload complete',process)
            (self.root/'scenes/native.py').write_text(source.replace('native-before.ppm','native-after.ppm'),encoding='utf-8')
            self.wait_log('native-after.ppm',process)
            self.assertGreater(base.point(self.root/'native-after.ppm',100,100)[0],240)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_auto_selects_compiled_direct3d_backend(self):
        self.config['renderer']['backend']='auto';self.write_config()
        self.run_scene("""import forge
frames=0
def on_start():forge.spawn({'kind':'sprite','screen':True,'position':[200,120,0],'scale':[60,60,1],'color':[0,1,0,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        assert forge.renderer_stats()['backend']=='direct3d11'
        forge.screenshot('auto.ppm')
""",frames=4,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'auto.ppm',200,120)[1],240)
    def test_glsl_uniform_name_survives_hlsl_keyword_renaming(self):
        (self.root/'graphics/keyword.frag').write_text(
            '#version 330 core\nuniform vec4 register;out vec4 c;void main(){c=register;}\n',encoding='utf-8')
        self.config['renderer']['fragment_shader']='keyword.frag';self.write_config()
        self.run_scene("""import forge
frames=0
def on_start():
    forge.set_shader_uniform('register',[0,1,0,1])
    forge.spawn({'kind':'sprite','screen':True,'position':[100,80,0],'scale':[80,60,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('keyword-green.ppm')
    if frames==3:forge.set_shader_uniform('register',[1,0,0,1])
    if frames==4:forge.screenshot('keyword-red.ppm')
""",frames=6,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'keyword-green.ppm',100,80)[1],240)
        self.assertGreater(base.point(self.root/'keyword-red.ppm',100,80)[0],240)

if __name__=='__main__':unittest.main()
