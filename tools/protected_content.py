"""Optional desktop content container; embedded in the native launcher at compile time.

This deters casual extraction, not a hostile owner of the machine: the package
includes its key and the runtime needs plaintext files. No license enforcement.
"""
from pathlib import Path, PurePosixPath, PureWindowsPath
import hmac
import json
import os
import shutil
import stat
import struct
import unicodedata
import tempfile
import zipfile

FORMAT = 'forge.content/1'
MAGIC = b'FORGE-CONTENT\x00\x01'
KEY_PREFIX = b'FORGE-CONTENT-KEY\0'
KEY_SLOT = KEY_PREFIX + b'0123456789abcdef' * 4 + b'\0FORGE-CONTENT-END\0'
CHUNK = 1024 * 1024
MAX_BYTES = 64 * 1024 ** 3
MAX_FILES = 100000


def _crypt(data, key, iv, encrypt):
    from _forge_content_crypto import aes_cbc
    return aes_cbc(data, key, iv, encrypt)


def _encrypt(source, target, key):
    header = MAGIC + os.urandom(16)
    target.write(header)
    sequence = 0
    while True:
        data = source.read(CHUNK)
        iv = os.urandom(16)
        cipher = _crypt(data, key[:32], iv, True) if data else b''
        record = struct.pack('<I', len(cipher)) + iv + cipher
        tag = hmac.digest(key[32:], header + struct.pack('<Q', sequence) + record, 'sha256')
        target.write(record + tag)
        if not data:
            break
        sequence += 1


def _read(source, size):
    result = source.read(size)
    if len(result) != size:
        raise RuntimeError('Protected content is truncated')
    return result


def _decrypt(source, target, key):
    header = _read(source, len(MAGIC) + 16)
    if not header.startswith(MAGIC):
        raise RuntimeError('Unsupported protected content format')
    sequence, total = 0, 0
    while True:
        prefix = _read(source, 20)
        length, = struct.unpack('<I', prefix[:4])
        if length > CHUNK + 16 or length % 16:
            raise RuntimeError('Invalid protected content record size')
        cipher = _read(source, length)
        tag = _read(source, 32)
        expected = hmac.digest(key[32:], header + struct.pack('<Q', sequence) + prefix + cipher, 'sha256')
        if not hmac.compare_digest(tag, expected):
            raise RuntimeError('Protected content integrity check failed')
        if length == 0:
            if source.read(1):
                raise RuntimeError('Unexpected data after protected content')
            return
        data = _crypt(cipher, key[:32], prefix[4:], False)
        total += len(data)
        if total > MAX_BYTES:
            raise RuntimeError('Protected content exceeds 64 GiB limit')
        target.write(data)
        sequence += 1


def _member_path(name):
    # Validate both OS path syntaxes even when packaging on the other OS.
    path = PurePosixPath(name)
    if (not name or '\x00' in name or '\\' in name or path.is_absolute()
            or PureWindowsPath(name).drive or any(p in ('', '.', '..') for p in name.rstrip('/').split('/'))):
        raise RuntimeError('Unsafe protected content path: ' + repr(name))
    for part in path.parts:
        if (part.endswith(('.', ' ')) or any(c in part for c in ':<>"|?*')
                or any(ord(c) < 32 for c in part)
                or part.split('.')[0].rstrip(' ').upper() in
                {'CON', 'PRN', 'AUX', 'NUL', 'CONIN$', 'CONOUT$',
                 *('COM' + c for c in '123456789¹²³'), *('LPT' + c for c in '123456789¹²³')}):
            raise RuntimeError('Nonportable protected content path: ' + repr(name))
    return path


def _validate_members(members):
    if len(members) > MAX_FILES or sum(m.file_size for m in members) > MAX_BYTES:
        raise RuntimeError('Protected content exceeds file count or 64 GiB limit')
    seen, files, spelling = set(), set(), {}
    # Include implicit directories when rejecting case/Unicode aliases.
    for member in members:
        path = _member_path(member.filename)
        mode = member.external_attr >> 16
        if stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise RuntimeError('Protected content cannot contain links or special files')
        name = unicodedata.normalize('NFC', str(path)).casefold()
        if name in seen:
            raise RuntimeError('Duplicate protected content path: ' + member.filename)
        seen.add(name)
        for prefix in (path, *path.parents):
            if str(prefix) == '.':
                continue
            identity = unicodedata.normalize('NFC', str(prefix)).casefold()
            if spelling.setdefault(identity, str(prefix)) != str(prefix):
                raise RuntimeError('Aliased protected content path: ' + member.filename)
        if not member.is_dir():
            files.add(name)
    for member in members:
        path = _member_path(member.filename)
        if any(unicodedata.normalize('NFC', str(p)).casefold() in files for p in path.parents if str(p) != '.'):
            raise RuntimeError('Protected content file/directory conflict')


