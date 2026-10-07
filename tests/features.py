"""Forge 2.0 public APIs, exercised inside the real native runtime."""
import json, os, shutil, subprocess, sys, tempfile, time, unittest
from pathlib import Path
import integration as base
from model_fixture import animated_triangle
ROOT, ENGINE = base.ROOT, base.ENGINE

class FeatureTests(unittest.TestCase):
    setUp, tearDown = base.EngineTests.setUp, base.EngineTests.tearDown
    write_config, run_engine, script_scene = base.EngineTests.write_config, base.EngineTests.run_engine, base.EngineTests.script_scene

    def test_native_input_actions_capture_and_managed_profiles(self):
        self.script_scene('''import forge
from input_actions import InputManager
from pathlib import Path
from saves import SaveError
def on_start():
    assert 'native_input' in forge.capabilities()['features']
    manager=InputManager({'jump':['key:SPACE','pad:any:a'],
                          'fire':'key:F',
                          'move':[{'input':'key:A','scale':-1},'key:D','axis:any:left_x']},automatic=False)
    assert manager.load() is False
    forge.inject_input({'keys':['A','SPACE']});manager.update(.1)
    assert manager.value('move')==-1 and manager.pressed('jump')
    forge.inject_input({'buttons':[0]})
    manager.begin_rebind('jump')
    manager.update(.1);assert manager.capturing,'Opening click captured'
    forge.inject_input({});manager.update(.1)
    forge.inject_input({'keys':['CTRL','J']});manager.update(.1)
    assert manager.capture_state()['status']=='bound' and not manager.down('jump')
    manager.update(.1);assert not manager.down('jump'),'Held capture activated game action'
    forge.inject_input({});manager.update(.1)
    forge.inject_input({'keys':['RIGHT_CTRL','J']});manager.update(.1)
    assert manager.pressed('jump')
    manager.save()
    assert manager.bindings['default']['jump'][0]['modifiers']==['CTRL']
    manager.bind('jump',[]);assert manager.load()
    assert manager.bindings['default']['jump'][0]['input']=='key:J'
    # Defaults are independent; backup-only recovery and corrupt profiles preserve state.
    manager.bind('jump','key:K');manager.save()
    path=Path(forge.storage_path('saves/preferences/native_input.json'))
    path.unlink();assert manager.load()
    assert manager.bindings['default']['jump'][0]['input']=='key:J'
    before=manager.export_profile();manager.store.write(manager.slot,{'format':'broken'})
    try:manager.load()
    except ValueError:pass
    else:raise AssertionError('Bad profile accepted')
    assert manager.export_profile()==before
    manager.reset('jump');assert manager.bindings['default']['jump'][0]['input']=='key:SPACE'
    # Rebinding one slot must not reject a previously shared, unchanged slot.
    manager.bind('shared','pad:any:a')
    forge.inject_input({});manager.update(.1);manager.begin_rebind('jump')
    forge.inject_input({'keys':['L']});manager.update(.1)
    assert manager.capture_state()['status']=='bound'
    manager.close();forge.log('NATIVE_INPUT_PROFILE_OK');forge.quit()
''')
        self.assertIn('NATIVE_INPUT_PROFILE_OK',self.run_engine())

    def test_native_input_validation_and_profile_copies(self):
        self.script_scene('''import forge,copy,gc,weakref
from input_actions import InputManager
reference=None
class Invalid(InputManager):
    def __init__(self):
        global reference
        reference=weakref.ref(self)
        super().__init__({'bad':'key:NOT_A_KEY'})
def rejects(call):
    try:call()
    except (ValueError,RuntimeError,TypeError):return
    raise AssertionError('Invalid input accepted')
def on_start():
    rejects(Invalid);gc.collect();assert reference() is None,'Failed construction retained listener'
    for zone in (True,False,float('nan'),float('inf'),1,-.1):
        rejects(lambda:forge.InputManager({},deadzone=zone))
    m=forge.InputManager({'jump':'key:SPACE','exact':{'input':'key:CTRL','exact_modifiers':True}})
    m.update_snapshot({'keys':['CTRL']},.1);assert m.down('exact')
    before=m.export_profile()
    for binding in ('key:F1junk','mouse:8','pad:16:a','axis:0:wrong',
                    {'input':'key:J','scale':True},{'input':'key:J','scale':1e20},
                    {'input':'key:J','modifiers':['CTRL','CTRL']},
                    {'input':'key:J','direction':1},{'input':'key:J','deadzone':1}):
        rejects(lambda:m.bind('jump',binding));assert m.export_profile()==before
    snapshot=m.bindings;snapshot['default']['jump'][0]['input']='key:J'
    assert m.export_profile()==before
    bad=copy.deepcopy(before);bad['contexts']['default']['jump']=['key:unknown']
    m.begin_rebind('jump');rejects(lambda:m.import_profile(bad));assert m.capturing
    for dt in (True,False,float('nan'),float('inf'),-1,2):
        rejects(lambda:m.update_snapshot({},dt));assert m.capturing
    rejects(lambda:m.update_snapshot({'gamepads':[{'id':0,'axes':[0,0,0,0,-1,2]}]}))
    rejects(lambda:m.repeat('jump',interval=0));rejects(lambda:m.repeat('jump',delay=False))
    m.cancel_rebind();m.update_snapshot({'gamepads':[{'id':0}]},.1)
    assert not m.down('jump')
    # Profiles merge new game defaults while keeping unknown data and explicit unbinding.
    saved=copy.deepcopy(before);saved['custom']={'language':'en'}
    saved['contexts']['default']['jump']=[]
    upgraded=forge.InputManager({'jump':'key:SPACE','added':'key:N'})
    upgraded.import_profile(saved)
    assert upgraded.bindings['default']['jump']==[] and 'added' in upgraded.bindings['default']
    assert upgraded.export_profile()['custom']==saved['custom']
    forge.log('NATIVE_INPUT_VALIDATION_OK');forge.quit()
''')
        self.assertIn('NATIVE_INPUT_VALIDATION_OK',self.run_engine())

    def test_native_input_automatic_updates_follow_replay_and_continue_paused(self):
        self.script_scene('''import forge
from input_actions import InputManager,InputReplay
count=0
def on_start():
    global manager,replay
    replay=InputReplay({'format':'forge.input/1','frames':[{'keys':['J'],'dt':.1},{'keys':[],'dt':.1}]})
    manager=InputManager({'jump':'key:J'})
    forge.on_frame(check)
    forge.set_paused(True)
def check(dt):
    global count
    count+=1
    if count==1:assert manager.pressed('jump')
    if count==2:
        assert manager.released('jump')
        manager.close();replay.close();forge.log('NATIVE_INPUT_PAUSED_OK');forge.quit()
''')
        self.assertIn('NATIVE_INPUT_PAUSED_OK',self.run_engine())

    def test_native_input_preferences_rollback_on_failed_reload(self):
        self.script_scene('''import forge
from input_actions import InputManager
from pathlib import Path
import weakref,gc
references=[]
def on_start():
    global manager
    stage=forge.settings().get('stage',0)
    manager=InputManager({'jump':'key:SPACE'})
    manager.bind('jump','key:'+('K' if stage==1 else 'J' if stage==0 else 'L'))
    manager.save()
    if stage==1:
        # Share only a weak reference with the old scene to check abandoned listener release.
        import builtins
        builtins.forge_input_candidate=weakref.ref(manager)
        raise RuntimeError('INPUT_REJECT_CANDIDATE')
    if stage==2:
        import builtins
        gc.collect();assert builtins.forge_input_candidate() is None,'Candidate listener retained after reload'
    forge.log('INPUT_STAGE_'+str(stage))
def on_reload_failed(error):
    import builtins
    # The exception traceback is still alive during this callback; inspect its
    # weak reference only after the next successful reload.
    assert manager.bindings['default']['jump'][0]['input']=='key:J'
    assert manager.store.read(manager.slot)['contexts']['default']['jump'][0]['input']=='key:J'
    forge.log('INPUT_REJECTED')
def on_update(dt):
    if Path(forge.project_path('stop')).exists():forge.quit()
def on_destroy():manager.close()
''')
        process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        def wait(marker):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log=self.root/'forge.log'
                if log.exists() and marker in log.read_text():return
                if process.poll() is not None:self.fail(log.read_text() if log.exists() else 'Engine exited')
                time.sleep(.03)
            self.fail('Timeout '+marker+(log.read_text() if log.exists() else ''))
        try:
            wait('INPUT_STAGE_0')
            self.config['stage']=1;self.write_config();wait('INPUT_REJECTED')
            file=self.root/'saves/preferences/native_input.json'
            self.assertEqual(json.loads(file.read_text())['data']['contexts']['default']['jump'][0]['input'],'key:J')
            self.config['stage']=2;self.write_config();wait('INPUT_STAGE_2')
            wait('Hot reload complete')
            deadline=time.monotonic()+15
            while json.loads(file.read_text())['data']['contexts']['default']['jump'][0]['input']!='key:L':
                if time.monotonic()>deadline:self.fail('Preferences not committed')
                time.sleep(.03)
            (self.root/'stop').touch();self.assertEqual(process.wait(timeout=15),0)
        finally:
            if process.poll() is None:process.kill();process.wait()

    def test_invalid_fonts_report_errors_instead_of_native_crashes(self):
        font=self.root/'graphics/broken.ttf'
        self.config['renderer']['font']='broken.ttf';self.write_config()
        for content in (b'',b'not a font',b'\x00\x01\x00\x00'+b'\x00'*8,
                        b'ttcf'+b'\x00'*12,(self.root/'graphics/font.ttf').read_bytes()[:128]):
            with self.subTest(content=content):
                font.write_bytes(content)
                self.assertIn('Invalid TrueType font',self.run_engine('validate',expected=1))

    def test_replay_atomic_write_does_not_follow_existing_temp_symlink(self):
        outside=self.root.parent/(self.root.name+'-outside.json')
        try:
            outside.write_text('untouched',encoding='utf-8')
            folder=self.root/'replay';folder.mkdir()
            try:(folder/'session.json.tmp').symlink_to(outside)
            except OSError:self.skipTest('Symlink creation unavailable')
            self.script_scene('''import forge
from input_actions import InputRecorder
def on_start():
    record=InputRecorder(automatic=False);record.update();record.save('replay/session.json')
    forge.log('REPLAY_ATOMIC_OK');forge.quit()
''')
            self.assertIn('REPLAY_ATOMIC_OK',self.run_engine())
            self.assertEqual(outside.read_text(encoding='utf-8'),'untouched')
            self.assertEqual(json.loads((folder/'session.json').read_text())['format'],'forge.input/1')
        finally:outside.unlink(missing_ok=True)

    def test_whole_project_validation_rejects_resource_symlink_escapes(self):
        with tempfile.TemporaryDirectory(prefix='forge-outside-') as directory:
            for group,name,content in [('materials','external.json',b'{"color":[1,1,1,1]}'),
                                       ('modules','external.py',b'VALUE=1\n'),
                                       ('textures','external.png',(self.root/'textures/icon.png').read_bytes())]:
                with self.subTest(group=group):
                    source=Path(directory)/name;source.write_bytes(content)
                    alias=self.root/self.config['paths'][group]/name
                    try:alias.symlink_to(source)
                    except OSError:self.skipTest('Symlink creation unavailable')
                    try:self.assertIn('escapes project root',self.run_engine('validate',expected=1))
                    finally:alias.unlink()

    def test_failed_action_map_construction_releases_frame_listener(self):
        self.script_scene('''import forge,gc,weakref
from input_actions import ActionMap,InputRecorder
reference=None
class InvalidActionMap(ActionMap):
    def __init__(self):
        global reference
        reference=weakref.ref(self)
        super().__init__({'bad':'not-a-binding'})
def on_start():
    try:InvalidActionMap()
    except ValueError:pass
    else:raise AssertionError('Invalid binding accepted')
    gc.collect();assert reference() is None,'Failed ActionMap retained by frame listener'
    for limit in (0,-1,True,.5,float('nan'),float('inf')):
        try:InputRecorder(max_frames=limit)
        except ValueError:pass
        else:raise AssertionError('Invalid recording capacity accepted')
    forge.log('INPUT_CONSTRUCTION_OK');forge.quit()
''')
        self.assertIn('INPUT_CONSTRUCTION_OK',self.run_engine())

    def test_scene_preloader_includes_lod_emitters_and_skips_render_targets(self):
        (self.root/'scenes/preload-lod.json').write_text(json.dumps({'emitters':[{'texture':'icon.png'}],'entities':[
            {'kind':'mesh','model':'crystal.obj','texture':'@target:screen','optimization':{'levels':[{'distance':10,'model':'sphere.obj','texture':'particle.png'}]},
             'material_properties':{'normal_texture':'pbr-normal.png'}}]}),encoding='utf-8')
        self.script_scene('''import forge
from assets import ScenePreloader
def on_start():
    with ScenePreloader.scene('preload-lod.json') as loader:
        loader.wait()
        assert ('textures','particle.png') in loader.handles
        assert ('textures','icon.png') in loader.handles
        assert ('models','sphere.obj') in loader.handles
        assert ('textures','pbr-normal.png') in loader.handles
        assert not any(name.startswith('@') for group,name in loader.handles)
    forge.log('LOD_PRELOAD_OK');forge.quit()
''')
        self.assertIn('LOD_PRELOAD_OK',self.run_engine())
    def test_event_unsubscribe_is_idempotent_and_releases_callbacks(self):
        self.script_scene("""import forge,gc,weakref
from events import EventBus
def on_start():
    bus=EventBus();calls=[]
    callback=lambda:calls.append('called')
    first=bus.on('event',callback);second=bus.on('event',callback)
    first();first();bus.emit('event');assert calls==['called']
    second();second()
    class Receiver:
        def callback(self):pass
    receiver=Receiver();reference=weakref.ref(receiver)
    unsubscribe=bus.on('owned',receiver.callback)
    del receiver;gc.collect();assert reference() is not None
    unsubscribe();gc.collect();assert reference() is None,'Cleanup retained retired callback owner'
    for i in range(1000):bus.emit('unused-'+str(i))
    assert not bus._listeners,'Emitting unknown events retained empty entries'
    forge.log('EVENT_LIFETIME_OK');forge.quit()
""")
        self.assertIn('EVENT_LIFETIME_OK',self.run_engine())
    def test_scheduler_cancellation_bounds_retained_queue(self):
        self.script_scene("""import forge
from ai import Scheduler
def on_start():
    scheduler=Scheduler(automatic=False,budget=3);calls=[]
    for i in range(10):scheduler.call_later(1,lambda i=i:calls.append(i))
    canceled=[scheduler.call_later(1000,lambda:None) for _ in range(5000)]
    for handle in canceled:scheduler.cancel(handle)
    assert scheduler.pending==10 and len(scheduler._queue)<300,'Canceled timers retained an unbounded heap'
    assert scheduler.update(1)==3 and calls==[0,1,2]
    while scheduler.pending:scheduler.update(0)
    assert calls==list(range(10));scheduler.close()
    forge.log('SCHEDULER_QUEUE_OK');forge.quit()
""")
        self.assertIn('SCHEDULER_QUEUE_OK',self.run_engine())
    def test_scheduler_rejects_overflow_without_poisoning_clock(self):
        self.script_scene("""import forge
from ai import Scheduler
def on_start():
    scheduler=Scheduler(automatic=False);scheduler.update(1e308)
    for operation in (lambda:scheduler.update(1e308),lambda:scheduler.call_later(1e308,lambda:None)):
        try:operation()
        except ValueError:pass
        else:raise AssertionError('Overflowing timer clock accepted')
        assert scheduler.time==1e308 and scheduler.pending==0
    repeat=Scheduler(automatic=False);calls=[]
    repeat.every(1e308,lambda:calls.append('once'))
    try:repeat.update(1e308)
    except ValueError:pass
    else:raise AssertionError('Overflowing repeating deadline accepted')
    assert calls==['once'] and repeat.pending==0 and not repeat._queue
    repeat.close()
    scheduler.close();forge.log('SCHEDULER_NUMBERS_OK');forge.quit()
""")
        self.assertIn('SCHEDULER_NUMBERS_OK',self.run_engine())
    def test_legacy_animation_rejects_invalid_steps_atomically(self):
        self.script_scene("""import forge
from animation import Tween,SpriteAnimation
def on_start():
    entity=forge.spawn({'kind':'empty'})
    for duration in (float('nan'),float('inf'),-1,0):
        try:Tween(entity,'position',(1,2,3),duration)
        except ValueError:pass
        else:raise AssertionError('Invalid tween duration accepted')
    try:Tween(entity,'position',(1,2),1)
    except ValueError:pass
    else:raise AssertionError('Mismatched tween shape accepted')
    tween=Tween(entity,'position',(1,2,3),1)
    for dt in (-1,float('nan'),float('inf')):
        try:tween.update(dt)
        except ValueError:pass
        else:raise AssertionError('Invalid tween step accepted')
        assert tween.elapsed==0 and tuple(entity.position)==(0,0,0)
    tween.update(.5);assert tuple(entity.position)==(.5,1,1.5)
    invalid=Tween(entity,'position',(1e100,0,0),1)
    before=tuple(entity.position)
    try:invalid.update(1)
    except RuntimeError:pass
    else:raise AssertionError('Overflowing native tween accepted')
    assert invalid.elapsed==0 and tuple(entity.position)==before
    animation=SpriteAnimation(entity,('a','b'),fps=1e308)
    try:animation.update(10)
    except ValueError:pass
    else:raise AssertionError('Overflowing sprite frame accepted')
    assert animation.elapsed==0 and entity.texture==''
    forge.log('LEGACY_ANIMATION_NUMBERS_OK');forge.quit()
""")
        self.assertIn('LEGACY_ANIMATION_NUMBERS_OK',self.run_engine())
    def test_asset_cache_detects_size_changes_with_preserved_timestamp(self):
        self.script_scene("""import forge,os
from pathlib import Path
from assets import AssetHandle
def on_start():
    path=Path(forge.asset_path('audio','changed.bin'))
    path.write_bytes(b'old');stamp=path.stat()
    with AssetHandle('audio','changed.bin') as first:
        assert first.wait().bytes()==b'old'
        path.write_bytes(b'new contents')
        os.utime(path,ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
        with AssetHandle('audio','changed.bin') as second:
            assert second.wait().bytes()==b'new contents','Asset cache returned old bytes'
            assert first.bytes()==b'old','A pinned old generation changed'
    forge.log('ASSET_GENERATIONS_OK');forge.quit()
""")
        self.assertIn('ASSET_GENERATIONS_OK',self.run_engine())
    def test_model_cache_tracks_external_buffer_generations(self):
        import base64
        file=self.root/'models/external.gltf';animated_triangle(file)
        model=json.loads(file.read_text());data=base64.b64decode(model['buffers'][0]['uri'].split(',')[1])
        (self.root/'models/external.bin').write_bytes(data)
        model['buffers'][0]['uri']='external.bin';file.write_text(json.dumps(model),encoding='utf-8')
        accessor=model['animations'][0]['samplers'][0]['input']
        offset=model['bufferViews'][model['accessors'][accessor]['bufferView']]['byteOffset']+4
        self.config['external_time_offset']=offset
        self.script_scene("""import forge,os,struct
from pathlib import Path
from assets import AssetHandle,AssetError
def on_start():
    with AssetHandle('models','external.gltf') as old:
        old.wait();assert old.info['model']['animations'][0]['duration']==1
        path=Path(forge.asset_path('models','external.bin'));stamp=path.stat()
        data=bytearray(path.read_bytes());struct.pack_into('<f',data,forge.settings()['external_time_offset'],2)
        path.write_bytes(data);os.utime(path,ns=(stamp.st_atime_ns,stamp.st_mtime_ns+1000000000))
        with AssetHandle('models','external.gltf') as fresh:
            fresh.wait();assert fresh.info['model']['animations'][0]['duration']==2,'Model reused an old external buffer'
        assert old.info['model']['animations'][0]['duration']==1,'Pinned model generation changed'
        assert forge.model_info('external.gltf')['animations'][0]['duration']==2
        path.unlink()
        try:
            with AssetHandle('models','external.gltf') as missing:missing.wait()
        except AssetError:pass
        else:raise AssertionError('Missing model dependency returned cached success')
        path.write_bytes(data)
        with AssetHandle('models','external.gltf') as restored:
            restored.wait();assert restored.info['model']['animations'][0]['duration']==2
    forge.log('MODEL_DEPENDENCY_GENERATIONS_OK');forge.quit()
""")
        self.assertIn('MODEL_DEPENDENCY_GENERATIONS_OK',self.run_engine())
    def test_slot_manager_rechecks_redirected_storage_directory(self):
        self.script_scene("""import forge,tempfile
from pathlib import Path
from saves import SaveManager,SaveError
def on_start():
    store=SaveManager(directory='guarded-slots')
    store.write('a',{'value':1})
    root=store.root;root.rename(root.with_name('previous-slots'))
    with tempfile.TemporaryDirectory() as external:
        root.symlink_to(external,target_is_directory=True)
        for operation in (lambda:store.write('a',{'value':2}),lambda:store.read('a'),lambda:store.info('a'),lambda:store.delete('a'),lambda:store.slots()):
            try:operation()
            except SaveError:pass
            else:raise AssertionError('Save manager accepted an escaped storage directory')
        assert not list(Path(external).iterdir())
        root.unlink()
    forge.log('SAVE_DIRECTORY_GUARD_OK');forge.quit()
""")
        self.assertIn('SAVE_DIRECTORY_GUARD_OK',self.run_engine())
    def test_deeply_corrupt_slots_recover_and_report_without_parser_crashes(self):
        self.script_scene("""import forge
from saves import SaveManager,SaveError
def on_start():
    store=SaveManager();store.write('deep',{'value':1});store.write('deep',{'value':2})
    primary=store.root/'deep.json';backup=store.root/'deep.json.bak'
    import sys
    depth=max(100000,sys.getrecursionlimit()+100)
    damage='['*depth+'0'+']'*depth
    primary.write_text(damage,encoding='utf-8')
    assert store.info('deep')['status']=='recoverable'
    assert store.read('deep')=={'value':1}
    backup.write_text(damage,encoding='utf-8')
    assert store.info('deep')['status']=='corrupt'
    try:store.read('deep')
    except SaveError:pass
    else:raise AssertionError('Corrupt slot accepted')
    for limit in (True,.5,float('nan'),float('inf')):
        try:SaveManager(max_bytes=limit)
        except ValueError:pass
        else:raise AssertionError('Invalid save size limit accepted')
    forge.log('CORRUPT_SLOT_PARSER_OK');forge.quit()
""")
        self.assertIn('CORRUPT_SLOT_PARSER_OK',self.run_engine())
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
    def test_deferred_delete_rechecks_storage_at_commit(self):
        with base.tempfile.TemporaryDirectory(prefix='forge external saves ') as external:
            victim=Path(external)/'victim.json';victim.write_text('keep',encoding='utf-8')
            self.config['external_test_directory']=external
            self.script_scene('''import forge
from pathlib import Path
from saves import SaveManager
def on_start():
    store=SaveManager(directory='guarded-slots')
    if not forge.settings().get('redirect_at_commit',False):
        store.write('victim',{'valid':True});forge.log('DELETE_READY');return
    store.delete('victim')
    root=store.root;previous=root.with_name('old-guarded-slots')
    root.rename(previous)
    root.symlink_to(forge.settings()['external_test_directory'],target_is_directory=True)
    def finish():
        root.unlink();previous.rename(root)
        forge.log('DELETE_COMMIT_COMPLETE');forge.quit()
    forge.defer_persistence(finish)
''')
            process=subprocess.Popen([str(ENGINE),'dev','--project',str(self.root/'engine.json'),'--headless','--no-open-log'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    log=self.root/'forge.log'
                    if log.exists() and 'DELETE_READY' in log.read_text():break
                    if process.poll() is not None:self.fail(log.read_text())
                    time.sleep(.03)
                else:self.fail('Timed out waiting for deferred delete fixture')
                self.config['redirect_at_commit']=True;self.write_config()
                self.assertEqual(process.wait(timeout=15),0)
                self.assertIn('DELETE_COMMIT_COMPLETE',log.read_text())
                self.assertTrue(victim.exists(),'Deferred deletion escaped the storage root')
                self.assertEqual(victim.read_text(),'keep')
                self.assertTrue((self.root/'guarded-slots/victim.json').exists())
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

    def test_audio_rejected_replacement_and_settings_preserve_state(self):
        (self.root/'audio/broken.wav').write_bytes(b'not a wav')
        self.script_scene("""import forge
frames=0
def on_start():
    forge.configure_audio({'max_voices':1})
    old=forge.play_sound('notify.wav',loop=True)
    before=forge.audio_stats()
    try:forge.play_sound('broken.wav',loop=True)
    except RuntimeError:pass
    else:raise AssertionError('invalid audio decoded')
    assert forge.channel_sounds('sfx')==[old] and forge.sound_playing(old)
    assert forge.audio_stats()['stolen']==before['stolen']
    for settings in ({'max_voices':1.5},{'channel_limits':{'voice':1.5}},{'channel_limits':[]},{'ducking':{}},{'follow_camera':1},{'stream_threshold_bytes':-1}):
        before=forge.audio_stats()
        try:forge.configure_audio(settings)
        except RuntimeError:pass
        else:raise AssertionError('invalid settings accepted '+str(settings))
        assert forge.audio_stats()==before
    for options in ({'stream':1},{'spatial':1}):
        try:forge.set_sound_options(old,options)
        except RuntimeError:pass
        else:raise AssertionError('invalid voice settings accepted')
        assert forge.channel_sounds('sfx')==[old]
    forge.stop_sounds();forge.configure_audio({'max_voices':4,'ducking':[{'source':'voice','target':'music','gain':.2,'attack':0,'release':0}]})
    forge.play_sound('notify.wav',loop=True,channel='music');forge.play_sound('notify.wav',loop=True,channel='voice')
def on_update(dt):
    global frames
    frames+=1
    if frames==1:
        assert abs(forge.audio_stats()['duck_gains']['music']-.2)<1e-5
        forge.configure_audio({'ducking':[]})
    if frames==2:
        assert forge.audio_stats()['duck_gains'].get('music',1)==1
        forge.log('AUDIO_FAILURE_PRESERVES_STATE');forge.quit()
""")
        self.assertIn('AUDIO_FAILURE_PRESERVES_STATE',self.run_engine())

    def test_scene_preloader_handles_prefabs_procedural_meshes_and_pbr_maps(self):
        (self.root/'materials/preload.json').write_text(json.dumps({'shading':'pbr','normal_texture':'icon.png','emissive_texture':'particle.png'}))
        (self.root/'objects/preload-base.json').write_text(json.dumps({'kind':'sprite','material':'preload.json'}))
        (self.root/'objects/preload-child.json').write_text(json.dumps({'extends':'preload-base.json','name':'Inherited'}))
        (self.root/'scenes/preload-all.json').write_text(json.dumps({'entities':[{'id':'a','prefab':'preload-child.json'},{'kind':'mesh','model':'@mesh:generated'},{'kind':'sprite','material_properties':{'occlusion_texture':'icon.png'}}]}))
        self.script_scene("""import forge
from assets import ScenePreloader
def on_start():
    with ScenePreloader.scene('preload-all.json') as loader:
        loader.wait()
        assert ('materials','preload.json') in loader.handles
        assert ('textures','icon.png') in loader.handles and ('textures','particle.png') in loader.handles
        assert not any(file.startswith('@') for group,file in loader.handles)
    forge.log('PRELOADER_RESOURCES_OK');forge.quit()
""")
        self.assertIn('PRELOADER_RESOURCES_OK',self.run_engine())

if __name__=='__main__':unittest.main(verbosity=2)
