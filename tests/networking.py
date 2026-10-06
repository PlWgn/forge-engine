"""Actual native UDP/Python and scene transaction regressions (requires localhost sockets)."""
import json, os, subprocess, time, unittest
import integration as base


class NetworkingTests(unittest.TestCase):
    setUp, tearDown = base.EngineTests.setUp, base.EngineTests.tearDown
    write_config, script_scene, run_engine = base.EngineTests.write_config, base.EngineTests.script_scene, base.EngineTests.run_engine

    def test_bytes_limits_and_native_facades(self):
        self.script_scene(r'''import forge,time,json
from network import Host,backends
import steam
def on_start():
    assert not steam.info()['enabled'] and steam.info()['app_id']==480
    try:steam.unlock('TEST')
    except RuntimeError:pass
    else:raise AssertionError('disabled Steam accepted achievement')
    for backend in backends():
        if backend not in ('lan','sockets'):continue
        with Host(backend=backend,bind='127.0.0.1') as server, Host(backend=backend) as client:
            peer=client.connect('127.0.0.1',server.stats()['port']);connected=False;remote=None
            deadline=time.monotonic()+8
            while not connected or remote is None:
                assert time.monotonic()<deadline,'connection timed out'
                for e in client.poll():
                    if e['type']=='connected':connected=True
                for e in server.poll():
                    if e['type']=='connected':remote=e['peer']
                time.sleep(.001)
            for action in (lambda:client.send(peer,'not-bytes'),lambda:client.send(peer,b'x',channel=99),lambda:client.send(peer,b'x'*65537),lambda:client.poll(0)):
                try:action()
                except (RuntimeError,TypeError,ValueError):pass
                else:raise AssertionError('invalid network input accepted')
            assert client.send(peer,b'\x00binary\xff',channel=2)
            assert client.send_json(peer,{'text':'Unicode ✓','sequence':1},channel=1)
            messages=[]
            while len(messages)<2:
                assert time.monotonic()<deadline,'message timed out'
                client.poll()
                messages.extend(e for e in server.poll() if e['type']=='message')
                time.sleep(.001)
            assert next(e for e in messages if e['channel']==2)['data']==b'\x00binary\xff'
            assert json.loads(next(e for e in messages if e['channel']==1)['data'])['text']=='Unicode ✓'
        assert server.closed and client.closed
    forge.log('NETWORK_BYTES_OK');forge.quit()
''')
        self.assertIn('NETWORK_BYTES_OK', self.run_engine())

    @unittest.skipIf(os.environ.get("FORGE_TEST_LAN")=="0", "ENet transport not compiled")
    def test_scene_scope_and_application_adoption(self):
        (self.root/'scenes/second.py').write_text('''import forge,builtins
from network import Host
def on_start():
    global named
    named=Host(bind='127.0.0.1',name='session',lifetime='application')
    assert named.native is builtins.named.native and not named.closed
    try:Host(bind='127.0.0.1',name='session',lifetime='application',channels=2)
    except RuntimeError:pass
    else:raise AssertionError('mismatching named host adopted')
def on_update(dt):
    assert builtins.scoped.closed
    forge.log('NETWORK_SCENE_OK');forge.quit()
''',encoding='utf-8')
        self.script_scene('''import forge,builtins
from network import Host
def on_start():
    builtins.scoped=Host(bind='127.0.0.1')
    builtins.named=Host(bind='127.0.0.1',name='session',lifetime='application')
def on_update(dt):forge.change_scene('second.py')
''')
        self.assertIn('NETWORK_SCENE_OK',self.run_engine())

    @unittest.skipIf(os.environ.get("FORGE_TEST_LAN")=="0", "ENet transport not compiled")
    def test_rejected_reload_closes_candidates_and_preserves_session(self):
        candidate='''import forge,builtins
from network import Host
def on_start():
    assert Host(bind='127.0.0.1',name='session',lifetime='application').native is builtins.named.native
    builtins.rejected=Host(bind='127.0.0.1',name='new-session',lifetime='application')
    builtins.rejected_scope=Host(bind='127.0.0.1')
    try:builtins.named.connect('127.0.0.1',12345)
    except RuntimeError as e:assert 'during reload' in str(e)
    else:raise AssertionError('network side effect accepted during reload')
    raise RuntimeError('reject network candidate')
'''
        repaired='''import forge,builtins
from network import Host
def on_start():
    global named
    named=Host(bind='127.0.0.1',name='session',lifetime='application')
    assert named.native is builtins.named.native
    assert builtins.rejected.closed and builtins.rejected_scope.closed
def on_update(dt):
    assert builtins.scoped.closed
    forge.log('NETWORK_RELOAD_OK');forge.quit()
'''
        (self.root/'candidate.txt').write_text(candidate)
        (self.root/'repaired.txt').write_text(repaired)
        self.config['development']['watch_interval']=.05
        self.script_scene('''import forge,builtins
from pathlib import Path
from network import Host
changed=False
def on_start():
    builtins.named=Host(bind='127.0.0.1',name='session',lifetime='application')
    builtins.scoped=Host(bind='127.0.0.1')
def on_update(dt):
    global changed
    if not changed:
        changed=True
        Path(__file__).write_text(Path(forge.project_path('candidate.txt')).read_text())
def on_reload_failed(error):
    assert 'reject network candidate' in error
    assert builtins.rejected.closed and builtins.rejected_scope.closed
    assert not builtins.named.closed and not builtins.scoped.closed
    Path(__file__).write_text(Path(forge.project_path('repaired.txt')).read_text())
''')
        self.assertIn('NETWORK_RELOAD_OK',self.run_engine('dev',frames=600))

    def test_two_processes_exchange_real_messages(self):
        self.script_scene("import forge\ndef on_start():forge.log('BACKENDS:'+','.join(forge.network_backends()));forge.quit()\n")
        available=self.run_engine()
        transports=[name for name in ('lan','sockets') if name in available]
        if not transports:self.skipTest('No IP transports compiled')
        (self.root/'scenes/server.py').write_text(r'''import forge,time,os
from pathlib import Path
from network import Host
def on_start():
    global host,deadline
    host=Host(backend=os.environ['FORGE_NETWORK_TEST_BACKEND'],bind='127.0.0.1');deadline=time.monotonic()+10
    Path(forge.project_path('port.txt')).write_text(str(host.stats()['port']))
def on_update(dt):
    assert time.monotonic()<deadline,'server timeout'
    for e in host.poll():
        if e['type']=='message':
            if e['data']==b'cross-process\x00':assert host.send(e['peer'],e['data'])
            elif e['data']==b'done':forge.log('SERVER_PROCESS_OK');forge.quit()
    time.sleep(.001)
''',encoding='utf-8')
        (self.root/'scenes/client.py').write_text(r'''import forge,time,os
from pathlib import Path
from network import Host
def on_start():
    global host,peer,deadline,done
    host=Host(backend=os.environ['FORGE_NETWORK_TEST_BACKEND']);peer=host.connect('127.0.0.1',int(Path(forge.project_path('port.txt')).read_text()))
    deadline=time.monotonic()+10;done=0
def on_update(dt):
    global done
    assert time.monotonic()<deadline,'client timeout'
    for e in host.poll():
        if e['type']=='connected':assert host.send(peer,b'cross-process\x00')
        elif e['type']=='message':
            assert e['data']==b'cross-process\x00';assert host.send(peer,b'done');done=1
    if done:
        done+=1
        if done==10:forge.log('CLIENT_PROCESS_OK');forge.quit()
    time.sleep(.001)
''',encoding='utf-8')
        command=[str(base.ENGINE),'run','--project',str(self.root/'engine.json'),'--headless','--frames','10000','--no-open-log']
        for backend in transports:
            env=dict(os.environ,FORGE_NETWORK_TEST_BACKEND=backend)
            (self.root/'port.txt').unlink(missing_ok=True)
            server=subprocess.Popen([*command,'--scene','server.py'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
            try:
                deadline=time.monotonic()+10
                while not (self.root/'port.txt').exists():
                    if server.poll() is not None:self.fail(server.communicate()[0])
                    self.assertLess(time.monotonic(),deadline);time.sleep(.01)
                client=subprocess.run([*command,'--scene','client.py'],capture_output=True,text=True,timeout=15,env=env)
                self.assertEqual(client.returncode,0,client.stdout+client.stderr)
                output=server.communicate(timeout=15)[0]
                self.assertEqual(server.returncode,0,output)
                self.assertIn('SERVER_PROCESS_OK',output);self.assertIn('CLIENT_PROCESS_OK',client.stdout+client.stderr)
            finally:
                if server.poll() is None:server.kill();server.communicate()

    def test_worker_host_creation_is_rejected_before_runtime_mutation(self):
        self.script_scene('''import forge,threading
from network import Host
errors=[]
def worker():
    for call in (lambda:Host(),lambda:forge.steam_info()):
        try:call()
        except RuntimeError as e:errors.append(str(e))
        else:raise AssertionError('worker changed main-thread services')
def on_start():
    thread=threading.Thread(target=worker);thread.start();thread.join()
    assert len(errors)==2 and all('owning thread' in e for e in errors),errors
    forge.log('NETWORK_THREAD_OK');forge.quit()
''')
        self.assertIn('NETWORK_THREAD_OK',self.run_engine())

    def test_steam_settings_restart_preserves_running_service(self):
        self.config['development']['watch_interval']=.05
        self.script_scene('''import forge,json
from pathlib import Path
changed=False
def on_update(dt):
    global changed
    if not changed:
        changed=True;file=Path(forge.project_path('engine.json'));config=json.loads(file.read_text());config['steam']['app_id']=1234;file.write_text(json.dumps(config))
def on_reload_failed(error):
    assert 'Steam settings changes require restarting' in error,error
    assert forge.steam_info()['app_id']==480 and not forge.steam_info()['enabled']
    forge.log('STEAM_RESTART_OK');forge.quit()
''')
        self.assertIn('STEAM_RESTART_OK',self.run_engine('dev',frames=300))

    def test_steam_configuration_validation_and_unavailable_sdk(self):
        for value in ({'enabled':1},{'app_id':0},{'app_id':True},{'app_id':2**32},{'relay':'yes'}):
            self.config['steam']=value;self.write_config()
            self.assertIn('steam.',self.run_engine('validate',expected=1))
        self.config['steam']={'enabled':False,'app_id':1234};self.script_scene("import forge\ndef on_start():assert forge.steam_info()['app_id']==1234;forge.log('APP_ID_OK');forge.quit()\n")
        self.assertIn('APP_ID_OK',self.run_engine())
        self.config['steam']={'enabled':True,'app_id':480};self.write_config()
        # Optional actual SDK builds are tested separately with a running Steam client.
        result=subprocess.run([str(base.ENGINE),'run','--project',str(self.root/'engine.json'),'--headless','--frames','2','--no-open-log'],capture_output=True,text=True,timeout=15)
        output=result.stdout+result.stderr
        if 'no Steamworks SDK support' in output:
            self.assertEqual(result.returncode,1)
            self.assertIn('no Steamworks SDK support',self.run_engine('validate',expected=1))
            self.assertIn('no Steamworks SDK support',self.run_engine('build',expected=1,extra=('--output',self.root/'unavailable-package')))
            self.assertFalse((self.root/'unavailable-package').exists())
        else:self.skipTest('SDK build requires Steam-client integration checks')


if __name__ == '__main__':unittest.main()
