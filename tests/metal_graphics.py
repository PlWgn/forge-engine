"""Real macOS Metal pixels, native MSL customization and reload transactions."""
import subprocess,sys,unittest
import features_graphics as base
ROOT,ENGINE=base.ROOT,base.ENGINE

@unittest.skipUnless(sys.platform=='darwin','Metal runs on macOS')
class MetalGraphicsTests(unittest.TestCase):
    tearDown=base.FeatureGraphicsTests.tearDown
    run_scene,write_config,wait_log=base.FeatureGraphicsTests.run_scene,base.FeatureGraphicsTests.write_config,base.FeatureGraphicsTests.wait_log
    def setUp(self):
        base.FeatureGraphicsTests.setUp(self)
        self.config['window'].update(width=640,height=480,vsync=False)
        self.config['renderer']['backend']='metal';self.config['renderer']['metal']={};self.write_config()
    def native_scene(self,fragment='metal/unlit.frag.metal'):
        self.config['renderer']['metal']['shaders']={'scene':{
            'vertex':'metal/unlit.vert.metal','fragment':fragment}}
        self.write_config()
    def test_native_msl_uniforms_textures_clipping_and_orientation(self):
        base.sheet(self.root/'textures/sheet.bmp')
        fragment=(self.root/'graphics/metal/unlit.frag.metal').read_text(encoding='utf-8')
        fragment=fragment.replace('float4 u_color;','float4 u_color;float4 u_custom_tint;')
        fragment=fragment.replace('material.u_color*input.color','material.u_color*input.color*material.u_custom_tint')
        (self.root/'graphics/metal/custom.frag.metal').write_text(fragment,encoding='utf-8');self.native_scene('metal/custom.frag.metal')
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
        assert info['backend']=='metal' and info['native_programs']>=1,info
        forge.screenshot('native-green.ppm')
    if frames==3:forge.set_shader_uniform('u_custom_tint',[1,0,0,1])
    if frames==4:forge.screenshot('native-red.ppm')
""",frames=6,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'native-green.ppm',100,80)[1],240)
        self.assertGreater(base.point(self.root/'native-red.ppm',100,80)[0],240)
        self.assertGreater(base.point(self.root/'native-green.ppm',285,65)[1],240)
        self.assertEqual(base.point(self.root/'native-green.ppm',315,80),(0,0,0))
        self.assertEqual(base.point(self.root/'native-green.ppm',100,400),(0,0,0))
    def test_native_msl_error_rolls_back_and_recovers(self):
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
        (self.root/'scenes/native.py').write_text(source,encoding='utf-8');self.config['entry_scene']='native.py';self.write_config()
        fragment=self.root/'graphics/metal/unlit.frag.metal';original=fragment.read_text(encoding='utf-8')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--silent-audio','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('native-before.ppm',process)
            fragment.write_text('This is not Metal source',encoding='utf-8');self.wait_log('native-rollback.ppm',process)
            self.assertEqual((self.root/'native-before.ppm').read_bytes(),(self.root/'native-rollback.ppm').read_bytes())
            fragment.write_text(original.replace('return color;','return float4(1,0,0,1);'),encoding='utf-8');self.wait_log('Hot reload complete',process)
            (self.root/'scenes/native.py').write_text(source.replace('native-before.ppm','native-after.ppm'),encoding='utf-8');self.wait_log('native-after.ppm',process)
            self.assertGreater(base.point(self.root/'native-after.ppm',100,100)[0],240)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_auto_selects_metal(self):
        self.config['renderer']['backend']='auto';self.write_config()
        self.run_scene("""import forge
