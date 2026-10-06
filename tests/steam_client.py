"""Explicit live-client check. Needs official SDK build + running Steam; never runs in CI."""
from pathlib import Path
import argparse,json,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--app-id',type=int,default=480)
    parser.add_argument('--lobbies',action='store_true',help='Also create/join/leave a private test lobby')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='forge-steam-client-') as temporary:
        root=Path(temporary);config=json.loads((ROOT/'engine.json').read_text(encoding='utf-8'))
        for group,path in config['paths'].items():shutil.copytree(ROOT/path,root/path,ignore=shutil.ignore_patterns('__pycache__'))
        config['startup_scripts']=[];config['steam']={'enabled':True,'app_id':args.app_id,'relay':False}
        config['entry_scene']='steam_client.py';config['logging']['open_on_error']=False
        (root/'engine.json').write_text(json.dumps(config),encoding='utf-8')
        source='''import forge,steam,time
from network import Host
ready=False;request=None;lobby=None
def on_start():
    global deadline
    info=steam.info();assert info['compiled'] and info['enabled'] and info['app_id']==APP_ID,info
    assert info['steam_id'].isdigit() and isinstance(steam.friends(),list)
    deadline=time.monotonic()+30
    forge.log('STEAM_CLIENT_INITIALIZED '+info['steam_id'])
def on_update(dt):
    global ready,request,lobby
    assert time.monotonic()<deadline,'Steam callback timeout'
    for e in steam.poll():
        if e['type']=='stats_ready':assert e['ok'],e;ready=True
        elif e['type']=='lobby_create':
            assert e['ok'],e;lobby=e['lobby'];assert steam.lobby_info(lobby)['owner']==steam.info()['steam_id']
            assert steam.set_lobby_data(lobby,'forge_test','binary');assert steam.lobby_data(lobby,'forge_test')=='binary'
            request=steam.join_lobby(lobby)
        elif e['type']=='lobby_join':
            assert e['ok'],e;steam.leave_lobby(lobby);forge.log('STEAM_LOBBY_OK');forge.quit()
    if ready and request is None:
        if LOBBIES:request=steam.create_lobby(2,0)
        else:forge.log('STEAM_CLIENT_OK');forge.quit()
    time.sleep(.01)
'''.replace('APP_ID',repr(args.app_id)).replace('LOBBIES',repr(args.lobbies))
        (root/'scenes/steam_client.py').write_text(source,encoding='utf-8')
        subprocess.run([str(args.binary.resolve()),'run','--project',str(root/'engine.json'),'--headless','--frames','4000','--no-open-log'],check=True,timeout=45)


if __name__=='__main__':main()
