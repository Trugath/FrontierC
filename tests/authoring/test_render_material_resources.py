"""Validate native material records and the six-bit polygon acceptance table."""
import csv
from pathlib import Path
import tempfile
import unittest

from render_material_resources import read_materials

ROOT = Path(__file__).resolve().parents[2]
RESOURCE = ROOT / 'data/main/frontier'


class MaterialResources(unittest.TestCase):
    def test_native_record_layout(self):
        words = read_materials(RESOURCE / 'texture_materials.tsv')
        self.assertEqual(len(words), 37 * 8)
        self.assertEqual(words[:8], [0xd0, 0x40, 0x60, 0x50, 0x30, 0x20, 0x10, 0x10])
        self.assertIn(0x30dd, words[::8])
        self.assertIn(0xffff, words)

    def test_masks_match_axis_intersection_predicate(self):
        with (RESOURCE / 'polygon_vertex_acceptance_masks.csv').open() as stream:
            values = [int(x, 0) for row in csv.reader(stream) for x in row]
        self.assertEqual(len(values), 64)
        for code, value in enumerate(values):
            expected = int((bool(code & 2) or code & 5 == 5)
                           and (bool(code & 16) or code & 40 == 40))
            self.assertEqual(value, expected, f'outcode {code:06b}')

    def test_malformed_materials_rejected(self):
        source = (RESOURCE / 'texture_materials.tsv').read_text()
        variants = [source.replace('index\t', 'unknown\t', 1),
                    source.replace('\n1\t', '\n0\t', 1),
                    source.replace('0x0040', '0x1000', 1),
                    source.replace('0x00d0', '0x10000', 1),
                    source.splitlines()[0] + '\n',
                    source.replace('\t0x0010\n', '\n', 1)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.tsv'
            for variant in variants:
                path.write_text(variant, encoding='ascii')
                with self.assertRaises(ValueError):
                    read_materials(path)


if __name__ == '__main__':
    unittest.main()