frames=0
def on_start():forge.spawn({'kind':'sprite','screen':True,'position':[200,120,0],'scale':[60,60,1],'color':[0,1,0,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        assert forge.renderer_stats()['backend']=='metal'
        forge.screenshot('auto.ppm')
""",frames=4,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'auto.ppm',200,120)[1],240)
    def test_glsl_scalar_arrays_and_reserved_names_preserve_api_shape(self):
        (self.root/'graphics/array.frag').write_text('''#version 330 core
uniform float values[2];uniform vec4 kernel;out vec4 c;
void main(){c=vec4(values[0],values[1],0,1)*kernel;}
''',encoding='utf-8')
        self.config['renderer']['fragment_shader']='array.frag';self.write_config()
        self.run_scene("""import forge
frames=0
def on_start():
    forge.set_shader_uniform('kernel',[1,1,1,1])
    forge.set_shader_uniform('values[0]',0.0);forge.set_shader_uniform('values[1]',1.0)
    forge.spawn({'kind':'sprite','screen':True,'position':[100,80,0],'scale':[80,60,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('array-green.ppm')
    if frames==3:
        forge.set_shader_uniform('values[0]',1.0);forge.set_shader_uniform('values[1]',0.0)
    if frames==4:forge.screenshot('array-red.ppm')
""",frames=6,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'array-green.ppm',100,80)[1],240)
        self.assertGreater(base.point(self.root/'array-red.ppm',100,80)[0],240)

    def test_procedural_replacement_reuses_layout_pipeline(self):
        self.run_scene("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    global actor,data
    data={'positions':[[0,0,0],[100,0,0],[0,100,0]],'colors':[[1,0,0,1]]*3}
    mesh=forge.set_mesh('pickup',data)
    actor=forge.spawn({'kind':'mesh','screen':True,'model':mesh,'position':[100,100,0]})
    other=forge.set_mesh('reference',dict(data,colors=[[0,0,1,1]]*3))
    forge.spawn({'kind':'mesh','screen':True,'model':other,'position':[400,100,0]})
def on_update(dt):
    global frames,actor,warm
    frames+=1
    if frames==2:forge.screenshot('mesh-red.ppm')
    if frames==4:warm=forge.renderer_stats()['draw_pipeline_builds']
    if 4<=frames<=20:
        data['colors']=[[0,1,0,1]]*3
        if frames%4==0:
            actor.destroy();forge.remove_mesh('pickup')
            name=forge.set_mesh('pickup',data)
            actor=forge.spawn({'kind':'mesh','screen':True,'model':name,'position':[100,100,0]})
        else:forge.set_mesh('pickup',data)
    if frames==21:
        info=forge.renderer_stats()
        assert info['draw_pipeline_builds']==warm,info
        forge.screenshot('mesh-green.ppm')
        forge.log('LAYOUT_PIPELINES_STABLE '+str(info['draw_pipeline_builds']))
""",frames=23,extra=('--silent-audio',))
        self.assertGreater(base.point(self.root/'mesh-red.ppm',120,120)[0],240)
        self.assertGreater(base.point(self.root/'mesh-green.ppm',120,120)[1],240)
        self.assertGreater(base.point(self.root/'mesh-green.ppm',420,120)[2],240)

    def test_texture_updates_and_presentation_do_not_drain_each_frame(self):
        self.run_scene("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    global label
    label=forge.spawn({'kind':'text','screen':True,'text':'A','font_size':80,'position':[300,120,0]})
    forge.spawn({'kind':'sprite','screen':True,'position':[100,100,0],'scale':[40,40,1],'color':[0,1,0,1]})
def on_update(dt):
    global frames,baseline
    frames+=1
    if frames==5:forge.screenshot('glyph-before.ppm')
    if frames==6:baseline=forge.renderer_stats()['synchronous_flushes']
    if 6<=frames<=33:label.text=chr(65+(frames-5)%26)
    if frames==34:
        info=forge.renderer_stats()
        assert info['synchronous_flushes']==baseline,info
        assert info['uniform_arena_bytes']<=info['uniform_budget_bytes'],info
        assert info['peak_inflight_command_buffers']<=info['max_inflight_command_buffers']==3,info
        forge.screenshot('glyph-after.ppm')
""",frames=37,extra=('--silent-audio',))
        self.assertNotEqual((self.root/'glyph-before.ppm').read_bytes(),(self.root/'glyph-after.ppm').read_bytes())
        self.assertGreater(base.point(self.root/'glyph-after.ppm',100,100)[1],240)

    def test_uniform_budget_exhaustion_reports_a_runtime_error(self):
        self.config['renderer']['batching']=False
        self.config['renderer']['metal']['uniform_budget_bytes']=65536
        self.config['entry_scene']='budget.py';self.write_config()
        (self.root/'scenes/budget.py').write_text("""import forge
def on_start():
    for i in range(16):
        forge.spawn({'kind':'sprite','screen':True,'position':[100+i*10,80,0],'scale':[10,10,1]})
""",encoding='utf-8')
        result=subprocess.run([str(ENGINE),'run','--project',str(self.root/'engine.json'),'--frames','3','--silent-audio','--no-open-log'],capture_output=True,text=True,timeout=30)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Metal uniform snapshot budget exceeded',result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
