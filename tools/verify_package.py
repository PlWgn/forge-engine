"""Verify hashes/notices and launch a directory package with poisoned host Python env."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile

def verify(folder):
    folder=Path(folder).resolve()
    data=folder/'Contents/Resources' if folder.suffix.lower()=='.app' else folder
    manifest=json.loads((data/'manifest.json').read_text(encoding='utf-8'))
    for relative,digest in manifest['files'].items():
        file=(folder/relative).resolve()
        if not file.is_relative_to(folder) or not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest()!=digest:
            raise RuntimeError(f'Package manifest mismatch: {relative}')
    for notice in ('LICENSE','NOTICE','THIRD_PARTY.md','ATTRIBUTION.md'):
        if not (data/notice).is_file():raise RuntimeError(f'Missing package notice: {notice}')
    runtime=data/'runtime'
    if not runtime.is_dir():raise RuntimeError('Private Python missing')
    binary=folder/'Contents/MacOS/Game' if folder.suffix.lower()=='.app' else folder/('Game.exe' if os.name=='nt' else 'Game')
    env=dict(os.environ,PYTHONHOME='/nonexistent/host-python',PYTHONPATH='/nonexistent/host-modules')
    with tempfile.TemporaryDirectory(prefix='forge standalone проверка ') as temporary:
        # Keep the distribution clean: logs/preferences belong to a disposable copy.
        sandbox=Path(temporary);copy=sandbox/folder.name;shutil.copytree(folder,copy)
        binary=copy/binary.relative_to(folder);cwd=sandbox/'cwd';cwd.mkdir()
        for scene in ('welcome.json','simulation.json','authoring.py'):
            result=subprocess.run([str(binary),'run','--scene',scene,'--headless','--frames','12','--no-open-log'],cwd=cwd,env=env,capture_output=True,text=True,encoding='utf-8',timeout=90)
            if result.returncode or '[ERROR]' in result.stdout+result.stderr:
                raise RuntimeError(result.stdout+result.stderr)
    print(f'Verified Forge {manifest["engine_version"]}: {len(manifest["files"])} manifest files; three standalone scenes')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder');verify(parser.parse_args().folder)
