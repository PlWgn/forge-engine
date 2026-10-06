"""Optional GPU regression: pass a binary compiled with render_optimization_example.cpp."""
import unittest
import features_graphics as base
class PolicyCheck(unittest.TestCase):
    setUp,tearDown=base.FeatureGraphicsTests.setUp,base.FeatureGraphicsTests.tearDown
    write_config,run_scene=base.FeatureGraphicsTests.write_config,base.FeatureGraphicsTests.run_scene
    def test_public_policy_install_and_restore(self):
        self.run_scene("""import forge
frames=0
def build():return {'mode':'3d','physics_enabled':False,'background':[0,0,0,1],'camera':{'position':[0,0,0],'target':[0,0,-1]},'rendering':{'optimization':{'enabled':True}}}
def on_start():
    global e
    e=forge.spawn({'kind':'cube','position':[0,0,-4],'color':[0,1,0,1]})
    forge.use_example_render_policy()
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('visible.ppm')
    if frames==3:e.data={'example_hide':True}
    if frames==4:forge.screenshot('custom.ppm')
    if frames==5:
        assert forge.renderer_stats()['optimization']['passes']['main']['custom_culled']==1
        forge.use_example_render_policy(False)
    if frames==6:forge.screenshot('restored.ppm')
""",frames=8)
        self.assertGreater(base.point(self.root/'visible.ppm',320,240)[1],30)
        self.assertEqual(base.point(self.root/'custom.ppm',320,240),(0,0,0))
        self.assertEqual(base.ppm(self.root/'visible.ppm'),base.ppm(self.root/'restored.ppm'))
unittest.main(verbosity=2)
