#!/usr/bin/env python3
"""Measure indexed lookup and native physics on a reproducible separated-box scene.

No timing gate: results depend on hardware and include Python bridge overhead.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def measure(binary, count, backend):
    with tempfile.TemporaryDirectory(prefix='forge-benchmark-') as folder:
        root = Path(folder)
        config = json.loads((ROOT / 'engine.json').read_text(encoding='utf-8'))
        config['startup_scripts'] = []
        config['entry_scene'] = 'benchmark.py'
        config['logging']['open_on_error'] = False
        for path in config['paths'].values():
            shutil.copytree(ROOT / path, root / path, ignore=shutil.ignore_patterns('__pycache__'))
        (root / 'engine.json').write_text(json.dumps(config), encoding='utf-8')
        source = '''import forge, json, time
from pathlib import Path
COUNT=COUNT_VALUE
BACKEND=BACKEND_VALUE
def build():
    return {'mode':'3d', 'gravity':[0,0,0], 'physics_enabled':False,
            'physics':{'backend':BACKEND},
            'entities':[{'id':'box'+str(i), 'kind':'empty', 'position':[i*3,0,0],
                         'collider':[1,1,1]} for i in range(COUNT)]}
def on_start():
    forge.physics_step(1/120)  # warm bodies/caches before measuring
    start=time.perf_counter()
    for _ in range(10):
        for i in range(COUNT):
            assert forge.find('box'+str(i)) is not None
    lookup=(time.perf_counter()-start)*1000
    start=time.perf_counter()
    for _ in range(10):forge.physics_step(1/120)
    step=(time.perf_counter()-start)*100
    result={'backend':BACKEND,'entities':COUNT,'lookups':COUNT*10,
            'lookup_ms':lookup,'step_ms':step,'world':forge.world_stats()}
    Path(forge.project_path('result.json')).write_text(json.dumps(result))
    forge.quit()
'''.replace('COUNT_VALUE', str(count)).replace('BACKEND_VALUE', repr(backend))
        (root / 'scenes/benchmark.py').write_text(source, encoding='utf-8')
        run = subprocess.run([str(binary), 'run', '--project', str(root / 'engine.json'),
                              '--headless', '--no-open-log'], capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        return json.loads((root / 'result.json').read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--entities', type=int, nargs='+', default=[1000, 5000, 10000])
    parser.add_argument('--backend', choices=['legacy', 'bullet', 'both'], default='both')
    args = parser.parse_args()
    if any(n < 1 or n > 10000 for n in args.entities):
        parser.error('entities must be 1..10000 (the default Bullet body budget)')
    backends = ['legacy', 'bullet'] if args.backend == 'both' else [args.backend]
    results = [measure(args.binary.resolve(), n, backend) for backend in backends for n in args.entities]
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
