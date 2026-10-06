"""Standalone directory bundler. Runs INSIDE the engine's embedded interpreter."""
from pathlib import Path
import hashlib, json, os, platform, plistlib, shutil, subprocess, sys, sysconfig, tempfile
import forge

def _copy_tree(src, dst, *, stdlib=False):
    patterns = ['__pycache__', '*.pyc', '.DS_Store', '.git']
    if stdlib:
        patterns += ['site-packages', 'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', '_tkinter*', '_test*']
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns(*patterns))

def _validate_copy_source(root, source, output, validated):
    # Traverse aliases as copytree does; reject escape, cycles and output aliases.
    active = set()
    stack = [(source, False)]
    while stack:
        path, leaving = stack.pop()
        if leaving:
            active.remove(path); validated.add(path)
            continue
        try: real = path.resolve(strict=True)
        except (OSError, RuntimeError) as error: raise RuntimeError(f'Invalid source symlink/path: {path}: {error}') from error
        if not real.is_relative_to(root): raise RuntimeError(f'Copy source escapes project root: {path}')
        if not real.is_dir(): continue
        if output.is_relative_to(real): raise RuntimeError('Output cannot be inside an asset directory (including nested aliases)')
        if real in active: raise RuntimeError(f'Copy source symlink cycle: {path}')
        if real in validated: continue
        active.add(real); stack.append((real, True))
        for child in real.iterdir():
            if child.name in ('__pycache__', '.git', '.DS_Store') or child.suffix == '.pyc': continue
            stack.append((child, False))

def _native_libraries(executable, runtime, source_library, engine_source=None):
    lib = runtime / 'lib'; lib.mkdir(exist_ok=True)
    if sys.platform == 'darwin':
        version = f'{sys.version_info.major}.{sys.version_info.minor}'
        target = lib / f'libpython{version}.dylib'
        shutil.copy2(source_library, target)
        binaries = [executable, target, *runtime.rglob('*.so'), *runtime.rglob('*.dylib')]
        processed, sources = set(), {target: source_library, **({executable: engine_source} if engine_source else {})}
        search = [source_library.parent, Path(sysconfig.get_config_var('LIBDIR'))]
        def dependencies(path):
            result = subprocess.run(['otool', '-L', str(path)], check=True, text=True, encoding='utf-8', capture_output=True).stdout
            return list(dict.fromkeys(line.strip().split(' (')[0] for line in result.splitlines() if line.startswith('\t')))
        while binaries:
            binary = binaries.pop(0)
            if binary in processed: continue
            processed.add(binary)
            original = sources.get(binary, binary)
            # Shared objects need their original directory for @loader_path dependencies.
            if binary.suffix == '.so' and binary not in sources:
                relative = binary.relative_to(runtime / 'lib' / f'python{version}')
                original = Path(sysconfig.get_path('stdlib')) / relative
            for dep in dependencies(binary):
                if dep.startswith(('/usr/lib/', '/System/')): continue
                if (Path(dep).name == 'Python' and 'Python.framework/' in dep) or Path(dep).name.startswith('libpython'):
                    replacement = target
                else:
                    candidate = Path(dep)
                    if dep.startswith('@loader_path/'):
                        candidate = original.parent / dep[len('@loader_path/'):]
                    elif dep.startswith('@rpath/'):
                        filename = dep[len('@rpath/'):]
                        candidates = [original.parent / filename, *(folder / filename for folder in search)]
                        candidate = next((p for p in candidates if p.exists()), candidate)
                    if not candidate.exists(): raise RuntimeError(f'Cannot resolve shared library {dep} referenced by {original}')
                    replacement = lib / candidate.name
                    if replacement == binary: continue  # dylib's own install ID
                    if not replacement.exists():
                        shutil.copy2(candidate, replacement); sources[replacement] = candidate; binaries.append(replacement)
                if replacement == binary: continue
                relative = os.path.relpath(replacement, binary.parent)
                subprocess.run(['install_name_tool', '-change', dep, '@loader_path/' + relative, str(binary)], check=True, capture_output=True)
            if binary.suffix == '.dylib':
                subprocess.run(['install_name_tool', '-id', '@rpath/' + binary.name, str(binary)], check=True, capture_output=True)
        # Relocated Mach-O signatures must be replaced, including CPython extension modules.
        for binary in processed:
            subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True, capture_output=True)
    elif sys.platform == 'win32':
        prefix = Path(sys.base_prefix)
        required = prefix / f'python{sys.version_info.major}{sys.version_info.minor}.dll'
        if not required.exists(): raise RuntimeError(f'CPython DLL missing: {required}')
        for dll in prefix.glob('*.dll'): shutil.copy2(dll, executable.parent / dll.name)
        for dll in (prefix / 'DLLs').glob('*.dll'): shutil.copy2(dll, executable.parent / dll.name)
        # VC runtime is redistributed app-local when present beside the build Python.
        for dll in prefix.glob('vcruntime*.dll'): shutil.copy2(dll, executable.parent / dll.name)
    else:
        raise RuntimeError('Standalone builds currently support macOS and Windows')

