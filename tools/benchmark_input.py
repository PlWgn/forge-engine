#!/usr/bin/env python3
"""Compare native and legacy Python actions in one runtime; no timing gates."""
import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def measure(binary, actions, frames):
    with tempfile.TemporaryDirectory(prefix='forge-input-benchmark-') as temporary:
        root = Path(temporary)
        config = json.loads((ROOT/'engine.json').read_text(encoding='utf-8'))
        config.update(entry_scene='input-benchmark.py', startup_scripts=[])
        config['logging']['open_on_error'] = False
        for path in config['paths'].values():
            shutil.copytree(ROOT/path, root/path, ignore=shutil.ignore_patterns('__pycache__'))
        (root/'engine.json').write_text(json.dumps(config), encoding='utf-8')
        source = '''import forge,json,time,statistics
from pathlib import Path
from input_actions import ActionMap
COUNT=ACTION_COUNT
FRAMES=FRAME_COUNT
def on_start():
    bindings={f'action_{i}':['key:SPACE','pad:0:a','axis:0:left_x'] for i in range(COUNT)}
    native=forge.InputManager(bindings)
    legacy=ActionMap(bindings,automatic=False)
    forge.inject_input({'keys':['SPACE'],'gamepads':[{'id':0,'buttons':[0],'axes':[.5,0,0,0,-1,-1]}]})
    result={'actions':COUNT,'updates_per_sample':FRAMES,'samples':5}
    for name,manager in (('native',native),('python',legacy)):
        for _ in range(10):manager.update(1/60)
        samples=[]
        for _ in range(5):
            start=time.perf_counter()
            for _ in range(FRAMES):manager.update(1/60)
            samples.append((time.perf_counter()-start)*1000/FRAMES)
        result[name+'_median_update_ms']=statistics.median(samples)
        assert manager.value('action_0')==1
    result['measured_speedup']=result['python_median_update_ms']/result['native_median_update_ms']
    legacy.close()
    Path(forge.project_path('input-result.json')).write_text(json.dumps(result))
    forge.quit()
'''.replace('ACTION_COUNT', str(actions)).replace('FRAME_COUNT', str(frames))
        (root/'scenes/input-benchmark.py').write_text(source, encoding='utf-8')
        run = subprocess.run([str(binary), 'run', '--project', str(root/'engine.json'),
                              '--headless', '--no-open-log'], capture_output=True,
                             text=True, timeout=300)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        return json.loads((root/'input-result.json').read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--actions', type=int, default=256)
    parser.add_argument('--frames', type=int, default=2000)
    args = parser.parse_args()
    if not 1 <= args.actions <= 1024 or not 1 <= args.frames <= 10000:
        parser.error('actions: 1..1024; frames: 1..10000')
    print(json.dumps(measure(args.binary.resolve(), args.actions, args.frames), indent=2))


if __name__ == '__main__':
    main()
