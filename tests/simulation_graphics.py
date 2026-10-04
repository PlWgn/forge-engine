"""Real GPU particle batching, billboards, UV/blending/depth and reload checks."""
import subprocess, unittest
import features_graphics as base
ROOT, ENGINE = base.ROOT, base.ENGINE


class SimulationGraphicsTests(unittest.TestCase):
    setUp, tearDown = base.FeatureGraphicsTests.setUp, base.FeatureGraphicsTests.tearDown
    write_config, wait_log = base.FeatureGraphicsTests.write_config, base.FeatureGraphicsTests.wait_log
    run_scene = base.FeatureGraphicsTests.run_scene

    def test_particle_pool_is_batched_with_per_vertex_colors_and_uv(self):
        base.sheet(self.root/'textures/sheet.bmp')
        self.run_scene('''import forge
from particles import Emitter
frames=0
def build():return {'mode':'2d','background':[0,0,0,1]}
def on_start():
    global gradient
    forge.set_paused(True);forge.on_frame(on_update)
    Emitter(rate=0,burst=5000,max_particles=5000,screen=True,position=[160,240,0],velocity=[0,0,0],size=[80,80],lifetime=[100,100],texture='sheet.bmp',uv=[.5,0,.5,1],color_end=[1,1,1,1])
    gradient=Emitter(rate=0,burst=1,screen=True,position=[480,240,0],velocity=[0,0,0],size=[80,40],lifetime=[1,1],color_start=[1,0,0,1],color_end=[0,1,0,1])
    forge.particle_step(.5)
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        stats=forge.renderer_stats()
        assert stats['particle_quads']==5001 and stats['particle_draw_calls']<=3,stats
        assert stats['particle_instancing'] and stats['particle_upload_bytes']==5001*52,stats
        assert stats['texture_path_resolutions']<=1,stats
        forge.screenshot('batch.ppm')
    if frames==3:assert forge.renderer_stats()['texture_path_resolutions']==0,forge.renderer_stats()
''',frames=4)
        green, yellow = base.point(self.root/'batch.ppm',160,240), base.point(self.root/'batch.ppm',480,240)
        self.assertGreater(green[1],230);self.assertLess(green[0],20)
        self.assertTrue(115<yellow[0]<140 and 115<yellow[1]<140,yellow)
        self.assertEqual(base.point(self.root/'batch.ppm',480,278),(0,0,0))

    def test_stock_particle_shader_crlf_retains_instancing(self):
        vertex=self.root/'graphics/particle.vert'
        vertex.write_bytes(vertex.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
        self.run_scene("""import forge
from particles import Emitter
frames=0
def on_start():
    forge.set_paused(True);forge.on_frame(on_update)
    Emitter(rate=0,burst=1,screen=True,position=[320,240,0],size=[40,40],lifetime=[100,100])
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        stats=forge.renderer_stats();assert stats['particle_instancing'] and stats['particle_upload_bytes']==52,stats
        forge.screenshot('crlf.ppm')
""",frames=4)
        self.assertGreater(base.point(self.root/'crlf.ppm',320,240)[0],230)

    def test_legacy_custom_particle_shader_and_explicit_fallback(self):
        source="""import forge
from particles import Emitter
frames=0
def build():return {'background':[0,0,0,1]}
def on_start():
    forge.set_paused(True);forge.on_frame(on_update)
    Emitter(rate=0,burst=1,screen=True,position=[320,240,0],velocity=[0,0,0],size=[100,100],rotation=[45,45],lifetime=[100,100],color_start=[1,0,0,1],color_end=[1,0,0,1])
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        stats=forge.renderer_stats();assert not stats['particle_instancing'] and stats['particle_upload_bytes']==6*36,stats
        forge.screenshot('fallback.ppm')
"""
        for custom in (False,True):
            with self.subTest(custom=custom):
                self.config['renderer']={'particle_instancing':custom}
                if custom:
                    vertex=(self.root/'graphics/particle.vert').read_text()+'\n// custom compatible vertex\n'
                    (self.root/'graphics/custom-particle.vert').write_text(vertex)
                    self.config['renderer']['particle_vertex_shader']='custom-particle.vert'
                self.write_config();self.run_scene(source,frames=4)
                self.assertGreater(base.point(self.root/'fallback.ppm',320,240)[0],230)
                self.assertEqual(base.point(self.root/'fallback.ppm',385,305),(0,0,0))

    def test_billboard_depth_and_camera_render_targets(self):
        self.run_scene('''import forge
from particles import Emitter
frames=0
def build():return {'mode':'3d','background':[0,0,0,1],'camera':{'position':[5,3,5],'target':[0,0,0]}}
def on_start():
    global blocker
    forge.set_paused(True);forge.on_frame(on_update)
    blocker=forge.spawn({'kind':'cube','position':[2,1.2,2],'scale':[3,3,3],'color':[1,0,0,1]})
    Emitter(rate=0,burst=1,velocity=[0,0,0],size=[2,2],lifetime=[100,100],color_start=[0,1,0,1],color_end=[0,1,0,1])
    target=forge.set_render_target('particles',{'width':160,'height':120,'position':[-5,3,5],'target':[0,0,0],'mode':'3d'})
    forge.spawn({'screen':True,'texture':target,'position':[540,70,0],'scale':[160,120,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('blocked.ppm')
    if frames==3:blocker.visible=False
    if frames==4:
        assert forge.renderer_stats()['particle_quads']==2
        forge.screenshot('billboard.ppm')
''',frames=6)
        blocked, visible = base.point(self.root/'blocked.ppm',320,240), base.point(self.root/'billboard.ppm',320,240)
        self.assertGreater(blocked[0],100);self.assertLess(blocked[1],20)
        self.assertGreater(visible[1],230);self.assertLess(visible[0],20)
        self.assertGreater(base.point(self.root/'billboard.ppm',540,70)[1],230)

    def test_transparent_particles_sort_back_to_front_and_additive_restores_state(self):
        self.run_scene('''import forge
from particles import Emitter
frames=0
def build():return {'mode':'3d','background':[0,0,0,1],'camera':{'position':[0,0,5],'target':[0,0,0]}}
def on_start():
    forge.set_paused(True);forge.on_frame(on_update)
    Emitter(rate=0,burst=1,position=[0,0,1],velocity=[0,0,0],size=[2,2],lifetime=[100,100],color_start=[1,0,0,.5],color_end=[1,0,0,.5])
    Emitter(rate=0,burst=1,position=[0,0,0],velocity=[0,0,0],size=[2,2],lifetime=[100,100],color_start=[0,1,0,.5],color_end=[0,1,0,.5])
    Emitter(rate=0,burst=1,screen=True,position=[100,100,0],velocity=[0,0,0],size=[40,40],lifetime=[100,100],blend='additive',color_start=[0,0,1,.5],color_end=[0,0,1,.5])
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('blend.ppm')
''',frames=4)
        center = base.point(self.root/'blend.ppm',320,240)
        self.assertTrue(120<center[0]<140 and 55<center[1]<75,center)
        self.assertGreater(base.point(self.root/'blend.ppm',100,100)[2],120)

    def test_bad_particle_shader_reload_preserves_old_program_and_pool(self):
        source='''import forge
from pathlib import Path
from particles import Emitter
frames=0
def on_start():
    global emitter
    forge.set_paused(True);forge.on_frame(on_update)
    emitter=Emitter(screen=True,rate=0,burst=20,velocity=[0,0,0],position=[320,240,0],size=[100,100],lifetime=[100,100],color_start=[.2,.8,.4,1],color_end=[.2,.8,.4,1])
    forge.log('PARTICLES_READY')
def on_reload_failed(error):
    assert emitter.info['alive']==20
    forge.screenshot('failed.ppm')
    forge.log('PARTICLE_ROLLBACK_OK')
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('before.ppm')
    if Path(forge.project_path('stop')).exists():forge.quit()
'''
        (self.root/'scenes/particle-reload.py').write_text(source)
        self.config['entry_scene']='particle-reload.py';self.config['window'].update(width=640,height=480,vsync=True);self.write_config()
        shader=self.root/'graphics/particle.frag';original=shader.read_text()
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('before.ppm',process)
            shader.write_text('this is not GLSL')
            self.wait_log('PARTICLE_ROLLBACK_OK',process);self.wait_log('failed.ppm',process)
            self.assertEqual((self.root/'before.ppm').read_bytes(),(self.root/'failed.ppm').read_bytes())
            shader.write_text(original);self.wait_log('Hot reload complete',process)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()


if __name__=='__main__':unittest.main(verbosity=2)
