"""Native geometry body roundtrip and inline block grammar tests."""
import unittest
import re
from pathlib import Path
from model_geometry_resources import compile_source, decode_source
from model_render_programs import compile_source as compile_program


class GeometrySources(unittest.TestCase):
    def test_primary_secondary_and_layers_are_editable_words(self):
        source = '''primary_words 0 1 2 3
secondary_words 4 5 6 7
layer_base 10
layer 8 9
end_layers
raw 0xcc 0x01 0x02 0x03 0x00 0x00
'''
        data = compile_source(source)
        self.assertEqual(compile_source(decode_source(data)), data)
        self.assertIn('secondary_words', decode_source(data))
        self.assertIn('layer 0x0008 0x0009', decode_source(data))

    def test_selection_flag_consumes_29_words_even_with_4000(self):
        source = ('primary_words 0x5000 1 2 3\nselection_words ' +
                  ' '.join(str(i) for i in range(29)) + '\nlayer_base 0\nraw 0 0\n')
        data = compile_source(source)
        decoded = decode_source(data)
        self.assertIn('selection_words', decoded)
        self.assertNotIn('secondary_words', decoded)
        self.assertEqual(compile_source(decoded), data)

    def test_declared_geometry_file_length_is_checked(self):
        text = 'geometry_pipeline token=0x009f words=0,0 file=geometry/test.geometry'
        self.assertEqual(len(compile_program(text, lambda _: 'raw 0 1 2 3 4 5 6 7')), 14)
        with self.assertRaises(ValueError):
            compile_program(text, lambda _: 'raw 0 1')
        with self.assertRaises(ValueError):
            compile_program(text)

    def test_truncated_header_remains_raw(self):
        data = bytes.fromhex('00 10 11 22 33 44')
        self.assertTrue(decode_source(data).startswith('raw'))
        self.assertEqual(compile_source(decode_source(data)), data)

    def test_rejects_malformed_blocks(self):
        for text in ('primary_words 1 2', 'selection_words 0', 'raw 256',
                     'layer 0 1', 'layer 1', 'end_layers 1', 'guess 0'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                compile_source(text)

    def test_signed_vectors_isolated_points_and_zero_first_curve_x(self):
        source = '''primary_words 0x4000 0 0 0
layer_base 0
vector_phase tag=0xcc xyz=-128,127,-1 phase=255
begin_curve tag=4 selector=-7
isolated_point xyz=0,1,2 parameter=3
curve_start xyz=0,2,3
curve_point xyz=1,-2,3
end_curve
end_records
'''
        data = compile_source(source)
        decoded = decode_source(data)
        self.assertIn('vector_phase tag=0xcc xyz=-128,127,-1 phase=255', decoded)
        self.assertIn('isolated_point xyz=0,1,2 parameter=3', decoded)
        self.assertEqual(compile_source(decoded), data)

    def test_rejects_ambiguous_zero_x_or_incomplete_curve(self):
        for source in ('begin_curve tag=1 selector=0\ncurve_start xyz=1,2,3\nend_curve',
                       'begin_curve tag=1 selector=0\ncurve_start xyz=1,2,3\ncurve_point xyz=0,2,3',
                       'vector_phase tag=1 xyz=1,2,3 phase=0',
                       'vector_phase tag=128 xyz=-129,0,0 phase=0',
                       'begin_curve tag=0 selector=0',
                       'begin_curve tag=1 selector=0\nend_records'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_source(source)

    def test_all_geometry_files_roundtrip(self):
        root = Path(__file__).resolve().parents[1] / 'data'
        if not any(root.rglob('*.geometry')):
            self.skipTest('unpacked model geometry is not present')
        paths = list(root.rglob('*.geometry'))
        self.assertEqual(len(paths), 29)
        referenced = {model.parent.parent / name for model in root.rglob('*.model')
                      for name in re.findall(r'geometry_pipeline[^\n]* file=(\S+)',
                                             model.read_text(encoding='ascii'))}
        self.assertEqual(set(paths), referenced)
        for path in paths:
            with self.subTest(file=path.name):
                data = compile_source(path.read_text(encoding='ascii'))
                self.assertEqual(compile_source(decode_source(data)), data)

    def test_padding_aligns_only_after_record_terminator(self):
        source = 'primary_words 0x4000 0 0 0\nlayer_base 0\nend_records\npadding 0\n'
        data = compile_source(source)
        self.assertIn('padding 0x00', decode_source(data))
        self.assertEqual(len(data) % 2, 0)
        for source in ('padding 0', 'end_records\npadding 256', 'end_records\npadding 0\npadding 0'):
            with self.assertRaises(ValueError):
                compile_source(source)


if __name__ == '__main__':
    unittest.main()
