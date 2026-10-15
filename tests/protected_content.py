"""Protected desktop packaging through the real embedded Python and platform AES."""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENGINE = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location('protected_content', ROOT / 'tools/protected_content.py')
content = importlib.util.module_from_spec(spec)
spec.loader.exec_module(content)


class ArchivePaths(unittest.TestCase):
    def test_rejects_unsafe_nonportable_members_before_writing(self):
        for name in ('../escape', '/absolute', 'C:/escape', 'C:escape', 'a\\b', 'a/../b',
                     'a//b', './b', 'a:b', 'NUL.txt', 'a.', 'a ', 'a\x01b'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                archive = io.BytesIO()
                with zipfile.ZipFile(archive, 'w') as bundle:
                    bundle.writestr('first.txt', 'must not extract')
                    bundle.writestr(name, 'bad')
                archive.seek(0)
                with self.assertRaises(RuntimeError):
                    content._extract(archive, Path(folder))
                self.assertEqual(list(Path(folder).iterdir()), [])

    def test_rejects_links_aliases_conflicts_and_budgets(self):
        cases = [('File', 'file'), ('a', 'a/b'), ('caf\u00e9', 'cafe\u0301'), ('A/file', 'a/another')]
        for names in cases:
            with self.subTest(names=names), tempfile.TemporaryDirectory() as folder:
                archive = io.BytesIO()
                with zipfile.ZipFile(archive, 'w') as bundle:
                    for name in names: bundle.writestr(name, 'x')
                archive.seek(0)
                with self.assertRaises(RuntimeError): content._extract(archive, Path(folder))
                self.assertEqual(list(Path(folder).iterdir()), [])
        with tempfile.TemporaryDirectory() as folder:
            archive = io.BytesIO()
            with zipfile.ZipFile(archive, 'w') as bundle:
                link = zipfile.ZipInfo('link');link.external_attr = (stat.S_IFLNK | 0o777) << 16
                bundle.writestr(link, '../outside')
            archive.seek(0)
            with self.assertRaisesRegex(RuntimeError, 'links'): content._extract(archive, Path(folder))
            archive = io.BytesIO()
            with zipfile.ZipFile(archive, 'w') as bundle: bundle.writestr('file', '123')
            archive.seek(0)
            limit = content.MAX_BYTES
            try:
                content.MAX_BYTES = 2
                with self.assertRaisesRegex(RuntimeError, 'limit'): content._extract(archive, Path(folder))
            finally: content.MAX_BYTES = limit
            archive.seek(0)
            count_limit = content.MAX_FILES
            try:
                content.MAX_FILES = 0
                with self.assertRaisesRegex(RuntimeError, 'limit'): content._extract(archive, Path(folder))
            finally: content.MAX_FILES = count_limit
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_executable_slot_must_be_unique_and_preserves_other_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'Game'
            for image in (b'no slot', content.KEY_SLOT * 2):
                binary.write_bytes(image)
                with self.assertRaisesRegex(RuntimeError, 'unique'): content.embed_key(binary, bytes(64))
                self.assertEqual(binary.read_bytes(), image)
            image = b'before' + content.KEY_SLOT + b'after'
            binary.write_bytes(image)
            content.embed_key(binary, bytes(64))
            expected = image.replace(content.KEY_SLOT, content.KEY_PREFIX + bytes(64) + b'\0FORGE-CONTENT-END\0')
            self.assertEqual(binary.read_bytes(), expected)

    def test_project_native_libraries_are_rejected_before_encryption(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'source';source.mkdir()
            target = Path(folder) / 'target';target.mkdir()
            (source / 'plugin.dll').write_bytes(b'native library')
            with self.assertRaisesRegex(RuntimeError, 'native libraries'):
                content.seal(source, target, {})
            self.assertEqual(list(target.iterdir()), [])


@unittest.skipUnless(sys.platform in ('darwin', 'win32'), 'Desktop native encryption requires macOS/Windows')
class ProtectedBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='forge protected тест ')
        cls.root = Path(cls.temp.name)
        cls.project = cls.root / 'source';cls.project.mkdir()
        cls.config = json.loads((ROOT / 'engine.json').read_text(encoding='utf-8'))
        cls.config['paths'] = {group: 'ресурсы/' + group for group in cls.config['paths']}
        for group, relative in cls.config['paths'].items():
            target = cls.project / relative
            if group in ('graphics', 'locales'):
                shutil.copytree(ROOT / group, target)
            else: target.mkdir(parents=True)
        cls.config['project'].pop('icon', None)
        cls.config['project']['name'] = 'Protected тест'
        cls.config['storage'] = {'mode': 'project', 'application_id': 'org.forge.protected-test'}
        cls.config['entry_scene'] = 'test.py'
        cls.config['startup_scripts'] = []
        cls.config['python_paths'] = ['extra modules', 'extra modules/nested']
        extra = cls.project / 'extra modules/nested';extra.mkdir(parents=True)
        (extra / 'secret_helper.py').write_text("VALUE='PRIVATE_CONTENT_MARKER_6f24'\n", encoding='utf-8')
        (extra / 'license_checks.py').write_text("VALUE='PRIVATE_LICENSE_CODE'\n", encoding='utf-8')
        (extra / 'CUSTOM-LICENSE.txt').write_text('Custom dependency notice', encoding='utf-8')
        (cls.project / cls.config['paths']['textures'] / 'secret.txt').write_text('PRIVATE_ASSET_MARKER_f257', encoding='utf-8')
        # Multiple cipher records, with incompressible content, exercise streaming.
        (cls.project / cls.config['paths']['textures'] / 'large.bin').write_bytes(os.urandom(2 * content.CHUNK + 73))
        cls.scene = cls.project / cls.config['paths']['scenes'] / 'test.py'
        cls.scene.write_text('''import forge,os
from pathlib import Path
from secret_helper import VALUE
def on_start():
    assert VALUE=='PRIVATE_CONTENT_MARKER_6f24'
    assert Path(forge.asset_path('textures','secret.txt')).read_text()=='PRIVATE_ASSET_MARKER_f257'
    assert forge.settings()['storage']['mode']=='user'
    path=Path(forge.storage_path('saves/launches.txt'));path.parent.mkdir(parents=True,exist_ok=True)
    count=int(path.read_text())+1 if path.exists() else 1;path.write_text(str(count))
    root=Path(forge.project_path(''))
    assert root.stat().st_mode & 0o077 == 0 if os.name!='nt' else True
    forge.log('EXTRACTED_ROOT '+str(root))
    forge.log('PROTECTED_STARTED '+str(count));forge.quit()
''', encoding='utf-8')
        (cls.scene.parent / 'failed.py').write_text("def on_start(): raise RuntimeError('PROTECTED_SCENE_FAILURE')\n", encoding='utf-8')
        (cls.project / 'engine.json').write_text(json.dumps(cls.config, ensure_ascii=False), encoding='utf-8')
        cls.output = cls.root / 'distribution'
        cls.build(cls.output)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    @classmethod
    def build(cls, output):
        result = subprocess.run([str(ENGINE), 'build', '--project', str(cls.project / 'engine.json'),
                                 '--output', str(output), '--protect-content', '--no-open-log'],
                                capture_output=True, text=True, encoding='utf-8', timeout=240)
        if result.returncode: raise AssertionError(result.stdout + result.stderr)

    def launch(self, output=None, extra=(), expected=0):
        output = output or self.output
        binary = output / 'Contents/MacOS/Game' if output.suffix == '.app' else output / ('Game.exe' if os.name == 'nt' else 'Game')
        cwd = self.root / 'other cwd';cwd.mkdir(exist_ok=True)
        temporary = self.root / 'process temp';temporary.mkdir(exist_ok=True)
        env = dict(os.environ, PYTHONHOME='/invalid/host', PYTHONPATH='/invalid/host',
                   FORGE_USER_ROOT=str(self.root / 'user'), TMPDIR=str(temporary), TEMP=str(temporary), TMP=str(temporary))
        result = subprocess.run([str(binary), *extra, '--headless', '--frames', '2', '--no-open-log'],
                                cwd=cwd, env=env, capture_output=True, text=True, encoding='utf-8', timeout=60)
        text = result.stdout + result.stderr
        self.assertEqual(result.returncode, expected, text)
        self.assertEqual(list(temporary.glob('forge-content-*')), [], text)
        return text

    def test_native_crypto_known_answer_and_container_failures(self):
        script = self.project / self.config['paths']['scenes'] / 'crypto.py'
        script.write_text('''import forge,io,importlib.util,os
from pathlib import Path
from _forge_content_crypto import aes_cbc
spec=importlib.util.spec_from_file_location('content', ''' + repr(str(ROOT / 'tools/protected_content.py')) + ''')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
def rejects(call):
    try:call()
    except RuntimeError:return
    raise AssertionError('Invalid encrypted content accepted')
def on_start():
    # NIST SP800-38A AES-256-CBC first block; PKCS7 adds a second block.
    key=bytes.fromhex('603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4')
    iv=bytes.fromhex('000102030405060708090a0b0c0d0e0f')
    plain=bytes.fromhex('6bc1bee22e409f96e93d7e117393172a')
    cipher=aes_cbc(plain,key,iv,True)
    assert cipher[:16].hex()=='f58c4c04d6e5f1ba779eabfb5f7bfbd6'
    assert aes_cbc(cipher,key,iv,False)==plain
    rejects(lambda:aes_cbc(plain,b'bad',iv,True))
    secret=os.urandom(64);data=os.urandom(c.CHUNK*2+37)
    packed=io.BytesIO();c._encrypt(io.BytesIO(data),packed,secret);raw=packed.getvalue()
    restored=io.BytesIO();c._decrypt(io.BytesIO(raw),restored,secret);assert restored.getvalue()==data
    for broken in (raw[:-1],raw[:-52],raw+b'extra',raw[:70]+bytes([raw[70]^1])+raw[71:]):
        rejects(lambda:c._decrypt(io.BytesIO(broken),io.BytesIO(),secret))
    rejects(lambda:c._decrypt(io.BytesIO(raw),io.BytesIO(),os.urandom(64)))
    # Authenticated sequence and per-container identity prevent reordering/splicing.
    offset=len(c.MAGIC)+16;record=20+c.CHUNK+16+32
    reordered=raw[:offset]+raw[offset+record:offset+record*2]+raw[offset:offset+record]+raw[offset+record*2:]
    rejects(lambda:c._decrypt(io.BytesIO(reordered),io.BytesIO(),secret))
    second=io.BytesIO();c._encrypt(io.BytesIO(data),second,secret)
    spliced=raw[:offset]+second.getvalue()[offset:offset+record]+raw[offset+record:]
    rejects(lambda:c._decrypt(io.BytesIO(spliced),io.BytesIO(),secret))
    forge.log('CRYPTO_OK');forge.quit()
''', encoding='utf-8')
        result = subprocess.run([str(ENGINE), 'run', '--project', str(self.project / 'engine.json'), '--scene', 'crypto.py',
                                 '--headless', '--frames', '2', '--no-open-log'], capture_output=True, text=True, encoding='utf-8', timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('CRYPTO_OK', result.stdout)
        script.unlink()

    def test_protected_distribution_and_persistent_user_storage(self):
        public = json.loads((self.output / 'game.json').read_text(encoding='utf-8'))
        self.assertEqual(public['protected_content'], content.FORMAT)
        self.assertNotIn('paths', public)
        self.assertFalse((self.output / 'ресурсы').exists())
        self.assertFalse((self.output / 'extra modules').exists())
        self.assertFalse((self.output / '.forge-content').exists())
        self.assertFalse((self.output / 'content.key').exists())
        raw = (self.output / 'content.forge').read_bytes()
        self.assertNotIn(b'PRIVATE_CONTENT_MARKER_6f24', raw)
        self.assertNotIn(b'PRIVATE_ASSET_MARKER_f257', raw)
        self.assertTrue((self.output / 'licenses/content/ресурсы/graphics/FONT-LICENSE.txt').is_file())
        self.assertTrue((self.output / 'licenses/content/extra modules/nested/CUSTOM-LICENSE.txt').is_file())
        self.assertFalse((self.output / 'licenses/content/extra modules/nested/license_checks.py').exists())
        manifest = json.loads((self.output / 'manifest.json').read_text(encoding='utf-8'))
        for name, digest in manifest['files'].items():
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest, name)
        # Tests share the package but each launch leaves its save in external user storage.
        before = self.root / 'user/org.forge.protected-test/saves/launches.txt'
        count = int(before.read_text()) if before.exists() else 0
        self.assertIn('PROTECTED_STARTED ' + str(count + 1), self.launch())
        self.assertIn('PROTECTED_STARTED ' + str(count + 2), self.launch())
        self.assertIn('validated', self.launch(extra=('validate',)))
        self.assertIn('PROTECTED_STARTED', self.launch(extra=('--project', str(self.output) + '/./game.json')))
        original = (self.output / 'content.forge').read_bytes()
        with self.assertRaises(AssertionError): self.build(self.output)
        self.assertEqual((self.output / 'content.forge').read_bytes(), original)

    def test_flag_requires_desktop_build(self):
        for command in ('run', 'validate', 'build-quest'):
            result = subprocess.run([sys.executable, str(ROOT / 'tools/forge.py'), command, '--protect-content'],
                                    capture_output=True, text=True, encoding='utf-8', timeout=15)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('--protect-content requires build', result.stderr)
        result = subprocess.run([str(ENGINE), 'run', '--protect-content'],
                                capture_output=True, text=True, encoding='utf-8', timeout=15)
        self.assertEqual(result.returncode, 1)
        self.assertIn('--protect-content requires build', result.stderr)

    def test_tampering_missing_container_and_failed_scene_cleanup(self):
        file = self.output / 'content.forge';original = file.read_bytes()
        try:
            broken = bytearray(original);broken[100] ^= 1;file.write_bytes(broken)
            text = self.launch(expected=1)
            self.assertIn('integrity check failed', text)
            self.assertNotIn('PROTECTED_STARTED', text)
            file.write_bytes(original[:-1])
            self.assertIn('truncated', self.launch(expected=1))
        finally: file.write_bytes(original)
        try:
            file.unlink()
            self.assertIn('content.forge', self.launch(expected=1))
        finally: file.write_bytes(original)
        self.assertIn('PROTECTED_SCENE_FAILURE', self.launch(extra=('--scene', 'failed.py'), expected=1))
        self.assertIn('Missing file', self.launch(extra=('--scene', 'absent.py'), expected=1))
        self.assertIn('source project', self.launch(extra=('dev',), expected=1))
        self.assertIn('source project', self.launch(extra=('project',), expected=1))

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS application bundle')
    def test_protected_app_signature_and_launch(self):
        output = self.root / 'Protected.app';self.build(output)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(output)], check=True, capture_output=True)
        self.assertIn('PROTECTED_STARTED', self.launch(output))
        self.assertFalse((output / 'Contents/Resources/ресурсы').exists())


if __name__ == '__main__': unittest.main(verbosity=2)
