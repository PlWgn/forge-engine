"""Verify hashes/notices and launch a directory package with poisoned host Python env."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile

def verify(folder, graphics_backend=None):
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
        if graphics_backend:
            package_data=copy/'Contents/Resources' if copy.suffix.lower()=='.app' else copy
            config_file=package_data/'game.json';config=json.loads(config_file.read_text(encoding='utf-8'))
            config.setdefault('renderer',{})['backend']=graphics_backend
            if graphics_backend=='direct3d11':
                config['renderer'].setdefault('direct3d11',{})['driver']='warp'
            if graphics_backend in ('direct3d11','metal'):
                for notice in ('glslang.txt','spirv_cross.txt','Shader-NOTICE.txt'):
                    if not (package_data/'licenses'/notice).is_file():raise RuntimeError(f'Missing shader compiler notice: {notice}')
            probe=package_data/config['paths']['scripts']/'_forge_backend_probe.py'
            probe.write_text("import forge\nframes=0\ndef on_start(): forge.on_frame(tick,persistent=True)\ndef tick(dt):\n    global frames\n    frames+=1\n    if frames==3:\n        actual=forge.renderer_stats()['backend']\n        assert actual=="+repr(graphics_backend)+",actual\n        forge.log('VERIFIED_GRAPHICS_BACKEND '+actual)\n",encoding='utf-8')
            config.setdefault('startup_scripts',[]).append(probe.name)
            config_file.write_text(json.dumps(config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        for scene in ('welcome.json','simulation.json','authoring.py'):
            mode=['--silent-audio'] if graphics_backend else ['--headless']
            result=subprocess.run([str(binary),'run','--scene',scene,*mode,'--frames','12','--no-open-log'],cwd=cwd,env=env,capture_output=True,text=True,encoding='utf-8',timeout=90)
            if result.returncode or '[ERROR]' in result.stdout+result.stderr:
                raise RuntimeError(result.stdout+result.stderr)
            if graphics_backend and 'VERIFIED_GRAPHICS_BACKEND '+graphics_backend not in result.stdout:
                raise RuntimeError('Standalone scene did not confirm requested graphics backend: '+scene)
    print(f'Verified Forge {manifest["engine_version"]}: {len(manifest["files"])} manifest files; three standalone scenes'+(' / '+graphics_backend if graphics_backend else ' / headless'))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder')
    parser.add_argument('--graphics-backend',choices=('opengl','direct3d11','metal'),help='Also launch actual graphical scenes (Direct3D uses WARP; audio is silent)')
    args=parser.parse_args();verify(args.folder,args.graphics_backend)
