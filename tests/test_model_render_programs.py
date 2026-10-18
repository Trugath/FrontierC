"""Tests for native renderer grammar, conservative decoding and byte preservation."""
import unittest
import configparser
from pathlib import Path
from model_render_programs import compile_source, decode_source


class RendererPrograms(unittest.TestCase):
    def test_dispatch_and_little_endian_operands(self):
        source = 'inline_curve token=0x0041 words=0x1234,0xabcd\nreturn_stream token=0x0000 words=-\n'
        self.assertEqual(compile_source(source), bytes.fromhex('41 00 34 12 cd ab 00 00'))

    def test_parameter_inline_table_length(self):
        data = bytes.fromhex('1a 76 00 82') + bytes.fromhex('60 07') * 7
        source, count, consumed = decode_source(data)
        self.assertEqual((count, consumed), (1, len(data)))
        self.assertEqual(compile_source(source), data)
        with self.assertRaises(ValueError):
            compile_source('update_parameter token=0x761a words=0x8200')

    def test_unknown_grammar_and_odd_suffix_remain_raw(self):
        data = bytes.fromhex('06 00 03 00 77')
        source, count, consumed = decode_source(data)
        self.assertEqual((count, consumed), (1, 2))
        self.assertIn('raw 0x03 0x00 0x77', source)
        self.assertEqual(compile_source(source), data)

    def test_control_variants_and_signed_basis_consume_proven_words(self):
        for data in (bytes.fromhex('a6 ff 22 11'), bytes.fromhex('1c 80 22 11'),
                     bytes.fromhex('06 20 22 11'), bytes.fromhex('06 40')):
            source, count, consumed = decode_source(data)
            self.assertEqual((count, consumed), (1, len(data)))
            self.assertEqual(compile_source(source), data)

    def test_basis_columns_always_consume_phase_word(self):
        for token in (0x003c, 0x09dc, 0x801c):
            data = token.to_bytes(2, 'little') + bytes.fromhex('38 00 00 00')
            source, count, consumed = decode_source(data)
            self.assertEqual((count, consumed), (2, len(data)))
            self.assertIn('words=0x0038', source)
            self.assertNotIn('point_list', source)
            self.assertEqual(compile_source(source), data)
            with self.assertRaises(ValueError):
                compile_source(f'load_basis_columns token={token} words=-')

    def test_nested_context_operand_sign_controls_extra_vertex_words(self):
        for source, expected in (
                ('nested_model_context token=14 words=0x8000,0x1234,0x5678', 8),
                ('nested_model_context token=14 words=0', 4),
                ('conditional_nested_model token=27 words=0x8000,0,1,2', 10)):
            data = compile_source(source)
            self.assertEqual(len(data), expected)
            self.assertEqual(compile_source(decode_source(data)[0]), data)
        with self.assertRaises(ValueError):
            compile_source('nested_model_context token=14 words=0x8000')

    def test_polygon_visible_terminator_is_independent_of_rejected_normal_skip(self):
        source = 'polygon_value_sequence token=5 words=0x0800,0x0201,0x1234,0x5678,0'
        data = compile_source(source)
        self.assertEqual(compile_source(decode_source(data)[0]), data)
        # The visible-normal path exits at the explicit slot-zero token; its
        # command extent is independent of unsigned DH.
        normal = compile_source('polygon_value_sequence token=5 words=0x0600,0x0201,0x1234,0x5678,0')
        self.assertEqual(compile_source(decode_source(normal)[0]), normal)
        # DH is a separate byte target from after the polygon header; it need
        # not equal the visible path's distance to slot zero.
        mismatch = compile_source('polygon_value_sequence token=5 '
                                  'words=0x8800,0x0201,0x1234,0x5678,0')
        self.assertEqual(compile_source(decode_source(mismatch)[0]), mismatch)
        with self.assertRaises(ValueError):
            compile_source('polygon_value_sequence token=5 words=0x0800,0x0201,0x1234')

    def test_polygon_rejected_normal_skip_adds_an_independent_stream_entry(self):
        source = ('polygon_value_sequence token=0xab05 normal_index=0x3c '
                  'skip_bytes=0x10 skip_if_z_clipped=true header=0x10bc '
                  'commands=adjacent_operator:@0x0e00|line_join:@0x0a00|end:@0x0000')
        data = compile_source(source)
        self.assertEqual(len(data), 10)
        # Preserve ten uninterpreted bytes between visible completion and the
        # rejected-normal continuation at byte 20.
        data += bytes.fromhex('df ff 00 00 00 00 00 00 00 00')
        data += compile_source('return_stream token=0 words=-')
        decoded, count, covered = decode_source(data, follow_branches=True)
        self.assertEqual((count, covered), (2, 12))
        self.assertIn('polygon_value_sequence token=0xab05', decoded)
        self.assertIn('return_stream token=0x0000 words=- # program +0x0014', decoded)
        self.assertEqual(compile_source(decoded), data)

    def test_polygon_subcommands_are_named_without_normalizing_encoded_words(self):
        source = ('polygon_value_sequence token=0xe1a5 words=0x123c '
                  'commands=bezier:18,52,171,205@0x0210,0x1234,0xabcd|'
                  'line:77,13@0xc54d,0xc50d|line_cont:33@0x0621|'
                  'bezier_cont:34,51,68@0x0822,0x3344|line_join:@0x0a00|'
                  'circle:37,68,9@0x0c25,0x4409|end:@0x4060')
        data = compile_source(source)
        decoded, count, consumed = decode_source(data)
        self.assertEqual((count, consumed), (1, len(data)))
        self.assertIn('bezier:18,52,171,205@0x0210,0x1234,0xabcd', decoded)
        self.assertIn('line:77,13@0xc54d,0xc50d', decoded)
        self.assertIn('circle:37,68,9@0x0c25,0x4409', decoded)
        self.assertIn('line_join:@0x0a00', decoded)
        self.assertIn('end:@0x4060', decoded)
        self.assertIn('normal_index=0x3c skip_bytes=0x12 '
                      'skip_if_z_clipped=false header=0x123c', decoded)
        self.assertEqual(compile_source(decoded), data)
        legacy = source.replace('line_join:', 'join:')
        self.assertEqual(compile_source(legacy), data)
        with self.assertRaises(ValueError):
            compile_source('polygon_value_sequence token=5 normal_index=1 '
                           'skip_bytes=2 skip_if_z_clipped=false '
                           'header=0x0301 commands=end:@0')
        clipped = compile_source('polygon_value_sequence token=5 normal_index=0x3c '
                                 'skip_bytes=0x12 skip_if_z_clipped=true '
                                 'header=0x12bc commands=end:@0')
        self.assertIn('skip_if_z_clipped=true', decode_source(clipped)[0])
        with self.assertRaises(ValueError):
            compile_source('polygon_value_sequence token=5 normal_index=0x3c '
                           'skip_bytes=0x12 skip_if_z_clipped=false '
                           'header=0x12bc commands=end:@0')
        for commands in ('bezier:0x0400,0,0|end:0',
                         'line:0x0200,0|end:0',
                         'bezier:0x0200,0,0'):
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                compile_source('polygon_value_sequence token=5 words=0 commands=' + commands)

    def test_point_list_requires_common_normal_and_discard_terminator(self):
        source = 'point_list token=24 words=0x4000,0x0201,0x0403,0x7f7f'
        data = compile_source(source)
        self.assertEqual(compile_source(decode_source(data)[0]), data)
        # Any low-byte 7Fh ends both render and discard paths. A high-byte-only
        # marker in a later word ends render early, so that stream remains raw.
        low_byte_marker = compile_source('point_list token=24 words=0x4000,0x0201,0x017f')
        self.assertEqual(compile_source(decode_source(low_byte_marker)[0]), low_byte_marker)
        followed_by_return = low_byte_marker + compile_source('return_stream token=0 words=-')
        source, count, consumed = decode_source(followed_by_return)
        self.assertEqual((count, consumed), (2, len(followed_by_return)))
        self.assertIn('return_stream token=0x0000', source)
        first_high_byte_vertex = compile_source('point_list token=24 words=0x4000,0x7f01,0x017f')
        self.assertEqual(compile_source(decode_source(first_high_byte_vertex)[0]), first_high_byte_vertex)
        for words in ('0x4000,0x0201,0x7f03,0x017f', '0x4000,0x0102'):
            with self.assertRaises(ValueError):
                compile_source('point_list token=24 words=' + words)

    def test_geometry_declared_payload_preserves_byte_data(self):
        source = 'geometry_pipeline token=0x005f words=0x1234,0x5678 payload=ff010002'
        data = compile_source(source)
        self.assertEqual(data, bytes.fromhex('5f 00 34 12 78 56 ff 01 00 02'))
        self.assertEqual(compile_source(decode_source(data)[0]), data)
        for source in ('geometry_pipeline token=0x005f words=0,0 payload=ff01',
                       'geometry_pipeline token=0x005f words=0 payload=ff010002',
                       'geometry_pipeline token=0x005e words=0,0 payload=ff010002'):
            with self.assertRaises(ValueError):
                compile_source(source)

    def test_point_pairs_expose_signed_vertices_in_projection_order(self):
        source = 'point_list token=24 parameter=0x4000 points=-128,-1|3,2|4,end'
        data = compile_source(source)
        self.assertEqual(data, bytes.fromhex('18 00 00 40 ff 80 02 03 7f 04'))
        decoded, count, consumed = decode_source(data)
        self.assertEqual((count, consumed), (1, len(data)))
        self.assertIn('points=-128,-1|3,2|4,end', decoded)
        self.assertEqual(compile_source(decoded), data)
        for points in ('127,end', '1,2|end,end'):
            source = 'point_list token=24 parameter=0 points=' + points
            data = compile_source(source)
            self.assertIn('points=' + points, decode_source(data)[0])

    def test_point_pairs_reject_invalid_indices_and_ambiguous_boundaries(self):
        for points in ('128,end', '-129,end', 'end,end', '1,127',
                       '1,2|127,end', '1,2|end,3|4,end',
                       '1,end|2,end', '1,2', '1', '1,2|'):
            with self.subTest(points=points), self.assertRaises(ValueError):
                compile_source('point_list token=24 parameter=0 points=' + points)
        with self.assertRaises(ValueError):
            compile_source('point_list token=24 parameter=0x10000 points=1,end')

    def test_point_size_and_material_fields_preserve_raw_aliases(self):
        cases = (
            ('0x9898', 'fixed:0@0x0040', 'rgb12:0x888', 'false'),
            ('0x9998', 'depth_scaled:128@0x4000', 'rgb12:0x888', 'true'),
            ('0xe018', 'frame:-64@0x80c0', 'table:0x000', 'false'),
            ('0xe638', 'object:114@0x8080', 'table:0x190', 'false'),
        )
        for token, size, material, inherit in cases:
            source = (f'point_list token={token} size={size} material={material} '
                      f'inherit_material={inherit} points=1,end')
            with self.subTest(source=source):
                data = compile_source(source)
                self.assertEqual(compile_source(decode_source(data)[0]), data)
                self.assertIn('size=' + size, decode_source(data)[0])
        for size, material, inherit in (
                ('fixed:1@0x0040', 'rgb12:0x888', 'false'),
                ('depth_scaled:0@0x0040', 'rgb12:0x888', 'false'),
                ('frame:-64@0x0040', 'rgb12:0x888', 'false'),
                ('fixed:0@0x0040', 'rgb12:0x999', 'false'),
                ('fixed:0@0x0040', 'table:0x988', 'false'),
                ('fixed:0@0x0040', 'rgb12:0x888', 'true'),
                ('fixed:0@0x0040', 'rgb12:0x888', 'False')):
            with self.subTest(size=size, material=material, inherit=inherit), self.assertRaises(ValueError):
                compile_source(f'point_list token=0x9898 size={size} material={material} '
                               f'inherit_material={inherit} points=1,end')

    def test_truncated_fixed_command_and_geometry_are_explicit_and_lossless(self):
        fixed = bytes.fromhex('08 06 00 00')
        source, count, covered = decode_source(fixed, follow_branches=True)
        self.assertEqual((count, covered), (0, 0))
        self.assertIn('adjusted_textured_quad token=0x0608 words=0x0000 truncated=true', source)
        self.assertEqual(compile_source(source), fixed)

        partial_geometry = ('geometry_pipeline token=0x06ff '
                            'words=0x0088,0xc22d payload=2dc2c20c truncated=true')
        data = compile_source(partial_geometry)
        decoded, count, covered = decode_source(data, follow_branches=True)
        self.assertEqual((count, covered), (0, 0))
        self.assertIn('payload=2dc2c20c truncated=true', decoded)
        self.assertEqual(compile_source(decoded), data)
        with self.assertRaises(ValueError):
            compile_source('geometry_pipeline token=0x06ff words=0x0088,0xc22d '
                           'payload=2dc2c20c')

    def test_return_does_not_decode_unreachable_tail(self):
        source, count, consumed = decode_source(bytes.fromhex('00 00 06 00'))
        self.assertEqual((count, consumed), (1, 2))
        self.assertIn('raw 0x06 0x00', source)

    def test_filter_target_recovers_second_block_after_return(self):
        # Slot19 token0033h encodes two skipped bytes after its operand.
        data = bytes.fromhex('33 00 01 00 00 00 06 00 00 00')
        source, count, covered = decode_source(data, follow_branches=True)
        self.assertEqual((count, covered), (4, len(data)))
        self.assertIn('parameter_control token=0x0006 words=- # program +0x0006', source)
        self.assertEqual(compile_source(source), data)

    def test_branch_into_operand_falls_back_without_false_alias(self):
        # Target6 is inside the fallthrough bitmap operand interval4..10.
        data = bytes.fromhex('33 00 01 00 03 00 06 00 00 00 00 00')
        source, count, covered = decode_source(data, follow_branches=True)
        self.assertEqual((count, covered), (3, 12))
        self.assertIn('bitmap_surface', source)
        self.assertNotIn('parameter_control', source)
        self.assertEqual(compile_source(source), data)

    def test_rejects_wrong_slot_width_truncation_and_extra_fields(self):
        for source in ('inline_curve token=2 words=1,2',
                       'inline_curve token=1 words=1',
                       'return_stream token=65536 words=-',
                       'raw 256',
                       'return_stream token=0 words=- extra=1'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_source(source)

    def test_all_native_models_roundtrip_without_crossing_contact_insertions(self):
        root = Path(__file__).resolve().parents[1] / 'data'
        if not any((root / 'models/frontier/models').glob('*.model')):
            self.skipTest('unpacked model sources are not present')
        paths = []
        for directory in ('models/frontier/models', 'models/frontier_auxiliary/models',
                          'intro/models'):
            paths.extend((root / directory).glob('*.model'))
        self.assertEqual(len(paths), 353)
        for path in paths:
            with self.subTest(model=path.name):
                config = configparser.ConfigParser(interpolation=None)
                config.read(path, encoding='ascii')
                data = compile_source(config['program']['instructions'],
                    lambda name: (path.parent.parent / name).read_text(encoding='ascii'))
                limit = config.getint('contact', 'program_offset') if 'contact' in config else len(data)
                from model_instance_directories import program_entry_offsets
                decoded, _, consumed = decode_source(data[:limit], follow_branches=True,
                                                    entry_offsets=program_entry_offsets(config))
                self.assertLessEqual(consumed, limit)
                self.assertEqual(compile_source(decoded), data[:limit])
                self.assertEqual(compile_source(decode_source(data)[0]), data)


if __name__ == '__main__':
    unittest.main()
