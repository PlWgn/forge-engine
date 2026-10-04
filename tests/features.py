"""Forge 2.0 public APIs, exercised inside the real native runtime."""
import json, os, shutil, subprocess, sys, time, unittest
from pathlib import Path
import integration as base
from model_fixture import animated_triangle
ROOT, ENGINE = base.ROOT, base.ENGINE

class FeatureTests(unittest.TestCase):
    setUp, tearDown = base.EngineTests.setUp, base.EngineTests.tearDown
    write_config, run_engine, script_scene = base.EngineTests.write_config, base.EngineTests.run_engine, base.EngineTests.script_scene
    def test_actions_record_replay_and_contexts(self):
        self.script_scene('''import forge
from input_actions import ActionMap,InputRecorder,InputReplay
def on_start():
    actions=ActionMap({'jump':['key:SPACE','pad:0:a'],'move':[{'input':'key:A','scale':-1},'axis:0:left_x'],'trigger':'axis:0:right_trigger'},automatic=False)
    record=InputRecorder(automatic=False)
    frame={'keys':['SPACE','A'],'pressed':['SPACE'],'buttons':[0],'position':[12,34],'delta':[1,2],'scroll':[0,1],
           'gamepads':[{'id':0,'buttons':[0],'axes':[.575,0,0,0,-1,-1]}],'events':[{'type':'text','codepoint':1040}],'dt':.02}
    forge.inject_input(frame);actions.update();record.update()
    assert actions.pressed('jump') and abs(actions.value('move')+.5)<1e-5
    assert not actions.down('trigger')
    assert forge.key_pressed('SPACE') and forge.mouse_down() and tuple(forge.mouse_position())==(12,34)
    assert forge.input_events()[0]['codepoint']==1040 and len(forge.gamepads())==1
    forge.inject_input({'keys':[],'dt':.01});actions.update();record.update();assert actions.released('jump')
    forge.inject_input({'gamepads':[{'id':0,'axes':[0,0,0,0,-1,1]}]});actions.update();assert actions.value('trigger')==1
    record.save('replay/session.json')
    replay=InputReplay({'format':'forge.input/1','frames':record.frames},automatic=False)
    replay.update();actions.update();assert actions.pressed('jump') and forge.key_down('A')
    replay.update();actions.update();assert actions.released('jump') and replay.finished
    actions.bind('accept','key:ENTER',context='menu');actions.push_context('menu')
    forge.inject_input({'keys':['SPACE','ENTER']});actions.update();assert actions.down('accept') and not actions.down('jump')
    actions.pop_context();actions.bind('jump','key:J');actions.save();actions.bind('jump','key:K');actions.load()
    assert actions.bindings['default']['jump'][0]['input']=='key:J'
    record.close();replay.close();actions.close();forge.log('INPUT_REPLAY_OK');forge.quit()
''')
        self.assertIn('INPUT_REPLAY_OK',self.run_engine())
        self.assertEqual(len(json.loads((self.root/'replay/session.json').read_text())['frames']),2)
    def test_input_validation_and_font_fallback_diagnostics(self):
        self.config['renderer']['fallback_fonts']=['font.ttf']
        self.script_scene('''import forge
def on_start():
    forge.inject_input({'keys':['A'],'position':[1,2]});before=forge.input_snapshot()
    for value in ({'keys':'A'},{'keys':[4]},{'position':[0]},{'buttons':[8]},{'gamepads':[{'id':0,'axes':[0]}]},{'dt':2}):
        try:forge.inject_input(value)
        except RuntimeError:pass
        else:raise AssertionError('Invalid input accepted')
        assert forge.input_snapshot()==before
    first=forge.measure_text('Hello / Привет',24);assert first[0]>0
    forge.measure_text('\\U0001f984',24);forge.measure_text('\\U0001f984',48)
    forge.log('FONT_INPUT_OK');forge.quit()
''')
        output=self.run_engine();self.assertIn('FONT_INPUT_OK',output)
        self.assertEqual(output.count('Missing glyph U+1F984'),1)
    def test_runtime_loads_future_assets_on_demand(self):
        (self.root/'textures/future.png').write_bytes(b'not an image')
        self.script_scene('''import forge
from assets import AssetHandle,AssetError
def on_start():
    future=AssetHandle('textures','future.png')
    try:future.wait()
    except AssetError:pass
    else:raise AssertionError('Invalid future asset decoded')
    future.close();forge.log('LAZY_ASSETS_OK');forge.quit()
''')
        self.assertIn('LAZY_ASSETS_OK',self.run_engine())
        self.assertIn('Invalid texture',self.run_engine('validate',expected=1))
    def test_scheduler_states_timeline_and_character(self):
        self.script_scene('''import forge
from ai import Scheduler,StateMachine,Timeline,Sequence
from character import CharacterController
def build(): return {'mode':'3d','gravity':[0,-10,0]}
def on_start():
    calls=[];scheduler=Scheduler(automatic=False,budget=2)
    cancelled=scheduler.call_later(.1,lambda:calls.append('bad'));scheduler.cancel(cancelled)
    scheduler.call_later(.1,lambda:calls.append('a'));scheduler.call_later(.1,lambda:calls.append('b'));scheduler.call_later(.1,lambda:calls.append('c'))
    assert scheduler.update(.1)==2 and calls==['a','b'];scheduler.update(0);assert calls[-1]=='c'
    repeated=scheduler.every(.1,lambda:calls.append('r'));scheduler.update(.5);assert calls.count('r')==1
    forge.set_paused(True);scheduler.update(1);assert calls.count('r')==1;forge.set_paused(False)
    scheduler.cancel(repeated);scheduler.close()
    machine=StateMachine({'idle':{'enter':lambda p:calls.append('idle')},'walk':{'enter':lambda p:calls.append(p),'event':lambda n:calls.append(n)}},'idle')
    machine.change('walk');machine.event('jump');assert calls[-2:]==['idle','jump'];machine.close()
    cues=[];timeline=Timeline([(0,lambda:cues.append(0)),(.5,lambda:cues.append(1))],duration=1,loop=True)
    timeline.update(1.25);assert cues==[0,1,0]
    sequence=Sequence([(.1,lambda:cues.append(2)),(.2,lambda:cues.append(3))]);assert sequence.update(.15);assert not sequence.update(.2) and cues[-2:]==[2,3]
    body=forge.spawn({'kind':'empty','position':[0,2,0],'collider':[1,2,1]})
    floor=forge.spawn({'kind':'empty','position':[0,-.5,0],'collider':[100,1,100]})
    wall=forge.spawn({'kind':'empty','position':[3,5,0],'collider':[1,10,100]})
    controller=CharacterController(body,automatic=False)
    result=controller.move((20,-20,2));assert result['grounded'] and 1.99<body.position[0]<2.01 and abs(body.position[2]-2)<1e-5
    assert controller.jump();controller.update(.01);assert body.position[1]>1.01
    before=body.position;forge.set_physics_enabled(False);controller.update(.1);assert body.position==before
    controller.close();forge.log('AI_CHARACTER_OK');forge.quit()
''')
        self.assertIn('AI_CHARACTER_OK',self.run_engine())
    def test_assets_preload_memory_and_modern_models(self):
        animated_triangle(self.root/'models/animated.gltf')
        shutil.copy2(ROOT/'vendor/assimp/test/models/FBX/box.fbx',self.root/'models/box.fbx')
        (self.root/'scenes/preload.json').write_text(json.dumps({'preload':[{'group':'audio','file':'notify.wav'}], 'entities':[{'kind':'mesh','model':'animated.gltf'},{'texture':'icon.png'}]}))
        self.script_scene('''import forge
from assets import AssetHandle,ScenePreloader,AssetError
def on_start():
    preloader=ScenePreloader.scene('preload.json');preloader.wait();assert preloader.ready and preloader.progress==1
    info=forge.model_info('animated.gltf');assert info['bones']==1 and info['animations'][0]['name']=='Move',info
    first=forge.animation_pose('animated.gltf','Move',0);last=forge.animation_pose('animated.gltf','Move',1,False)
    assert first!=last and forge.model_info('box.fbx')['vertices']>=36
    assert forge.animation_pose('animated.gltf','Move',1e300) # large finite time must remain bounded
    entity=forge.spawn({'kind':'mesh','model':'animated.gltf'});forge.play_animation(entity,'Move');forge.pause_animation(entity);assert entity.animation=='Move'
    stats=forge.asset_stats();assert stats['handles']==3 and stats['resident_bytes']>0
    try:forge.set_asset_budget(1)
    except RuntimeError:pass
    else:raise AssertionError('Pinned assets exceeded budget')
    preloader.close();forge.set_asset_budget(1);assert forge.asset_stats()['resident_bytes']==0
    failed=AssetHandle('textures','icon.png')
    try:failed.wait()
    except AssetError:pass
    else:raise AssertionError('Texture exceeded budget')
    failed.close();forge.set_asset_budget(256*1024*1024)
    with AssetHandle('audio','notify.wav') as audio:assert audio.wait().bytes()[:4]==b'RIFF'
    forge.log('ASSETS_MODELS_OK');forge.quit()
''')
        self.assertIn('ASSETS_MODELS_OK',self.run_engine())
    def test_audio_dsp_ducking_subtitles_and_limits(self):
        self.script_scene('''import forge
from audio import music,voice,sfx
from subtitles import Subtitles
from ui import Label
frames=0
def on_start():
    global narration,caption,subtitles
    forge.configure_audio({'max_voices':3,'channel_limits':{'voice':1},'streaming':'stream','ducking':[{'source':'voice','target':'music','gain':.2,'attack':0,'release':0}]})
    music.play('notify.wav',loop=True)
    narration=voice.play('notify.wav',loop=True,pan=-.5,position=(-2,0,0),lowpass=1500,highpass=100,echo_seconds=.05)
    caption=Label();subtitles=Subtitles(narration,[{'start':0,'end':10,'text':'Hello'}],caption,automatic=False)
    subtitles.update();assert caption.value=='Hello'
    narration.pause();subtitles.update();assert caption.value=='Hello' and narration.info['paused']
    narration.resume();narration.pan=.5;narration.position=(2,0,0)
    info=narration.info;assert info['pan']==.5 and info['position']==[2,0,0] and info['stream']
    forge.set_audio_listener((0,0,0),(0,0,-1))
    sfx.play('notify.wav',loop=True);sfx.play('notify.wav',loop=True)
    assert forge.audio_stats()['voices']==3 and forge.audio_stats()['stolen']==1
def on_update(dt):
    global frames
    frames+=1
    if frames==2:
        assert narration.cursor>0, narration.info
        assert abs(forge.audio_stats()['duck_gains']['music']-.2)<1e-5,forge.audio_stats()
        narration.stop();subtitles.update();assert caption.value=='';subtitles.close()
        forge.log('AUDIO_DSP_OK');forge.quit()
''')
        self.assertIn('AUDIO_DSP_OK',self.run_engine())
    def test_theme_navigation_sheet_and_window_preferences(self):
        self.script_scene('''import forge
from ui import Canvas,Theme,ScreenStack,Column,Button,Label
from settings import WindowSettings
from animation import SpriteSheet
from input_actions import ActionMap
def on_start():
    canvas=Canvas(Column(),automatic=False,theme=Theme({'Button':{'background':(.9,.1,.2,1),'transition':0}}))
    stack=ScreenStack(canvas,{'home':Column(Button('A',lambda:None),width=500),'options':Column(Label('Options'),width=500)},duration=.2)
    stack.state['chapter']=3;stack.show('home');stack.update(.2);canvas.update(.2)
    button=stack.screens['home'].children[0];assert all(abs(a-b)<1e-6 for a,b in zip(button._entities['background'].color,(.9,.1,.2,1)))
    stack.show('options');stack.update(.1);assert abs(stack.screens['options'].opacity-.5)<1e-6
    stack.update(.1);assert not stack.screens['home'].visible;stack.back();stack.update(.2);assert stack.current=='home' and stack.state['chapter']==3
    settings=WindowSettings();settings.set(width=1600,height=900,fullscreen=True,vsync=False)
    assert settings.values['width']==1600
    entity=forge.spawn({'texture':'icon.png'});sheet=SpriteSheet('icon.png',2,2);animation=sheet.animate(entity,fps=2)
    animation.update(.5);assert tuple(entity.uv)==(.5,0,.5,.5)
    stack.close();canvas.close();forge.log('UI_NAVIGATION_OK');forge.quit()
''')
        self.assertIn('UI_NAVIGATION_OK',self.run_engine())
        self.assertEqual(json.loads((self.root/'saves/preferences/window.json').read_text())['data']['width'],1600)
    def test_user_storage_and_crash_reports(self):
        self.config['storage']={'mode':'user','application_id':'org.forge.test'}
        self.script_scene('''import forge,json
from pathlib import Path
from saves import SaveManager
def on_start():
    assert 'org.forge.test' in forge.user_path('data')
    for kind in ('data','saves','config','cache','logs'): assert 'org.forge.test' in forge.user_path(kind)
    try:forge.user_path('data','../escape')
    except RuntimeError:pass
    else:raise AssertionError('User path escaped')
    forge.save('progress',{'chapter':2});assert forge.load('progress')['chapter']==2
    store=SaveManager();store.write('a',{'value':3});assert store.read('a')['value']==3
    report=Path(forge.crash_report('USER_REPORT'));assert json.loads(report.read_text())['message']=='USER_REPORT'
    raise RuntimeError('EXPECTED_CRASH')
''')
        env=dict(os.environ,FORGE_USER_ROOT=str(self.root/'user'))
        result=subprocess.run([str(ENGINE),'run','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],env=env,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        user=self.root/'user/org.forge.test'
        self.assertTrue((user/'saves/progress.json').exists())
        self.assertTrue((user/'logs/forge.log').exists())
        self.assertGreaterEqual(len(list((user/'logs/crash-reports').glob('*.json'))),2)
        self.assertFalse((self.root/'saves/progress.json').exists())
    def test_render_configuration_validation_and_scene_serialization(self):
        self.script_scene('''import forge,json
from pathlib import Path
def on_start():
    before=forge.render_settings()
    for options in ({'width':0},{'width':1.5},{'mode':'bad'},{'position':[0,0,0],'target':[0,0,0]}):
        try:forge.set_render_target('bad',options)
        except RuntimeError:pass
        else:raise AssertionError('Invalid target accepted')
        assert forge.render_settings()==before
    texture=forge.set_render_target('door',{'width':320,'height':240,'position':[1,1,5],'target':[0,0,0]})
    assert texture=='@target:door'
    forge.set_postprocess({'grain':.2,'bloom':.5,'gamma':1.1});forge.set_shader_uniform('custom_color',[.2,.3,.4,1])
    forge.set_lights([{'type':'directional','direction':[0,-1,0],'shadows':True}])
    forge.spawn({'id':'monitor','texture':texture,'uv':[0,0,1,1],'layer':2})
    forge.save_scene('serialized.json');data=json.loads(Path(forge.asset_path('scenes','serialized.json')).read_text())
    assert data['rendering']['targets']['door']['width']==320 and data['entities'][0]['layer']==2
    forge.log('RENDER_API_OK');forge.quit()
''')
        self.assertIn('RENDER_API_OK',self.run_engine())
    def test_deferred_persistence_commits_only_successful_reload(self):
        self.script_scene('''import forge
from pathlib import Path
from saves import SaveManager
def on_start():
    stage=forge.settings().get('stage',0)
    forge.save('simple',{'stage':stage})
    SaveManager().write('managed',{'stage':stage})
    forge.defer_persistence(lambda:Path(forge.storage_path('deferred.txt')).write_text(str(stage)))
    if stage==1:
        forge.asset_request('textures','icon.png') # abandoned raw handle must also roll back
        forge.inject_input({'keys':['X'],'dt':.5})
        raise RuntimeError('REJECT_CANDIDATE')
    forge.log('STAGE_'+str(stage))
def on_reload_failed(error):
    assert forge.asset_stats()['handles']==0
    assert not forge.key_down('X') and abs(forge.dt()-1/60)<1e-5
    forge.log('REJECTED')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
''')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and marker in log.read_text():return
                if process.poll() is not None:self.fail(log.read_text())
                time.sleep(.03)
            self.fail('Timeout '+marker)
        try:
            wait('STAGE_0')
            self.config['stage']=1;self.write_config();wait('REJECTED')
            assert json.loads((self.root/'saves/simple.json').read_text())['stage']==0
            assert json.loads((self.root/'saves/slots/managed.json').read_text())['data']['stage']==0
            self.assertEqual((self.root/'deferred.txt').read_text(),'0')
            self.config['stage']=2;self.write_config();wait('STAGE_2')
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                if json.loads((self.root/'saves/simple.json').read_text())['stage']==2 and (self.root/'deferred.txt').read_text()=='2':break
                if process.poll() is not None:self.fail((self.root/'forge.log').read_text())
                time.sleep(.03)
            else:self.fail('Timed out waiting for persistence commit')
            self.assertEqual(json.loads((self.root/'saves/simple.json').read_text())['stage'],2)
            self.assertEqual((self.root/'deferred.txt').read_text(),'2')
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()
    @unittest.skipUnless(sys.platform=='darwin','macOS .app packaging')
    def test_app_bundle_private_runtime_user_paths_manifest_and_signature(self):
        self.script_scene('''import forge
from pathlib import Path
def on_start():
    forge.save('app',{'ok':True});assert 'user'==forge.settings()['storage']['mode'];forge.log('APP_STARTED');forge.quit()
''')
        output=self.root/'Game.app';self.run_engine('build',extra=('--output',output))
        import plistlib,hashlib
        info=plistlib.loads((output/'Contents/Info.plist').read_bytes());self.assertEqual(info['CFBundleExecutable'],'Game')
        self.assertTrue((output/'Contents/Resources/Game.icns').exists())
        manifest=json.loads((output/'Contents/Resources/manifest.json').read_text())
        for name,digest in manifest['files'].items():self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(),digest,name)
        subprocess.run(['codesign','--verify','--deep','--strict',str(output)],check=True,capture_output=True)
        env=dict(os.environ,FORGE_USER_ROOT=str(self.root/'app-user'),PYTHONHOME='/invalid',PYTHONPATH='/invalid')
        result=subprocess.run([str(output/'Contents/MacOS/Game'),'--headless','--no-open-log'],cwd='/tmp',env=env,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertIn('APP_STARTED',result.stdout)
        self.assertTrue((self.root/'app-user'/self.config['storage']['application_id']/'saves/app.json').exists())

if __name__=='__main__':unittest.main(verbosity=2)
