"""Standalone directory bundler. Runs INSIDE the engine's embedded interpreter."""
from pathlib import Path
import hashlib, json, os, platform, shutil, subprocess, sys, sysconfig, tempfile
import forge

def _copy_tree(src, dst):
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', '*.pyc', 'site-packages', 'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', '_tkinter*', '_test*', '.DS_Store'))

def _native_libraries(executable, runtime, source_library):
    lib = runtime / 'lib'; lib.mkdir(exist_ok=True)
    if sys.platform == 'darwin':
        version = f'{sys.version_info.major}.{sys.version_info.minor}'
        target = lib / f'libpython{version}.dylib'
        shutil.copy2(source_library, target)
        binaries = [executable, target, *runtime.rglob('*.so'), *runtime.rglob('*.dylib')]
        processed, sources = set(), {target: source_library}
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
    plan = {}
    def folder(raw):
        source, relative = _project_path(root, raw)
        if source == root: raise RuntimeError('Asset folders must be separate from the project root for packaging')
        if not source.is_dir(): raise RuntimeError(f'Missing asset directory: {raw}')
        if output.is_relative_to(source): raise RuntimeError('Output cannot be inside an asset directory (paths or python_paths)')
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
    plan = _copy_plan(root, settings, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.forge-build-', dir=output.parent)).resolve()
    try:
        for relative, source in plan.items():
            _copy_tree(source, _stage_path(stage, relative))
        icon = settings.get('project', {}).get('icon')
        if icon:
            target = _stage_path(stage, icon); target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(root / icon, target)
        # Game code is shipped independently from the engine C++ sources.
        (stage / 'game.json').write_text(json.dumps(settings, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        binary = stage / ('Game.exe' if sys.platform == 'win32' else 'Game')
        shutil.copy2(engine_file, binary)
        runtime = stage / 'runtime'; runtime.mkdir()
        version = f'{sys.version_info.major}.{sys.version_info.minor}'
        stdlib = Path(sysconfig.get_path('stdlib'))
        stdlib_target = runtime / ('Lib' if sys.platform == 'win32' else f'lib/python{version}')
        _copy_tree(stdlib, stdlib_target)
        if sys.platform == 'win32':
            dlls = Path(sys.base_prefix) / 'DLLs'
            if dlls.exists(): _copy_tree(dlls, runtime / 'DLLs')
            source_library = None
        else:
            libdir = Path(sysconfig.get_config_var('LIBDIR'))
            source_library = (libdir / f'libpython{version}.dylib').resolve()
            if not source_library.exists(): raise RuntimeError(f'Python shared library missing: {source_library}')
        _native_libraries(binary, runtime, source_library)
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
        for name, filename in [('glfw','LICENSE.md'), ('glm','copying.txt'), ('pybind11','LICENSE')]:
            f = vendor / name / filename
            if f.exists(): shutil.copy2(f, licenses / (name + '.txt'))
        for filename in ['stb_image.h', 'stb_truetype.h', 'miniaudio.h', 'json.hpp']:
            if (vendor / filename).exists(): shutil.copy2(vendor / filename, licenses / filename) # license text is embedded in header
        (stage / 'START.txt').write_text('Run Game.exe (Windows) or ./Game (macOS).\nThe directory must be writable for forge.log and saves.\nKeep all files in this directory together.\n', encoding='utf-8')
        shutil.copy2(source_root / 'engine/resources/Unicode-LICENSE.txt', licenses / 'Unicode.txt')
        manifest = {'engine_version': forge.__version__, 'platform': platform.system(), 'architecture': platform.machine(), 'python': platform.python_version(), 'files': {}}
        for file in sorted(stage.rglob('*')):
            if file.is_file(): manifest['files'][file.relative_to(stage).as_posix()] = hashlib.sha256(file.read_bytes()).hexdigest()
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    print(f'Standalone game ready: {output}')
