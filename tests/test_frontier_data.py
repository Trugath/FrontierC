"""The asset dat round-trips through Python and the native data tool."""
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from frontier_data import read_archive, records_from_assets, write_archive, write_tree

ROOT = Path(__file__).resolve().parents[1]
DATA_SOURCES = (
    'data_tool.c', 'asset_archive.c', 'asset_files.c', 'asset_ini.c', 'asset_image.c',
    'inflate.c', 'sha256.c', 'asset_program.c', 'asset_decode.c', 'asset_model.c',
    'asset_tree.c', 'asset_unpack.c', 'asset_adlib.c', 'asset_tables.c',
)


def compile_data_tool(directory):
    from compiler import windows_executable_inputs, zig
    seed = directory / 'authored_seed.c'
    subprocess.run([sys.executable, str(ROOT / 'tools' / 'embed_authored.py'), str(seed)], check=True)
    tool = directory / ('frontier-data' + ('.exe' if sys.platform == 'win32' else ''))
    subprocess.run([str(zig()), 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-I', str(ROOT / 'src/engine/host'),
                    *[str(ROOT / 'src/engine/host' / name) for name in DATA_SOURCES],
                    *windows_executable_inputs(zig(), directory),
                    str(seed), '-o', str(tool)], check=True)
    return tool


