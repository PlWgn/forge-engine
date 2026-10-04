#!/usr/bin/env python3
"""Local hot-path measurements; no timing gates. Particles require desktop OpenGL."""
import argparse,json,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def measure(binary,count,backend,particles):
    with tempfile.TemporaryDirectory(prefix='forge-hotpaths-') as folder:
        root=Path(folder);config=json.loads((ROOT/'engine.json').read_text(encoding='utf-8'))
        config.update(entry_scene='benchmark.py',startup_scripts=[])
        config['window'].update(width=640,height=480,vsync=False)
        for path in config['paths'].values():shutil.copytree(ROOT/path,root/path,ignore=shutil.ignore_patterns('__pycache__'))
        (root/'engine.json').write_text(json.dumps(config),encoding='utf-8')
        source='''import forge,time,json
from pathlib import Path
COUNT=COUNT_VALUE
BACKEND=BACKEND_VALUE
PARTICLES=PARTICLES_VALUE
frames=0
samples=[]
def build():return {'mode':'3d','physics_enabled':False,'physics':{'backend':BACKEND},'entities':[{'id':'e'+str(i),'kind':'empty','position':[i*3,0,0],'collider':[1,1,1]} for i in range(COUNT)]}
def on_start():
    global result
    items=forge.entities();forge.physics_step(0)
    start=time.perf_counter()
    for i,e in enumerate(items):e.position=(i*3,1,0)
    setters=(time.perf_counter()-start)*1000
    start=time.perf_counter()
    for i in range(100):forge.raycast((0,1,5),(0,0,-1),10)
    queries=(time.perf_counter()-start)*1000
    payload={'values':[{'x':i,'name':'item'+str(i)} for i in range(100)]}
    start=time.perf_counter()
    for _ in range(100):items[0].data=payload;assert items[0].data==payload
    bridge=(time.perf_counter()-start)*1000
    result={'entities':COUNT,'backend':BACKEND,'setter_ms':setters,'queries_100_ms':queries,'json_roundtrips_100_ms':bridge}
    if PARTICLES:forge.particle_emitter({'rate':0,'burst':PARTICLES,'max_particles':PARTICLES,'screen':True,'position':[100,100,0],'texture':'particle.png','size':[1,1],'lifetime':[100,100],'velocity':[0,0,0]})
    else:finish()
def finish():
    if samples:result['render_ms']=sum(samples)/len(samples);result['renderer']=forge.renderer_stats()
    result['world']=forge.world_stats()
    Path(forge.project_path('result.json')).write_text(json.dumps(result));forge.quit()
def on_update(dt):
    global frames
    frames+=1
    if frames>4:samples.append(forge.renderer_stats()['render_ms'])
    if frames==20:finish()
'''.replace('COUNT_VALUE',str(count)).replace('BACKEND_VALUE',repr(backend)).replace('PARTICLES_VALUE',str(particles))
        (root/'scenes/benchmark.py').write_text(source,encoding='utf-8')
        command=[str(binary),'run','--project',str(root/'engine.json'),'--no-open-log']
        if not particles:command.append('--headless')
        result=subprocess.run(command,capture_output=True,text=True,timeout=120)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return json.loads((root/'result.json').read_text(encoding='utf-8'))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
    parser.add_argument('--entities',type=int,default=1000);parser.add_argument('--particles',type=int,default=0)
    parser.add_argument('--backend',choices=['legacy','bullet','both'],default='both');args=parser.parse_args()
    if not 1<=args.entities<=10000 or not 0<=args.particles<=100000:parser.error('entities: 1..10000; particles: 0..100000')
    print(json.dumps([measure(args.binary.resolve(),args.entities,b,args.particles) for b in (['legacy','bullet'] if args.backend=='both' else [args.backend])],indent=2))
if __name__=='__main__':main()
