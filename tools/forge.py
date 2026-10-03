#!/usr/bin/env python3
"""Cross-platform developer entry point; native forge owns validation and runtime."""
from pathlib import Path
import argparse, datetime, json, os, shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]

def log(message, level='INFO', root=ROOT):
    line = f'[{datetime.datetime.now():%Y-%m-%d %H:%M:%S}] [{level}] {message}'
    print(line, file=sys.stderr if level == 'ERROR' else sys.stdout, flush=True)
    with (root / 'forge.log').open('a', encoding='utf-8') as f: f.write(line + '\n')

def cmake_path():
    executable = shutil.which('cmake')
    local = ROOT / '.tools' / ('Scripts/cmake.exe' if os.name == 'nt' else 'bin/cmake')
    if executable: return executable
    if local.exists(): return str(local)
    raise RuntimeError('CMake >= 3.24 required. Install it or run: python -m pip install cmake==3.31.6')

def execute(args, record_output=True):
    log(' '.join(map(str, args)))
    process = subprocess.Popen(list(map(str, args)), cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8', errors='replace')
    for line in process.stdout:
        print(line, end='', flush=True)
        if record_output:
            with (ROOT / 'forge.log').open('a', encoding='utf-8') as f: f.write(line)
    code = process.wait()
    if code: raise RuntimeError(f'Command exited with code {code}: {args[0]}')

def binary():
    base = ROOT / 'build' / 'bin'
    for path in [base / 'forge', base / 'forge.exe', base / 'Release' / 'forge.exe', base / 'Debug' / 'forge.exe']:
        if path.exists(): return path
    raise RuntimeError('Engine is not compiled. Run: python tools/forge.py compile')

def configure(settings):
    if not (ROOT / 'vendor/glfw/CMakeLists.txt').exists(): execute([sys.executable, ROOT / 'tools/dependencies.py'])
    args = [cmake_path(), '-S', ROOT, '-B', ROOT / 'build', '-DCMAKE_BUILD_TYPE=Release', f'-DPython_EXECUTABLE={sys.executable}']
    data = json.loads(settings.read_text(encoding='utf-8')) if settings.exists() else {}
    native = []
    for path in data.get('native_modules', []):
        source = (settings.parent / path).resolve()
        if not source.is_relative_to(settings.parent): raise RuntimeError('native_modules paths must stay inside project')
        if not source.is_file(): raise RuntimeError(f'Native module source missing: {source}')
        native.append(str(source))
    args.append('-DFORGE_MODULE_SOURCES=' + ';'.join(native))
    execute(args)

def scaffold(destination):
    destination = destination.resolve()
    if destination.exists() and any(destination.iterdir()): raise RuntimeError('init target must be empty')
    destination.mkdir(parents=True, exist_ok=True)
    for directory in ('graphics', 'modules', 'scenes', 'scripts', 'textures', 'materials', 'models', 'objects', 'audio', 'locales'):
        shutil.copytree(ROOT / directory, destination / directory, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    shutil.copy2(ROOT / 'engine.json', destination / 'engine.json')
    shutil.copy2(ROOT / 'Инструкция.md', destination / 'Инструкция.md')
    for filename in ('LICENSE', 'NOTICE', 'CORE.md', 'ATTRIBUTION.md', 'THIRD_PARTY.md'):
        shutil.copy2(ROOT / filename, destination / filename)
    (destination / 'licenses').mkdir()
    shutil.copy2(ROOT / 'engine/resources/Unicode-LICENSE.txt', destination / 'licenses/Unicode.txt')
    log(f'Project created: {destination}')

def main():
    parser = argparse.ArgumentParser(description='Forge engine development tools')
    parser.add_argument('command', choices=['configure', 'compile', 'validate', 'dev', 'run', 'build', 'init', 'test'])
    parser.add_argument('--project', type=Path, default=ROOT / 'engine.json')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--frames', type=int)
    parser.add_argument('--no-open-log', action='store_true')
    args = parser.parse_args(); settings = args.project.resolve()
    if args.command == 'init':
        if not args.output: parser.error('init requires --output')
        scaffold(args.output); return
    if args.command in ('configure', 'compile'):
        configure(settings)
        if args.command == 'compile': execute([cmake_path(), '--build', ROOT / 'build', '--config', 'Release', '--parallel', str(min(os.cpu_count() or 2, 4))])
        return
    if args.command == 'test': execute([sys.executable, ROOT / 'tests/integration.py', binary()]); return
    command = [binary(), args.command, '--project', settings]
    if args.output: command += ['--output', args.output.resolve()]
    if args.headless: command += ['--headless']
    if args.frames is not None: command += ['--frames', str(args.frames)]
    if args.no_open_log: command += ['--no-open-log']
    execute(command, record_output=False)

if __name__ == '__main__':
    try: main()
    except (Exception, KeyboardInterrupt) as exc:
        log(str(exc) or 'Interrupted', 'ERROR'); sys.exit(1)
