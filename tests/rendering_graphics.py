"""Pixel regressions for PBR maps, transparency, live meshes and hierarchy."""
import base64,json,struct,zlib,subprocess,unittest
import features_graphics as base
ROOT,ENGINE=base.ROOT,base.ENGINE

def png(path,rgba):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes(rgba)*2)*2))+chunk(b'IEND',b''))

class RenderingGraphicsTests(unittest.TestCase):
    setUp,tearDown=base.FeatureGraphicsTests.setUp,base.FeatureGraphicsTests.tearDown
    run_scene,write_config,wait_log=base.FeatureGraphicsTests.run_scene,base.FeatureGraphicsTests.write_config,base.FeatureGraphicsTests.wait_log
    def test_hot_reload_updates_external_model_buffer_pixels(self):
        file=self.root/'models/external.gltf';base.animated_triangle(file)
        model=json.loads(file.read_text());data=bytearray(base64.b64decode(model['buffers'][0]['uri'].split(',')[1]))
        buffer=self.root/'models/external.bin';buffer.write_bytes(data)
        model['buffers'][0]['uri']='external.bin';file.write_text(json.dumps(model),encoding='utf-8')
        scene=self.root/'scenes/external.py'
        scene.write_text("""import forge
from pathlib import Path
frames=0
def build():return {'mode':'3d','physics_enabled':False,'background':[0,0,0,1],'camera':{'position':[0,0,4],'target':[0,0,0]}}
def on_start():
    global changed
    changed=Path(forge.project_path('changed')).exists()
    forge.spawn({'kind':'mesh','model':'external.gltf'})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        forge.screenshot('buffer-after.ppm' if changed else 'buffer-before.ppm')
    if frames==4 and changed:forge.quit()
""",encoding='utf-8')
        self.config['entry_scene']='external.py';self.config['window'].update(width=640,height=480,vsync=False);self.write_config()
        process=subprocess.Popen([*base.engine_command('dev'),'--project',str(self.root/'engine.json'),'--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            self.wait_log('buffer-before.ppm',process)
            (self.root/'changed').touch()
            position=model['accessors'][model['meshes'][0]['primitives'][0]['attributes']['POSITION']]
            offset=model['bufferViews'][position['bufferView']]['byteOffset']
            for vertex in range(3):
                at=offset+vertex*12
                struct.pack_into('<f',data,at,struct.unpack_from('<f',data,at)[0]+2)
            buffer.write_bytes(data)
            self.wait_log('buffer-after.ppm',process)
            self.assertEqual(process.wait(timeout=15),0)
            self.assertGreater(base.point(self.root/'buffer-before.ppm',320,240)[1],30)
            self.assertEqual(base.point(self.root/'buffer-after.ppm',320,240),(0,0,0))
            self.assertGreater(base.point(self.root/'buffer-after.ppm',528,240)[1],30)
        finally:
            if process.poll() is None:process.kill();process.wait()
    def test_removed_custom_uniforms_do_not_leak_between_entities_or_frames(self):
        (self.root/'graphics/scoped.frag').write_text('#version 330 core\nuniform vec4 bias;out vec4 color;\nvoid main(){color=vec4(0,1,0,1)+bias;}\n',encoding='utf-8')
        self.config['renderer']['fragment_shader']='scoped.frag';self.write_config()
        self.run_scene("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    global first
    first=forge.spawn({'screen':True,'position':[100,100,0],'scale':[60,60,1],'uniforms':{'bias':[1,-1,0,0]}})
    forge.spawn({'screen':True,'position':[300,100,0],'scale':[60,60,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('scoped.ppm')
    if frames==3:first.uniforms={}
    if frames==4:forge.screenshot('removed.ppm')
    if frames==5:
        forge.set_shader_uniform('bias',[1,-1,0,0]);first.uniforms={'bias':[0,0,0,0]}
    if frames==6:forge.screenshot('global.ppm')
    if frames==7:first.uniforms={}
    if frames==8:forge.screenshot('global-removed.ppm')
""",frames=10)
        self.assertGreater(base.point(self.root/'scoped.ppm',100,100)[0],240)
        self.assertEqual(base.point(self.root/'scoped.ppm',300,100),(0,255,0))
        self.assertEqual(base.point(self.root/'removed.ppm',100,100),(0,255,0))
        self.assertGreater(base.point(self.root/'global.ppm',300,100)[0],240)
        self.assertEqual(base.point(self.root/'global.ppm',100,100),(0,255,0))
        self.assertEqual(base.point(self.root/'global-removed.ppm',100,100),(255,0,0))
    def test_removed_postprocess_uniforms_restore_zero_defaults(self):
        (self.root/'graphics/scoped-post.frag').write_text('#version 330 core\nuniform vec4 bias;out vec4 color;\nvoid main(){color=vec4(0,1,0,1)+bias;}\n',encoding='utf-8')
        self.config['renderer']['post_shader']='scoped-post.frag';self.write_config()
        self.run_scene("""import forge
frames=0
def on_start():forge.set_postprocess({'enabled':True,'uniforms':{'bias':[1,-1,0,0]}})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('post.ppm')
    if frames==3:forge.set_postprocess({'uniforms':{}})
    if frames==4:forge.screenshot('post-removed.ppm')
""",frames=6)
        self.assertEqual(base.point(self.root/'post.ppm',320,240),(255,0,0))
        self.assertEqual(base.point(self.root/'post-removed.ppm',320,240),(0,255,0))
    def test_pbr_factors_and_six_texture_channels_change_pixels(self):
        for name,color in {'normal':(230,128,180,255),'orm':(30,180,240,255),'metal':(20,20,20,255),'rough':(220,220,220,255),'ao':(0,0,0,255),'emission':(0,255,0,255),'albedo':(255,30,30,255)}.items():png(self.root/f'textures/{name}.png',color)
        self.run_scene('''import forge
frames=0
def build():return {'mode':'3d','physics_enabled':False,'background':[0,0,0,1],'camera':{'position':[0,0,4],'target':[0,0,0]},'rendering':{'ambient':[.3,.3,.3],'lights':[{'type':'directional','direction':[-1,0,-1],'intensity':3}]}}
def on_start():
    global e,settings
    settings={'shading':'pbr','metallic':.7,'roughness':.2}
    e=forge.spawn({'kind':'sprite','scale':[3,3,1],'material_properties':settings})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('base.ppm')
    changes={3:{'roughness':.9},5:{'metallic':0,'albedo_texture':'albedo.png'},7:{'normal_texture':'normal.png'},9:{'metallic':1,'metallic_roughness_texture':'orm.png','metallic_texture':'metal.png','roughness_texture':'rough.png'},11:{'occlusion_texture':'ao.png'},13:{'emissive':[0,1,0],'emissive_texture':'emission.png'}}
    if frames in changes:settings.update(changes[frames]);e.material_properties=settings
    if frames in (4,6,8,10,12,14):forge.screenshot(str(frames)+'.ppm')
''',frames=16)
        frames=[self.root/name for name in ('base.ppm','4.ppm','6.ppm','8.ppm','10.ppm','12.ppm','14.ppm')]
        for first,second in zip(frames,frames[1:]):self.assertNotEqual(first.read_bytes(),second.read_bytes(),(first.name,second.name))
        self.assertGreater(base.point(frames[-1],320,240)[1],200)
    def test_transparent_pass_order_depth_and_alpha_mask(self):
        png(self.root/'textures/clear.png',(0,255,0,0))
        self.run_scene('''import forge
frames=0
def build():return {'mode':'3d','background':[0,0,0,1],'camera':{'position':[0,0,5],'target':[0,0,0]},'physics_enabled':False}
def on_start():
    global masked
    forge.spawn({'kind':'sprite','position':[0,0,1],'scale':[2,2,1],'color':[1,0,0,.5],'material_properties':{'shading':'unlit','alpha_mode':'blend'}})
    forge.spawn({'kind':'sprite','position':[0,0,0],'scale':[2,2,1],'color':[0,1,0,.5],'material_properties':{'shading':'unlit','alpha_mode':'blend'}})
    forge.spawn({'kind':'sprite','position':[0,0,-1],'scale':[3,3,1],'color':[0,0,1,1],'material_properties':{'shading':'unlit','alpha_mode':'opaque'}})
    masked=forge.spawn({'kind':'sprite','screen':True,'texture':'clear.png','position':[100,100,0],'scale':[60,60,1],'material_properties':{'shading':'unlit','alpha_mode':'mask'}})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('blend.ppm')
    if frames==3:masked.material_properties={'shading':'unlit','alpha_mode':'opaque'}
    if frames==4:forge.screenshot('opaque.ppm')
''',frames=6)
        c=base.point(self.root/'blend.ppm',320,240);self.assertTrue(120<c[0]<140 and 55<c[1]<75 and 55<c[2]<75,c)
        self.assertEqual(base.point(self.root/'blend.ppm',100,100),(0,0,0))
        self.assertGreater(base.point(self.root/'opaque.ppm',100,100)[1],240)
    def test_procedural_mesh_update_vertex_color_and_gpu_release(self):
        self.run_scene('''import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    global e,data
    data={'positions':[[0,0,0],[100,0,0],[0,100,0]],'colors':[[1,0,0,1]]*3}
    name=forge.set_mesh('live',data)
    e=forge.spawn({'kind':'mesh','screen':True,'model':name,'position':[100,100,0]})
def on_update(dt):
    global frames,before,after
    frames+=1
    if frames==2:forge.screenshot('first.ppm');before=forge.renderer_stats()['gpu_bytes']
    if frames==3:
        data['positions']=[[200,0,0],[300,0,0],[200,100,0]];data['colors']=[[0,1,0,1]]*3
        forge.set_mesh('live',data)
    if frames==4:forge.screenshot('updated.ppm')
    if frames==5:e.destroy();forge.remove_mesh('live')
    if frames==7:assert forge.renderer_stats()['gpu_bytes']<before
''',frames=9)
        self.assertGreater(base.point(self.root/'first.ppm',120,120)[0],240)
        self.assertGreater(base.point(self.root/'updated.ppm',320,120)[1],240)
        self.assertEqual(base.point(self.root/'updated.ppm',120,120),(0,0,0))
    def test_scene_change_reuses_mesh_name_with_fresh_source(self):
        (self.root/'scenes/next.py').write_text("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    name=forge.set_mesh('same',{'positions':[[0,0,0],[100,0,0],[0,100,0]],'colors':[[0,1,0,1]]*3})
    forge.spawn({'kind':'mesh','screen':True,'model':name,'position':[100,100,0]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('second-scene.ppm')
""")
        self.run_scene("""import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False}
def on_start():
    name=forge.set_mesh('same',{'positions':[[0,0,0],[100,0,0],[0,100,0]],'colors':[[1,0,0,1]]*3})
    forge.spawn({'kind':'mesh','screen':True,'model':name,'position':[100,100,0]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('first-scene.ppm')
    if frames==3:forge.change_scene('next.py')
""",frames=9)
        self.assertGreater(base.point(self.root/'first-scene.ppm',120,120)[0],240)
        self.assertGreater(base.point(self.root/'second-scene.ppm',120,120)[1],240)
    def test_hierarchy_render_matches_world_coordinates(self):
        self.run_scene('''import forge
frames=0
def build():return {'mode':'2d','background':[0,0,0,1],'physics_enabled':False,'entities':[{'id':'root','kind':'empty','screen':True,'position':[160,240,0],'rotation':[0,0,90]}, {'id':'child','kind':'sprite','screen':True,'parent':'root','position':[80,0,0],'scale':[40,40,1],'color':[0,1,0,1]}]}
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        p=forge.find('child').world_position;assert abs(p[0]-160)<.01 and abs(p[1]-320)<.01
        forge.screenshot('tree.ppm')
''',frames=4)
        self.assertGreater(base.point(self.root/'tree.ppm',160,320)[1],240)
        self.assertEqual(base.point(self.root/'tree.ppm',240,240),(0,0,0))
    def test_imported_gltf_pbr_embedded_maps_are_used(self):
        base.animated_triangle(self.root/'models/mapped.gltf')
        texture=self.root/'textures/embedded.png';png(texture,(0,255,0,255))
        model=json.loads((self.root/'models/mapped.gltf').read_text());model['images']=[{'uri':'data:image/png;base64,'+base64.b64encode(texture.read_bytes()).decode()}];model['textures']=[{'source':0}]
        model['materials'][0]['emissiveFactor']=[0,1,0];model['materials'][0]['emissiveTexture']={'index':0}
        (self.root/'models/mapped.gltf').write_text(json.dumps(model))
        self.run_scene('''import forge
frames=0
def build():return {'mode':'3d','background':[0,0,0,1],'physics_enabled':False,'camera':{'position':[0,0,3],'target':[0,0,0]},'rendering':{'ambient':[0,0,0],'lights':[{'type':'directional','direction':[0,0,1],'intensity':0}]},'entities':[{'kind':'mesh','model':'mapped.gltf'}]}
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('import.ppm')
''',frames=4)
        self.assertGreater(base.point(self.root/'import.ppm',320,240)[1],240)
    def test_texture_filter_mipmap_budget_and_user_capture(self):
        self.config['renderer'].update(texture_filter='nearest',mipmaps=True);self.write_config()
        png(self.root/'textures/solid.png',(0,255,0,255))
        self.run_scene('''import forge
from pathlib import Path
frames=0
def build():return {'mode':'2d','physics_enabled':False}
def on_start():
    forge.spawn({'screen':True,'texture':'solid.png','position':[320,240,0],'scale':[100,100,1]})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        assert forge.renderer_stats()['gpu_bytes']>=20
        forge.user_screenshot('forge-test.ppm')
        forge.log('USER_CAPTURE_PATH='+forge.user_path('captures','forge-test.ppm'))
''',frames=4)
        log=(self.root/'forge.log').read_text();line=next(l for l in log.splitlines() if 'USER_CAPTURE_PATH=' in l);path=base.Path(line.split('USER_CAPTURE_PATH=',1)[1]);self.assertTrue(path.exists());self.assertNotEqual(path.parent,self.root)
        try:self.assertGreater(base.point(path,320,240)[1],240)
        finally:path.unlink(missing_ok=True)
    def test_morph_skinning_retarget_and_shared_model_isolation(self):
        from model_fixture import authoring_triangle
        authoring_triangle(self.root/'models/authoring.gltf')
        authoring_triangle(self.root/'models/target.gltf',joint='TargetJoint',rest=2)
        self.run_scene("""import forge
from animation import Animator
frame=0
def build():return {'mode':'3d','physics_enabled':False,'background':[0,0,0,1],'camera':{'position':[0,0,4],'target':[0,0,0]}}
def on_start():
    global e,b,target,a,t
    e=forge.spawn({'kind':'mesh','model':'authoring.gltf','material_properties':{'shading':'unlit'}})
    a=Animator(e,'Idle',auto_update=False)
    b=forge.spawn({'kind':'mesh','model':'authoring.gltf','position':[1.8,0,0],'visible':False,'material_properties':{'shading':'unlit'}})
    target=forge.spawn({'kind':'mesh','model':'target.gltf','visible':False,'material_properties':{'shading':'unlit'}})
    t=Animator(target,layers=[{'model':'authoring.gltf','clip':'Move','mapping':{'Joint':'TargetJoint'}}],auto_update=False)
def on_update(dt):
    global frame
    frame+=1
    if frame==2:forge.screenshot('morph0.ppm')
    if frame==3:a.morph(Smile=1)
    if frame==4:forge.screenshot('morph1.ppm')
    if frame==5:
        e.position=(-1,0,0);b.visible=True
    if frame==6:forge.screenshot('independent.ppm')
    if frame==7:forge.set_morph_weights(b,{'Smile':1})
    if frame==8:forge.screenshot('both.ppm')
    if frame==9:
        e.visible=False;b.visible=False;target.visible=True;t.seek(0)
    if frame==10:forge.screenshot('retarget0.ppm')
    if frame==11:t.seek(.8)
    if frame==12:forge.screenshot('retarget1.ppm')
    if frame==13:
        target.visible=False;e.visible=True;e.position=(0,0,0);forge.set_morph_weights(e,{})
        a.configure(layers=[{'clip':'Move','time':.8}],auto_update=False)
    if frame==14:forge.screenshot('animated-morph.ppm')
    if frame==15:a.configure(layers=[{'clip':'Authored','duration':2,'time':1,'tracks':[{'node':'Joint','position':[{'time':0,'value':[0,0,0]},{'time':2,'value':[0,2,0]}]}]}],auto_update=False)
    if frame==16:forge.screenshot('authored.ppm')
""",frames=18)
        for first,second in [('morph0.ppm','morph1.ppm'),('independent.ppm','both.ppm'),('retarget0.ppm','retarget1.ppm'),('morph0.ppm','animated-morph.ppm'),('morph0.ppm','authored.ppm')]:
            self.assertNotEqual((self.root/first).read_bytes(),(self.root/second).read_bytes(),(first,second))
        # Updating the second instance must not alter pixels of the first instance.
        w,h,a=base.ppm(self.root/'independent.ppm');_,_,b=base.ppm(self.root/'both.ppm')
        for y in range(h):self.assertEqual(a[(y*w)*3:(y*w+w//2)*3],b[(y*w)*3:(y*w+w//2)*3])

    def test_animation_editor_panel_with_native_playback(self):
        from model_fixture import authoring_triangle
        authoring_triangle(self.root/'models/authoring.gltf')
        self.config['window'].update(width=1280,height=720)
        # run_scene supplies its standard viewport; the inspector panel still opens.
        output=self.run_scene("""import forge
def build():return {'mode':'3d','physics_enabled':False}
def on_start():
    e=forge.spawn({'kind':'mesh','model':'authoring.gltf','animator':{'layers':[{'clip':'Move','events':[{'name':'middle','time':.5}]}]}})
    forge.set_window({'width':1280,'height':900})
    forge.editor_select(e);forge.screenshot('animation-editor.ppm')
""",command='edit',frames=4)
        self.assertNotIn('[ERROR]',output)
        w,h,pixels=base.ppm(self.root/'animation-editor.ppm')
        self.assertGreater(len(set(pixels)),30)

    def test_large_finite_camera_and_light_directions_keep_correct_pixels(self):
        self.run_scene("""import forge
frames=0
def build():return {'mode':'3d','physics_enabled':False,'background':[0,0,0,1],'camera':{'position':[0,0,4],'target':[0,0,-1e20]},'rendering':{'ambient':[.1,.1,.1],'lights':[{'type':'directional','direction':[0,-1e20,-1e20],'intensity':2}]}}
def on_start():
    forge.spawn({'kind':'cube','scale':[2,2,2],'material_properties':{'shading':'pbr','roughness':.8,'base_color':[.2,.8,.5,1]}})
def on_update(dt):
    global frames
    frames+=1
    if frames==2:forge.screenshot('large.ppm')
    if frames==3:
        forge.set_camera((0,0,4),(0,0,0));forge.set_lights([{'type':'directional','direction':[0,-1,-1],'intensity':2}])
    if frames==4:forge.screenshot('normal.ppm')
""",frames=6)
        large=base.point(self.root/'large.ppm',320,240);normal=base.point(self.root/'normal.ppm',320,240)
        self.assertGreater(normal[1],60);self.assertTrue(all(abs(a-b)<=2 for a,b in zip(large,normal)),(large,normal))

if __name__=='__main__':unittest.main(verbosity=2)
