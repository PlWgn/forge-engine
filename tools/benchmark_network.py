#!/usr/bin/env python3
"""Measure native reliable loopback messages through Python; no timing gate/FPS claim."""
from pathlib import Path
import argparse,json,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--backend',choices=('lan','sockets'),default='lan')
    parser.add_argument('--messages',type=int,default=10000)
    parser.add_argument('--size',type=int,default=256)
    args=parser.parse_args()
    if not 1<=args.messages<=1000000 or not 4<=args.size<=65536:parser.error('messages: 1..1000000; size: 4..65536')
    with tempfile.TemporaryDirectory(prefix='forge-network-benchmark-') as temporary:
        root=Path(temporary);config=json.loads((ROOT/'engine.json').read_text(encoding='utf-8'))
        for path in config['paths'].values():shutil.copytree(ROOT/path,root/path,ignore=shutil.ignore_patterns('__pycache__'))
        config['startup_scripts']=[];config['steam']={'enabled':False};config['entry_scene']='network_benchmark.py';config['logging']['open_on_error']=False
        (root/'engine.json').write_text(json.dumps(config),encoding='utf-8')
        source='''import forge,time,json
from network import Host
def on_start():
    with Host(backend=BACKEND,bind='127.0.0.1') as server,Host(backend=BACKEND) as client:
        peer=client.connect('127.0.0.1',server.stats()['port']);connected=False;remote=None;deadline=time.monotonic()+60
        while not connected or remote is None:
            assert time.monotonic()<deadline,'connect timeout'
            for e in client.poll():
                if e['type']=='connected':connected=True
            for e in server.poll():
                if e['type']=='connected':remote=e['peer']
            time.sleep(.001)
        sent=received=refused=0;start=time.perf_counter();padding=b'x'*(SIZE-4)
        while received<COUNT:
            assert time.monotonic()<deadline,'exchange timeout'
            for _ in range(min(128,COUNT-sent)):
                if client.send(peer,sent.to_bytes(4,'big')+padding):sent+=1
                else:refused+=1;break
            client.poll()
            for e in server.poll():
                if e['type']=='message':
                    assert len(e['data'])==SIZE and int.from_bytes(e['data'][:4],'big')==received
                    received+=1
            time.sleep(.0001)
        seconds=time.perf_counter()-start
        forge.log('NETWORK_BENCHMARK '+json.dumps({'backend':BACKEND,'messages':received,'size':SIZE,'seconds':seconds,'messages_per_second':received/seconds,'MiB_per_second':received*SIZE/seconds/(1024*1024),'backpressure_refusals':refused,'server':server.stats()}))
    forge.quit()
'''.replace('BACKEND',repr(args.backend)).replace('SIZE',repr(args.size)).replace('COUNT',repr(args.messages))
        (root/'scenes/network_benchmark.py').write_text(source,encoding='utf-8')
        result=subprocess.run([str(args.binary.resolve()),'run','--project',str(root/'engine.json'),'--headless','--frames','2','--no-open-log'],capture_output=True,text=True,timeout=75)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        for line in result.stdout.splitlines():
            if 'NETWORK_BENCHMARK ' in line:print(json.dumps(json.loads(line.split('NETWORK_BENCHMARK ',1)[1]),indent=2));break
        else:raise RuntimeError('No benchmark result: '+result.stdout+result.stderr)


if __name__=='__main__':main()
