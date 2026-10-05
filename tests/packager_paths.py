"""Copy planning without CPython relocation or filesystem copies of the game."""
from pathlib import Path
import copy
import importlib.util
import sys
import tempfile
import types
import unittest

sys.modules['forge'] = types.SimpleNamespace(__version__='test')
spec = importlib.util.spec_from_file_location('packager', Path(__file__).resolve().parents[1]/'tools/packager.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class CopyPaths(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='forge-copy-')
        self.root = Path(self.temp.name).resolve()/'fnaf'
        self.root.mkdir()
        for folder in ('modules', 'extra', 'textures'): (self.root/folder).mkdir()
        (self.root/'textures/icon.png').write_bytes(b'icon')
        self.settings = dict(paths={'modules':'modules'}, python_paths=['extra'], project={'icon':'textures/icon.png'})

    def tearDown(self): self.temp.cleanup()

    def test_output_cannot_be_in_any_copy_source(self):
        for folder in ('modules', 'extra'):
            with self.subTest(folder=folder), self.assertRaisesRegex(RuntimeError, 'inside an asset directory'):
                packager._copy_plan(self.root, copy.deepcopy(self.settings), self.root/folder/'Game')
            self.assertFalse((self.root/folder/'Game').exists())

    def test_aliases_are_normalized_in_plan_and_config(self):
        self.settings['paths']['modules'] = '../fnaf/modules'
        self.settings['python_paths'] = ['extra', '../fnaf/extra']
        self.settings['project']['icon'] = '../fnaf/textures/icon.png'
        self.settings['save_directory'] = '../fnaf/saves'
        plan = packager._copy_plan(self.root, self.settings, self.root/'dist/Game')
        self.assertEqual(plan, {'modules':self.root/'modules', 'extra':self.root/'extra'})
        self.assertEqual(self.settings['paths']['modules'], 'modules')
        self.assertEqual(self.settings['python_paths'], ['extra', 'extra'])
        self.assertEqual(self.settings['project']['icon'], 'textures/icon.png')
        self.assertEqual(self.settings['save_directory'], 'saves')

    def test_root_external_and_absolute_sources_are_rejected(self):
        for path in ('.', '..', str(self.root/'modules')):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                packager._copy_plan(self.root, dict(paths={'modules':path}), self.root/'dist/Game')
        link = self.root/'escape'
        try: link.symlink_to(self.root.parent, target_is_directory=True)
        except OSError as error: self.skipTest(f'Symlinks unavailable: {error}')
        with self.assertRaisesRegex(RuntimeError, 'escapes project root'):
            packager._copy_plan(self.root, dict(paths={}, python_paths=['escape']), self.root/'dist/Game')

    def test_destination_is_checked_independently(self):
        stage = self.root/'stage';stage.mkdir()
        outside = self.root/'outside';outside.mkdir()
        self.assertEqual(packager._stage_path(stage, 'modules'), stage/'modules')
        for path in ('.', '../outside/file', str(outside/'file')):
            with self.subTest(path=path), self.assertRaisesRegex(RuntimeError, 'destination escapes'):
                packager._stage_path(stage, path)
        try: (stage/'escape').symlink_to(outside, target_is_directory=True)
        except OSError as error: self.skipTest(f'Symlinks unavailable: {error}')
        with self.assertRaisesRegex(RuntimeError, 'destination escapes'):
            packager._stage_path(stage, 'escape/file')


    def test_nested_symlinks_are_confined_and_cycles_rejected(self):
        outside=self.root.parent/'private.txt';outside.write_text('private')
        link=self.root/'extra/link'
        try:link.symlink_to(outside)
        except OSError as error:self.skipTest(f'Symlinks unavailable: {error}')
        with self.assertRaisesRegex(RuntimeError,'escapes project root'):
            packager._copy_plan(self.root,copy.deepcopy(self.settings),self.root/'dist/Game')
        link.unlink();link.symlink_to(self.root/'extra',target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError,'cycle'):
            packager._copy_plan(self.root,copy.deepcopy(self.settings),self.root/'dist/Game')
        link.unlink();link.symlink_to(self.root/'textures/icon.png')
        packager._copy_plan(self.root,copy.deepcopy(self.settings),self.root/'dist/Game')

    def test_game_directories_named_tests_are_preserved(self):
        source=self.root/'extra'
        for folder in ('test','tests','tkinter'):
            (source/folder).mkdir();(source/folder/'content.py').write_text('VALUE=1')
        target=self.root/'stage'
        packager._copy_tree(source,target)
        for folder in ('test','tests','tkinter'):
            self.assertTrue((target/folder/'content.py').exists(),folder)

if __name__ == '__main__': unittest.main(verbosity=2)