def _extract(archive, root):
    with zipfile.ZipFile(archive) as bundle:
        members = bundle.infolist()
        # Validate all members before writing even the first one. Never extract links.
        _validate_members(members)
        for member in members:
            target = root.joinpath(*PurePosixPath(member.filename).parts)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(member) as source, target.open('xb') as output:
                    shutil.copyfileobj(source, output, CHUNK)


def seal(source, destination, settings):
    """Replace a separate staging content tree with an encrypted container."""
    source, destination = Path(source), Path(destination)
    # Project-relative storage in a disposable extraction directory would lose saves.
    settings.setdefault('storage', {})['mode'] = 'user'
    settings['storage'].setdefault('application_id', 'org.forge.game')
    (source / 'game.json').write_text(json.dumps(settings, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    key = os.urandom(64)  # Independent AES-256 and HMAC-SHA256 keys, per build.
    with tempfile.TemporaryFile() as archive:
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, allowZip64=True) as bundle:
            count, total, seen = 0, 0, set()
            for file in sorted(source.rglob('*')):
                relative = file.relative_to(source).as_posix()
                _member_path(relative)
                if file.is_symlink() or not (file.is_dir() or file.is_file()):
                    raise RuntimeError('Protected content cannot contain links or special files')
                if file.is_file() and file.suffix.lower() in ('.pyd', '.dll', '.dylib', '.so'):
                    raise RuntimeError('Protected content cannot contain native libraries; use an ordinary build: ' + relative)
                name = unicodedata.normalize('NFC', relative).casefold()
                if name in seen:
                    raise RuntimeError('Duplicate protected content path: ' + relative)
                seen.add(name)
                count += 1
                total += file.stat().st_size if file.is_file() else 0
                if count > MAX_FILES or total > MAX_BYTES:
                    raise RuntimeError('Protected content exceeds file count or 64 GiB limit')
                bundle.write(file, relative)
            _validate_members(bundle.infolist())
        if archive.tell() > MAX_BYTES:
            raise RuntimeError('Protected content archive exceeds 64 GiB limit')
        archive.seek(0)
        with (destination / 'content.forge').open('xb') as output:
            _encrypt(archive, output, key)
    public = {'protected_content': FORMAT, 'project': settings.get('project', {}), 'storage': settings['storage'], 'logging': settings.get('logging', {})}
    (destination / 'game.json').write_text(json.dumps(public, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return key


def embed_key(executable, key):
    """Personalize a copied executable before platform relocation/signing."""
    if len(key) != 64:
        raise RuntimeError('Invalid protected content key')
    with Path(executable).open('r+b') as binary:
        image = binary.read()
        offset = image.find(KEY_SLOT)
        if offset < 0 or image.find(KEY_SLOT, offset + 1) >= 0:
            raise RuntimeError('Engine has no unique protected content key slot; rebuild the engine')
        binary.seek(offset + len(KEY_PREFIX))
        binary.write(key)


def open_bundle(folder):
    """Authenticate completely, then extract into a fresh process-owned directory.

    Caller owns the returned TemporaryDirectory and must clean it after the runtime.
    The key is shipped with the game; this is deliberately not a trust boundary.
    """
    folder = Path(folder)
    public = json.loads((folder / 'game.json').read_text(encoding='utf-8'))
    if public.get('protected_content') != FORMAT:
        raise RuntimeError('Unsupported protected content format')
    from _forge_content_crypto import package_key
    key = package_key()
    if len(key) != 64:
        raise RuntimeError('Invalid protected content key')
    temporary = tempfile.TemporaryDirectory(prefix='forge-content-')
    root = Path(temporary.name)
    try:
        # A separate file avoids clashing with any archive member name.
        with tempfile.TemporaryFile() as archive:
            with (folder / 'content.forge').open('rb') as source:
                _decrypt(source, archive, key)
            archive.seek(0)
            _extract(archive, root)
        settings = json.loads((root / 'game.json').read_text(encoding='utf-8'))
        if settings.get('storage', {}).get('mode') != 'user':
            raise RuntimeError('Protected content requires user storage')
        return temporary
    except BaseException:
        temporary.cleanup()
        raise
