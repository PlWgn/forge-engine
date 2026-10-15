"""Quest staging, target-library rejection and source/destination confinement."""
from pathlib import Path
import importlib.util
import io
import hashlib
import json
import struct
import sys
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import quest

class QuestPackaging(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='forge-quest-')
        self.root=Path(self.temp.name).resolve();self.game=self.root/'Game с пробелами';self.game.mkdir()
        self.prefix=self.root/'Python';self.prefix.mkdir()
        (self.prefix/'include/python3.14').mkdir(parents=True);(self.prefix/'include/python3.14/Python.h').write_text('fixture')
        (self.prefix/'lib/python3.14/encodings').mkdir(parents=True);(self.prefix/'lib/python3.14/encodings/__init__.py').write_text('')
        header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';struct.pack_into('<H',header,18,183)
        (self.prefix/'lib/libpython3.14.so').write_bytes(header)
        (self.prefix/'LICENSE.txt').write_text('CPython fixture license')
        paths={key:key for key in ('modules','scenes','scripts','graphics','textures','materials','models','objects','audio')}
        for folder in paths.values():(self.game/folder).mkdir()
        (self.game/'extra').mkdir();(self.game/'extra/hello.py').write_text('VALUE=1')
        self.settings={'schema_version':1,'project':{'name':'VR & Quest'},'entry_scene':'test.py','paths':paths,'python_paths':['extra']}
        self.config=self.game/'engine.json';self.write()
        self.output=self.root/'out'
    def tearDown(self):self.temp.cleanup()
    def write(self):self.config.write_text(json.dumps(self.settings),encoding='utf-8')
    def prepare(self):return quest.prepare(self.config,self.output,self.prefix)
    def test_target_elf_rejects_host_and_wrong_architecture(self):
        lib=self.prefix/'lib/libpython3.14.so'
        for data in (b'MachO',b'MZ',b'\x7fELF\x02\x01'+b'\x00'*58):
            lib.write_bytes(data)
            with self.assertRaisesRegex(RuntimeError,'arm64 ELF'):self.prepare()
        self.assertFalse(self.output.exists())
    def test_paths_cycles_and_output_preservation(self):
        self.settings['python_paths']=['../Python'];self.write()
        with self.assertRaisesRegex(RuntimeError,'escapes'):self.prepare()
        self.settings['python_paths']=['extra'];self.write()
        try:(self.game/'extra/link').symlink_to(self.game/'extra',target_is_directory=True)
        except OSError as error:self.skipTest(str(error))
        with self.assertRaisesRegex(RuntimeError,'cycle'):self.prepare()
        (self.game/'extra/link').unlink()
        self.output.mkdir();(self.output/'keep').write_text('unchanged')
        with self.assertRaisesRegex(RuntimeError,'already exists'):self.prepare()
        self.assertEqual((self.output/'keep').read_text(),'unchanged')
    def test_apk_rejects_debug_suffixed_nativeactivity_library(self):
        data=b'{}'
        manifest={'python':'3.14','files':{'game.json':hashlib.sha256(data).hexdigest()}}
        payload=io.BytesIO()
        with zipfile.ZipFile(payload,'w') as content:
            content.writestr('manifest.json',json.dumps(manifest))
            content.writestr('game.json',data)
        apk=self.root/'Game.apk'
        header=(self.prefix/'lib/libpython3.14.so').read_bytes()
        with zipfile.ZipFile(apk,'w') as archive:
            archive.writestr('assets/forge-content.zip',payload.getvalue())
            for name in ('libforged.so','libopenxr_loader.so','libc++_shared.so','libpython3.14.so'):
                archive.writestr('lib/arm64-v8a/'+name,header)
        with self.assertRaisesRegex(RuntimeError,'libforge.so'):quest.verify_apk(apk)
        with zipfile.ZipFile(apk,'a') as archive:archive.writestr('lib/arm64-v8a/libforge.so',header)
        with self.assertRaisesRegex(RuntimeError,'ANativeActivity_onCreate'):quest.verify_apk(apk)

    def test_native_module_paths_are_confined(self):
        self.settings['native_modules']=['../private.cpp'];self.write()
        with self.assertRaisesRegex(RuntimeError,'escapes'):self.prepare()
        self.assertFalse(self.output.exists())

    def test_prefix_escape_and_reserved_paths(self):
        (self.prefix/'lib/escape').symlink_to(self.root)
        with self.assertRaisesRegex(RuntimeError,'symlink'):self.prepare()
        (self.prefix/'lib/escape').unlink()
        (self.game/'runtime').mkdir();self.settings['paths']['modules']='runtime';self.write()
        with self.assertRaisesRegex(RuntimeError,'reserved'):self.prepare()
    @unittest.skipUnless((ROOT/'vendor/openxr').is_dir(),'Optional pinned OpenXR dependency is not fetched')
    def test_stages_private_python_unicode_content_and_licenses(self):
        self.prepare()
        with zipfile.ZipFile(self.output/'app/src/main/assets/forge-content.zip') as archive:
            settings=json.loads(archive.read('game.json'))
            self.assertTrue(settings['xr']['enabled']);self.assertEqual(settings['storage']['mode'],'user')
            self.assertEqual(settings['renderer']['backend'],'opengl')
            self.assertEqual(archive.read('extra/hello.py'),b'VALUE=1')
            self.assertIn('runtime/lib/python3.14/encodings/__init__.py',archive.namelist())
            self.assertIn('licenses/openxr/jsoncpp.txt',archive.namelist())
            manifest=json.loads(archive.read('manifest.json'))
            import hashlib
            for name,digest in manifest['files'].items():self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(),digest)
        self.assertIn('VR &amp; Quest',(self.output/'app/src/main/res/values/strings.xml').read_text())

if __name__=='__main__':unittest.main(verbosity=2)
