"""Build an Android arm64 OpenXR game with a private target CPython runtime.

Uses an installed Gradle 8.11.1/JDK 17/Android SDK (CMake 3.31.6, NDK r28c).
No signing keys or platform downloads are embedded in the source distribution.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from xml.sax.saxutils import escape
from packager import _project_path, _copy_plan, _copy_tree, _stage_path, copy_xr_licenses

ROOT = Path(__file__).resolve().parents[1]


def android_library(path):
    """Check ELF class/architecture; callers must supply Android ABI libraries."""
    header = path.read_bytes()[:20]
    if len(header) < 20 or header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 183:
        raise RuntimeError(f'Expected an Android arm64 ELF shared library: {path}')


def _prefix_files(prefix):
    # Prefixes are external inputs too. Reject cycles/escapes instead of following them into an APK.
    for item in prefix.rglob('*'):
        if item.is_symlink():
            target = item.resolve(strict=True)
            if not target.is_relative_to(prefix) or target.is_dir():
                raise RuntimeError(f'Python prefix symlink escapes or aliases a directory: {item}')
        elif not item.is_file() and not item.is_dir():
            raise RuntimeError(f'Unsupported Python prefix file: {item}')


def prepare(config_file, output, prefix, version='3.14', application_id='org.forge.game', version_code=1):
    config_file, output, prefix = (Path(p).resolve() for p in (config_file, output, prefix))
    if not re.fullmatch(r'3\.(?:1[3-9]|[2-9][0-9])', version): raise RuntimeError('Quest requires target Python 3.13+ (major.minor)')
    if not re.fullmatch(r'[a-zA-Z][a-zA-Z0-9_]*(?:\.[a-zA-Z][a-zA-Z0-9_]*)+', application_id): raise RuntimeError('Invalid Android application ID')
    if type(version_code) is not int or not 1 <= version_code <= 2100000000: raise RuntimeError('Invalid Android version code')
    if not (prefix / f'include/python{version}/Python.h').is_file(): raise RuntimeError('Target Python headers missing')
    library = prefix / f'lib/libpython{version}.so'
    android_library(library)
    _prefix_files(prefix)
    stdlib = prefix / f'lib/python{version}'
    if not (stdlib / 'encodings/__init__.py').is_file(): raise RuntimeError('Target Python standard library missing')
    python_license = next((p for p in (prefix/'LICENSE.txt', prefix/'LICENSE', stdlib/'LICENSE.txt') if p.is_file()), None)
    if python_license is None: raise RuntimeError('Target Python license is missing')
    settings = json.loads(config_file.read_text(encoding='utf-8'))
    settings.setdefault('xr', {})['enabled'] = True
    settings.setdefault('renderer', {})['backend'] = 'opengl'
    settings.setdefault('storage', {}).update(mode='user', application_id=application_id)
    settings['logging'] = dict(settings.get('logging', {}), open_on_error=False)
    # Keep project paths independent of engine paths; reuse desktop confinement/alias validation.
    plan = _copy_plan(config_file.parent, settings, output)
    native_modules = []
    for raw in settings.get('native_modules', []):
        source, _ = _project_path(config_file.parent, raw)
        if not source.is_file() or ';' in str(source): raise RuntimeError('Invalid native module source path')
        native_modules.append(str(source))
    for relative in [*plan, *settings.get('python_paths', [])]:
        if Path(relative).parts[0] in ('runtime', 'licenses'):
            raise RuntimeError('Quest content paths cannot overlap reserved runtime/licenses directories')
    if prefix == output or prefix.is_relative_to(output) or output.is_relative_to(prefix): raise RuntimeError('Output overlaps Python prefix')
    output.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='.forge-build-quest-', dir=output.parent))
    try:
        _copy_tree(ROOT/'android', work)
        stage = work/'content'; stage.mkdir()
        for relative, source in plan.items(): _copy_tree(source, _stage_path(stage, relative))
        icon = settings.get('project', {}).get('icon')
        if icon:
            target = _stage_path(stage, icon); target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(config_file.parent/icon, target)
        (stage/'game.json').write_text(json.dumps(settings, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        runtime = stage/'runtime'; (runtime/'lib').mkdir(parents=True)
        _copy_tree(stdlib, runtime/f'lib/python{version}', stdlib=True)
        shutil.copy2(python_license, runtime/'LICENSE.txt')
        for notices in ('licenses', 'share/licenses'):
            if (prefix/notices).is_dir():
                _copy_tree(prefix/notices, runtime/'licenses'/Path(notices).name)
        # Android extension modules stay in the private runtime; their shared dependencies are app-native libs.
        jni = work/'app/src/main/jniLibs/arm64-v8a'; jni.mkdir(parents=True)
        for lib in sorted((prefix/'lib').glob('*.so')):
            android_library(lib)
            if lib.name != library.name: shutil.copy2(lib, jni/lib.name)
        for lib in stage.rglob('*.so'): android_library(lib)
        licenses = stage/'licenses'; licenses.mkdir()
        for name in ('LICENSE','NOTICE','CORE.md','ATTRIBUTION.md','THIRD_PARTY.md'): shutil.copy2(ROOT/name, stage/name)
        for name, license_name in (('glfw','LICENSE.md'),('glm','copying.txt'),('pybind11','LICENSE'),('assimp','LICENSE'),('bullet','LICENSE.txt')):
            shutil.copy2(ROOT/'vendor'/name/license_name, licenses/(name+'.txt'))
        for name in ('stb_image.h','stb_truetype.h','miniaudio.h','json.hpp'): shutil.copy2(ROOT/'vendor'/name, licenses/name)
        for item in (ROOT/'vendor/assimp/contrib').rglob('*'):
            if item.is_file() and ('license' in item.name.lower() or 'copying' in item.name.lower()):
                target=licenses/'assimp-contrib'/item.relative_to(ROOT/'vendor/assimp/contrib'); target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(item,target)
        shutil.copy2(ROOT/'engine/resources/Unicode-LICENSE.txt', licenses/'Unicode.txt')
        shutil.copy2(ROOT/'engine/resources/Android-NOTICE.txt', licenses/'Android-NOTICE.txt')
        copy_xr_licenses(ROOT, licenses)
        shutil.copytree(ROOT/'engine/resources/python-android-licenses', licenses/'python-android')
        manifest = {'engine_version': re.search(r'project\(Forge VERSION ([\d.]+)', (ROOT/'CMakeLists.txt').read_text())[1],
                    'platform':'Android', 'architecture':'arm64-v8a', 'python':version, 'application_id':application_id,
                    'files': {p.relative_to(stage).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(stage.rglob('*')) if p.is_file()}}
        (stage/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        assets=work/'app/src/main/assets';assets.mkdir()
        with zipfile.ZipFile(assets/'forge-content.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for item in sorted(stage.rglob('*')):
                if item.is_file(): archive.write(item,item.relative_to(stage).as_posix())
        shutil.rmtree(stage)
        strings=work/'app/src/main/res/values'; strings.mkdir(parents=True)
        # Escape Android string-resource syntax as well as XML syntax.
        label=escape(settings['project']['name']).replace('\\','\\\\').replace("'", "\\'").replace('"','\\"').replace('@','\\@').replace('?','\\?')
        (strings/'strings.xml').write_text(f'<resources><string name="app_name" formatted="false">{label}</string></resources>\n',encoding='utf-8')
        (work/'forge-build.json').write_text(json.dumps({'source':str(ROOT),'python_prefix':str(prefix),'python_version':version,
            'application_id':application_id,'version_code':version_code,'native_modules':native_modules},indent=2)+'\n',encoding='utf-8')
        work.rename(output)
    except BaseException:
        shutil.rmtree(work, ignore_errors=True); raise
    return output




def _elf_export(data, symbol):
    """Read ELF64 little-endian dynamic exports without executing a target binary."""
    if len(data)<64:return False
    offset=struct.unpack_from('<Q',data,40)[0]
    stride,count=struct.unpack_from('<HH',data,58)
    if stride<64 or offset+stride*count>len(data):return False
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*stride) for i in range(count)]
    for section in sections:
        if section[1]!=11 or section[6]>=count or section[9]<24:continue # SHT_DYNSYM
        strings=sections[section[6]]
        begin,size=section[4],section[5]
        if begin+size>len(data) or strings[4]+strings[5]>len(data):return False
        names=data[strings[4]:strings[4]+strings[5]]
        for pos in range(begin,begin+size-section[9]+1,section[9]):
            name,info,other,index=struct.unpack_from('<IBBH',data,pos)
            if index and info>>4 in (1,2) and other&3 in (0,3) and name<len(names):
                if names[name:].split(b'\0',1)[0]==symbol.encode():return True
    return False


def verify_apk(apk):
    """Check the NativeActivity library, target ABI, private Python and content hashes.

    This inspects a build artifact; it does not establish headset/runtime behavior.
    """
    with zipfile.ZipFile(apk) as archive:
        names=archive.namelist()
        if len(names)!=len(set(names)):raise RuntimeError('APK contains duplicate paths')
        libraries=[name for name in names if name.startswith('lib/') and name.endswith('.so')]
        if not libraries or any(not name.startswith('lib/arm64-v8a/') for name in libraries):
            raise RuntimeError('Quest APK must contain only arm64 native libraries')
        with zipfile.ZipFile(io.BytesIO(archive.read('assets/forge-content.zip'))) as content:
            manifest=json.loads(content.read('manifest.json'))
            required=('libforge.so','libopenxr_loader.so','libc++_shared.so',f"libpython{manifest['python']}.so")
            for name in required:
                path='lib/arm64-v8a/'+name
                if path not in names:raise RuntimeError('Quest APK is missing required native library: '+name)
                header=archive.read(path)[:20]
                if len(header)<20 or header[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',header,18)[0]!=183:
                    raise RuntimeError('Quest APK library is not arm64 ELF: '+name)
            if not _elf_export(archive.read('lib/arm64-v8a/libforge.so'),'ANativeActivity_onCreate'):
                raise RuntimeError('Quest APK engine does not export ANativeActivity_onCreate')
            for name,digest in manifest['files'].items():
                if hashlib.sha256(content.read(name)).hexdigest()!=digest:
                    raise RuntimeError('Quest content hash mismatch: '+name)
            settings=json.loads(content.read('game.json'))
            if not settings.get('xr',{}).get('enabled') or settings.get('storage',{}).get('mode')!='user':
                raise RuntimeError('Quest APK must enable XR and use private user storage')
            for name in ('licenses/Android-NOTICE.txt','licenses/openxr/LICENSE','runtime/LICENSE.txt'):
                if name not in content.namelist():raise RuntimeError('Quest APK notice missing: '+name)
    return len(manifest['files'])


def assemble(output, gradle='gradle', ndk_version='28.2.13676358'):
    output=Path(output).resolve()
    data=json.loads((output/'forge-build.json').read_text(encoding='utf-8'))
    gradle = str(Path(gradle).resolve()) if "/" in str(gradle) or "\\" in str(gradle) else str(gradle)
    subprocess.run([gradle,'--no-daemon','assembleDebug',
        '-PforgeSource='+data['source'],'-PforgePython='+data['python_prefix'],
        '-PforgePythonVersion='+data['python_version'],'-PforgeApplicationId='+data['application_id'],
        '-PforgeVersionCode='+str(data['version_code']),'-PforgeNdkVersion='+ndk_version,
        '-PforgeModuleSources='+';'.join(data.get('native_modules',[]))],cwd=output,check=True)
    apk=output/'app/build/outputs/apk/debug/app-debug.apk'
    if not apk.is_file(): raise RuntimeError('Gradle did not produce the Quest APK')
    count=verify_apk(apk)
    shutil.copy2(apk, output/'Game.apk')
    print(f'Verified Quest arm64 libraries and {count} content hashes')
    print(f'Quest development APK: {output / "Game.apk"}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--python-prefix',type=Path,required=True)
    parser.add_argument('--python-version',default='3.14')
    parser.add_argument('--application-id',default='org.forge.game')
    parser.add_argument('--version-code',type=int,default=1)
    parser.add_argument('--engine',type=Path,required=True,help='Desktop Forge executable for complete project validation')
    parser.add_argument('--gradle',default='gradle')
    parser.add_argument('--ndk-version',default='28.2.13676358')
    parser.add_argument('--prepare-only',action='store_true',help='Stage Gradle project without compiling an APK')
    args=parser.parse_args()
    subprocess.run([str(args.engine.resolve()),'validate','--project',str(args.project.resolve()),'--no-open-log'],check=True)
    if not (ROOT/'vendor/openxr/CMakeLists.txt').is_file(): raise RuntimeError('Run python tools/dependencies.py --openxr first')
    output=prepare(args.project,args.output,args.python_prefix,args.python_version,args.application_id,args.version_code)
    if args.prepare_only: print(f'Prepared Quest Gradle project: {output}')
    else: assemble(output,args.gradle,args.ndk_version)


if __name__ == '__main__': main()