def _project_path(root, raw):
    path = Path(raw)
    if path.is_absolute(): raise RuntimeError(f'Project paths must be relative: {raw}')
    source = (root / path).resolve()
    if not source.is_relative_to(root): raise RuntimeError(f'Path escapes project root: {raw}')
    return source, source.relative_to(root).as_posix()

def _copy_plan(root, settings, output):
    if output == root or root.is_relative_to(output): raise RuntimeError('Output cannot replace the source project or its parent')
    if output.exists(): raise RuntimeError(f'Output already exists: {output}. Choose a new directory or remove it explicitly.')
    # Canonical project-relative names are used both for copying and game.json.
    plan, validated = {}, set()
    def folder(raw):
        source, relative = _project_path(root, raw)
        if source == root: raise RuntimeError('Asset folders must be separate from the project root for packaging')
        if not source.is_dir(): raise RuntimeError(f'Missing asset directory: {raw}')
        if output.is_relative_to(source): raise RuntimeError('Output cannot be inside an asset directory (paths or python_paths)')
        _validate_copy_source(root, source, output, validated)
        plan[relative] = source
        return relative
    settings['paths'] = {group: folder(raw) for group, raw in settings['paths'].items()}
    settings['python_paths'] = [folder(raw) for raw in settings.get('python_paths', [])]
    icon = settings.get('project', {}).get('icon')
    if icon: settings['project']['icon'] = _project_path(root, icon)[1]
    if 'save_directory' in settings:
        settings['save_directory'] = _project_path(root, settings['save_directory'])[1]
    return plan

def _stage_path(stage, relative):
    target = (stage / relative).resolve()
    if target == stage or not target.is_relative_to(stage):
        raise RuntimeError(f'Copy destination escapes temporary build: {relative}')
    return target