class FrontierData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / 'data/video/font.bdf').is_file():
            raise unittest.SkipTest('unpacked Frontier data is not present')
        scores = ('hallking', 'elitet', 'babayaga', 'barem', 'jupalt', 'valkries', 'bluedan', 'ggofkiev', 'intro')
        if any(not (ROOT / 'data/music' / f'{name}.mid').is_file() for name in scores):
            raise unittest.SkipTest('unpacked MIDI scores are not present')
        cls.records = records_from_assets()

    def test_archive_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            dat = Path(tmp) / 'frontier.dat'
            write_archive(self.records, dat)
            self.assertEqual(read_archive(dat), self.records)
            self.assertTrue(dat.read_bytes().startswith(b'FE2DATA1'))

    def test_required_records(self):
        for name in ('font', 'palette', 'widths', 'text/builtin', 'text-info', 'bank/0', 'bank/1',
                     'model-offsets/0', 'model-offsets/1'):
            self.assertIn(name, self.records)
        self.assertEqual(len(self.records['font']) % 10, 0)
        self.assertEqual(len(self.records['palette']) % 3, 0)
        self.assertEqual(len(self.records['widths']), 256)
        self.assertEqual(len(self.records['model-offsets/0']), 480)
        self.assertTrue(any(name.startswith('graphic/') for name in self.records))
        self.assertTrue(any(name.startswith('model/0/') for name in self.records))
        for name in ('hallking', 'elitet', 'babayaga', 'barem', 'jupalt', 'valkries', 'bluedan', 'ggofkiev', 'intro'):
            self.assertTrue(self.records[f'music/{name}'].startswith(b'MThd'))
        self.assertEqual(len(self.records['adlib/image']), 0x67dd)
        self.assertEqual(len(self.records['galaxy/density']), 16514)
        self.assertEqual(len(self.records['composites']), 1488)
        self.assertEqual(len(self.records['intro/models']), 27440)
        self.assertEqual(len(self.records['intro/orbit']), 286)
        self.assertEqual(len(self.records['intro/scenes']), 22 * 286)
        self.assertEqual(len(self.records['text/names']), 3785)
        self.assertEqual(len(self.records['text/intro']), 470)
        self.assertEqual(len(self.records['text/flight']), 1230)
        self.assertEqual(len(self.records['text/mining']), 1008)
        self.assertEqual(len(self.records['text/offences']), 365)
        self.assertEqual(len(self.records['text/interaction']), 19542)
        self.assertEqual(len(self.records['text/commander']), 2164)
        self.assertEqual(len(self.records['text/status']), 3575)
        self.assertEqual(len(self.records['table/market']), 0x649)
        self.assertEqual(len(self.records['table/systems']), 0x3e7f)
        self.assertEqual(len(self.records['table/catalogue']), 331 * 48)
        self.assertEqual(len(self.records['table/input']), 256)
        self.assertEqual(len(self.records['table/pointer']), 210)
        self.assertEqual(len(self.records['table/pool']), 30)
        self.assertEqual(len(self.records['table/motion']), 10)
        self.assertEqual(len(self.records['table/contact']), 18)
        self.assertEqual(len(self.records['table/position']), 128)
        self.assertEqual(len(self.records['table/objects']), 0x9b)
        self.assertEqual(len(self.records['table/probe']), 18)
        self.assertEqual(len(self.records['table/people']), 493)
        self.assertEqual(len(self.records['table/panels']), 93)
        self.assertEqual(len(self.records['table/intro']), 294)
        self.assertEqual(len(self.records['table/navigation']), 0x6a)
        self.assertGreater(len(self.records['table/render']), 800)
        self.assertEqual(len(self.records['table/offences']), 1292)
        self.assertEqual(len(self.records['table/mining']), 171)
        self.assertEqual(len(self.records['table/commander']), 567)

    def test_native_packer_matches(self):
        try:
            from compiler import zig
            zig()
        except SystemExit:
            self.skipTest('zig cc was not found')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dat = root / 'frontier.dat'
            write_archive(self.records, dat)
            write_tree(self.records, root / 'records')
            tool = compile_data_tool(root)
            subprocess.run([str(zig()), 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            '-I', str(ROOT / 'src/engine'), '-I', str(ROOT / 'src/engine/host'),
                            '-c', str(ROOT / 'src/engine/host/asset_bind.c'),
                            '-o', str(root / 'asset_bind.o')], check=True)
            packed = root / 'packed.dat'
            subprocess.run([str(tool), 'pack', str(root / 'records'), str(packed)], check=True)
            self.assertEqual(packed.read_bytes(), dat.read_bytes())
            unpacked = root / 'unpacked'
            subprocess.run([str(tool), 'unpack', str(packed), str(unpacked)], check=True)
            self.assertEqual((unpacked / 'assets.txt').read_bytes(), (root / 'records' / 'assets.txt').read_bytes())
            again = root / 'again.dat'
            subprocess.run([str(tool), 'pack', str(unpacked), str(again)], check=True)
            self.assertEqual(again.read_bytes(), dat.read_bytes())
            tree = root / 'tree.dat'
            subprocess.run([str(tool), 'pack', str(ROOT / 'data'), str(tree)], check=True)
            self.assertEqual(tree.read_bytes(), dat.read_bytes())

    def test_native_unpack_rejects_the_wrong_install(self):
        try:
            from compiler import zig
            zig()
        except SystemExit:
            self.skipTest('zig cc was not found')
        names = ('el2g.grf', 'el2mcga3.ovl', 'el2m13.ovl', 'frontier.exe', 'el2m08.ovl',
                 'babayaga.rol', 'barem.rol', 'bluedan.rol', 'elitet.rol', 'ggofkiev.rol',
                 'hallking.rol', 'intro.rol', 'jupalt.rol', 'valkries.rol',
                 'el2m00.ovl', 'el2m02.ovl', 'el2m03.ovl', 'el2m09.ovl', 'el2m10.ovl',
                 'el2m14.ovl', 'el2m15.ovl', 'el2m17.ovl', 'el2m18.ovl', 'el2m19.ovl',
                 'el2adlib.ovl', 'el2setup.ovl')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            tool = compile_data_tool(root)
            empty = root / 'empty'
            empty.mkdir()
            missing = subprocess.run([str(tool), 'unpack', str(empty), str(root / 'out')],
                                     capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn('This Frontier install is missing:', missing.stderr)
            self.assertIn('el2g.grf', missing.stderr)
            install = root / 'install'
            install.mkdir()
            for name in names:
                (install / name).write_bytes(b'not-this-version')
            wrong = subprocess.run([str(tool), 'unpack', str(install), str(root / 'out')],
                                   capture_output=True, text=True)
            self.assertEqual(wrong.returncode, 2)
            self.assertIn('not the Frontier: Elite II version this tool supports.', wrong.stderr)
            self.assertFalse((root / 'out').exists())
            archive = root / 'install.zip'
            with zipfile.ZipFile(archive, 'w') as zipped:
                for name in names:
                    zipped.writestr(name, b'not-this-version')
            repacked = root / 'repacked.dat'
            repack = subprocess.run([str(tool), 'repack', str(archive), str(repacked)],
                                    capture_output=True, text=True)
            self.assertEqual(repack.returncode, 2)
            self.assertIn('not the Frontier: Elite II version this tool supports.', repack.stderr)
            self.assertFalse(repacked.exists())
            usage = subprocess.run([str(tool)], capture_output=True, text=True)
            self.assertEqual(usage.returncode, 2)
            self.assertIn('repack <frontier.zip> <frontier.dat>', usage.stderr)


if __name__ == '__main__':
    unittest.main()
