"""Versioned JSON slots, integrity checks, backup recovery and autosave.
Checksums detect accidental damage; they are not encryption or anti-cheat.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import math
import os
import re
import tempfile
import forge


class SaveError(RuntimeError):
    pass


class SaveCorrupt(SaveError):
    pass


class SaveVersion(SaveError):
    pass


_MISSING = object()


def _encoded(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('utf-8')


def _atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=path.name+'.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
        if os.name != 'nt':
            directory = os.open(path.parent, os.O_RDONLY)
            try: os.fsync(directory)
            finally: os.close(directory)
    finally:
        if os.path.exists(temp): os.unlink(temp)


class SaveManager:
    def __init__(self, version=1, *, migrations=None, validate=None, directory=None, max_bytes=16*1024*1024):
        if type(version) is not int or version < 1 or max_bytes < 1:
            raise ValueError('Save version and max_bytes must be positive integers')
        directory = directory or forge.settings().get('save_directory', 'saves')+'/slots'
        self.root = Path(forge.project_path(directory))
        self.version, self.migrations, self.validate = version, dict(migrations or {}), validate
        self.max_bytes, self._autosave = max_bytes, None
        self._listener, self._elapsed = None, 0

    def _path(self, slot):
        slot = str(slot)
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', slot):
            raise SaveError('Slot id must contain 1..80 ASCII letters, digits, underscores or hyphens')
        return self.root/(slot+'.json')

    def _read(self, path):
        try:
            if path.stat().st_size > self.max_bytes:
                raise SaveCorrupt('Save exceeds size limit')
            value = json.loads(path.read_text(encoding='utf-8'))
            if not isinstance(value, dict) or value.get('format') != 'forge.slot/1':
                raise SaveCorrupt('Unknown slot envelope')
            payload = {key: value[key] for key in ('format', 'version', 'metadata', 'data')}
            checksum = hashlib.sha256(_encoded(payload)).hexdigest()
            if checksum != value.get('checksum'):
                raise SaveCorrupt('Checksum mismatch')
            if type(value['version']) is not int or value['version'] < 1 or not isinstance(value['metadata'], dict):
                raise SaveCorrupt('Invalid version or metadata')
            return value
        except FileNotFoundError:
            raise
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise SaveCorrupt(f'{path.name}: {error}') from error

    def _validate(self, data):
        if self.validate is not None:
            try:
                if self.validate(data) is False:
                    raise ValueError('Validator returned False')
            except Exception as error:
                raise SaveError(f'Invalid game save data: {error}') from error

    def write(self, slot, data, *, title='', description='', metadata=None):
        self._validate(data)
        path = self._path(slot)
        details = dict(metadata or {})
        details.update(title=str(title), description=str(description),
                       saved_at=datetime.now(timezone.utc).isoformat())
        value = dict(format='forge.slot/1', version=self.version, metadata=details, data=data)
        value['checksum'] = hashlib.sha256(_encoded(value)).hexdigest()
        encoded = _encoded(value)
        if len(encoded) > self.max_bytes:
            raise SaveError('Save exceeds size limit')
        try:
            # Keep the last verified primary; never overwrite a good backup with damage.
            if path.exists():
                try: self._read(path)
                except SaveCorrupt: pass
                else: _atomic(path.with_suffix('.json.bak'), path.read_bytes())
            _atomic(path, encoded)
        except OSError as error:
            raise SaveError(f'Cannot write slot {slot}: {error}') from error
        forge.log(f'Saved slot {slot}')
        return details

    def read(self, slot, default=_MISSING, *, recover=True):
        path = self._path(slot)
        try:
            value = self._read(path)
        except (FileNotFoundError, SaveCorrupt) as error:
            backup = path.with_suffix('.json.bak')
            if recover and backup.exists():
                value = self._read(backup)
                forge.log(f'Slot {slot} recovered from backup: {error}', 'WARN')
            elif isinstance(error, FileNotFoundError) and default is not _MISSING:
                return default
            else:
                raise SaveError(f'Cannot load slot {slot}: {error}') from error
        version, data = value['version'], value['data']
        if version > self.version:
            raise SaveVersion(f'Slot {slot}: version {version} is newer than supported {self.version}')
        while version < self.version:
            migration = self.migrations.get(version)
            if migration is None:
                raise SaveVersion(f'Missing migration {version} -> {version+1}')
            try: data = migration(data)
            except Exception as error: raise SaveVersion(f'Migration {version} failed: {error}') from error
            version += 1
        self._validate(data)
        return data

    def info(self, slot):
        path = self._path(slot)
        if not path.exists():
            return dict(slot=str(slot), status='empty', title='Пустой слот')
        try:
            value = self._read(path)
            return dict(value['metadata'], slot=str(slot), status='ok', version=value['version'])
        except SaveCorrupt as error:
            return dict(slot=str(slot), status='corrupt', title='Повреждённое сохранение', error=str(error))

    def slots(self):
        return [self.info(path.stem) for path in sorted(self.root.glob('*.json'))]

    def delete(self, slot):
        path = self._path(slot)
        for candidate in (path, path.with_suffix('.json.bak')):
            candidate.unlink(missing_ok=True)

    def autosave(self, capture, *, interval=60, slot='auto', title='Автосохранение', while_paused=False):
        if not callable(capture) or not isinstance(interval, (float, int)) or not math.isfinite(interval) or interval <= 0:
            raise ValueError('Autosave needs a callable and positive interval')
        self._path(slot)
        self.close()
        self._autosave = (capture, interval, slot, title, while_paused)
        self._elapsed = 0
        self._listener = forge.on_frame(self.update)
        return self

    def update(self, dt):
        if self._autosave is None:
            return
        capture, interval, slot, title, while_paused = self._autosave
        if forge.is_paused() and not while_paused:
            return
        self._elapsed += dt
        if self._elapsed >= interval:
            self._elapsed %= interval
            self.write(slot, capture(), title=title)

    def close(self):
        if self._listener is not None:
            forge.remove_listener(self._listener)
        self._listener, self._autosave = None, None