def build_bundle(config_file, engine_file, output):
    config_file, engine_file, output = map(lambda p: Path(p).resolve(), (config_file, engine_file, output))
    root = config_file.parent
    settings = json.loads(config_file.read_text(encoding='utf-8'))
    app = output.suffix.lower() == '.app'
    if app and sys.platform != 'darwin': raise RuntimeError('.app packaging requires macOS')
    if app:
        settings.setdefault('storage', {})['mode'] = 'user'
        settings['storage'].setdefault('application_id', 'org.forge.game')
    plan = _copy_plan(root, settings, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    container = Path(tempfile.mkdtemp(prefix='.forge-build-', dir=output.parent)).resolve()
    stage = container / 'Contents' / 'Resources' if app else container
    stage.mkdir(parents=True, exist_ok=True)
    try:
        for relative, source in plan.items():
            _copy_tree(source, _stage_path(stage, relative))
        icon = settings.get('project', {}).get('icon')
        if icon:
            target = _stage_path(stage, icon); target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(root / icon, target)
        # Game code is shipped independently from the engine C++ sources.
        (stage / 'game.json').write_text(json.dumps(settings, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        binary = container / 'Contents' / 'MacOS' / 'Game' if app else stage / ('Game.exe' if sys.platform == 'win32' else 'Game')
        binary.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(engine_file, binary)
        runtime = stage / 'runtime'; runtime.mkdir()
        version = f'{sys.version_info.major}.{sys.version_info.minor}'
        stdlib = Path(sysconfig.get_path('stdlib'))
        stdlib_target = runtime / ('Lib' if sys.platform == 'win32' else f'lib/python{version}')
        _copy_tree(stdlib, stdlib_target, stdlib=True)
        if sys.platform == 'win32':
            dlls = Path(sys.base_prefix) / 'DLLs'
            if dlls.exists(): _copy_tree(dlls, runtime / 'DLLs', stdlib=True)
            source_library = None
        else:
            libdir = Path(sysconfig.get_config_var('LIBDIR'))
            source_library = (libdir / f'libpython{version}.dylib').resolve()
            if not source_library.exists(): raise RuntimeError(f'Python shared library missing: {source_library}')
        if sys.platform == "win32" and forge.capabilities()["steamworks"]:
            steam_runtime=engine_file.parent / "steam_api64.dll"
            if not steam_runtime.is_file(): raise RuntimeError("SDK runtime missing beside engine: steam_api64.dll")
            shutil.copy2(steam_runtime, binary.parent / steam_runtime.name)
        _native_libraries(binary, runtime, source_library, engine_file)
        license_candidates = [stdlib / 'LICENSE.txt', Path(sys.base_prefix) / 'LICENSE.txt', Path(sys.base_prefix) / 'LICENSE', Path(sys.base_prefix) / 'Resources/English.lproj/Documentation/_sources/license.rst.txt']
        license_file = next((p for p in license_candidates if p.exists()), None)
        if license_file is None: raise RuntimeError('Python distribution license is missing; supply it in the build Python prefix')
        shutil.copy2(license_file, runtime / 'LICENSE.txt')
        source_root = Path(__file__).resolve().parents[1]
        for filename in ('LICENSE', 'NOTICE', 'CORE.md', 'ATTRIBUTION.md'):
            shutil.copy2(source_root / filename, stage / filename)
        if (source_root / 'THIRD_PARTY.md').exists(): shutil.copy2(source_root / 'THIRD_PARTY.md', stage / 'THIRD_PARTY.md')
        licenses = stage / 'licenses'; licenses.mkdir()
        vendor = source_root / 'vendor'
        for name, filename in [('glfw','LICENSE.md'), ('glm','copying.txt'), ('pybind11','LICENSE'), ('assimp','LICENSE'), ('imgui','LICENSE.txt'), ('bullet','LICENSE.txt'), ('glslang','LICENSE.txt'), ('spirv_cross','LICENSE')]:
            f = vendor / name / filename
            if f.exists(): shutil.copy2(f, licenses / (name + '.txt'))
        for filename in ['stb_image.h', 'stb_truetype.h', 'miniaudio.h', 'json.hpp']:
            if (vendor / filename).exists(): shutil.copy2(vendor / filename, licenses / filename) # license text is embedded in header
        if (vendor / 'glslang').exists() or (vendor / 'spirv_cross').exists():
            shutil.copy2(source_root / 'engine/resources/Shader-NOTICE.txt', licenses / 'Shader-NOTICE.txt')
        # Preserve the license texts for Assimp's compiled internal dependencies too.
        for license_file in (vendor / 'assimp' / 'contrib').rglob('*'):
            if license_file.is_file() and ('license' in license_file.name.lower() or 'copying' in license_file.name.lower()):
                target = licenses / 'assimp-contrib' / license_file.relative_to(vendor / 'assimp' / 'contrib')
                target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(license_file, target)
        (stage / 'START.txt').write_text('Open the .app, run Game.exe (Windows), or ./Game (macOS directory).\nStorage is controlled by game.json storage.mode; .app uses Application Support and Library/Logs.\nKeep all files together.\n', encoding='utf-8')
        network_licenses=source_root/'engine/resources/network-licenses'
        if network_licenses.is_dir(): shutil.copytree(network_licenses,licenses/'network')
        shutil.copy2(source_root / 'engine/resources/Unicode-LICENSE.txt', licenses / 'Unicode.txt')
        if app:
            project = settings['project']
            info = dict(CFBundleExecutable='Game', CFBundleIdentifier=settings['storage']['application_id'],
                        CFBundleName=project['name'], CFBundleDisplayName=project['name'], CFBundlePackageType='APPL',
                        CFBundleShortVersionString=project.get('version','1.0.0'), CFBundleVersion=project.get('build_number','1'),
                        NSHighResolutionCapable=True, LSMinimumSystemVersion='11.0')
            if icon:
                if Path(icon).suffix.lower() == '.icns': shutil.copy2(root / icon, stage / 'Game.icns')
                else:
                    iconset = container / 'Game.iconset'; iconset.mkdir()
                    for size in (16,32,128,256,512):
                        for density in (1,2):
                            name = f'icon_{size}x{size}' + ('@2x' if density == 2 else '') + '.png'
                            subprocess.run(['sips','-s','format','png','-z',str(size*density),str(size*density),str(root / icon),'--out',str(iconset / name)], check=True, capture_output=True)
                    subprocess.run(['iconutil','-c','icns',str(iconset),'-o',str(stage / 'Game.icns')], check=True, capture_output=True)
                    shutil.rmtree(iconset)
                info['CFBundleIconFile'] = 'Game.icns'
            (container / 'Contents' / 'Info.plist').write_bytes(plistlib.dumps(info))
            (container / 'Contents' / 'PkgInfo').write_bytes(b'APPL????')
        manifest = {'engine_version': forge.__version__, 'network_backends': forge.network_backends(), 'steamworks': forge.capabilities()['steamworks'], 'platform': platform.system(), 'architecture': platform.machine(), 'python': platform.python_version(), 'files': {}}
        if app: manifest['signature_managed_files'] = ['Contents/MacOS/Game', 'Contents/_CodeSignature/CodeResources']
        for file in sorted(container.rglob('*')):
            if file.is_file() and not (app and file == binary): manifest['files'][file.relative_to(container).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        # Sign the outer bundle after its manifest. Existing nested Mach-O signatures remain intact.
        if app: subprocess.run(['codesign','--force','--sign','-',str(container)], check=True, capture_output=True)
        container.rename(output)
    except BaseException:
        shutil.rmtree(container, ignore_errors=True)
        raise
    print(f'Standalone game ready: {output}')
