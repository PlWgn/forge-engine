"""Forge project API v1. Standard library only; independent of any editor GUI.

A Client speaks JSON-lines to `forge project --serve`. Extensions are ordinary
trusted Python callables and use this same API, regardless of the host shell.
"""
from copy import deepcopy
from pathlib import Path
import importlib.util
import json
import subprocess
import threading

API_VERSION = 1

class ProjectError(RuntimeError):
    def __init__(self, error):
        super().__init__(error['message'])
        self.code = error['code']
        self.paths = error.get('paths', [])

class Client:
    def __init__(self, binary, project):
        self.binary = Path(binary).resolve()
        self.project = Path(project).resolve()
        self._lock = threading.RLock()
        self._sequence = 0
        self._process = subprocess.Popen(
            [str(self.binary), 'project', '--project', str(self.project), '--serve', '--no-open-log'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8', bufsize=1)
        self.commands = {}
        self._plugins = []
        try:
            if self.request('capabilities')['api_version'] != API_VERSION:
                raise RuntimeError('Unsupported project API version')
        except BaseException:
            self.close()
            raise

    def request(self, op, **arguments):
        with self._lock:
            if self._process.poll() is not None:
                raise RuntimeError('Project service is not running')
            self._sequence += 1
            request = dict(arguments, op=op, api_version=API_VERSION, id=self._sequence)
            self._process.stdin.write(json.dumps(request, ensure_ascii=False, allow_nan=False) + '\n')
            self._process.stdin.flush()
            line = self._process.stdout.readline()
            if not line:
                raise RuntimeError('Project service closed its output')
            response = json.loads(line)
            if response.get('id') != self._sequence or response.get('api_version') != API_VERSION:
                raise RuntimeError('Invalid project response')
            if not response['ok']:
                raise ProjectError(response['error'])
            return response['result']

    def document(self, file, group=''):
        return Document(self, file, group)

    def register(self, name, command):
        if not isinstance(name, str) or '.' not in name or name.startswith('forge.') or name in self.commands or not callable(command):
            raise ValueError('Extension command requires a unique namespace.name')
        self.commands[name] = command

    def command(self, name, **arguments):
        return self.commands[name](self, **arguments)

    def extension(self, path):
        """Explicitly load trusted code; plugins are not a security sandbox."""
        path = Path(path).resolve()
        spec = importlib.util.spec_from_file_location(f'_forge_extension_{len(self._plugins)}', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        if getattr(module, 'API_VERSION', None) != API_VERSION:
            raise ValueError('Extension must declare API_VERSION = 1')
        before = dict(self.commands)
        try:
            module.register(self)
        except BaseException:
            self.commands = before
            raise
        self._plugins.append(module)
        return module

    def launch(self, command='run', *, scene=None, output=None, headless=False, frames=None, shell=None, silent_audio=False):
        """Return Popen; the shell owns cancellation, progress and exit reporting."""
        if command not in ('validate', 'run', 'dev', 'edit', 'build'):
            raise ValueError('Unsupported engine command')
        args = [str(self.binary), command, '--project', str(self.project), '--no-open-log']
        if scene is not None: args += ['--scene', str(scene)]
        if output is not None: args += ['--output', str(Path(output).resolve())]
        if headless: args += ['--headless']
        if silent_audio: args += ['--silent-audio']
        if frames is not None: args += ['--frames', str(frames)]
        if shell is not None: args += ['--shell', str(shell)]
        return subprocess.Popen(args)

    def close(self):
        process = self._process
        if process.stdin and not process.stdin.closed:
            process.stdin.close()
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        if process.stdout: process.stdout.close()

    def __enter__(self): return self
    def __exit__(self, *_): self.close()

class Document:
    def __init__(self, client, file, group=''):
        self.client, self.file, self.group = client, str(file), group
        self.undo_stack, self.redo_stack = [], []
        self.reload()

    def reload(self):
        snapshot = self.client.request('read', file=self.file, group=self.group)
        self._base = deepcopy(snapshot['data'])
        self._data = deepcopy(self._base)
        self.revision = snapshot['revision']
        self.undo_stack.clear(); self.redo_stack.clear()
        return self

    def replace(self, data):
        data = deepcopy(data)
        self.client.request('check', file=self.file, group=self.group, data=data)
        if data != self._data:
            self.undo_stack.append(deepcopy(self._data))
            self.undo_stack[:] = self.undo_stack[-100:]
            self.redo_stack.clear()
            self._data = data
        return self

    def patch(self, operations):
        # Use the engine's RFC 6902 implementation without touching the disk.
        # The server check and dry patch are separate from commit.
        data = self.client.request('apply_patch', data=self._data, patch=operations)
        return self.replace(data)

    def undo(self):
        if not self.undo_stack: return False
        candidate = self.undo_stack[-1]
        if candidate is not None: self.client.request('check', file=self.file, group=self.group, data=candidate)
        self.redo_stack.append(deepcopy(self._data)); self._data = self.undo_stack.pop()
        return True

    def redo(self):
        if not self.redo_stack: return False
        candidate = self.redo_stack[-1]
        if candidate is not None: self.client.request('check', file=self.file, group=self.group, data=candidate)
        self.undo_stack.append(deepcopy(self._data)); self._data = self.redo_stack.pop()
        return True

    def save(self):
        snapshot = self.client.request('commit', file=self.file, group=self.group, base=self._base, data=self._data)
        # External changes are now visible to the shell and become its new baseline.
        self._base = deepcopy(snapshot['data']); self._data = deepcopy(self._base)
        self.revision = snapshot['revision']
        self.undo_stack.clear(); self.redo_stack.clear()
        return self

    @property
    def data(self): return deepcopy(self._data)

    @property
    def base(self): return deepcopy(self._base)

    @property
    def dirty(self): return self._base != self._data

class RuntimeClient(Client):
    """Adapter for the optional builtin/viewport shell, with identical extensions.

    Only constructed inside Forge. Does not start another engine or Python runtime.
    """
    def __init__(self, binary, project):
        import forge
        self._forge = forge
        self.binary, self.project = Path(binary), Path(project)
        self.commands, self._plugins = {}, []

    def request(self, op, **arguments):
        response=self._forge.project_response(dict(arguments, op=op, api_version=API_VERSION))
        if not response['ok']: raise ProjectError(response['error'])
        return response['result']

    def editor(self, op, **arguments):
        return self._forge.editor_command(dict(arguments, op=op))

    def close(self): pass
